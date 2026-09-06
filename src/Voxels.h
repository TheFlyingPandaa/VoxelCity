#pragma once
#include "World.h"
#include <span>

namespace vc {
// Zero is empty. GPU records are also used by the independent CPU reference.
struct VoxelPrimitive {
    float lo[3]; uint32_t data=0;
    float hi[3]; uint32_t material=0; // nonzero: homogeneous run; zero: 8^3 brick
};
static_assert(sizeof(VoxelPrimitive)==32);
struct VoxelModel {
    std::vector<VoxelPrimitive> primitives;
    std::vector<uint32_t> words; // four 8-bit palette indices per word
};
enum VoxelMaterial : uint8_t {
    VEmpty, VGrass, VAsphalt, VMarking, VConcrete, VBrick, VPlaster,
    VRoof, VGlass, VMetal, VWood, VLeaf, VRubber, VPaint, VLight,
    VCommercial, VIndustrial, VService, VTrim
};
struct VoxelRayHit { float t=0; float normal[3]{}; uint32_t material=0; };
bool intersectVoxel(const VoxelPrimitive&,std::span<const uint32_t>,const float origin[3],const float direction[3],float tMin,float tMax,VoxelRayHit&);
VoxelModel terrainVoxels(const World&,int chunk);
VoxelModel buildingVoxels(ParcelVisual);
VoxelModel carVoxels();
}
