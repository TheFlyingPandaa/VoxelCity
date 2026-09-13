#include "VoxelRenderer.h"
#include "Renderer.h"
#include "Voxels.h"
#include <d3dcompiler.h>
#include <fstream>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <random>
#include <chrono>

namespace vc {
using namespace DirectX;
namespace {
void checked(HRESULT hr,const char* what){if(FAILED(hr))throw std::runtime_error(std::string(what)+" (HRESULT "+std::to_string(uint32_t(hr))+")");}
void barrier(ID3D12GraphicsCommandList4* l,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){if(a==b)return;D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&v);}
void order(ID3D12GraphicsCommandList4* l,ID3D12Resource* r){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=r;l->ResourceBarrier(1,&b);}
std::vector<char> code(const wchar_t* name){wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);std::ifstream f(std::filesystem::path(exe).parent_path()/L"shaders"/name,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("Missing voxel shader. Rebuild shaders.");std::vector<char> v(size_t(f.tellg()));f.seekg(0);f.read(v.data(),v.size());return v;}
struct Instance {uint32_t primitive,words,id,color;XMFLOAT4 pose,previous,roof,roofProfile,height;};
static_assert(sizeof(Instance)==96);
struct Constants {XMFLOAT4X4 inverse,vp,previous;XMFLOAT4 eye,oldEye,sun,screen,settings,hover;};
}
struct VoxelRenderer::Impl {
    ID3D12Device5* device;
    ID3D12GraphicsCommandList4* list=nullptr;
    struct Model {VoxelModel cpu;ComPtr<ID3D12Resource> blas;uint32_t first=0,words=0;uint64_t bytes=0;D3D12_GPU_VIRTUAL_ADDRESS address=0;XMFLOAT4 roof{},roofProfile{};};
    struct Frame {
        ComPtr<ID3D12Resource> tlas,scratch,descs,instances,constants,styles;
        unsigned capacity=0;
        ComPtr<ID3D12Resource> primitiveData,wordData,primitiveUpload,wordUpload;
        size_t primitiveCapacity=0,wordCapacity=0;
        uint64_t dataRevision=0;
        std::vector<ComPtr<ID3D12Resource>> retired;
        std::vector<std::shared_ptr<Model>> oldModels;
    };
    std::array<Frame,3> frames;
    std::array<std::shared_ptr<Model>,ChunkCount> terrain;
    std::unordered_map<uint64_t,std::vector<std::weak_ptr<Model>>> terrainCache;
    std::map<unsigned,std::shared_ptr<Model>> buildings;
    std::shared_ptr<Model> flat,car;
    std::array<std::shared_ptr<Model>,3> trainModels;
    std::array<std::shared_ptr<Model>,TreeVariantCount> treeModels;
    std::array<std::vector<TreeInstance>,ChunkCount> treeChunks;
    std::array<uint64_t,ChunkCount> treeRevisions{};
    size_t treeCount=0;
    std::vector<VoxelPrimitive> packedPrimitives;
    std::vector<uint32_t> packedWords;
    uint64_t dataRevision=0;
    std::vector<std::pair<size_t,std::shared_ptr<Model>>> parcels;
    struct CarPose {XMFLOAT4 pose;unsigned frame;uint32_t id;float y=0;};
    uint32_t nextCarId=0x800000u;
    std::unordered_map<uint64_t,CarPose> oldCars;
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> sceneDescs;
    std::vector<Instance> sceneInstances;
    uint64_t parcelRevision=~0ull,replacement=~0ull,styleRevision=~0ull;
    bool buffersDirty=true,validHistory=false;
    unsigned width=0,height=0,previousFrame=~0u;int oldQuality=-1;bool oldShadows=true;
    XMFLOAT4X4 previousVP{};XMFLOAT3 previousEye{};
    ComPtr<ID3D12RootSignature> root;
    std::array<ComPtr<ID3D12PipelineState>,4> pipelines;
    ComPtr<ID3D12DescriptorHeap> heap,rtvs;
    unsigned srvStride=0,rtvStride=0;
    // Two sets of full-resolution surfaces, two HDR histories, one half-res light.
    struct Texture {ComPtr<ID3D12Resource> resource;D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;DXGI_FORMAT format;};
    std::array<Texture,11> textures;
    ComPtr<ID3D12QueryHeap> queries;ComPtr<ID3D12Resource> readback;
    bool timingReady[3]{};
    uint64_t timestampFrequency=1;
    Impl(ID3D12Device5* d):device(d){
        D3D12_ROOT_PARAMETER p[7]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;p[0].Descriptor={0,0};
        for(unsigned i=1;i<=5;++i){p[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p[i].Descriptor={i-1,0};}
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,9,5,0,0};p[6].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[6].DescriptorTable={1,&range};
        D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ShaderRegister=0;sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=7;desc.pParameters=p;desc.NumStaticSamplers=1;desc.pStaticSamplers=&sampler;
        ComPtr<ID3DBlob> blob,error;checked(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"Serialize voxel root");checked(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"Create voxel root");
        auto vs=code(L"voxel.vs.cso");const wchar_t* names[]={L"voxel.surface.cso",L"voxel.light.cso",L"voxel.resolve.cso",L"voxel.post.cso"};
        for(int i=0;i<4;++i){auto ps=code(names[i]);D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};pso.pRootSignature=root.Get();pso.VS={vs.data(),vs.size()};pso.PS={ps.data(),ps.size()};pso.SampleMask=UINT_MAX;pso.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pso.RasterizerState.DepthClipEnable=TRUE;pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pso.SampleDesc.Count=1;
            pso.NumRenderTargets=i==0?4:1;for(unsigned j=0;j<pso.NumRenderTargets;++j){pso.BlendState.RenderTarget[j].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;pso.RTVFormats[j]=i==3?DXGI_FORMAT_R8G8B8A8_UNORM:(i==0&&j==0?DXGI_FORMAT_R32G32B32A32_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT);}
            checked(device->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&pipelines[i])),"Create voxel pipeline");}
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=18;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"Create voxel SRVs");srvStride=device->GetDescriptorHandleIncrementSize(hd.Type);
        hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=11;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;checked(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvs)),"Create voxel RTVs");rtvStride=device->GetDescriptorHandleIncrementSize(hd.Type);
        D3D12_QUERY_HEAP_DESC q{};q.Count=15;q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;checked(device->CreateQueryHeap(&q,IID_PPV_ARGS(&queries)),"Create voxel timings");
        readback=buffer(15*sizeof(uint64_t),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        // Timestamp frequency is constant for direct queues on a device.
        ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC qd{};qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;checked(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"Create timing queue");queue->GetTimestampFrequency(&timestampFrequency);
    }
    ComPtr<ID3D12Resource> buffer(uint64_t size,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE){
        D3D12_HEAP_PROPERTIES h{};h.Type=type;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=std::max(256ull,(size+255)&~255ull);d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.Flags=flags;
        auto initial=type==D3D12_HEAP_TYPE_DEFAULT&&state!=D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE?D3D12_RESOURCE_STATE_COMMON:state;
        ComPtr<ID3D12Resource> r;checked(device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,initial,nullptr,IID_PPV_ARGS(&r)),"Allocate voxel buffer");if(initial!=state)barrier(list,r.Get(),initial,state);return r;
    }
    void fill(ID3D12Resource* r,const void* source,size_t bytes){void* p=nullptr;D3D12_RANGE empty{};checked(r->Map(0,&empty,&p),"Map voxel data");if(bytes)std::memcpy(p,source,bytes);r->Unmap(0,nullptr);}
    std::shared_ptr<Model> model(VoxelModel cpu,Frame& f){
        auto m=std::make_shared<Model>();m->cpu=std::move(cpu);std::vector<D3D12_RAYTRACING_AABB> boxes;
        for(const auto& p:m->cpu.primitives)boxes.push_back({p.lo[0],p.lo[1],p.lo[2],p.hi[0],p.hi[1],p.hi[2]});
        auto a=buffer(boxes.size()*sizeof(boxes[0]),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);fill(a.Get(),boxes.data(),boxes.size()*sizeof(boxes[0]));
        D3D12_RAYTRACING_GEOMETRY_DESC geometry{};geometry.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;geometry.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;geometry.AABBs.AABBCount=boxes.size();geometry.AABBs.AABBs={a->GetGPUVirtualAddress(),sizeof(boxes[0])};
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};in.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;in.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;in.NumDescs=1;in.pGeometryDescs=&geometry;in.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO size{};device->GetRaytracingAccelerationStructurePrebuildInfo(&in,&size);
        m->blas=buffer(size.ResultDataMaxSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        m->bytes=size.ResultDataMaxSizeInBytes;m->address=m->blas->GetGPUVirtualAddress();
        auto scratch=buffer(size.ScratchDataSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC b{};b.Inputs=in;b.DestAccelerationStructureData=m->blas->GetGPUVirtualAddress();b.ScratchAccelerationStructureData=scratch->GetGPUVirtualAddress();list->BuildRaytracingAccelerationStructure(&b,0,nullptr);order(list,m->blas.Get());f.retired.push_back(a);f.retired.push_back(scratch);buffersDirty=true;return m;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv(unsigned i){auto h=rtvs->GetCPUDescriptorHandleForHeapStart();h.ptr+=i*rtvStride;return h;}
    void state(unsigned i,D3D12_RESOURCE_STATES s){auto& t=textures[i];barrier(list,t.resource.Get(),t.state,s);t.state=s;}
    void resize(unsigned w,unsigned h){
        width=w;height=h;validHistory=false;
        for(unsigned i=0;i<11;++i){auto& t=textures[i];t.resource.Reset();t.state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;t.format=i<8&&i%4==0?DXGI_FORMAT_R32G32B32A32_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT;
            D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=i==10?(w+1)/2:w;d.Height=i==10?(h+1)/2:h;d.DepthOrArraySize=1;d.MipLevels=1;d.Format=t.format;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
            checked(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,t.state,nullptr,IID_PPV_ARGS(&t.resource)),"Create voxel surface");device->CreateRenderTargetView(t.resource.Get(),nullptr,rtv(i));}
        for(unsigned ping=0;ping<2;++ping){unsigned ids[]={ping*4,ping*4+1,ping*4+2,ping*4+3,(1-ping)*4,(1-ping)*4+1,8+1-ping,10,8+ping};
            for(unsigned j=0;j<9;++j){auto hnd=heap->GetCPUDescriptorHandleForHeapStart();hnd.ptr+=(ping*9+j)*srvStride;D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=textures[ids[j]].format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;device->CreateShaderResourceView(textures[ids[j]].resource.Get(),&srv,hnd);}}
    }
    void render(ID3D12GraphicsCommandList4* l,unsigned number,World& world,const View& view,std::span<const CarInstance> cars,D3D12_CPU_DESCRIPTOR_HANDLE target,RenderStats& stats){
        using CpuClock=std::chrono::steady_clock;auto cpuStart=CpuClock::now();
        auto elapsed=[&](){auto now=CpuClock::now();double ms=std::chrono::duration<double,std::milli>(now-cpuStart).count();cpuStart=now;return ms;};
        list=l;unsigned slot=number%3,ping=number%2;auto& f=frames[slot];f.retired.clear();f.oldModels.clear();stats.voxelCpuRetireMs=elapsed();
        if(timingReady[slot]){uint64_t* p=nullptr;D3D12_RANGE range{slot*5*sizeof(uint64_t),(slot+1)*5*sizeof(uint64_t)};checked(readback->Map(0,&range,reinterpret_cast<void**>(&p)),"Read voxel timings");double factor=1000./double(timestampFrequency);stats.voxelSceneMs=(p[slot*5+1]-p[slot*5])*factor;stats.voxelSurfaceMs=(p[slot*5+2]-p[slot*5+1])*factor;stats.voxelLightMs=(p[slot*5+3]-p[slot*5+2])*factor;stats.voxelResolveMs=(p[slot*5+4]-p[slot*5+3])*factor;D3D12_RANGE empty{};readback->Unmap(0,&empty);}
        list->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*5);
        auto dirty=world.takeDirty();bool changed=!dirty.empty()||parcelRevision!=world.parcelRevision()||replacement!=world.replacementRevision();
        if(!flat){VoxelModel ground;ground.primitives.push_back({{0,-.25f,0},0,{float(ChunkSize),0,float(ChunkSize)},VGrass});flat=model(std::move(ground),f);terrain.fill(flat);car=model(carVoxels(),f);for(unsigned i=0;i<3;++i)trainModels[i]=model(trainVoxels(i+1),f);}
        stats.rebuiltChunks=0;
        for(int chunk:dirty){
            auto next=flat;
            if(!world.chunkEmpty(chunk)){
                auto geometry=terrainVoxels(world,chunk);const auto& previous=terrain[chunk]->cpu;
                // Neighbor invalidation can leave a chunk visually identical.
                // Keep its GPU model and packed offsets when every voxel agrees.
                if(geometry.words==previous.words&&geometry.primitives.size()==previous.primitives.size()&&
                   (geometry.primitives.empty()||std::memcmp(geometry.primitives.data(),previous.primitives.data(),geometry.primitives.size()*sizeof(VoxelPrimitive))==0))continue;
                // A sampled hash only selects candidates; full comparison below
                // makes sharing exact even when different chunks hash alike.
                uint64_t key=1469598103934665603ull;
                auto mix=[&](uint64_t value){key=(key^value)*1099511628211ull;};
                mix(geometry.primitives.size());mix(geometry.words.size());
                const auto* bytes=reinterpret_cast<const unsigned char*>(geometry.primitives.data());
                for(size_t i=0;i<geometry.primitives.size()*sizeof(VoxelPrimitive);++i)mix(bytes[i]);
                for(size_t i=0;i<geometry.words.size();i+=std::max(size_t(1),geometry.words.size()/64))mix(geometry.words[i]);
                auto& candidates=terrainCache[key];
                std::erase_if(candidates,[](const auto& entry){return entry.expired();});
                next.reset();
                for(const auto& entry:candidates)if(auto candidate=entry.lock()){
                    const auto& cached=candidate->cpu;
                    if(geometry.words==cached.words&&geometry.primitives.size()==cached.primitives.size()&&
                       (geometry.primitives.empty()||std::memcmp(geometry.primitives.data(),cached.primitives.data(),geometry.primitives.size()*sizeof(VoxelPrimitive))==0)){
                        next=std::move(candidate);break;
                    }
                }
                if(!next){next=model(std::move(geometry),f);candidates.push_back(next);}
            }
            if(next==terrain[chunk])continue;
            f.oldModels.push_back(terrain[chunk]);terrain[chunk]=next;buffersDirty=true;++stats.rebuiltChunks;
        }
        if(number%60==0)std::erase_if(terrainCache,[](auto& bucket){
            std::erase_if(bucket.second,[](const auto& entry){return entry.expired();});return bucket.second.empty();
        });
        if(parcelRevision!=world.parcelRevision()||replacement!=world.replacementRevision()){
            parcels.clear();for(size_t i=0;i<world.parcels().size();++i){auto p=world.parcels()[i];if(!p.kind)continue;unsigned key=unsigned(p.kind)*24+p.level*8+p.variant;auto& m=buildings[key];if(!m){m=model(buildingVoxels(p),f);
                if(p.kind==1&&p.level){
                    bool dense=p.variant>=4;uint8_t art=p.variant&3;bool narrow=!dense&&art==2;
                    m->roof={(!narrow&&(art&2))?0.f:1.f,(!narrow&&(art&2))?1.f:0.f,narrow?7.5f:8.f,dense?14.f+p.level*10.f+(art%3)*2.f:5.f+p.level*3.f+(narrow?0.f:(art%3)*1.5f)};
                    m->roofProfile=narrow?XMFLOAT4{4,.75f,.375f,.5f}:XMFLOAT4{5.75f,.75f,.1875f,.25f};
                }
                if(p.kind==3&&p.level){m->roof={1,0,2,4.5f+p.level*.75f};m->roofProfile={4,.5f,.125f,.125f};}
            }parcels.emplace_back(i,m);}parcelRevision=world.parcelRevision();}
        treeCount=0;
        for(int chunk=0;chunk<ChunkCount;++chunk){
            if(replacement!=world.replacementRevision()||treeRevisions[chunk]!=world.vegetationChunkRevision(chunk)){
                treeChunks[chunk]=world.trees(chunk);treeRevisions[chunk]=world.vegetationChunkRevision(chunk);changed=true;
            }
            treeCount+=treeChunks[chunk].size();
        }
        if(treeCount&&!treeModels[0])for(uint8_t i=0;i<TreeVariantCount;++i)treeModels[i]=model(treeVoxels(i),f);
        if(replacement!=world.replacementRevision()){oldCars.clear();nextCarId=0x800000u;}
        replacement=world.replacementRevision();stats.voxelCpuTerrainMs=elapsed();
        if(buffersDirty){
            auto& all=packedPrimitives;auto& packed=packedWords;all.clear();packed.clear();std::vector<std::shared_ptr<Model>> unique{flat,car};
            std::unordered_set<const Model*> seen{flat.get(),car.get()};
            for(const auto& m:terrain)if(seen.insert(m.get()).second)unique.push_back(m);for(const auto& [key,m]:buildings)unique.push_back(m);
            for(const auto& m:treeModels)if(m)unique.push_back(m);for(auto m:trainModels)if(m)unique.push_back(m);
            size_t primitiveCount=0,wordCount=0;for(const auto& m:unique){primitiveCount+=m->cpu.primitives.size();wordCount+=m->cpu.words.size();}all.reserve(primitiveCount);packed.reserve(wordCount);
            stats.voxelBytes=0;
            for(auto& m:unique){m->first=uint32_t(all.size());m->words=uint32_t(packed.size());all.insert(all.end(),m->cpu.primitives.begin(),m->cpu.primitives.end());packed.insert(packed.end(),m->cpu.words.begin(),m->cpu.words.end());stats.voxelBytes+=m->bytes;}
            stats.voxelBytes+=all.size()*sizeof(VoxelPrimitive)+packed.size()*4;stats.voxelPrimitives=all.size();
            ++dataRevision;buffersDirty=false;
        }
        if(f.dataRevision!=dataRevision){
            // Reuse fence-protected upload/default buffers. Reallocating and
            // retiring the entire scene on every brush stroke stalls the driver.
            auto update=[&](ComPtr<ID3D12Resource>& gpu,ComPtr<ID3D12Resource>& upload,size_t& capacity,const void* source,size_t bytes){
                if(!gpu||capacity<bytes){capacity=std::max(size_t(256),bytes*2);gpu=buffer(capacity,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);upload=buffer(capacity,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);}
                fill(upload.Get(),source,bytes);barrier(list,gpu.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);if(bytes)list->CopyBufferRegion(gpu.Get(),0,upload.Get(),0,bytes);barrier(list,gpu.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            };
            update(f.primitiveData,f.primitiveUpload,f.primitiveCapacity,packedPrimitives.data(),packedPrimitives.size()*sizeof(VoxelPrimitive));
            update(f.wordData,f.wordUpload,f.wordCapacity,packedWords.data(),packedWords.size()*4);f.dataRevision=dataRevision;
        }
        stats.voxelCpuPackMs=elapsed();
        auto& descs=sceneDescs;auto& instances=sceneInstances;descs.clear();instances.clear();
        const size_t count=ChunkCount+parcels.size()+cars.size()+treeCount;descs.reserve(count);instances.reserve(count);
        auto add=[&](const std::shared_ptr<Model>& m,XMFLOAT4 pose,XMFLOAT4 old,uint32_t id,uint32_t color,float y=0,float oldY=0){
            D3D12_RAYTRACING_INSTANCE_DESC d{};d.Transform[0][0]=pose.w;d.Transform[0][2]=pose.z;d.Transform[0][3]=pose.x;d.Transform[1][1]=1;d.Transform[1][3]=y;d.Transform[2][0]=-pose.z;d.Transform[2][2]=pose.w;d.Transform[2][3]=pose.y;d.InstanceID=UINT(instances.size());d.InstanceMask=255;d.AccelerationStructure=m->address;descs.push_back(d);instances.push_back({m->first,m->words,id,color,pose,old,m->roof,m->roofProfile,{y,oldY,0,0}});};
        for(unsigned i=0;i<ChunkCount;++i){XMFLOAT4 p{float(i%ChunksAcross*ChunkSize),float(i/ChunksAcross*ChunkSize),0,1};add(terrain[i],p,p,i+1,0);}
        for(auto& [tile,m]:parcels){XMFLOAT4 p{float(tile%MapSize*TileSize),float(tile/MapSize*TileSize),0,1};add(m,p,p,uint32_t(2048+tile),uint32_t(tile));}
        for(const auto& chunk:treeChunks)for(const auto& tree:chunk){auto pose=treeVoxelPose(tree);XMFLOAT4 p{pose[0],pose[1],pose[2],pose[3]};
            // Vary scrub within the darker existing leaf colors at instance setup.
            uint32_t color=tree.variant==4?2u+((tree.tile*2654435761u)>>31):tree.variant;
            add(treeModels[tree.variant],p,p,0x100000u+tree.tile,color);
        }
        // G-buffer identities must remain exactly representable as float32.
        // Reassign on exhaustion and discard history rather than aliasing live cars.
        if(uint64_t(nextCarId)+cars.size()>=0xffffffu){oldCars.clear();nextCarId=0x800000u;changed=true;}
        if(oldCars.bucket_count()<cars.size())oldCars.reserve(cars.size()*2);
        for(auto& c:cars){float len=std::sqrt(c.dx*c.dx+c.dz*c.dz);XMFLOAT4 p{c.x,c.z,len>0?c.dx/len:0,len>0?c.dz/len:1};auto [found,inserted]=oldCars.try_emplace(c.id,CarPose{p,number,nextCarId});if(inserted)++nextCarId;auto old=inserted||found->second.frame+1!=number?p:found->second.pose;add(c.vehicle?trainModels[std::min(3u,c.vehicle)-1]:car,p,old,found->second.id,c.color,c.y,inserted?c.y:found->second.y);found->second.y=c.y;found->second.pose=p;found->second.frame=number;}
        if(number%60==0)std::erase_if(oldCars,[&](const auto& entry){return entry.second.frame!=number;});
        stats.voxelInstances=UINT(count);stats.visibleCars=UINT(cars.size());stats.triangles=stats.visibleTriangles=0;stats.visibleChunks=ChunkCount;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};in.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;in.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;in.NumDescs=UINT(count);in.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        if(f.capacity<count){f.capacity=std::max(unsigned(count),std::max(2048u,f.capacity*2));auto max=in;max.NumDescs=f.capacity;D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO size{};device->GetRaytracingAccelerationStructurePrebuildInfo(&max,&size);
            f.tlas=buffer(size.ResultDataMaxSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);f.scratch=buffer(size.ScratchDataSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);f.descs=buffer(f.capacity*sizeof(D3D12_RAYTRACING_INSTANCE_DESC),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);f.instances=buffer(f.capacity*sizeof(Instance),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);}
        fill(f.descs.Get(),descs.data(),descs.size()*sizeof(descs[0]));fill(f.instances.Get(),instances.data(),instances.size()*sizeof(instances[0]));
        in.InstanceDescs=f.descs->GetGPUVirtualAddress();D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};build.Inputs=in;build.DestAccelerationStructureData=f.tlas->GetGPUVirtualAddress();build.ScratchAccelerationStructureData=f.scratch->GetGPUVirtualAddress();list->BuildRaytracingAccelerationStructure(&build,0,nullptr);order(list,f.tlas.Get());
        if(!f.constants){f.constants=buffer(sizeof(Constants),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);f.styles=buffer(MapSize*MapSize*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);}
        fill(f.styles.Get(),world.tileStyles().data(),world.tileStyles().size()*4);
        bool reset=!validHistory||changed||styleRevision!=world.styleRevision()||previousFrame+1!=number||oldQuality!=view.quality||oldShadows!=view.shadows;
        float cameraDelta=std::abs(view.eye.x-previousEye.x)+std::abs(view.eye.y-previousEye.y)+std::abs(view.eye.z-previousEye.z);reset|=cameraDelta>50;
        // Exact view equality prevents stationary history from drifting through reprojection.
        bool stationaryView=!reset&&cameraDelta==0&&std::memcmp(&view.viewProjection,&previousVP,sizeof(previousVP))==0;
        Constants c{};c.inverse=view.rayInverse;c.vp=view.viewProjection;c.previous=reset?view.viewProjection:previousVP;c.eye={view.eye.x,view.eye.y,view.eye.z,0};c.oldEye={previousEye.x,previousEye.y,previousEye.z,0};c.sun={-.55f,.82f,-.3f,view.signalPhase};c.screen={float(width),float(height),float(number%4096),reset?1.f:0.f};c.settings={float(view.quality),view.shadows?1.f:0.f,stationaryView?1.f:0.f,0};c.hover={float(view.hover.x),float(view.hover.z),(view.erase?1.f:0.f)+2.f*(view.hoverSpan-1),view.grid?1.f:0.f};fill(f.constants.Get(),&c,sizeof(c));
        previousVP=view.viewProjection;previousEye=view.eye;previousFrame=number;oldQuality=view.quality;oldShadows=view.shadows;styleRevision=world.styleRevision();validHistory=true;
        ID3D12DescriptorHeap* heaps[]={heap.Get()};list->SetDescriptorHeaps(1,heaps);list->SetGraphicsRootSignature(root.Get());list->SetGraphicsRootConstantBufferView(0,f.constants->GetGPUVirtualAddress());list->SetGraphicsRootShaderResourceView(1,f.tlas->GetGPUVirtualAddress());list->SetGraphicsRootShaderResourceView(2,f.primitiveData->GetGPUVirtualAddress());list->SetGraphicsRootShaderResourceView(3,f.wordData->GetGPUVirtualAddress());list->SetGraphicsRootShaderResourceView(4,f.instances->GetGPUVirtualAddress());list->SetGraphicsRootShaderResourceView(5,f.styles->GetGPUVirtualAddress());auto table=heap->GetGPUDescriptorHandleForHeapStart();table.ptr+=ping*9*srvStride;list->SetGraphicsRootDescriptorTable(6,table);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        stats.voxelCpuInstancesMs=elapsed();
        list->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*5+1);
        auto draw=[&](unsigned pass,unsigned w,unsigned h){D3D12_VIEWPORT vp{0,0,float(w),float(h),0,1};D3D12_RECT scissor{0,0,LONG(w),LONG(h)};list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&scissor);list->SetPipelineState(pipelines[pass].Get());list->DrawInstanced(3,1,0,0);};
        D3D12_CPU_DESCRIPTOR_HANDLE outputs[4];for(unsigned j=0;j<4;++j){state(ping*4+j,D3D12_RESOURCE_STATE_RENDER_TARGET);outputs[j]=rtv(ping*4+j);}list->OMSetRenderTargets(4,outputs,FALSE,nullptr);draw(0,width,height);for(unsigned j=0;j<4;++j)state(ping*4+j,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        list->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*5+2);
        state(10,D3D12_RESOURCE_STATE_RENDER_TARGET);auto light=rtv(10);list->OMSetRenderTargets(1,&light,FALSE,nullptr);draw(1,(width+1)/2,(height+1)/2);state(10,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        list->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*5+3);
        state(8+ping,D3D12_RESOURCE_STATE_RENDER_TARGET);auto hdr=rtv(8+ping);list->OMSetRenderTargets(1,&hdr,FALSE,nullptr);draw(2,width,height);state(8+ping,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        list->OMSetRenderTargets(1,&target,FALSE,nullptr);draw(3,width,height);
        list->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*5+4);list->ResolveQueryData(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*5,5,readback.Get(),slot*5*sizeof(uint64_t));timingReady[slot]=true;
    }
};
VoxelRenderer::VoxelRenderer(ID3D12Device5* d):impl_(std::make_unique<Impl>(d)){}
VoxelRenderer::~VoxelRenderer()=default;
void VoxelRenderer::resize(unsigned w,unsigned h){impl_->resize(w,h);}
void VoxelRenderer::render(ID3D12GraphicsCommandList4* l,unsigned f,World& w,const View& v,std::span<const CarInstance> cars,D3D12_CPU_DESCRIPTOR_HANDLE target,RenderStats& stats){impl_->render(l,f,w,v,cars,target,stats);}
void VoxelRenderer::validateTraversal(){
    auto& s=*impl_;auto* device=s.device;
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList4> list;ComPtr<ID3D12CommandQueue> queue;
    checked(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Test allocator");checked(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"Test list");
    D3D12_COMMAND_QUEUE_DESC qd{};qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;checked(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"Test queue");s.list=list.Get();
    D3D12_ROOT_PARAMETER params[5]{};unsigned registers[]={1,2,14};for(int i=0;i<3;++i){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[i].Descriptor={registers[i],0};}params[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[3].Descriptor={0,0};params[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[4].Constants={1,0,1};
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};rootDesc.NumParameters=5;rootDesc.pParameters=params;ComPtr<ID3DBlob> blob,error;checked(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"Test root serialization");ComPtr<ID3D12RootSignature> root;checked(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"Test root");
    auto shader=code(L"voxel.test.cso");D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.CS={shader.data(),shader.size()};ComPtr<ID3D12PipelineState> pipeline;checked(device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pipeline)),"Test pipeline");
    auto model=carVoxels();model.primitives.push_back({{0,0,0},uint32_t(model.words.size()),{2,2,2},0});model.words.resize(model.words.size()+128);
    for(uint8_t variant=0;variant<TreeVariantCount;++variant){auto tree=treeVoxels(variant);uint32_t base=uint32_t(model.words.size());
        for(auto p:tree.primitives){if(!p.material)p.data=(p.data&0xff000000u)|((p.data&0xffffffu)+base);model.primitives.push_back(p);}
        model.words.insert(model.words.end(),tree.words.begin(),tree.words.end());
    }
    for(uint8_t variant:std::array<uint8_t,3>{0,1,3}){auto facade=buildingVoxels({1,1,variant,0});uint32_t base=uint32_t(model.words.size());
        for(auto p:facade.primitives){if(!p.material)p.data=(p.data&0xff000000u)|((p.data&0xffffffu)+base);model.primitives.push_back(p);}
        model.words.insert(model.words.end(),facade.words.begin(),facade.words.end());
    }
    unsigned glassTestIndex=unsigned(model.primitives.size()),glassWords=unsigned(model.words.size());
    model.primitives.push_back({{0,0,0},glassWords,{2,2,2},0});model.words.resize(glassWords+128);
    model.words[glassWords]=VGlass;model.words[glassWords+1]=VBrick;
    constexpr unsigned count=8192;std::vector<XMFLOAT4> rays(count*2);std::vector<VoxelRayHit> expected(count);std::vector<bool> valid(count);
    std::mt19937 rng(173);std::uniform_real_distribution<float> random(-1,1);
    for(unsigned i=0;i<count;++i){unsigned index=i<2?glassTestIndex:i%unsigned(model.primitives.size());const auto& p=model.primitives[index];float o[3],d[3];
        // Sample relative to each brick so finer geometry still exercises hits.
        for(int a=0;a<3;++a){o[a]=(p.lo[a]+p.hi[a])*.5f+random(rng)*(p.hi[a]-p.lo[a])*1.5f;d[a]=random(rng);}
        if(i%4==0){d[0]=d[2]=0;d[1]=i%8==0?1.f:-1.f;}float norm=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);for(auto& v:d)v/=norm;
        uint32_t ignored=i%2?VGlass:0;
        if(i<2){o[0]=-1;o[1]=o[2]=.125f;d[0]=1;d[1]=d[2]=0;}
        bool limited=i>=2&&i%3==0;
        rays[i*2]={o[0],o[1],o[2],float(index)};rays[i*2+1]={d[0],d[1],d[2],limited?-11.f:float(ignored)};
        if(limited){valid[i]=intersectVoxel(p,model.words,o,d,.003f,.75f,expected[i],VLeaf);
            if(!valid[i])valid[i]=intersectVoxel(p,model.words,o,d,.75f,1000,expected[i]);
        }else valid[i]=intersectVoxel(p,model.words,o,d,.003f,1000,expected[i],ignored);}
    auto prim=s.buffer(model.primitives.size()*sizeof(VoxelPrimitive),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);s.fill(prim.Get(),model.primitives.data(),model.primitives.size()*sizeof(VoxelPrimitive));
    auto vox=s.buffer(model.words.size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);s.fill(vox.Get(),model.words.data(),model.words.size()*4);
    auto input=s.buffer(rays.size()*sizeof(XMFLOAT4),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);s.fill(input.Get(),rays.data(),rays.size()*sizeof(XMFLOAT4));
    auto output=s.buffer(rays.size()*sizeof(XMFLOAT4),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);auto read=s.buffer(rays.size()*sizeof(XMFLOAT4),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    list->SetComputeRootSignature(root.Get());list->SetPipelineState(pipeline.Get());list->SetComputeRootShaderResourceView(0,prim->GetGPUVirtualAddress());list->SetComputeRootShaderResourceView(1,vox->GetGPUVirtualAddress());list->SetComputeRootShaderResourceView(2,input->GetGPUVirtualAddress());list->SetComputeRootUnorderedAccessView(3,output->GetGPUVirtualAddress());list->SetComputeRoot32BitConstant(4,count,0);list->Dispatch(count/64,1,1);barrier(list.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);list->CopyBufferRegion(read.Get(),0,output.Get(),0,rays.size()*sizeof(XMFLOAT4));checked(list->Close(),"Close test list");ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;checked(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Test fence");HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("Test event creation failed");checked(queue->Signal(fence.Get(),1),"Test signal");checked(fence->SetEventOnCompletion(1,event),"Test wait");auto waited=WaitForSingleObject(event,30000);CloseHandle(event);if(waited!=WAIT_OBJECT_0)throw std::runtime_error("GPU traversal test timed out");
    XMFLOAT4* results=nullptr;checked(read->Map(0,nullptr,reinterpret_cast<void**>(&results)),"Read traversal results");unsigned errors=0,hits=0;
    for(unsigned i=0;i<count;++i){bool hit=results[i*2].x>=0;hits+=hit;if(hit!=valid[i]||(hit&&(std::abs(results[i*2].x-expected[i].t)>.001f||unsigned(results[i*2+1].x)!=expected[i].material)))++errors;}
    D3D12_RANGE empty{};read->Unmap(0,&empty);s.list=nullptr;if(errors||hits<100)throw std::runtime_error("GPU/CPU voxel traversal mismatches: "+std::to_string(errors)+", hits: "+std::to_string(hits));
}

}
