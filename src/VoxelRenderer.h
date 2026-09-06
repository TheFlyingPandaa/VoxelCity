#pragma once
#include <memory>
#include <span>
#include <d3d12.h>
#include "Traffic.h"
namespace vc {
struct View;struct RenderStats;
class VoxelRenderer {
public:
    explicit VoxelRenderer(ID3D12Device5*);
    ~VoxelRenderer();
    void validateTraversal(); // headless GPU/CPU conformance test
    void resize(unsigned,unsigned); // caller has waited for the GPU
    void render(ID3D12GraphicsCommandList4*,unsigned frame,World&,const View&,std::span<const CarInstance>,D3D12_CPU_DESCRIPTOR_HANDLE target,RenderStats&);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
