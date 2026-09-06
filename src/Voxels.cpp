#include "Voxels.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace vc {
bool intersectVoxel(const VoxelPrimitive& b,std::span<const uint32_t> words,const float o[3],const float d[3],float tMin,float tMax,VoxelRayHit& hit) {
    float enter=-1e30f,leave=tMax;int axis=0;
    for(int a=0;a<3;++a){
        if(std::abs(d[a])<1e-10f){if(o[a]<b.lo[a]||o[a]>=b.hi[a])return false;continue;}
        float t0=(b.lo[a]-o[a])/d[a],t1=(b.hi[a]-o[a])/d[a];if(t0>t1)std::swap(t0,t1);
        if(t0>enter){enter=t0;axis=a;}leave=std::min(leave,t1);
    }
    float t=std::max(enter,tMin);if(t>leave||leave<tMin)return false;
    hit={};hit.normal[axis]=d[axis]>0?-1.f:1.f;
    if(b.material){hit.t=t;hit.material=b.material;return true;}
    int cell[3],step[3];float next[3],delta[3];
    for(int a=0;a<3;++a){
        cell[a]=std::clamp(int(std::floor((o[a]+d[a]*(t+1e-5f)-b.lo[a])*4)),0,7);
        step[a]=d[a]>=0?1:-1;delta[a]=std::abs(d[a])>1e-10f?std::abs(.25f/d[a]):1e30f;
        next[a]=std::abs(d[a])>1e-10f?(b.lo[a]+(cell[a]+(step[a]>0?1:0))*.25f-o[a])/d[a]:1e30f;
    }
    for(int iteration=0;iteration<25&&t<=leave;++iteration){
        unsigned i=unsigned(cell[0]+8*(cell[1]+8*cell[2]));
        uint32_t mat=(words[(b.data&0xffffffu)+i/4]>>((i%4)*8))&255;
        if(mat){hit.t=t;hit.material=mat;return true;}
        axis=next[0]<=next[1]?(next[0]<=next[2]?0:2):(next[1]<=next[2]?1:2);
        t=next[axis];next[axis]+=delta[axis];cell[axis]+=step[axis];
        hit.normal[0]=hit.normal[1]=hit.normal[2]=0;hit.normal[axis]=float(-step[axis]);
        if(cell[axis]<0||cell[axis]>=8)break;
    }
    return false;
}
namespace {
struct Canvas {
    int nx,ny,nz;float ox,oz;std::vector<uint8_t> v;
    Canvas(int x,int y,int z,float originX=0,float originZ=0):nx(x),ny(y),nz(z),ox(originX),oz(originZ),v(size_t(x)*y*z){}
    void box(float x0,float y0,float z0,float x1,float y1,float z1,uint8_t m){
        int a=std::clamp(int(std::round((x0-ox)*4)),0,nx),b=std::clamp(int(std::round(y0*4)),0,ny),c=std::clamp(int(std::round((z0-oz)*4)),0,nz);
        int e=std::clamp(int(std::round((x1-ox)*4)),0,nx),f=std::clamp(int(std::round(y1*4)),0,ny),g=std::clamp(int(std::round((z1-oz)*4)),0,nz);
        for(int z=c;z<g;++z)for(int y=b;y<f;++y)for(int x=a;x<e;++x)v[x+nx*(y+ny*z)]=m;
    }
    VoxelModel finish(){
        VoxelModel m;
        for(int z=0;z<nz;z+=8)for(int y=0;y<ny;y+=8)for(int x=0;x<nx;x+=8){
            uint32_t data[128]{};bool any=false,same=true;uint8_t representative=0;uint8_t first=v[x+nx*(y+ny*z)];
            for(int k=0;k<512;++k){int px=x+k%8,py=y+(k/8)%8,pz=z+k/64;
                uint8_t value=px<nx&&py<ny&&pz<nz?v[px+nx*(py+ny*pz)]:0;
                any|=value!=0;if(value)representative=value;same&=value==first;data[k/4]|=uint32_t(value)<<((k%4)*8);}
            if(!any)continue;
            VoxelPrimitive p{{ox+x*.25f,y*.25f,oz+z*.25f},(uint32_t(m.words.size())|(uint32_t(representative)<<24)),{ox+(x+8)*.25f,(y+8)*.25f,oz+(z+8)*.25f},same?first:0u};
            m.primitives.push_back(p);if(!same)m.words.insert(m.words.end(),std::begin(data),std::end(data));
        }
        return m;
    }
};
uint8_t terrainMaterial(Material m){switch(m){case Material::Grass:return VGrass;case Material::Asphalt:case Material::HighwayShoulder:return VAsphalt;case Material::Marking:return VMarking;default:return VConcrete;}}
}
VoxelModel terrainVoxels(const World& world,int chunk){
    VoxelModel model;
    if(world.chunkEmpty(chunk)){model.primitives.push_back({{0,-.25f,0},0,{float(ChunkSize),0,float(ChunkSize)},VGrass});return model;}
    // Lossless run compression of the terrain's integer columns. No triangle
    // geometry is consumed by the voxel pipeline.
    std::vector<Column> cols(ChunkSize*ChunkSize);std::vector<bool> used(cols.size());
    int ox=chunk%ChunksAcross*ChunkSize,oz=chunk/ChunksAcross*ChunkSize;
    for(int z=0;z<ChunkSize;++z)for(int x=0;x<ChunkSize;++x)cols[z*ChunkSize+x]=world.column(ox+x,oz+z);
    auto same=[&](int x,int z,Column c){return !used[z*ChunkSize+x]&&cols[z*ChunkSize+x].height==c.height&&cols[z*ChunkSize+x].material==c.material;};
    for(int z=0;z<ChunkSize;++z)for(int x=0;x<ChunkSize;++x){if(used[z*ChunkSize+x])continue;auto c=cols[z*ChunkSize+x];int w=1,h=1;
        while(x+w<ChunkSize&&same(x+w,z,c))++w;
        while(z+h<ChunkSize){bool good=true;for(int i=0;i<w;++i)good&=same(x+i,z+h,c);if(!good)break;++h;}
        for(int j=0;j<h;++j)for(int i=0;i<w;++i)used[(z+j)*ChunkSize+x+i]=true;
        model.primitives.push_back({{float(x),-.25f,float(z)},0,{float(x+w),float(c.height),float(z+h)},terrainMaterial(c.material)});
    }return model;
}
VoxelModel buildingVoxels(ParcelVisual p){
    Canvas c(64,128,64);int kind=p.kind;
    if(!kind)return {};
    c.box(1,0,1,15,.25f,15,VConcrete);
    if(!p.level){c.box(2,.25f,2,14,.5f,14,kind==1?VGrass:kind==2?VCommercial:VIndustrial);return c.finish();}
    if(kind==12){
        c.box(1,0,1,15,.25f,15,VGrass);c.box(7,.25f,1,9,.5f,15,VConcrete);
        for(float x:{4.f,12.f})for(float z:{4.f,12.f}){c.box(x-.25f,.25f,z-.25f,x+.25f,3,z+.25f,VWood);
            c.box(x-1.5f,2.5f,z-1.5f,x+1.5f,4.5f,z+1.5f,VLeaf);c.box(x-1,4.5f,z-1,x+1,5.5f,z+1,VLeaf);}
        c.box(4,.75f,7,6,1,8,VWood);c.box(4,1,7.75f,6,1.75f,8,VWood);return c.finish();
    }
    if(kind==5){
        for(float x:{4.f,11.f})for(float z:{4.f,11.f})c.box(x,.25f,z,x+.5f,9,z+.5f,VMetal);
        c.box(3,8,3,13,12,13,VService);c.box(3.5f,12,3.5f,12.5f,12.5f,12.5f,VMetal);
        c.box(7,.25f,7,9,8,9,VMetal);return c.finish();
    }
    if(kind==6){
        c.box(2,.25f,2,14,1.5f,14,VConcrete);c.box(3,1.5f,3,7,1.75f,13,VGlass);c.box(9,1.5f,3,13,1.75f,13,VGlass);
        c.box(2,1.5f,7.5f,14,2,8.5f,VMetal);return c.finish();
    }
    float h=5.f+p.level*3.f+(p.variant%3)*1.5f;
    uint8_t wall=kind==1?(p.variant%2?VPlaster:VBrick):kind==2?VCommercial:kind==3?VIndustrial:VService;
    c.box(3,.25f,3,13,h,13,wall);
    c.box(2.75f,.25f,2.75f,13.25f,1,13.25f,VConcrete);
    for(float y=2;y<h-1;y+=3){
        for(float x=4;x<12;x+=3){
            c.box(x-.25f,y-.25f,2.75f,x+1.75f,y+1.75f,3.25f,VTrim);c.box(x,y,2.75f,x+1.5f,y+1.5f,3,VEmpty);c.box(x,y,3,x+1.5f,y+1.5f,3.25f,VGlass);
            c.box(x-.25f,y-.25f,12.75f,x+1.75f,y+1.75f,13.25f,VTrim);c.box(x,y,13,x+1.5f,y+1.5f,13.25f,VEmpty);c.box(x,y,12.75f,x+1.5f,y+1.5f,13,VGlass);
            c.box(2.75f,y-.25f,x-.25f,3.25f,y+1.75f,x+1.75f,VTrim);c.box(2.75f,y,x,3,y+1.5f,x+1.5f,VEmpty);c.box(3,y,x,3.25f,y+1.5f,x+1.5f,VGlass);
            c.box(12.75f,y-.25f,x-.25f,13.25f,y+1.75f,x+1.75f,VTrim);c.box(13,y,x,13.25f,y+1.5f,x+1.5f,VEmpty);c.box(12.75f,y,x,13,y+1.5f,x+1.5f,VGlass);
        }
        if(kind!=1)c.box(2.75f,y+2,2.75f,13.25f,y+2.25f,13.25f,VConcrete);
    }
    c.box(7,.25f,2.75f,9,3,3.25f,VWood);c.box(6.5f,.25f,2,9.5f,.5f,3,VConcrete);
    if(kind==1){
        for(float r=0;r<4;r+=.25f)c.box(2.25f+r,h+r,2.25f,13.75f-r,h+r+.25f,13.75f,VRoof);
        c.box(10,h,9,11.25f,h+4,10.25f,VBrick);
        c.box(10,h+4,9,11.25f,h+4.25f,10.25f,VMetal);
    }else{
        c.box(2.5f,h,2.5f,13.5f,h+.5f,13.5f,VRoof);
        c.box(4,h+.5f,5,7,h+1.5f,8,VMetal);c.box(9,h+.5f,9,11,h+1.25f,11,VMetal);
        if(kind==2){c.box(3.5f,1,2.75f,6.5f,3.5f,3,VGlass);c.box(9.5f,1,2.75f,12.5f,3.5f,3,VGlass);c.box(3,3.5f,2,13,4,3,VWood);}
        if(kind==3){c.box(10,h+.5f,4,11,h+4,5,VBrick);c.box(4,.25f,2.75f,7,4,3,VMetal);}
        if(kind>3){c.box(6,4,2.5f,10,5,3,VTrim);c.box(7.5f,h+.5f,7.5f,8.5f,h+2,8.5f,VService);}
    }
    if(kind==4){
        for(float x:{4.f,7.f}){c.box(x,h,8,x+1.5f,h+6,9.5f,VBrick);c.box(x,h+4.5f,8,x+1.5f,h+5,9.5f,VTrim);}
        c.box(4,.25f,1,12,1.75f,2,VMetal);
    }
    if(kind==7||kind==9){
        for(float x:{4.f,9.f})c.box(x,.5f,2.75f,x+3,4,3,kind==9?VBrick:VMetal);
    }
    if(kind==8){c.box(7,4,2.25f,9,6.5f,2.75f,VTrim);c.box(6.25f,4.75f,2.25f,9.75f,5.75f,2.75f,VTrim);}
    if(kind==10){c.box(6,4,2.25f,10,5,2.75f,VCommercial);c.box(7.5f,h+.5f,7.5f,8,h+4,8,VMetal);}
    if(kind==11){c.box(2,.25f,1,14,.5f,2,VMarking);c.box(7,h+.5f,2.75f,9,h+2,3.25f,VTrim);}
    // Low hedges add scale and soften residential lots without changing occupancy.
    if(kind==1){c.box(1,.25f,5,1.5f,1.25f,14,VLeaf);c.box(14,.25f,5,14.5f,1.25f,14,VLeaf);}
    return c.finish();
}
VoxelModel carVoxels(){
    Canvas c(16,16,24,-2,-3);
    c.box(-1.5f,.5f,-3,1.5f,1.75f,3,VPaint);c.box(-1.25f,1.75f,-1.5f,1.25f,2.5f,1.5f,VGlass);
    c.box(-1.25f,2.5f,-1.25f,1.25f,2.75f,1.25f,VPaint);
    c.box(-1.5f,.5f,-3,1.5f,.75f,-2.75f,VMetal);c.box(-1.5f,.5f,2.75f,1.5f,.75f,3,VMetal);
    for(float x:{-1.75f,1.25f})for(float z:{-2.f,1.25f})c.box(x,.0f,z,x+.5f,1,z+.75f,VRubber);
    for(float x:{-1.25f,.75f})c.box(x,1,2.75f,x+.5f,1.5f,3,VLight);
    return c.finish();
}
}
