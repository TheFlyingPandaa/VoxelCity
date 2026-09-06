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
void checkModel(const VoxelModel& model){
    std::mt19937 rng(1949);std::uniform_real_distribution<float> coord(-5,5);
    for(int i=0;i<2000;++i){float o[]={coord(rng),coord(rng),coord(rng)},d[]={coord(rng),coord(rng),coord(rng)};
        if(i%4==0){d[0]=0;d[2]=0;d[1]=i%8==0?1.f:-1.f;}
        float norm=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);for(auto& v:d)v/=norm;
        float expected=1000,actual=1000;bool brute=false,dda=false;
        for(const auto& p:model.primitives){VoxelRayHit h;if(intersectVoxel(p,model.words,o,d,.003f,actual,h)){actual=h.t;dda=true;CHECK(h.material>0);CHECK(std::abs(h.normal[0])+std::abs(h.normal[1])+std::abs(h.normal[2])==1);}
            if(p.material){float t;if(box(p.lo,p.hi,o,d,t)&&t<expected){expected=t;brute=true;}continue;}
            for(int k=0;k<512;++k){auto material=(model.words[(p.data&0xffffffu)+k/4]>>((k%4)*8))&255;if(!material)continue;
                float lo[]={p.lo[0]+(k%8)*.25f,p.lo[1]+((k/8)%8)*.25f,p.lo[2]+(k/64)*.25f},hi[]={lo[0]+.25f,lo[1]+.25f,lo[2]+.25f};float t;
                if(box(lo,hi,o,d,t)&&t<expected){expected=t;brute=true;}}
        }
        CHECK(dda==brute);if(dda)CHECK(std::abs(expected-actual)<.001f);
    }
}
}
int main()try{
    checkModel(carVoxels());
    VoxelModel empty;empty.primitives.push_back({{0,0,0},0,{2,2,2},0});empty.words.resize(128);checkModel(empty);
    empty.words[127]=0x01000000;checkModel(empty);
    auto w=std::make_unique<World>();w->generateScenario(0);
    for(int mask=0;mask<16;++mask){w->setRoad(15,15,true);w->setRoad(15,14,mask&North);w->setRoad(16,15,mask&East);w->setRoad(15,16,mask&South);w->setRoad(14,15,mask&West);
        for(int chunk:{0,1,ChunksAcross}){auto model=terrainVoxels(*w,chunk);int ox=chunk%ChunksAcross*ChunkSize,oz=chunk/ChunksAcross*ChunkSize;
            for(int z=0;z<ChunkSize;z+=7)for(int x=0;x<ChunkSize;x+=7){float origin[]={x+.5f,8,z+.5f},direction[]={0,-1,0};VoxelRayHit nearest;nearest.t=100;
                for(auto& p:model.primitives){VoxelRayHit h;if(intersectVoxel(p,model.words,origin,direction,.003f,nearest.t,h))nearest=h;}
                CHECK(nearest.material!=0);CHECK(std::abs(8-nearest.t-w->column(ox+x,oz+z).height)<.001f);}}
    }
    for(int kind=1;kind<13;++kind)for(int level=0;level<3;++level)for(int v=0;v<4;++v){auto model=buildingVoxels({uint8_t(kind),uint8_t(level),uint8_t(v),0});CHECK(!model.primitives.empty());
        for(auto& p:model.primitives){CHECK(p.lo[0]>=0&&p.hi[0]<=16&&p.lo[2]>=0&&p.hi[2]<=16);if(!p.material)CHECK((p.data&0xffffffu)+128<=model.words.size());}}
    std::cout<<"Voxel traversal, empty/inside/axis rays, sparse bricks, terrain seams and model bounds passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
