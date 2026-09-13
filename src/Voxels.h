#pragma once
#include "World.h"
#include <span>

namespace vc {
// Zero is empty. GPU records are also used by the independent CPU reference.
// Sparse cells occupy one eighth of each axis of the primitive bounds.
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
    VCommercial, VIndustrial, VService, VTrim, VTailLight, VWater, VRedPaint, VBark, VPolishedMetal, VSheetRoof, VMembrane, VStone
};
struct VoxelRayHit { float t=0; float normal[3]{}; uint32_t material=0; };
bool intersectVoxel(const VoxelPrimitive&,std::span<const uint32_t>,const float origin[3],const float direction[3],float tMin,float tMax,VoxelRayHit&,uint32_t ignoredMaterial=0);
VoxelModel terrainVoxels(const World&,int chunk);
VoxelModel buildingVoxels(ParcelVisual);
VoxelModel carVoxels();
VoxelModel trainVoxels(unsigned kind);
VoxelModel treeVoxels(uint8_t variant);
// x/z translation and sin/cos yaw, rotating around the model's (8,8) center.
std::array<float,4> treeVoxelPose(const TreeInstance& tree);
}
