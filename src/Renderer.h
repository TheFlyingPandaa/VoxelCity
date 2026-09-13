#pragma once
#include "World.h"

#include "Traffic.h"
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <memory>
#include <string>
#include <future>
#include <thread>

struct ImFontAtlas;
struct UiDrawData;
namespace vc {
class VoxelRenderer;
using Microsoft::WRL::ComPtr;
struct View {
    DirectX::XMFLOAT4X4 viewProjection, rayInverse;
    DirectX::XMFLOAT3 eye;
    Cell hover;
    float signalPhase=0;
    bool erase=false, grid=true, shadows=true, voxels=true, showUI=true;
    int quality=2, hoverSpan=1;
};
struct RenderStats {
    double voxelCpuRetireMs=0,voxelCpuTerrainMs=0,voxelCpuPackMs=0,voxelCpuInstancesMs=0;
    double gpuMs=0, voxelSceneMs=0, voxelSurfaceMs=0, voxelLightMs=0, voxelResolveMs=0;
    uint64_t voxelBytes=0, voxelPrimitives=0, peakVideoMemory=0;
    unsigned voxelInstances=0;
    uint64_t triangles=0, visibleTriangles=0, videoMemory=0;
    unsigned visibleChunks=0, rebuiltChunks=0, debugErrors=0;
    unsigned visibleCars=0;
};
class Renderer {
public:
    Renderer(HWND window, unsigned width, unsigned height, bool debug, ImFontAtlas* fonts);
    ~Renderer();
    void resize(unsigned width, unsigned height);
    void render(World& world, const View& view, bool vsync, const std::filesystem::path& capture={},std::span<const CarInstance> cars={}, UiDrawData* ui=nullptr);
    void waitIdle();
    void invalidateWorld();
    RenderStats stats;
    std::string adapterName;
private:
    const std::thread::id ownerThread_=std::this_thread::get_id();
    void assertOwner() const;
    struct GpuMesh {
        ComPtr<ID3D12Resource> vertices, indices, blas;
        D3D12_VERTEX_BUFFER_VIEW vb{};
        D3D12_INDEX_BUFFER_VIEW ib{};
        uint32_t indexCount=0;
    };
    struct Frame {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12Resource> tlas, instances, scratch;
        ComPtr<ID3D12Resource> cars,styles;
        uint64_t styleRevision=~uint64_t(0);
        unsigned sceneCapacity=0;
        ComPtr<ID3D12Resource> buildings;
        size_t buildingCapacity=0;
        size_t carCapacity=0;
        std::vector<ComPtr<ID3D12Resource>> temporary;
        std::vector<std::shared_ptr<GpuMesh>> retired;
        uint64_t fence=0, generation=~uint64_t(0);
    };
    static constexpr unsigned FrameCount=3;
    std::unique_ptr<VoxelRenderer> voxelRenderer_;
    bool lastVoxelMode_=true;
    HWND window_;
    unsigned width_,height_,rtvStride_=0,frameNumber_=0;
    uint64_t fenceValue_=0,worldGeneration_=0,timestampFrequency_=1;
    bool tearing_=false,imguiInitialized_=false;
    HANDLE fenceEvent_=nullptr;
    ComPtr<IDXGIFactory6> factory_;
    ComPtr<IDXGIAdapter3> adapter_;
    ComPtr<ID3D12Device5> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<IDXGISwapChain3> swapchain_;
    ComPtr<ID3D12GraphicsCommandList4> list_;
    ComPtr<ID3D12Fence> fence_;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_,dsvHeap_,srvHeap_;
    ComPtr<ID3D12Resource> depth_;
    std::array<ComPtr<ID3D12Resource>,FrameCount> backbuffers_;
    std::array<Frame,FrameCount> frames_;
    std::array<std::shared_ptr<GpuMesh>,ChunkCount> chunks_;
    std::shared_ptr<GpuMesh> flatMesh_;
    struct MeshedChunk {int chunk;uint64_t revision;Mesh mesh;};
    std::future<std::vector<MeshedChunk>> meshing_;
    std::set<int> pendingChunks_;
    std::array<uint64_t,ChunkCount> chunkRevisions_{};
    bool initialWorld_=true;
    ComPtr<ID3D12RootSignature> root_;
    ComPtr<ID3D12PipelineState> pipeline_;
    ComPtr<ID3D12PipelineState> carPipeline_;
    std::shared_ptr<GpuMesh> carMesh_;
    std::array<std::shared_ptr<GpuMesh>,3> trainMeshes_;
    static constexpr size_t BuildingVisualVariants=8,TreeVariantBase=16*3*BuildingVisualVariants, ParcelVariants=TreeVariantBase+TreeVariantCount;
    struct ParcelInstance {float x,z;};
    struct SceneParcel {ParcelInstance position;size_t variant;};
    std::array<std::shared_ptr<GpuMesh>,ParcelVariants*2> parcelMeshes_;
    std::vector<SceneParcel> sceneParcels_;
    std::array<std::vector<ParcelInstance>,ParcelVariants*2> parcelGroups_;
    std::array<std::vector<TreeInstance>,ChunkCount> treeChunks_;
    std::array<uint64_t,ChunkCount> treeRevisions_{};
    uint64_t vegetationRevision_=~uint64_t(0);
    uint64_t parcelRevision_=~uint64_t(0);
    ComPtr<ID3D12PipelineState> buildingPipeline_;
    ComPtr<ID3D12QueryHeap> timestamps_;
    ComPtr<ID3D12Resource> timestampReadback_;
    void createTargets();
    void createPipeline();
    void wait(uint64_t value);
    ComPtr<ID3D12Resource> buffer(uint64_t bytes,D3D12_HEAP_TYPE heap,D3D12_RESOURCE_STATES state,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE);
    std::shared_ptr<GpuMesh> uploadMesh(const Mesh& mesh,Frame& frame,bool raytraced=true);
    void buildScene(Frame& frame);
    void collectDebugMessages();
};
}
