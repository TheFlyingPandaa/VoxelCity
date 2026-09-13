#include "Voxels.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#define CHECK(x) do {if(!(x))throw std::runtime_error("Failed: " #x);}while(0)
using namespace vc;
namespace {
// Independent exhaustive reference: intersect every occupied unit cell with slabs.
bool box(const float lo[3],const float hi[3],const float o[3],const float d[3],float& t){
    double nearT=.003,farT=1000;
    for(int a=0;a<3;++a){if(std::abs(d[a])<1e-10f){if(o[a]<lo[a]||o[a]>=hi[a])return false;}
        else {double first=(double(lo[a])-o[a])/d[a],last=(double(hi[a])-o[a])/d[a];nearT=std::max(nearT,std::min(first,last));farT=std::min(farT,std::max(first,last));}}
    t=float(nearT);return nearT<=farT;
}
void checkModel(const VoxelModel& model,uint32_t ignored=0){
    std::mt19937 rng(1949);std::uniform_real_distribution<float> coord(-5,5);
    for(int i=0;i<2000;++i){float o[]={coord(rng),coord(rng),coord(rng)},d[]={coord(rng),coord(rng),coord(rng)};
        if(i%4==0){d[0]=0;d[2]=0;d[1]=i%8==0?1.f:-1.f;}
        float norm=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);for(auto& v:d)v/=norm;
        float expected=1000,actual=1000;bool brute=false,dda=false;
        for(const auto& p:model.primitives){VoxelRayHit h;if(intersectVoxel(p,model.words,o,d,.003f,actual,h,ignored)){actual=h.t;dda=true;CHECK(h.material>0);CHECK(std::abs(h.normal[0])+std::abs(h.normal[1])+std::abs(h.normal[2])==1);}
            if(p.material&&p.material==ignored)continue;
            if(p.material){float t;if(box(p.lo,p.hi,o,d,t)&&t<expected){expected=t;brute=true;}continue;}
            for(int k=0;k<512;++k){auto material=(model.words[(p.data&0xffffffu)+k/4]>>((k%4)*8))&255;if(!material||material==ignored)continue;
                float lo[3],hi[3];int cell[]={k%8,(k/8)%8,k/64};
                for(int a=0;a<3;++a){float size=(p.hi[a]-p.lo[a])/8;lo[a]=p.lo[a]+cell[a]*size;hi[a]=lo[a]+size;}float t;
                if(box(lo,hi,o,d,t)&&t<expected){expected=t;brute=true;}}
        }
        CHECK(dda==brute);if(dda)CHECK(std::abs(expected-actual)<.001f);
    }
}
}
int main()try{
    // Deterministic quarter turns preserve canopy clearance even at jitter limits.
    unsigned turns=0;
    for(uint8_t variant=0;variant<4;++variant){auto tree=treeVoxels(variant);
        for(uint32_t tile=0;tile<32;++tile)for(float jitter:{-2.f,2.f}){
            TreeInstance instance{tile,jitter,jitter,variant};auto pose=treeVoxelPose(instance);
            CHECK(pose==treeVoxelPose(instance));
            CHECK(std::abs(pose[0]+8*pose[3]+8*pose[2]-(jitter+8))<.001f);
            CHECK(std::abs(pose[1]-8*pose[2]+8*pose[3]-(jitter+8))<.001f);
            turns|=pose[2]==1?2u:pose[2]==-1?8u:pose[3]==1?1u:4u;
            for(auto& brick:tree.primitives)for(float x:{brick.lo[0],brick.hi[0]})for(float z:{brick.lo[2],brick.hi[2]}){
                float X=pose[0]+x*pose[3]+z*pose[2],Z=pose[1]-x*pose[2]+z*pose[3];
                CHECK(X>=0&&X<=16&&Z>=0&&Z<=16);
            }
        }
    }
    CHECK(turns==15);
    checkModel(carVoxels());
    checkModel(carVoxels(),VGlass);
    // Windscreen rays must reach cabin furnishings beyond the thin pane.
    auto car=carVoxels();
    for(float x:{-.5f,.5f}){
        float o[]={x,2.0625f,4},d[]={0,0,-1};VoxelRayHit pane,seat;pane.t=seat.t=100;
        for(auto& p:car.primitives){VoxelRayHit h;
            if(intersectVoxel(p,car.words,o,d,.003f,pane.t,h))pane=h;
            if(intersectVoxel(p,car.words,o,d,.003f,seat.t,h,VGlass))seat=h;
        }
        CHECK(pane.material==VGlass);CHECK(seat.material==VRubber);CHECK(seat.t>pane.t+.5f);
    }
    VoxelModel glass;glass.primitives.push_back({{0,0,0},0,{2,2,2},VGlass});checkModel(glass);checkModel(glass,VGlass);
    VoxelModel layered;layered.primitives.push_back({{0,0,0},0,{2,2,2},0});layered.words.resize(128);
    layered.words[0]=VGlass;layered.words[1]=VBrick;
    float origin[]={-1,.125f,.125f},direction[]={1,0,0};VoxelRayHit hit;
    CHECK(intersectVoxel(layered.primitives[0],layered.words,origin,direction,.003f,100,hit));CHECK(hit.material==VGlass&&hit.t==1);
    CHECK(intersectVoxel(layered.primitives[0],layered.words,origin,direction,.003f,100,hit,VGlass));CHECK(hit.material==VBrick&&hit.t==2);
    checkModel(layered,VGlass);
    // Different cell sizes coexist without changing packed palette offsets.
    auto mixed=layered;
    mixed.primitives.push_back({{-2,0,-2},0,{-1,1,-1},0});
    mixed.primitives.push_back({{2,-1,-2},0,{3,1,2},0});
    checkModel(mixed);checkModel(mixed,VGlass);
    VoxelModel empty;empty.primitives.push_back({{0,0,0},0,{2,2,2},0});empty.words.resize(128);checkModel(empty);
    empty.words[127]=0x01000000;checkModel(empty);
    auto w=std::make_unique<World>();w->generateScenario(0);
    w->setRoad(4,4,true);
    auto litRoad=terrainVoxels(*w,0);
    CHECK(std::any_of(litRoad.primitives.begin(),litRoad.primitives.end(),[](auto& p){return p.material==VMetal&&p.hi[1]>5;}));
    for(auto& p:litRoad.primitives)CHECK(p.lo[0]>=0&&p.hi[0]<=ChunkSize&&p.lo[2]>=0&&p.hi[2]<=ChunkSize);
    w->setRoad(4,4,false);
    auto clearedRoad=terrainVoxels(*w,0);CHECK(clearedRoad.primitives.size()==1);CHECK(clearedRoad.primitives[0].material==VGrass);
    for(int mask=0;mask<16;++mask){w->setRoad(15,15,true);w->setRoad(15,14,mask&North);w->setRoad(16,15,mask&East);w->setRoad(15,16,mask&South);w->setRoad(14,15,mask&West);
        for(int chunk:{0,1,ChunksAcross}){auto model=terrainVoxels(*w,chunk);int ox=chunk%ChunksAcross*ChunkSize,oz=chunk/ChunksAcross*ChunkSize;
            for(int z=0;z<ChunkSize;z+=7)for(int x=0;x<ChunkSize;x+=7){float origin[]={x+.5f,8,z+.5f},direction[]={0,-1,0};VoxelRayHit nearest;nearest.t=100;
                for(auto& p:model.primitives){VoxelRayHit h;if(intersectVoxel(p,model.words,origin,direction,.003f,nearest.t,h))nearest=h;}
                CHECK(nearest.material!=0);CHECK(std::abs(8-nearest.t-(w->column(ox+x,oz+z).height?.25f:0.f))<.001f);}}
    }
    for(int kind=1;kind<13;++kind)for(int level=0;level<3;++level)for(int v=0;v<4;++v){auto model=buildingVoxels({uint8_t(kind),uint8_t(level),uint8_t(v),0});CHECK(!model.primitives.empty());
        for(auto& p:model.primitives){CHECK(p.lo[0]>=0&&p.hi[0]<=16&&p.lo[2]>=0&&p.hi[2]<=16);if(!p.material)CHECK((p.data&0xffffffu)+128<=model.words.size());}}
    // Fine factory roof bricks retain continuous coverage after merging their
    // packed data and vertical offset into the coarse building model.
    for(uint8_t level=1;level<3;++level){auto factory=buildingVoxels({3,level,0,0});
        for(float start:{2.f,6.f,10.f})for(int step=0;step<32;++step){
            float o[]={start+step*.125f+.0625f,20,8},d[]={0,-1,0};VoxelRayHit top;top.t=100;
            for(auto& brick:factory.primitives){VoxelRayHit h;if(intersectVoxel(brick,factory.words,o,d,.003f,top.t,h))top=h;}
            CHECK(top.material==VSheetRoof);
            CHECK(std::abs(20-top.t-(4.5f+level*.75f+(step/2+1)*.125f))<.001f);
        }
    }
    // Main residential roof coverage survives both ridge orientations.
    for(uint8_t variant:{0,1,3}){auto house=buildingVoxels({1,1,variant,0});
        for(int step=0;step<46;++step){
            float o[]={2.25f+step*.125f+.0625f,24,8},d[]={0,-1,0};
            if(variant&2)std::swap(o[0],o[2]);
            VoxelRayHit top;top.t=100;
            for(auto& brick:house.primitives){VoxelRayHit h;if(intersectVoxel(brick,house.words,o,d,.003f,top.t,h))top=h;}
            CHECK(top.material==VRoof||top.material==VMetal); // Eave gutter at the first two samples.
            CHECK(24-top.t>=8+(variant%3)*1.5f);
        }
    }
    // Tight detail bounds preserve the thin surround's exact depth on all faces.
    for(uint8_t variant:{0,1,3}){auto house=buildingVoxels({1,1,variant,0});
        for(int face=0;face<4;++face){
            float o[]={3.9375f,5.5f,face%2?16.f:0.f},d[]={0,0,face%2?-1.f:1.f};
            if(face>=2){std::swap(o[0],o[2]);std::swap(d[0],d[2]);}
            VoxelRayHit closest;closest.t=100;
            for(auto& brick:house.primitives){VoxelRayHit hit;if(intersectVoxel(brick,house.words,o,d,.003f,closest.t,hit))closest=hit;}
            CHECK(closest.material==VTrim);CHECK(std::abs(closest.t-2.875f)<.001f);
        }
    }
    // School classroom glazing opens onto the teaching boards across the room.
    auto school=buildingVoxels({11,1,0,0});
    for(float x:{7.5f,11.f}){float o[]={x,3,7},d[]={0,0,1};VoxelRayHit pane,board;pane.t=board.t=100;
        for(auto& p:school.primitives){VoxelRayHit h;
            if(intersectVoxel(p,school.words,o,d,.003f,pane.t,h))pane=h;
            if(intersectVoxel(p,school.words,o,d,.003f,board.t,h,VGlass))board=h;
        }
        CHECK(pane.material==VGlass);CHECK(board.material==VRubber);CHECK(board.t>pane.t+4);
    }
    // Display glazing must open into the shop, not terminate against its facade.
    for(uint8_t variant=0;variant<4;++variant){auto shop=buildingVoxels({2,1,variant,0});
        for(float x:{4.f,10.f}){float o[]={x,1.85f,2},d[]={0,0,1};VoxelRayHit pane;pane.t=100;
            for(auto p:shop.primitives){VoxelRayHit hit;if(intersectVoxel(p,shop.words,o,d,.003f,pane.t,hit))pane=hit;}
            CHECK(pane.material==VGlass);
            VoxelRayHit interior;interior.t=100;
            for(auto p:shop.primitives){VoxelRayHit hit;if(intersectVoxel(p,shop.words,o,d,.003f,interior.t,hit,VGlass))interior=hit;}
            CHECK(interior.material!=VEmpty);CHECK(interior.t>1.5f);
        }
    }
    // Narrow-house bay, wing and main elevations must all transmit into rooms.
    for(uint8_t level:{uint8_t(1),uint8_t(2)}){auto house=buildingVoxels({1,level,2,0});
        const float rays[][6]={{5.25f,2.5f,2,0,0,1},{5.25f,2.5f,13,0,0,-1},
            {3,2.5f,4.5f,1,0,0},{12,5.5f,4.5f,-1,0,0},
            {12,1.85f,6,0,0,1},{15,1.85f,8.5f,-1,0,0},{6.75f,2,1,0,0,1},
            {7,6.f+level*3,2,0,0,1},{7,6.f+level*3,13,0,0,-1}};
        for(auto& ray:rays){VoxelRayHit pane;pane.t=100;VoxelRayHit interior;interior.t=100;
            for(auto p:house.primitives){VoxelRayHit hit;
                if(intersectVoxel(p,house.words,ray,ray+3,.003f,pane.t,hit))pane=hit;
                if(intersectVoxel(p,house.words,ray,ray+3,.003f,interior.t,hit,VGlass))interior=hit;
            }
            // A ray may leave through the opposite pane; an opaque hit, if any,
            // must be beyond the facade thickness rather than directly behind glass.
            CHECK(pane.material==VGlass);CHECK(interior.t>pane.t+.5f);
        }
    }
    auto treatment=buildingVoxels({6,1,0,0});
    for(float x:{2.5f,4.f,10.f}){float o[]={x,5,6},d[]={0,-1,0};VoxelRayHit nearest;nearest.t=100;
        for(auto p:treatment.primitives){VoxelRayHit hit;if(intersectVoxel(p,treatment.words,o,d,.003f,nearest.t,hit))nearest=hit;}
        CHECK(nearest.material==(x==2.5f?VConcrete:VWater));
        CHECK(std::abs((5-nearest.t)-(x==2.5f?1.75f:1.25f))<.001f);
    }
    auto lowHome=buildingVoxels({1,2,0,0}),highHome=buildingVoxels({1,2,4,0});float lowTop=0,highTop=0;
    for(auto p:lowHome.primitives)lowTop=std::max(lowTop,p.hi[1]);for(auto p:highHome.primitives)highTop=std::max(highTop,p.hi[1]);CHECK(highTop>30);CHECK(highTop>lowTop*2);
    std::cout<<"Voxel traversal, empty/inside/axis rays, sparse bricks, terrain seams and model bounds passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
