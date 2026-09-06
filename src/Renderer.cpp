#include "Renderer.h"
#include "UiRenderer.h"
#include "VoxelRenderer.h"
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <DirectXCollision.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstring>

namespace vc {
using namespace DirectX;
static void check(HRESULT hr,const char* operation) {
    if(FAILED(hr)) { std::ostringstream s; s<<operation<<" failed (0x"<<std::hex<<uint32_t(hr)<<")."; throw std::runtime_error(s.str()); }
}
static D3D12_RESOURCE_BARRIER transition(ID3D12Resource* r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after}; return b;
}
static void uav(ID3D12GraphicsCommandList* list,ID3D12Resource* r) {
    D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV; b.UAV.pResource=r; list->ResourceBarrier(1,&b);
}
ComPtr<ID3D12Resource> Renderer::buffer(uint64_t bytes,D3D12_HEAP_TYPE heap,D3D12_RESOURCE_STATES state,D3D12_RESOURCE_FLAGS flags) {
    D3D12_HEAP_PROPERTIES h{}; h.Type=heap;
    D3D12_RESOURCE_DESC d{}; d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; d.Width=(bytes+255)&~255ull;
    d.Height=1; d.DepthOrArraySize=1; d.MipLevels=1; d.SampleDesc.Count=1; d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags=flags;
    // Recent runtimes create ordinary default-heap buffers in COMMON regardless
    // of InitialResourceState. Make the transition explicit on the active list.
    auto initial=(heap==D3D12_HEAP_TYPE_DEFAULT && state!=D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE)?D3D12_RESOURCE_STATE_COMMON:state;
    ComPtr<ID3D12Resource> r; check(device_->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,initial,nullptr,IID_PPV_ARGS(&r)),"Create buffer");
    if(initial!=state) {auto b=transition(r.Get(),initial,state);list_->ResourceBarrier(1,&b);}
    return r;
}
Renderer::Renderer(HWND window,unsigned width,unsigned height,bool debug,ImFontAtlas* fonts):window_(window),width_(width),height_(height) {
    unsigned factoryFlags=0;
    if(debug) {
        ComPtr<ID3D12Debug> layer;
        if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&layer)))) {layer->EnableDebugLayer();factoryFlags=DXGI_CREATE_FACTORY_DEBUG;}
        else throw std::runtime_error("DirectX debug layer unavailable. Install Windows Graphics Tools or omit --debug-layer.");
    }
    check(CreateDXGIFactory2(factoryFlags,IID_PPV_ARGS(&factory_)),"Create DXGI factory");
    for(unsigned i=0;;++i) {
        ComPtr<IDXGIAdapter1> a;
        if(factory_->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&a))==DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{}; a->GetDesc1(&desc);
        if(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        ComPtr<ID3D12Device5> candidate;
        if(FAILED(D3D12CreateDevice(a.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&candidate)))) continue;
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
        D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_5};
        if(FAILED(candidate->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,&options,sizeof(options))) || options.RaytracingTier<D3D12_RAYTRACING_TIER_1_1) continue;
        if(FAILED(candidate->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&model,sizeof(model))) || model.HighestShaderModel<D3D_SHADER_MODEL_6_5) continue;
        device_=candidate; check(a.As(&adapter_),"Query adapter");
        int length=WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,nullptr,0,nullptr,nullptr);
        adapterName.resize(length); WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,adapterName.data(),length,nullptr,nullptr); adapterName.pop_back(); break;
    }
    if(!device_) throw std::runtime_error("VoxelCity requires a DirectX 12 GPU with DXR 1.1 and Shader Model 6.5. Update your graphics driver or use a supported GPU.");
    BOOL supportsTearing=FALSE;
    factory_->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,&supportsTearing,sizeof(supportsTearing)); tearing_=supportsTearing!=FALSE;
    D3D12_COMMAND_QUEUE_DESC q{}; q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    check(device_->CreateCommandQueue(&q,IID_PPV_ARGS(&queue_)),"Create queue"); queue_->GetTimestampFrequency(&timestampFrequency_);
    DXGI_SWAP_CHAIN_DESC1 swap{}; swap.Width=width_; swap.Height=height_; swap.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    swap.SampleDesc.Count=1; swap.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; swap.BufferCount=FrameCount;
    swap.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD; swap.Flags=tearing_?DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING:0;
    ComPtr<IDXGISwapChain1> chain; check(factory_->CreateSwapChainForHwnd(queue_.Get(),window,&swap,nullptr,nullptr,&chain),"Create swap chain");
    check(chain.As(&swapchain_),"Query swap chain"); factory_->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER);
    D3D12_DESCRIPTOR_HEAP_DESC heap{}; heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heap.NumDescriptors=FrameCount;
    check(device_->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&rtvHeap_)),"Create RTV heap"); rtvStride_=device_->GetDescriptorHandleIncrementSize(heap.Type);
    heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV; heap.NumDescriptors=1;
    check(device_->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&dsvHeap_)),"Create DSV heap");
    heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap.NumDescriptors=1; heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    check(device_->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&srvHeap_)),"Create UI descriptor heap");
    for(auto& f:frames_) check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&f.allocator)),"Create allocator");
    check(device_->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,frames_[0].allocator.Get(),nullptr,IID_PPV_ARGS(&list_)),"Create command list");
    check(list_->Close(),"Close command list");
    check(device_->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence_)),"Create fence");
    fenceEvent_=CreateEventW(nullptr,FALSE,FALSE,nullptr); if(!fenceEvent_) throw std::runtime_error("Could not create GPU fence event.");
    D3D12_QUERY_HEAP_DESC queries{}; queries.Count=FrameCount*2; queries.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    check(device_->CreateQueryHeap(&queries,IID_PPV_ARGS(&timestamps_)),"Create timing queries");
    timestampReadback_=buffer(FrameCount*2*sizeof(uint64_t),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    createTargets(); createPipeline();
    ConfigureRenderUI(fonts);
    if(!ImGui_ImplDX12_Init(device_.Get(),FrameCount,DXGI_FORMAT_R8G8B8A8_UNORM,srvHeap_.Get(),srvHeap_->GetCPUDescriptorHandleForHeapStart(),srvHeap_->GetGPUDescriptorHandleForHeapStart()))
        throw std::runtime_error("Could not initialize the editor interface.");
    imguiInitialized_=true;
    ImGui_ImplDX12_NewFrame();
}
Renderer::~Renderer() {
    try { waitIdle(); } catch(...) {}
    if(imguiInitialized_) {ImGui_ImplDX12_Shutdown();}
    if(fenceEvent_) CloseHandle(fenceEvent_);
}
void Renderer::assertOwner() const {if(std::this_thread::get_id()!=ownerThread_)throw std::logic_error("Renderer accessed outside its owning thread");}
void Renderer::invalidateWorld() {
    assertOwner();waitIdle();voxelRenderer_.reset();parcelRevision_=~uint64_t(0);initialWorld_=true;pendingChunks_.clear();
    for(auto& revision:chunkRevisions_)++revision;
}
void Renderer::wait(uint64_t value) {
    if(value && fence_->GetCompletedValue()<value) {
        check(fence_->SetEventOnCompletion(value,fenceEvent_),"Wait for GPU");
        if(WaitForSingleObject(fenceEvent_,30000)!=WAIT_OBJECT_0) throw std::runtime_error("GPU did not respond within 30 seconds.");
    }
}
void Renderer::waitIdle() {assertOwner();if(queue_ && fence_) {check(queue_->Signal(fence_.Get(),++fenceValue_),"Signal GPU");wait(fenceValue_);}}
void Renderer::createTargets() {
    auto handle=rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for(unsigned i=0;i<FrameCount;++i) { check(swapchain_->GetBuffer(i,IID_PPV_ARGS(&backbuffers_[i])),"Get back buffer");device_->CreateRenderTargetView(backbuffers_[i].Get(),nullptr,handle);handle.ptr+=rtvStride_; }
    D3D12_RESOURCE_DESC d{}; d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width=width_;d.Height=height_;
    d.DepthOrArraySize=1;d.MipLevels=1;d.Format=DXGI_FORMAT_D32_FLOAT;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE clear{};clear.Format=d.Format;clear.DepthStencil.Depth=1;
    check(device_->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&depth_)),"Create depth buffer");
    device_->CreateDepthStencilView(depth_.Get(),nullptr,dsvHeap_->GetCPUDescriptorHandleForHeapStart());
}
void Renderer::resize(unsigned width,unsigned height) {
    assertOwner();
    if(!width || !height || (width==width_ && height==height_)) return;
    waitIdle(); for(auto& b:backbuffers_) b.Reset();depth_.Reset();width_=width;height_=height;
    check(swapchain_->ResizeBuffers(FrameCount,width,height,DXGI_FORMAT_R8G8B8A8_UNORM,tearing_?DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING:0),"Resize swap chain");createTargets();if(voxelRenderer_)voxelRenderer_->resize(width_,height_);
}
static std::vector<char> shader(const wchar_t* file) {
    wchar_t executable[32768]; GetModuleFileNameW(nullptr,executable,32768);
    auto path=std::filesystem::path(executable).parent_path()/L"shaders"/file;
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    if(!f) throw std::runtime_error("Compiled shaders are missing. Rebuild the shaders target.");
    auto size=f.tellg();std::vector<char> bytes(static_cast<size_t>(size)); f.seekg(0);f.read(bytes.data(),size);return bytes;
}
void Renderer::createPipeline() {
    D3D12_ROOT_PARAMETER parameters[3]{};
    parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; parameters[0].Constants={0,0,36}; parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;parameters[1].Descriptor={0,0};parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;parameters[2].Descriptor={1,0};parameters[2].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC root{};root.NumParameters=3;root.pParameters=parameters;root.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> data,error;check(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&data,&error),"Serialize root signature");
    check(device_->CreateRootSignature(0,data->GetBufferPointer(),data->GetBufferSize(),IID_PPV_ARGS(&root_)),"Create root signature");
    auto vs=shader(L"world.vs.cso"),ps=shader(L"world.ps.cso");
    D3D12_INPUT_ELEMENT_DESC layout[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"MATERIAL",0,DXGI_FORMAT_R32_UINT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};p.pRootSignature=root_.Get();p.VS={vs.data(),vs.size()};p.PS={ps.data(),ps.size()};
    p.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    p.SampleMask=UINT_MAX;p.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;p.RasterizerState.DepthClipEnable=TRUE;
    p.DepthStencilState.DepthEnable=TRUE;p.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;p.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS;
    p.InputLayout={layout,3};p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;p.NumRenderTargets=1;p.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
    p.DSVFormat=DXGI_FORMAT_D32_FLOAT;p.SampleDesc.Count=1;
    check(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&pipeline_)),"Create voxel pipeline");
    auto carVs=shader(L"cars.vs.cso"),carPs=shader(L"cars.ps.cso");
    D3D12_INPUT_ELEMENT_DESC carLayout[]={layout[0],layout[1],layout[2],
        {"INSTANCE",0,DXGI_FORMAT_R32G32B32A32_FLOAT,1,0,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1},
        {"COLOR",0,DXGI_FORMAT_R32_UINT,1,16,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1}};
    p.InputLayout={carLayout,5};p.VS={carVs.data(),carVs.size()};p.PS={carPs.data(),carPs.size()};
    check(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&carPipeline_)),"Create car pipeline");
    auto buildingVs=shader(L"buildings.vs.cso");
    D3D12_INPUT_ELEMENT_DESC buildingLayout[]={layout[0],layout[1],layout[2],{"INSTANCEOFFSET",0,DXGI_FORMAT_R32G32_FLOAT,1,0,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1}};
    p.InputLayout={buildingLayout,4};p.VS={buildingVs.data(),buildingVs.size()};p.PS={ps.data(),ps.size()};
    check(device_->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&buildingPipeline_)),"Create building pipeline");
}
static Mesh carMesh() {
    Mesh m;
    auto box=[&](float x0,float y0,float z0,float x1,float y1,float z1,uint32_t material) {
        const float vertices[8][3]={{x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},{x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}};
        const int faces[6][4]={{0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{3,2,6,7},{4,5,1,0}};
        const float normals[6][3]={{0,0,-1},{0,0,1},{-1,0,0},{1,0,0},{0,1,0},{0,-1,0}};
        for(int f=0;f<6;++f){uint32_t base=uint32_t(m.vertices.size());for(int k:faces[f])m.vertices.push_back({vertices[k][0],vertices[k][1],vertices[k][2],normals[f][0],normals[f][1],normals[f][2],material});
            for(int k:{0,1,2,0,2,3})m.indices.push_back(base+uint32_t(k));}
    };
    box(-1.5f,0.6f,-3,1.5f,1.9f,3,0);box(-1.2f,1.9f,-1.6f,1.2f,2.9f,1.3f,1);
    for(float x:{-1.55f,1.05f})for(float z:{-2.f,1.2f})box(x,0.15f,z,x+0.5f,1,z+0.8f,2);
    box(-1.2f,1,2.99f,1.2f,1.4f,3.01f,3);
    return m;
}
std::shared_ptr<Renderer::GpuMesh> Renderer::uploadMesh(const Mesh& mesh,Frame& frame,bool raytraced) {
    auto result=std::make_shared<GpuMesh>();
    auto upload=[&](const void* source,size_t bytes,D3D12_RESOURCE_STATES finalState) {
        auto staging=buffer(bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        void* mapped=nullptr;D3D12_RANGE empty{0,0};check(staging->Map(0,&empty,&mapped),"Map geometry");std::memcpy(mapped,source,bytes);staging->Unmap(0,nullptr);
        auto resource=buffer(bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
        list_->CopyBufferRegion(resource.Get(),0,staging.Get(),0,bytes);
        auto b=transition(resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,finalState);list_->ResourceBarrier(1,&b);frame.temporary.push_back(staging);return resource;
    };
    result->vertices=upload(mesh.vertices.data(),mesh.vertices.size()*sizeof(Vertex),D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    result->indices=upload(mesh.indices.data(),mesh.indices.size()*sizeof(uint32_t),D3D12_RESOURCE_STATE_INDEX_BUFFER|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    result->indexCount=uint32_t(mesh.indices.size());
    result->vb={result->vertices->GetGPUVirtualAddress(),UINT(mesh.vertices.size()*sizeof(Vertex)),sizeof(Vertex)};
    result->ib={result->indices->GetGPUVirtualAddress(),UINT(mesh.indices.size()*sizeof(uint32_t)),DXGI_FORMAT_R32_UINT};
    if(!raytraced)return result;
    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};geometry.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;geometry.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.VertexBuffer={result->vb.BufferLocation,sizeof(Vertex)};geometry.Triangles.VertexCount=UINT(mesh.vertices.size());geometry.Triangles.VertexFormat=DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.Triangles.IndexBuffer=result->ib.BufferLocation;geometry.Triangles.IndexCount=result->indexCount;geometry.Triangles.IndexFormat=DXGI_FORMAT_R32_UINT;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};inputs.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;inputs.NumDescs=1;inputs.pGeometryDescs=&geometry;inputs.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO size{};device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs,&size);
    result->blas=buffer(size.ResultDataMaxSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto scratch=buffer(size.ScratchDataSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};build.Inputs=inputs;build.DestAccelerationStructureData=result->blas->GetGPUVirtualAddress();build.ScratchAccelerationStructureData=scratch->GetGPUVirtualAddress();
    list_->BuildRaytracingAccelerationStructure(&build,0,nullptr);uav(list_.Get(),result->blas.Get());frame.temporary.push_back(scratch);return result;
}
void Renderer::buildScene(Frame& frame) {
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};inputs.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;inputs.NumDescs=UINT(ChunkCount+sceneParcels_.size());inputs.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    if(!frame.tlas || frame.sceneCapacity<inputs.NumDescs) {
        frame.sceneCapacity=std::max(inputs.NumDescs,frame.sceneCapacity*2);
        unsigned actual=inputs.NumDescs;inputs.NumDescs=frame.sceneCapacity;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO size{};device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs,&size);
        frame.tlas=buffer(size.ResultDataMaxSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        frame.scratch=buffer(size.ScratchDataSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        frame.instances=buffer(sizeof(D3D12_RAYTRACING_INSTANCE_DESC)*frame.sceneCapacity,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        inputs.NumDescs=actual;
    }
    D3D12_RAYTRACING_INSTANCE_DESC* instances=nullptr;D3D12_RANGE empty{0,0};check(frame.instances->Map(0,&empty,reinterpret_cast<void**>(&instances)),"Map instances");
    for(int i=0;i<ChunkCount;++i) {
        instances[i]={};auto& d=instances[i];d.Transform[0][0]=d.Transform[1][1]=d.Transform[2][2]=1;
        d.Transform[0][3]=float(i%ChunksAcross*ChunkSize);d.Transform[2][3]=float(i/ChunksAcross*ChunkSize);
        d.InstanceID=i;d.InstanceMask=0xff;d.AccelerationStructure=chunks_[i]->blas->GetGPUVirtualAddress();
    }
    for(size_t i=0;i<sceneParcels_.size();++i){auto& d=instances[ChunkCount+i];const auto& p=sceneParcels_[i];d={};d.Transform[0][0]=d.Transform[1][1]=d.Transform[2][2]=1;
        d.Transform[0][3]=p.position.x;d.Transform[2][3]=p.position.z;d.InstanceID=UINT(ChunkCount+i);d.InstanceMask=0xff;d.AccelerationStructure=parcelMeshes_[p.variant]->blas->GetGPUVirtualAddress();}
    frame.instances->Unmap(0,nullptr);inputs.InstanceDescs=frame.instances->GetGPUVirtualAddress();
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};build.Inputs=inputs;build.DestAccelerationStructureData=frame.tlas->GetGPUVirtualAddress();build.ScratchAccelerationStructureData=frame.scratch->GetGPUVirtualAddress();
    list_->BuildRaytracingAccelerationStructure(&build,0,nullptr);uav(list_.Get(),frame.tlas.Get());frame.generation=worldGeneration_;
}
void Renderer::collectDebugMessages() {
    ComPtr<ID3D12InfoQueue> info;
    if(SUCCEEDED(device_.As(&info))) {
        for(uint64_t i=0;i<info->GetNumStoredMessages();++i) {
            SIZE_T size=0;info->GetMessage(i,nullptr,&size);std::vector<char> storage(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if(SUCCEEDED(info->GetMessage(i,message,&size)) && message->Severity<=D3D12_MESSAGE_SEVERITY_WARNING) {
                if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR) ++stats.debugErrors;
                OutputDebugStringA(message->pDescription);
                std::ofstream log("d3d12-debug.log",std::ios::app);log<<message->pDescription<<'\n';
            }
        }
        info->ClearStoredMessages();
    }
}
void Renderer::render(World& world,const View& view,bool vsync,const std::filesystem::path& capture,std::span<const CarInstance> cars,UiDrawData* ui) {
    assertOwner();
    unsigned slot=frameNumber_%FrameCount;auto& frame=frames_[slot];wait(frame.fence);
    if(frame.fence) {
        uint64_t* times=nullptr;D3D12_RANGE range{slot*2*sizeof(uint64_t),(slot*2+2)*sizeof(uint64_t)};
        check(timestampReadback_->Map(0,&range,reinterpret_cast<void**>(&times)),"Read GPU timing");
        stats.gpuMs=double(times[slot*2+1]-times[slot*2])*1000/double(timestampFrequency_);D3D12_RANGE empty{0,0};timestampReadback_->Unmap(0,&empty);
    }
    frame.temporary.clear();frame.retired.clear();check(frame.allocator->Reset(),"Reset allocator");check(list_->Reset(frame.allocator.Get(),pipeline_.Get()),"Reset commands");
    list_->EndQuery(timestamps_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*2);
    unsigned index=swapchain_->GetCurrentBackBufferIndex();auto* target=backbuffers_[index].Get();
    auto barrier=transition(target,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);list_->ResourceBarrier(1,&barrier);
    auto rtv=rtvHeap_->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=index*rtvStride_;auto dsv=dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    float clear[]={0.26f,0.34f,0.39f,1};list_->ClearRenderTargetView(rtv,clear,0,nullptr);list_->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);list_->OMSetRenderTargets(1,&rtv,FALSE,&dsv);
    D3D12_VIEWPORT viewport{0,0,float(width_),float(height_),0,1};D3D12_RECT scissor{0,0,LONG(width_),LONG(height_)};list_->RSSetViewports(1,&viewport);list_->RSSetScissorRects(1,&scissor);
    if(lastVoxelMode_!=view.voxels){world.dirtyAll();lastVoxelMode_=view.voxels;}
    if(view.voxels) {
        if(!voxelRenderer_){voxelRenderer_=std::make_unique<VoxelRenderer>(device_.Get());voxelRenderer_->resize(width_,height_);}
        voxelRenderer_->render(list_.Get(),frameNumber_,world,view,cars,rtv,stats);
    } else {
    if(!flatMesh_) {
        Mesh flat;flat.vertices={{0,0,0,0,1,0,0},{0,0,float(ChunkSize),0,1,0,0},{float(ChunkSize),0,float(ChunkSize),0,1,0,0},{float(ChunkSize),0,0,0,1,0,0}};flat.indices={0,1,2,0,2,3};
        flatMesh_=uploadMesh(flat,frame);chunks_.fill(flatMesh_);
    }
    auto dirty=world.takeDirty();stats.rebuiltChunks=0;
    auto install=[&](int chunk,const Mesh& mesh){auto next=mesh.indices.empty()?flatMesh_:uploadMesh(mesh,frame);frame.retired.push_back(chunks_[chunk]);chunks_[chunk]=next;++stats.rebuiltChunks;++worldGeneration_;};
    for(int chunk:dirty)++chunkRevisions_[chunk];
    if(meshing_.valid()&&meshing_.wait_for(std::chrono::seconds(0))==std::future_status::ready)
        for(auto& result:meshing_.get())if(result.revision==chunkRevisions_[result.chunk])install(result.chunk,result.mesh);
    if(initialWorld_ || (dirty.size()<=8&&pendingChunks_.empty()&&!meshing_.valid())){
        for(int chunk:dirty)install(chunk,world.chunkEmpty(chunk)?Mesh{}:world.mesh(chunk));
    }else pendingChunks_.insert(dirty.begin(),dirty.end());
    initialWorld_=false;
    if(!meshing_.valid()&&!pendingChunks_.empty()){
        auto snapshot=std::make_shared<World>(world);std::vector<std::pair<int,uint64_t>> batch;
        while(!pendingChunks_.empty()&&batch.size()<16){int chunk=*pendingChunks_.begin();pendingChunks_.erase(pendingChunks_.begin());batch.push_back({chunk,chunkRevisions_[chunk]});}
        meshing_=std::async(std::launch::async,[snapshot,batch=std::move(batch)](){std::vector<MeshedChunk> results;for(auto [chunk,revision]:batch)results.push_back({chunk,revision,snapshot->chunkEmpty(chunk)?Mesh{}:snapshot->mesh(chunk)});return results;});
    }
    if(parcelRevision_!=world.parcelRevision() || !dirty.empty()){
        sceneParcels_.clear();const auto& parcels=world.parcels();
        for(size_t t=0;t<parcels.size();++t){auto p=parcels[t];if(!p.kind)continue;size_t key=p.kind*12+p.level*4+p.variant;
            if(!parcelMeshes_[key])parcelMeshes_[key]=uploadMesh(World::parcelMesh(p),frame);
            if(!parcelMeshes_[key+ParcelVariants])parcelMeshes_[key+ParcelVariants]=uploadMesh(World::parcelMesh(p,true),frame,false);
            sceneParcels_.push_back({{float(t%MapSize*TileSize),float(t/MapSize*TileSize)},key});}
        parcelRevision_=world.parcelRevision();++worldGeneration_;
    }
    if(!dirty.empty()) ++worldGeneration_;
    if(frame.generation!=worldGeneration_) buildScene(frame);
    if(!cars.empty() && !carMesh_)carMesh_=uploadMesh(carMesh(),frame,false);
    if(!frame.styles)frame.styles=buffer(MapSize*MapSize*sizeof(uint32_t),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    // Three fence-protected style buffers avoid rebuilding geometry for data overlays.
    {void* output=nullptr;D3D12_RANGE empty{0,0};check(frame.styles->Map(0,&empty,&output),"Map city overlay");std::memcpy(output,world.tileStyles().data(),world.tileStyles().size()*sizeof(uint32_t));frame.styles->Unmap(0,nullptr);}
    list_->SetGraphicsRootSignature(root_.Get());list_->SetGraphicsRootShaderResourceView(1,frame.tlas->GetGPUVirtualAddress());list_->SetGraphicsRootShaderResourceView(2,frame.styles->GetGPUVirtualAddress());list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    struct Constants {XMFLOAT4X4 vp;XMFLOAT4 offset,eye,hover,sun,options;} constants{};
    static_assert(sizeof(Constants)==36*4);
    constants.vp=view.viewProjection;constants.eye={view.eye.x,view.eye.y,view.eye.z,0};constants.hover={float(view.hover.x),float(view.hover.z),(view.erase?1.f:0.f)+2.f*(view.hoverSpan-1),view.grid?1.f:0.f};
    constants.sun={-0.65f,1,0.4f,view.signalPhase};constants.options={view.shadows?1.f:0.f,float(TileSize),float(GridSize),float(WorldSize)};
    // Extract world-space frustum planes from the row-vector view-projection matrix.
    XMMATRIX m=XMMatrixTranspose(XMLoadFloat4x4(&view.viewProjection));
    XMVECTOR planes[]={m.r[3]+m.r[0],m.r[3]-m.r[0],m.r[3]+m.r[1],m.r[3]-m.r[1],m.r[2],m.r[3]-m.r[2]};
    stats.triangles=0;stats.visibleTriangles=0;stats.visibleChunks=0;
    for(int i=0;i<ChunkCount;++i) {
        const auto& mesh=chunks_[i];stats.triangles+=mesh->indexCount/3;
        float x=float(i%ChunksAcross*ChunkSize),z=float(i/ChunksAcross*ChunkSize);bool visible=true;
        for(auto plane:planes) {
            XMFLOAT4 p;XMStoreFloat4(&p,plane);
            float px=x+(p.x>=0?ChunkSize:0),py=p.y>=0?24.f:0.f,pz=z+(p.z>=0?ChunkSize:0);
            if(p.x*px+p.y*py+p.z*pz+p.w<0) {visible=false;break;}
        }
        if(!visible) continue;
        ++stats.visibleChunks;stats.visibleTriangles+=mesh->indexCount/3;
        constants.offset={x,z,0,0};list_->SetGraphicsRoot32BitConstants(0,36,&constants,0);
        list_->IASetVertexBuffers(0,1,&mesh->vb);list_->IASetIndexBuffer(&mesh->ib);list_->DrawIndexedInstanced(mesh->indexCount,1,0,0,0);
    }

    for(auto& group:parcelGroups_)group.clear();size_t visibleBuildings=0;
    for(const auto& p:sceneParcels_){bool visible=true;for(auto plane:planes){XMFLOAT4 f;XMStoreFloat4(&f,plane);if(f.x*(p.position.x+8)+f.y*11+f.z*(p.position.z+8)+f.w+11*(std::abs(f.x)+std::abs(f.y)+std::abs(f.z))<0){visible=false;break;}}
        stats.triangles+=parcelMeshes_[p.variant]->indexCount/3;if(!visible)continue;
        float dx=p.position.x-view.eye.x,dz=p.position.z-view.eye.z;size_t key=p.variant+(dx*dx+dz*dz>1000*1000?ParcelVariants:0);parcelGroups_[key].push_back(p.position);++visibleBuildings;}
    if(visibleBuildings){
        if(frame.buildingCapacity<visibleBuildings){frame.buildingCapacity=std::max(visibleBuildings,std::max(size_t(1024),frame.buildingCapacity*2));frame.buildings=buffer(frame.buildingCapacity*sizeof(ParcelInstance),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);}
        ParcelInstance* output=nullptr;D3D12_RANGE empty{0,0};check(frame.buildings->Map(0,&empty,reinterpret_cast<void**>(&output)),"Map building instances");size_t offset=0;
        for(const auto& group:parcelGroups_){std::copy(group.begin(),group.end(),output+offset);offset+=group.size();}frame.buildings->Unmap(0,nullptr);
        list_->SetPipelineState(buildingPipeline_.Get());constants.offset={0,0,0,0};list_->SetGraphicsRoot32BitConstants(0,36,&constants,0);offset=0;
        for(size_t key=0;key<parcelGroups_.size();++key){const auto& group=parcelGroups_[key];if(group.empty())continue;const auto& mesh=parcelMeshes_[key];
            D3D12_VERTEX_BUFFER_VIEW views[]={mesh->vb,{frame.buildings->GetGPUVirtualAddress()+offset*sizeof(ParcelInstance),UINT(group.size()*sizeof(ParcelInstance)),sizeof(ParcelInstance)}};
            list_->IASetVertexBuffers(0,2,views);list_->IASetIndexBuffer(&mesh->ib);list_->DrawIndexedInstanced(mesh->indexCount,UINT(group.size()),0,0,0);offset+=group.size();stats.visibleTriangles+=group.size()*mesh->indexCount/3;}
    }
    stats.visibleCars=0;
    if(!cars.empty()) {
        if(frame.carCapacity<cars.size()) {frame.carCapacity=std::max(cars.size(),std::max(size_t(65536),frame.carCapacity*2));frame.cars=buffer(frame.carCapacity*sizeof(CarInstance),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);}
        CarInstance* output=nullptr;D3D12_RANGE empty{0,0};check(frame.cars->Map(0,&empty,reinterpret_cast<void**>(&output)),"Map car instances");
        for(const auto& car:cars) {
            bool visible=true;for(auto plane:planes){XMFLOAT4 p;XMStoreFloat4(&p,plane);
                if(p.x*car.x+p.y*1.5f+p.z*car.z+p.w+4.f*(std::abs(p.x)+std::abs(p.y)+std::abs(p.z))<0){visible=false;break;}}
            if(visible)output[stats.visibleCars++]=car;
        }
        frame.cars->Unmap(0,nullptr);
        if(stats.visibleCars){
            list_->SetPipelineState(carPipeline_.Get());list_->SetGraphicsRoot32BitConstants(0,36,&constants,0);
            D3D12_VERTEX_BUFFER_VIEW views[]={carMesh_->vb,{frame.cars->GetGPUVirtualAddress(),UINT(stats.visibleCars*sizeof(CarInstance)),sizeof(CarInstance)}};
            list_->IASetVertexBuffers(0,2,views);list_->IASetIndexBuffer(&carMesh_->ib);list_->DrawIndexedInstanced(carMesh_->indexCount,stats.visibleCars,0,0,0);
        }
        stats.triangles+=cars.size()*carMesh_->indexCount/3;stats.visibleTriangles+=stats.visibleCars*carMesh_->indexCount/3;
    }
    }
    ID3D12DescriptorHeap* heaps[]={srvHeap_.Get()};list_->SetDescriptorHeaps(1,heaps);if(view.showUI&&ui)ImGui_ImplDX12_RenderDrawData(ui,list_.Get());
    ComPtr<ID3D12Resource> screenshot;D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    if(!capture.empty()) {
        auto desc=target->GetDesc();uint64_t bytes=0;device_->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
        screenshot=buffer(bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        barrier=transition(target,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);list_->ResourceBarrier(1,&barrier);
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=target;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=screenshot.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;list_->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        barrier=transition(target,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PRESENT);
    } else barrier=transition(target,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);
    list_->ResourceBarrier(1,&barrier);list_->EndQuery(timestamps_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*2+1);
    list_->ResolveQueryData(timestamps_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*2,2,timestampReadback_.Get(),slot*2*sizeof(uint64_t));
    check(list_->Close(),"Close frame");ID3D12CommandList* lists[]={list_.Get()};queue_->ExecuteCommandLists(1,lists);
    check(swapchain_->Present(vsync?1:0,(!vsync&&tearing_)?DXGI_PRESENT_ALLOW_TEARING:0),"Present frame");
    frame.fence=++fenceValue_;check(queue_->Signal(fence_.Get(),frame.fence),"Signal frame");++frameNumber_;
    if(screenshot) {
        wait(frame.fence);void* data=nullptr;check(screenshot->Map(0,nullptr,&data),"Read screenshot");
        BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+width_*height_*4;
        BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=LONG(width_);info.biHeight=-LONG(height_);info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;info.biSizeImage=width_*height_*4;
        std::ofstream output(capture,std::ios::binary);output.write(reinterpret_cast<const char*>(&file),sizeof(file));output.write(reinterpret_cast<const char*>(&info),sizeof(info));
        std::vector<uint8_t> row(width_*4);
        for(unsigned y=0;y<height_;++y) {
            auto* src=static_cast<uint8_t*>(data)+footprint.Offset+y*footprint.Footprint.RowPitch;
            for(unsigned x=0;x<width_;++x) {row[x*4]=src[x*4+2];row[x*4+1]=src[x*4+1];row[x*4+2]=src[x*4];row[x*4+3]=255;}
            output.write(reinterpret_cast<const char*>(row.data()),row.size());
        }
        D3D12_RANGE empty{0,0};screenshot->Unmap(0,&empty);if(!output) throw std::runtime_error("Could not write screenshot.");
    }
    DXGI_QUERY_VIDEO_MEMORY_INFO memory{};adapter_->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&memory);stats.videoMemory=memory.CurrentUsage;stats.peakVideoMemory=std::max(stats.peakVideoMemory,stats.videoMemory);
    collectDebugMessages();
}
}
