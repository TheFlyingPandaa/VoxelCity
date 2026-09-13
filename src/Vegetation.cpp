#include "World.h"
#include "Binary.h"
#include <algorithm>

namespace vc {
namespace {
uint32_t landscapeHash(uint32_t v){v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;return v^(v>>16);}
}
void World::initializeVegetation(){
    vegetationEnabled_=true;landscapeSeed_=1949;
    std::fill(clearedTrees_.begin(),clearedTrees_.end(),uint8_t(0));
    vegetationChunks_.fill(++vegetationRevision_);
    for(int z=0;z<MapSize;++z)for(int x=0;x<MapSize;++x)
        if(roadOccupies({x,z})||hasRail({x,z})||parcels_[z*MapSize+x].kind)clearVegetation({x,z});
}
void World::clearVegetation(Cell c){
    if(!vegetationEnabled_||!valid(c.x,c.z))return;
    int t=c.z*MapSize+c.x;uint8_t bit=uint8_t(1u<<(t%8));
    if(clearedTrees_[t/8]&bit)return;
    clearedTrees_[t/8]|=bit;
    vegetationChunks_[c.z/ChunkTiles*ChunksAcross+c.x/ChunkTiles]=++vegetationRevision_;
}
void World::clearConstructionVegetation(Cell c){
    if(!vegetationEnabled_)return;
    // A new diagonal connection can reserve either neighboring corner tile.
    for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx){Cell n{c.x+dx,c.z+dz};
        if(roadOccupies(n)||hasRail(n))clearVegetation(n);
    }
}
std::vector<TreeInstance> World::trees(int chunk) const {
    std::vector<TreeInstance> result;
    if(!vegetationEnabled_||chunk<0||chunk>=ChunkCount)return result;
    int ox=chunk%ChunksAcross*ChunkTiles,oz=chunk/ChunksAcross*ChunkTiles;
    for(int z=oz;z<oz+ChunkTiles;++z)for(int x=ox;x<ox+ChunkTiles;++x){
        uint32_t tile=uint32_t(z*MapSize+x);
        if(clearedTrees_[tile/8]&(1u<<(tile%8)))continue;
        uint32_t h=landscapeHash(tile^landscapeSeed_),grove=landscapeHash(uint32_t((z/8)*64+x/8)^landscapeSeed_);
        int dx=x%8-3,dz=z%8-3;
        bool clustered=grove%4==0&&dx*dx+dz*dz<=16;
        bool tree=h%1000<(clustered?350u:5u);
        if(!tree&&h%1000>=45u)continue;
        if(roadOccupies({x,z})||hasRail({x,z})||parcels_[tile].kind)continue;
        // Model canopy plus this jitter remains inside the 16-unit tile.
        float jx=float((h>>10)%17)*.25f-2,jz=float((h>>15)%17)*.25f-2;
        result.push_back({tile,x*float(TileSize)+jx,z*float(TileSize)+jz,tree?uint8_t((h>>24)%4):uint8_t(4)});
    }
    return result;
}
std::vector<TreeBox> World::treeBoxes(uint8_t variant){
    if(variant==4)return {{4,.25f,5,7,2,8,true},{8,.25f,8,11,2.5f,11,true},{8,.25f,4,10,1.5f,6,true}};
    std::vector<TreeBox> boxes;float h=variant%2?11.f:8.f;
    boxes.push_back({7.5f,0,7.5f,8.5f,h-1,8.5f,false});
    if(variant<2){
        boxes.push_back({5, h-5,5,11,h-1,11,true});
        boxes.push_back({4, h-4,6,12,h-2,10,true});
        boxes.push_back({6, h-4,4,10,h-2,12,true});
        boxes.push_back({6, h-1,6,10,h+1,10,true});
    }else{
        for(int tier=0;tier<5;++tier){float r=3.5f-tier*.625f,y=2+tier*(h-2)/5;
            boxes.push_back({8-r,y,8-r,8+r,y+2,8+r,true});
        }
    }
    return boxes;
}
void World::writeVegetation(std::ostream& out) const {
    binary::write(out,uint32_t(1));binary::write(out,landscapeSeed_);
    out.write(reinterpret_cast<const char*>(clearedTrees_.data()),clearedTrees_.size());
    if(!out)throw std::runtime_error("Could not write vegetation");
}
Mesh World::treeMesh(uint8_t variant){
    Mesh mesh;
    for(auto b:treeBoxes(variant)){
        float x=b.x0,y=b.y0,z=b.z0,X=b.x1,Y=b.y1,Z=b.z1;
        uint32_t material=b.leaf?17u:16u;
        auto face=[&](std::array<std::array<float,3>,4> points,float nx,float ny,float nz){
            uint32_t first=uint32_t(mesh.vertices.size());
            for(auto p:points)mesh.vertices.push_back({p[0],p[1],p[2],nx,ny,nz,material});
            mesh.indices.insert(mesh.indices.end(),{first,first+1,first+2,first,first+2,first+3});
        };
        face({{{x,Y,z},{x,Y,Z},{X,Y,Z},{X,Y,z}}},0,1,0);
        face({{{x,y,Z},{x,y,z},{X,y,z},{X,y,Z}}},0,-1,0);
        face({{{x,y,z},{x,y,Z},{x,Y,Z},{x,Y,z}}},-1,0,0);
        face({{{X,y,Z},{X,y,z},{X,Y,z},{X,Y,Z}}},1,0,0);
        face({{{X,y,z},{x,y,z},{x,Y,z},{X,Y,z}}},0,0,-1);
        face({{{x,y,Z},{X,y,Z},{X,Y,Z},{x,Y,Z}}},0,0,1);
    }
    return mesh;
}
void World::readVegetation(std::istream& in){
    if(binary::read<uint32_t>(in)!=1)throw std::runtime_error("Unsupported vegetation generation version");
    auto seed=binary::read<uint32_t>(in);auto mask=clearedTrees_;
    in.read(reinterpret_cast<char*>(mask.data()),mask.size());
    if(!in)throw std::runtime_error("Truncated vegetation mask");
    landscapeSeed_=seed;clearedTrees_=std::move(mask);vegetationEnabled_=true;
    vegetationChunks_.fill(++vegetationRevision_);
}
}
