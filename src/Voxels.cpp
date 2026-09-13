#include "Voxels.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace vc {
bool intersectVoxel(const VoxelPrimitive& b,std::span<const uint32_t> words,const float o[3],const float d[3],float tMin,float tMax,VoxelRayHit& hit,uint32_t ignoredMaterial) {
    float enter=-1e30f,leave=tMax;int axis=0;
    for(int a=0;a<3;++a){
        if(std::abs(d[a])<1e-10f){if(o[a]<b.lo[a]||o[a]>=b.hi[a])return false;continue;}
        float t0=(b.lo[a]-o[a])/d[a],t1=(b.hi[a]-o[a])/d[a];if(t0>t1)std::swap(t0,t1);
        if(t0>enter){enter=t0;axis=a;}leave=std::min(leave,t1);
    }
    float t=std::max(enter,tMin);if(t>leave||leave<tMin)return false;
    hit={};hit.normal[axis]=d[axis]>0?-1.f:1.f;
    if(b.material==ignoredMaterial&&b.material)return false;
    if(b.material){hit.t=t;hit.material=b.material;return true;}
    int cell[3],step[3];float next[3],delta[3];
    for(int a=0;a<3;++a){
        float size=(b.hi[a]-b.lo[a])/8;
        cell[a]=std::clamp(int(std::floor((o[a]+d[a]*(t+1e-5f)-b.lo[a])/size)),0,7);
        step[a]=d[a]>=0?1:-1;delta[a]=std::abs(d[a])>1e-10f?std::abs(size/d[a]):1e30f;
        next[a]=std::abs(d[a])>1e-10f?(b.lo[a]+(cell[a]+(step[a]>0?1:0))*size-o[a])/d[a]:1e30f;
    }
    for(int iteration=0;iteration<25&&t<=leave;++iteration){
        unsigned i=unsigned(cell[0]+8*(cell[1]+8*cell[2]));
        uint32_t mat=(words[(b.data&0xffffffu)+i/4]>>((i%4)*8))&255;
        if(mat&&mat!=ignoredMaterial){hit.t=t;hit.material=mat;return true;}
        axis=next[0]<=next[1]?(next[0]<=next[2]?0:2):(next[1]<=next[2]?1:2);
        t=next[axis];next[axis]+=delta[axis];cell[axis]+=step[axis];
        hit.normal[0]=hit.normal[1]=hit.normal[2]=0;hit.normal[axis]=float(-step[axis]);
        if(cell[axis]<0||cell[axis]>=8)break;
    }
    return false;
}
namespace {
struct Canvas {
    int nx,ny,nz;float ox,oz,cellSize;std::vector<uint8_t> v;
    Canvas(int x,int y,int z,float originX=0,float originZ=0,float size=.25f):nx(x),ny(y),nz(z),ox(originX),oz(originZ),cellSize(size),v(size_t(x)*y*z){}
    void box(float x0,float y0,float z0,float x1,float y1,float z1,uint8_t m){
        int a=std::clamp(int(std::round((x0-ox)/cellSize)),0,nx),b=std::clamp(int(std::round(y0/cellSize)),0,ny),c=std::clamp(int(std::round((z0-oz)/cellSize)),0,nz);
        int e=std::clamp(int(std::round((x1-ox)/cellSize)),0,nx),f=std::clamp(int(std::round(y1/cellSize)),0,ny),g=std::clamp(int(std::round((z1-oz)/cellSize)),0,nz);
        for(int z=c;z<g;++z)for(int y=b;y<f;++y)for(int x=a;x<e;++x)v[x+nx*(y+ny*z)]=m;
    }
    void foliage(float x0,float y0,float z0,float x1,float y1,float z1){
        float cx=(x0+x1)*.5f,cy=(y0+y1)*.5f,cz=(z0+z1)*.5f;
        float rx=(x1-x0)*.5f,ry=(y1-y0)*.5f,rz=(z1-z0)*.5f;
        int a=std::clamp(int(std::floor((x0-ox)/cellSize)),0,nx),b=std::clamp(int(std::floor(y0/cellSize)),0,ny),c=std::clamp(int(std::floor((z0-oz)/cellSize)),0,nz);
        int e=std::clamp(int(std::ceil((x1-ox)/cellSize)),0,nx),f=std::clamp(int(std::ceil(y1/cellSize)),0,ny),g=std::clamp(int(std::ceil((z1-oz)/cellSize)),0,nz);
        for(int z=c;z<g;++z)for(int y=b;y<f;++y)for(int x=a;x<e;++x){
            float px=ox+(x+.5f)*cellSize,py=(y+.5f)*cellSize,pz=oz+(z+.5f)*cellSize;
            if(px<x0||px>=x1||py<y0||py>=y1||pz<z0||pz>=z1)continue;
            float dx=(px-cx)/rx,dy=(py-cy)/ry,dz=(pz-cz)/rz;
            // Paired-cell leaf clusters roughen the silhouette without isolated
            // floating voxels or any frame-dependent procedural state.
            uint32_t seed=uint32_t(x/2)*73856093u^uint32_t(y/2)*19349663u^uint32_t(z/2)*83492791u;
            seed^=seed>>13;seed*=1274126177u;
            float edge=.82f+.25f*float(seed&255u)/255.f;
            if(dx*dx+dy*dy+dz*dz<edge)v[x+nx*(y+ny*z)]=VLeaf;
        }
    }
    void rooms(float x0,float z0,float x1,float z1,float height,float groundStorey=3){
        box(x0,.75f,z0,x1,height-.25f,z1,VEmpty);
        for(float floor=.75f;floor<height-1;floor+=floor<1?groundStorey:3){
            box(x0,floor,z0,x1,floor+.25f,z1,VWood);
            float ceiling=std::min(floor+(floor<1?groundStorey:3),height-.25f),middle=(z0+z1)*.5f;
            box(x0,floor+.25f,middle,x1,ceiling,middle+.25f,VPlaster);
            // Furniture sits behind the glass, providing real depth and parallax.
            box(x0+.75f,floor+.25f,z0+2,x0+2.75f,floor+.75f,z0+3,VPaint);
            box(x0+.75f,floor+.75f,z0+2.75f,x0+2.75f,floor+1.25f,z0+3,VPaint);
            box(x1-1.75f,floor+.25f,z1-2,x1-.5f,floor+1,z1-1,VWood);
        }
    }
    VoxelModel finish(bool tightBounds=false){
        VoxelModel m;
        for(int z=0;z<nz;z+=8)for(int y=0;y<ny;y+=8)for(int x=0;x<nx;x+=8){
            uint32_t data[128]{};bool any=false,same=true;uint8_t representative=0;uint8_t first=v[x+nx*(y+ny*z)];
            for(int k=0;k<512;++k){int px=x+k%8,py=y+(k/8)%8,pz=z+k/64;
                uint8_t value=px<nx&&py<ny&&pz<nz?v[px+nx*(py+ny*pz)]:0;
                any|=value!=0;if(value)representative=value;same&=value==first;data[k/4]|=uint32_t(value)<<((k%4)*8);}
            if(!any)continue;
            VoxelPrimitive p{{ox+x*cellSize,y*cellSize,oz+z*cellSize},(uint32_t(m.words.size())|(uint32_t(representative)<<24)),{ox+(x+8)*cellSize,(y+8)*cellSize,oz+(z+8)*cellSize},same?first:0u};
            if(tightBounds&&!same){
                int low[]={8,8,8},high[]={0,0,0},width[]={8,8,8};
                for(int k=0;k<512;++k)if((data[k/4]>>((k%4)*8))&255u){
                    int q[]={k%8,(k/8)%8,k/64};
                    for(int a=0;a<3;++a){low[a]=std::min(low[a],q[a]);high[a]=std::max(high[a],q[a]+1);}
                }
                bool cropped=false;
                for(int a=0;a<3;++a){
                    width[a]=1;while(width[a]<high[a]-low[a])width[a]*=2;
                    low[a]=std::min(low[a],8-width[a]);cropped|=width[a]<8;
                    p.lo[a]+=low[a]*cellSize;p.hi[a]=p.lo[a]+width[a]*cellSize;
                }
                if(cropped){
                    // Power-of-two cropping repeats source cells exactly in the
                    // fixed 8^3 payload, preserving all occupied world volumes.
                    uint32_t compact[128]{};
                    for(int k=0;k<512;++k){
                        int X=low[0]+(k%8)*width[0]/8,Y=low[1]+((k/8)%8)*width[1]/8,Z=low[2]+(k/64)*width[2]/8;
                        int source=X+8*(Y+8*Z);
                        uint32_t value=(data[source/4]>>((source%4)*8))&255u;
                        compact[k/4]|=value<<((k%4)*8);
                    }
                    std::copy(std::begin(compact),std::end(compact),std::begin(data));
                    // Cropping can remove every empty cell from thin, solid details.
                    // Store those as exact solid boxes instead of traversing 512 cells.
                    const uint32_t solid=uint32_t(representative)*0x01010101u;
                    same=std::all_of(std::begin(data),std::end(data),[&](uint32_t word){return word==solid;});
                    if(same)p.material=representative;
                }
            }
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
        model.primitives.push_back({{float(x),-.25f,float(z)},0,{float(x+w),c.height*TerrainStepHeight,float(z+h)},terrainMaterial(c.material)});
    }
    for(auto b:world.railBoxes(chunk))model.primitives.push_back({{b.x0,b.y0,b.z0},0,{b.x1,b.y1,b.z1},b.material==Material::Roof?VWood:b.material==Material::Marking?VPolishedMetal:b.material==Material::Warning?VRedPaint:b.material==Material::HighwayShoulder?VAsphalt:VConcrete});
    // Fixtures belong to the rendered road parcel; rebuilding a road removes them.
    for(int tz=0;tz<ChunkTiles;++tz)for(int tx=0;tx<ChunkTiles;++tx){
        Cell tile{ox/TileSize+tx,oz/TileSize+tz};
        if(world.roadType(tile)!=1||world.highway(tile)||(tile.x+tile.z*3)%4!=0)continue;
        const int sites[][4]={{0,8,1,0},{15,8,-1,0},{8,0,0,1},{8,15,0,-1}};
        for(auto& site:sites){int x=tx*TileSize+site[0],z=tz*TileSize+site[1];
            if(cols[z*ChunkSize+x].material!=Material::Sidewalk)continue;
            float px=x+.5f,pz=z+.5f,dx=float(site[2]),dz=float(site[3]);
            auto beam=[&](float a,float b,float low,float high,float radius,uint8_t material){
                float ax=px+dx*a,bx=px+dx*b,az=pz+dz*a,bz=pz+dz*b;
                model.primitives.push_back({{std::min(ax,bx)-radius,low,std::min(az,bz)-radius},0,
                    {std::max(ax,bx)+radius,high,std::max(az,bz)+radius},material});
            };
            beam(0,0,.25f,.75f,.25f,VConcrete);
            beam(0,0,.75f,5.75f,.125f,VMetal);
            beam(0,.5f,5.5f,5.75f,.125f,VMetal);
            beam(.5f,1.5f,5.75f,6,.125f,VMetal);
            beam(1.25f,2.25f,5.875f,6.125f,.25f,VMetal);
            beam(1.375f,2.125f,5.75f,5.875f,.125f,VTrim);
            break;
        }
    }
    return model;
}
std::array<float,4> treeVoxelPose(const TreeInstance& tree){
    uint32_t seed=tree.tile^0x9e3779b9u;
    seed^=seed>>16;seed*=0x7feb352du;seed^=seed>>15;
    constexpr float sine[]={0,1,0,-1},cosine[]={1,0,-1,0};
    unsigned turn=seed&3u;float s=sine[turn],c=cosine[turn];
    return {tree.x+8-8*c-8*s,tree.z+8+8*s-8*c,s,c};
}
VoxelModel treeVoxels(uint8_t variant){
    if(variant==4){
        Canvas shrubs(96,24,96,2,2,.125f);
        // Uneven low crowns leave open ground between the woody clumps.
        for(int plant=0;plant<7;++plant){
            float a=plant*2.399963f,r=1.5f+(plant%3)*.875f;
            float x=8+std::cos(a)*r,z=8+std::sin(a)*r,top=1.25f+(plant%4)*.375f;
            shrubs.box(x-.125f,.25f,z-.125f,x+.125f,top-.25f,z+.125f,VBark);
            for(int spray=0;spray<3;++spray){float b=a+spray*2.094395f;
                float X=x+std::cos(b)*.625f,Z=z+std::sin(b)*.625f;
                float tip=top-(spray%2)*.375f;
                for(int step=0;step<=6;++step){float t=step/6.f;
                    float px=x+(X-x)*t,pz=z+(Z-z)*t,y=.375f+(tip-.75f)*t;
                    shrubs.box(px-.0625f,y,pz-.0625f,px+.0625f,y+.125f,pz+.0625f,VBark);
                }
                shrubs.foliage(X-.625f,std::max(.375f,tip-1),Z-.5f,X+.625f,tip+.125f,Z+.5f);
                shrubs.foliage(X-.25f,tip-.25f,Z-.25f,X+.25f,tip+.25f,Z+.25f);
            }
            shrubs.foliage(x-.375f,.125f,z-.375f,x+.375f,.75f,z+.375f);
        }
        // Partly buried, asymmetric stones interrupt the otherwise leafy patch.
        const float stones[][5]={{5,.875f,8.5f,.875f,.625f},{10.75f,.625f,6.25f,.625f,.75f},{8.25f,.375f,10.75f,.5f,.625f}};
        for(const auto& stone:stones){
            for(float z=stone[2]-stone[4];z<stone[2]+stone[4];z+=.125f)
                for(float y=0;y<stone[1];y+=.125f)
                    for(float x=stone[0]-stone[3];x<stone[0]+stone[3];x+=.125f){
                        float dx=(x+.0625f-stone[0])/stone[3],dz=(z+.0625f-stone[2])/stone[4],dy=(y+.0625f)/stone[1];
                        if(dx*dx+dz*dz+dy*dy<1&&std::abs(dx)*.7f+std::abs(dz)*.5f+dy+dx*.15f<1.35f)
                            shrubs.box(x,y,z,x+.125f,y+.125f,z+.125f,VStone);
                    }
        }
        return shrubs.finish(true);
    }
    // Shared tree models retain eighth-unit branch and leaf silhouettes at close range.
    Canvas c(128,128,128,0,0,.125f);
    float h=variant%2?11.f:8.f;
    // Tapered limbs are baked into the shared model; placement and clearing
    // still operate on the original single tree instance.
    auto limb=[&](float x,float y,float z,float X,float Y,float Z,float radius){
        int steps=int(std::ceil(std::max({std::abs(X-x),std::abs(Y-y),std::abs(Z-z)})*8));
        for(int i=0;i<=steps;++i){float t=float(i)/std::max(1,steps),r=std::max(.125f,radius*(1-.65f*t));
            float px=x+(X-x)*t,py=y+(Y-y)*t,pz=z+(Z-z)*t;
            c.box(px-r,py-r,pz-r,px+r,py+r,pz+r,VBark);
        }
    };
    limb(8,.25f,8,8.25f,h-.5f,7.75f,.5f);
    for(int i=0;i<5;++i){float a=i*1.256637f+variant*.7f;
        limb(8,.4f,8,8+std::cos(a)*1.4f,.125f,8+std::sin(a)*1.4f,.25f);
    }
    if(variant<2){
        // Fallen fork on the open side of the understory, with a pale break
        // exposed through the bark. It shares the tree's placement and clearing.
        float end=variant==0?10.875f:11.5f;
        limb(9.25f,.25f,9,end,.1875f,10.5f,.25f);
        limb(10.125f,.1875f,9.625f,10,.125f,11,.125f);
        if(variant==1)limb(10.75f,.1875f,10,11.75f,.125f,9.5f,.125f);
        c.box(9.125f,.125f,8.875f,9.375f,.375f,9,VWood);
        c.box(end-.125f,.125f,10.5f,end+.125f,.25f,10.625f,VWood);
        for(int i=0;i<9;++i){float a=i*2.399963f+variant*.8f;
            float reach=(2.5f+(i%3)*.35f-i*.08f)*(variant==0?1.f:.9f);
            float x=8+std::cos(a)*reach,z=8+std::sin(a)*reach,y=h-5+i*.5f;
            limb(8,std::max(2.f,h-6),8,x,y,z,.25f);
            // Fork each limb into smaller offset crowns, leaving irregular gaps.
            for(int fork=0;fork<3;++fork){float angle=a+(fork-1)*1.1f;
                float X=x+std::cos(angle)*.9f,Z=z+std::sin(angle)*.9f;
                float Y=y+.375f+((i+fork)%3)*.25f;
                limb(x,y,z,X,Y,Z,.125f);
                for(int spray=0;spray<3;++spray){
                    float turn=angle+spray*2.399963f;
                    float px=X+std::cos(turn)*.4f,pz=Z+std::sin(turn)*.4f;
                    float py=Y+(spray%2)*.375f,r=.75f+((i+spray)%2)*.125f;
                    limb(X,Y,Z,px,py,pz,.125f);
                    // Overlapping leaf sprays fill the spreading crown while
                    // keeping the supporting forks visible beneath its edge.
                    c.foliage(px-r,py-.375f,pz-r,px+r,py+.875f,pz+r);
                }
            }
        }
        // Cover the central leader while the surrounding crowns stagger in height.
        c.foliage(7.5f,h-.5f,7.5f,8.5f,h+.5f,8.5f);
        for(int i=0;i<5;++i){float a=i*2.399963f+variant;
            float x=8+std::cos(a)*.75f,z=8+std::sin(a)*.75f,y=h-.75f+i*.1875f;
            limb(8,h-1.5f,8,x,y,z,.125f);
            c.foliage(x-.625f,y-.375f,z-.625f,x+.625f,y+.75f,z+.625f);
        }
    }else{
        // Thin overlapping needle sprays expose branch gaps and tapered tips.
        for(int tier=0;tier<5;++tier){float reach=3.1f-tier*.52f,y=2+tier*(h-2)/5;
            for(int i=0;i<5;++i){float a=i*1.256637f+tier*.65f+variant;
                float mx=8+std::cos(a)*reach*.4f,mz=8+std::sin(a)*reach*.4f;
                limb(8,y+.7f,8,mx,y+.5f,mz,.25f);
                for(int fan=-1;fan<=1;++fan){
                    float angle=a+fan*.24f,length=reach*(fan==0?1.f:.84f);
                    float x=8+std::cos(angle)*length,z=8+std::sin(angle)*length;
                    float tip=y+.125f+((i+tier)%3)*.125f;
                    limb(mx,y+.5f,mz,x,tip,z,.125f);
                    for(int section=0;section<3;++section){float t=.4f+section*.3f;
                        float px=8+(x-8)*t,pz=8+(z-8)*t,py=y+.7f+(tip-y-.7f)*t;
                        float r=.85f-tier*.09f-section*.1f;
                        c.foliage(px-r,py-.125f,pz-r,px+r,py+.5f,pz+r);
                    }
                }
            }
            float core=.65f-tier*.06f;
            c.foliage(8-core,y+.4f,8-core,8+core,y+1.5f,8+core);
        }
        c.foliage(7.625f,h-.75f,7.625f,8.375f,h+.5f,8.375f);

    }
    // Mid-height shrubs fill the space between fern beds and broadleaf crowns.
    // Overlapping clumps form an asymmetric patch with an open side at the trunk.
    if(variant<2)for(int shrub=0;shrub<5;++shrub){
        float a=.5f+shrub*.5f+variant*2.f,reach=2.75f+(shrub%3)*.625f;
        float x=8+std::cos(a)*reach,z=8+std::sin(a)*reach;
        float height=.875f+((shrub*3+variant)%5)*.3125f;
        c.foliage(x-.75f,.125f,z-.625f,x+.75f,.875f,z+.625f);
        for(int shoot=0;shoot<5;++shoot){
            float angle=a+shoot*2.399963f;
            float X=x+std::cos(angle)*.625f,Z=z+std::sin(angle)*.625f;
            float Y=height-(shoot%2)*.375f;
            limb(x,.125f,z,X,Y,Z,.125f);
            c.foliage(X-.375f,Y-.5f,Z-.375f,X+.375f,Y+.375f,Z+.375f);
            float mx=(x+X)*.5f,mz=(z+Z)*.5f;
            c.foliage(mx-.375f,Y*.5f-.25f,mz-.375f,mx+.375f,Y*.5f+.375f,mz+.375f);
        }
    }
    // Low fern fans use small cells and open gaps between
    // fronds. They share the tree's deterministic pose and clearing lifetime.
    Canvas understory(96,8,96,2,2,.125f);
    auto sprig=[&](float x,float y,float z,float X,float Y,float Z){
        int steps=std::max(1,int(std::ceil(std::max({std::abs(X-x),std::abs(Y-y),std::abs(Z-z)})*16)));
        for(int s=0;s<=steps;++s){float t=float(s)/steps;
            float px=x+(X-x)*t,py=y+(Y-y)*t,pz=z+(Z-z)*t;
            understory.box(px-.0625f,py,pz-.0625f,px+.0625f,py+.125f,pz+.0625f,VLeaf);
        }
    };
    for(int i=0;i<7;++i){float a=i*2.399963f+variant,reach=1.5f+(i%3)*.8f;
        float x=8+std::cos(a)*reach,z=8+std::sin(a)*reach;
        understory.box(x-.125f,0,z-.125f,x+.125f,.375f,z+.125f,VLeaf);
        for(int fan=0;fan<5;++fan){float angle=a+fan*1.256637f;
            float dx=std::cos(angle),dz=std::sin(angle);
            float lastX=x,lastY=.125f,lastZ=z;
            for(int segment=0;segment<4;++segment){
                float length=(.25f+segment*.1875f)*(1+(fan%2)*.25f);
                float y=(segment==0?.375f:segment==1?.5f:segment==2?.375f:.25f)+(i%2)*.125f;
                float px=x+dx*length,pz=z+dz*length;
                sprig(lastX,lastY,lastZ,px,y,pz);
                lastX=px;lastY=y;lastZ=pz;
                float spread=segment==3?.125f:.25f;
                for(int side:{-1,1}){
                    float X=px-dz*spread*side+dx*.125f,Z=pz+dx*spread*side+dz*.125f;
                    sprig(px,y,pz,X,y-.125f,Z);
                }
            }
        }
    }
    auto model=c.finish(),plants=understory.finish();
    uint32_t offset=uint32_t(model.words.size());
    for(auto brick:plants.primitives){
        if(!brick.material)brick.data=(brick.data&0xff000000u)|((brick.data&0xffffffu)+offset);
        model.primitives.push_back(brick);
    }
    model.words.insert(model.words.end(),plants.words.begin(),plants.words.end());
    return model;
}
VoxelModel buildingVoxels(ParcelVisual p){
    if(p.kind>=13){VoxelModel model;for(auto b:World::railFacilityBoxes(p))model.primitives.push_back({{b.x0,b.y0,b.z0},0,{b.x1,b.y1,b.z1},b.material==Material::Window?VGlass:b.material==Material::Roof?VSheetRoof:b.material==Material::Warning?VLight:b.material==Material::Industrial?VIndustrial:b.material==Material::Service?VService:VConcrete});return model;}
    Canvas c(64,128,64);int kind=p.kind;bool dense=p.variant>=4;p.variant&=3;if(dense)c=Canvas(64,192,64);
    if(!kind)return {};
    c.box(1,0,1,15,.25f,15,VConcrete);
    if(!p.level){c.box(2,.25f,2,14,.5f,14,kind==1?VGrass:kind==2?VCommercial:VIndustrial);return c.finish();}
    if(dense&&(kind==1||kind==2)){
        uint8_t wall=kind==1?(p.variant&1?VPlaster:VBrick):VCommercial;
        float h=14.f+p.level*10.f+(p.variant%3)*2.f;
        c.box(2,.25f,2,14,2,14,VConcrete);c.box(2.5f,2,2.5f,13.5f,h,13.5f,wall);
        for(float y=4;y<h-1;y+=3){
            for(float x=3.25f;x<13;x+=2.25f){c.box(x,y,2.375f,x+1.25f,y+1.25f,2.625f,VGlass);c.box(x,y,13.375f,x+1.25f,y+1.25f,13.625f,VGlass);}
            for(float z=3.25f;z<13;z+=2.25f){c.box(2.375f,y,z,2.625f,y+1.25f,z+1.25f,VGlass);c.box(13.375f,y,z,13.625f,y+1.25f,z+1.25f,VGlass);}
        }
        c.box(2,h,2,14,h+.75f,14,VStone);c.box(6,h+.75f,6,10,h+2,10,VMetal);
        if(kind==1){c.box(5,2,1.5f,11,3,2.5f,VConcrete);c.box(7,2,1.375f,9,4,1.625f,VGlass);}
        else c.box(3,2,1.5f,13,4,2.5f,VCommercial);
        return c.finish();
    }
    // Connect the building frontage to the sidewalk at the same quarter-unit
    // elevation, rather than leaving a grass trench before the entrance.
    if(kind==3)c.box(2,0,0,14,.25f,3,VConcrete);
    else if(kind==1&&p.variant==2)c.box(9.25f,0,0,10.75f,.25f,3,VConcrete);
    else c.box(6.5f,0,0,9.5f,.25f,3,VConcrete);
    if(kind==12){
        c=Canvas(128,64,128,0,0,.125f);
        c.box(6.5f,0,0,9.5f,.25f,3,VConcrete);
        c.box(1,0,1,15,.25f,15,VGrass);c.box(7,.25f,1,9,.5f,15,VConcrete);
        c.box(4,.25f,6,12,.375f,7,VConcrete);
        int tree=0;
        for(float x:{4.f,12.f})for(float z:{4.f,12.f}){
            float height=4.25f+((tree+p.variant)%3)*.5f;
            c.box(x-.25f,.25f,z-.25f,x+.25f,height-.5f,z+.25f,VBark);
            for(int branch=0;branch<7;++branch){
                float angle=branch*2.399963f+tree,reach=.75f+(branch%3)*.25f;
                float X=x+std::cos(angle)*reach,Z=z+std::sin(angle)*reach,Y=height-1.75f+branch*.25f;
                for(int step=0;step<=12;++step){float t=step/12.f;
                    float px=x+(X-x)*t,pz=z+(Z-z)*t,py=height-2+(Y-height+2)*t;
                    c.box(px-.125f,py-.125f,pz-.125f,px+.125f,py+.125f,pz+.125f,VBark);
                }
                c.foliage(X-.625f,Y-.375f,Z-.625f,X+.625f,Y+.75f,Z+.625f);
            }
            c.foliage(x-.625f,height-.5f,z-.625f,x+.625f,height+.5f,z+.625f);
            // Low planting beds connect the trees to the lawn.
            for(int plant=0;plant<5;++plant){float a=plant*2.399963f+tree;
                float px=x+std::cos(a),pz=z+std::sin(a);
                c.foliage(px-.5f,.25f,pz-.375f,px+.5f,.625f+(plant%2)*.25f,pz+.375f);
            }
            ++tree;
        }
        // Slatted seats, open backs and metal legs flank the central walk.
        for(float x:{4.f,10.f}){
            for(float dx:{.125f,1.625f})for(float z:{7.125f,7.75f})c.box(x+dx,.25f,z,x+dx+.125f,.875f,z+.125f,VMetal);
            for(int slat=0;slat<4;++slat)c.box(x,.875f,7+slat*.25f,x+2,1,7.125f+slat*.25f,VWood);
            for(float dx:{.125f,1.625f})c.box(x+dx,.875f,7.875f,x+dx+.125f,1.875f,8,VMetal);
            for(int slat=0;slat<3;++slat)c.box(x,1.125f+slat*.25f,7.75f,x+2,1.25f+slat*.25f,7.875f,VWood);
        }
        return c.finish(true);
    }
    if(kind==5){
        // A stepped round reservoir, braced trestle and service catwalk.
        auto disc=[&](float radius,float y,float top,uint8_t material){
            for(float z=8-radius;z<8+radius;z+=.25f){float dz=z+.125f-8;
                float span=std::sqrt(std::max(0.f,radius*radius-dz*dz));
                c.box(8-span,y,z,8+span,top,z+.25f,material);
            }
        };
        for(float x:{4.f,11.f})for(float z:{4.f,11.f}){
            c.box(x-.25f,.25f,z-.25f,x+.75f,.75f,z+.75f,VConcrete);
            c.box(x,.75f,z,x+.5f,8,z+.5f,VMetal);
        }
        for(int step=0;step<28;++step){float t=step*.25f;
            for(float side:{4.f,11.25f}){
                c.box(4+t,1+t,side,4.5f+t,1.5f+t,side+.25f,VMetal);
                c.box(11-t,1+t,side,11.5f-t,1.5f+t,side+.25f,VMetal);
                c.box(side,1+t,4+t,side+.25f,1.5f+t,4.5f+t,VMetal);
                c.box(side,1+t,11-t,side+.25f,1.5f+t,11.5f-t,VMetal);
            }
        }
        c.box(4,7.5f,4,11.5f,7.75f,11.5f,VMetal);
        disc(5.25f,7.75f,8,VMetal);
        disc(5.25f,8.75f,9,VMetal);disc(5,8.75f,9,VEmpty);
        for(int i=0;i<16;++i){float a=i*6.2831853f/16,x=8+std::cos(a)*5,z=8+std::sin(a)*5;
            c.box(x-.125f,8,z-.125f,x+.125f,8.75f,z+.125f,VMetal);
        }
        c.box(7.25f,8,2.5f,8.75f,9,3,VEmpty);
        disc(4.5f,8,12,VService);
        for(float y:{8.f,10.f,11.75f})disc(4.75f,y,y+.25f,VMetal);
        for(int tier=0;tier<9;++tier)disc(4.75f-tier*.5f,12+tier*.25f,12.25f+tier*.25f,VRoof);
        c.box(7.75f,14.25f,7.75f,8.25f,14.75f,8.25f,VMetal);
        c.box(7.5f,.25f,7.5f,8.5f,8,8.5f,VMetal);
        for(float x:{7.25f,8.5f})c.box(x,.75f,2.5f,x+.25f,9,2.75f,VMetal);
        for(float y=1;y<8;y+=.5f)c.box(7.25f,y,2.25f,8.75f,y+.25f,2.5f,VMetal);
        return c.finish();
    }
    if(kind==6){
        c.box(2,.25f,2,14,1.75f,14,VConcrete);
        for(float x:{3.f,9.f}){
            c.box(x,.75f,3,x+4,1.75f,13,VEmpty);
            c.box(x,.75f,3,x+4,1.25f,13,VWater);
            c.box(x+.5f,1.25f,3,x+.75f,2.25f,4,VMetal);
            c.box(x+.5f,2,2.5f,x+1.5f,2.25f,3.25f,VMetal);
        }
        c.box(2,1.75f,7.5f,14,2,8.5f,VMetal);
        for(float z:{7.25f,8.5f}){
            c.box(2,2.75f,z,14,3,z+.25f,VMetal);
            for(float x=2;x<=14;x+=2)c.box(x,2,z,x+.25f,2.75f,z+.25f,VMetal);
        }
        // Central service access stays dry between the two recessed basins.
        for(int step=0;step<6;++step)c.box(7,.25f,step*.25f+.5f,9,.5f+step*.25f,2,VConcrete);
        c.box(7.25f,1.75f,11,8.75f,3,12.25f,VPaint);
        c.box(7.5f,2.25f,10.75f,8.5f,2.75f,11,VMetal);
        c.box(7.75f,2.5f,10.5f,8,2.75f,10.75f,VLight);
        return c.finish();
    }
    if(kind==11){
        // A low classroom wing encloses a paved school courtyard beside the entry.
        c.box(2,.25f,8,14,5,14,VBrick);
        c.box(2,.25f,3,6,7,10,VPlaster);
        c.box(2.25f,.75f,8.25f,13.75f,4.75f,13.75f,VEmpty);
        c.box(2.25f,.75f,3.25f,5.75f,6.75f,9.75f,VEmpty);
        c.box(5.5f,.75f,8.5f,6.25f,3.25f,9.5f,VEmpty);
        c.box(2.25f,.5f,8.25f,13.75f,.75f,13.75f,VWood);
        c.box(2.25f,.5f,3.25f,5.75f,.75f,9.75f,VConcrete);
        for(float x:{7.f,10.5f})for(float z:{8.f,13.75f}){
            c.box(x-.25f,1.25f,z-.25f,x+2.75f,4.25f,z+.5f,VTrim);
            c.box(x,1.5f,z-.25f,x+2.5f,4,z+.5f,VEmpty);
            c.box(x,1.5f,z,x+2.5f,4,z+.25f,VGlass);
            c.box(x+1.125f,1.5f,z,x+1.375f,4,z+.25f,VWood);
            c.box(x,2.5f,z,x+2.5f,2.75f,z+.25f,VWood);
        }
        // Separate classrooms, desks, chairs and wall boards visible through glass.
        c.box(9.5f,.75f,8.25f,9.75f,4.75f,13.75f,VPlaster);
        for(float x:{6.5f,8.f,10.25f,11.75f})for(float z:{10.f,11.75f}){
            c.box(x,1.5f,z,x+1.25f,1.75f,z+.75f,VWood);
            for(float X:{x,x+1.f})c.box(X,.75f,z,X+.25f,1.5f,z+.25f,VMetal);
            c.box(x+.25f,1,z+.875f,x+1,1.25f,z+1.375f,VWood);
            c.box(x+.25f,1.25f,z+1.125f,x+1,1.875f,z+1.375f,VWood);
        }
        for(float x:{6.25f,10.f})c.box(x,2,13.5f,x+2.75f,3.75f,13.75f,VRubber);
        c.box(2.75f,.5f,2.75f,5.25f,3.5f,3.25f,VTrim);
        c.box(3,.75f,2.75f,5,3.25f,3.25f,VEmpty);
        c.box(3,.75f,3,5,3.25f,3.25f,VGlass);
        c.box(3.875f,.75f,2.875f,4.125f,3.25f,3.125f,VMetal);
        c.box(2.5f,3.5f,1.75f,5.5f,3.75f,3.5f,VMetal);
        for(float x:{2.5f,5.25f})c.box(x,.25f,1.75f,x+.25f,3.5f,2,VMetal);
        c.box(3,0,0,5,.25f,3,VConcrete);
        // Stepped clock face above the covered entrance.
        c.box(3.25f,4.75f,2.75f,4.75f,6.25f,3,VTrim);
        c.box(3,5,2.75f,5,6,3,VTrim);
        c.box(3.875f,5.375f,2.5f,4.125f,6,2.75f,VRubber);
        c.box(4,5.375f,2.5f,4.625f,5.625f,2.75f,VRubber);
        c.box(1.75f,7,2.75f,6.25f,7.25f,10.25f,VRoof);
        c.box(6,5,7.75f,14.25f,5.25f,14.25f,VRoof);
        c.box(2,5,10,6,5.25f,14.25f,VRoof);
        for(float x:{7.f,11.f})c.box(x,5.25f,11,x+1.25f,5.75f,12,VMetal);
        c.box(6.25f,.25f,1.5f,14,.5f,7.5f,VAsphalt);
        for(int i=0;i<4;++i){float z=2+i*1.125f;
            c.box(8,.25f,z,9.5f,.5f,z+.25f,VMarking);
            c.box(8,.25f,z,8.25f,.5f,z+1.25f,VMarking);
            c.box(9.25f,.25f,z,9.5f,.5f,z+1.25f,VMarking);
        }
        c.box(8,.25f,6.5f,9.5f,.5f,6.75f,VMarking);
        c.box(12.5f,.75f,3,13.25f,1,6,VWood);
        for(float z:{3.f,5.5f})c.box(12.75f,.5f,z,13,.75f,z+.5f,VMetal);
        c.box(13.25f,1,3,13.5f,1.75f,6,VWood);
        return c.finish();
    }
    if(kind==8){
        float h=5.f+p.level*2;
        c.box(3,.25f,3,10,h,13,VPlaster);
        c.rooms(3.25f,3.25f,9.75f,12.75f,h);
        c.box(2.75f,.25f,2.75f,10.25f,.75f,13.25f,VConcrete);
        for(float y=2;y<h-1;y+=3){
            for(float x:{4.f,7.f})for(float z:{2.75f,12.75f}){
                c.box(x-.25f,y-.25f,z,x+1.75f,y+1.75f,z+.5f,VTrim);
                c.box(x,y,z,x+1.5f,y+1.5f,z+.5f,VGlass);
                c.box(x+.75f,y,z,x+1,y+1.5f,z+.5f,VMetal);
            }
            for(float z:{4.f,7.f,10.f})for(float x:{2.75f,9.75f}){
                c.box(x,y-.25f,z-.25f,x+.5f,y+1.75f,z+1.75f,VTrim);
                c.box(x,y,z,x+.5f,y+1.5f,z+1.5f,VGlass);
            }
        }
        c.box(2.5f,h,2.5f,10.5f,h+.25f,13.5f,VConcrete);
        for(float z:{2.5f,13.25f})c.box(2.5f,h+.25f,z,10.5f,h+.75f,z+.25f,VTrim);
        for(float x:{2.5f,10.25f})c.box(x,h+.25f,2.5f,x+.25f,h+.75f,13.5f,VTrim);
        c.box(5,h+.25f,9,7.5f,h+1,11,VMetal);
        for(float x=5.25f;x<7.5f;x+=.5f)c.box(x,h+1,9.25f,x+.25f,h+1.25f,10.75f,VRubber);
        // Lower glazed reception wing and covered approach form the clinic entrance.
        c.box(10,.25f,7,14,4.75f,13,VPlaster);
        c.rooms(10.25f,7.25f,13.75f,12.75f,4.75f,4);
        c.box(9.75f,.75f,8,10.25f,3.25f,11,VEmpty);
        c.box(10.25f,.5f,6.75f,13.75f,3.25f,7.25f,VMetal);
        c.box(10.5f,.75f,6.75f,13.5f,3,7.25f,VGlass);
        c.box(11.75f,.75f,6.5f,12,3,7.25f,VMetal);
        c.box(12.25f,1.5f,6.5f,12.5f,2,6.75f,VMetal);
        for(float z:{8.f,10.5f}){
            c.box(13.75f,1.25f,z,14.25f,3.25f,z+1.75f,VTrim);
            c.box(13.75f,1.5f,z+.25f,14.25f,3,z+1.5f,VGlass);
        }
        c.box(9.75f,4.75f,6.75f,14.25f,5,13.25f,VConcrete);
        c.box(10,0,0,14,.25f,7,VConcrete);
        c.box(9.75f,3.25f,5.5f,14.5f,3.5f,7.5f,VConcrete);
        for(float x:{10.f,14.f})c.box(x,.25f,5.75f,x+.25f,3.25f,6,VMetal);
        c.box(11,3.5f,6.5f,13,4.75f,6.75f,VTrim);
        c.box(11.75f,3.75f,6.25f,12.25f,4.5f,6.5f,VRedPaint);
        c.box(11.5f,4,6.25f,12.5f,4.25f,6.5f,VRedPaint);
        c.box(4,.75f,1.5f,6,1,2.25f,VWood);c.box(4,1,2,6,1.75f,2.25f,VWood);
        for(float x:{4.25f,5.5f})c.box(x,.25f,1.75f,x+.25f,.75f,2,VMetal);
        return c.finish();
    }
    if(kind==9){
        // Apparatus hall, rear offices and a narrow hose tower distinguish the
        // station from the generic civic-office massing.
        float h=5.75f+p.level*.25f;
        c.box(2,0,0,14,.25f,3,VConcrete);
        c.box(2.5f,.25f,3,13.5f,h,13,VBrick);
        c.rooms(2.75f,3.25f,13.25f,12.75f,h,4.5f);
        c.box(2.25f,.25f,2.75f,13.75f,.75f,13.25f,VConcrete);
        for(float x:{3.f,7.f}){
            c.box(x-.25f,.75f,2.5f,x+3.75f,4.25f,3.25f,VMetal);
            c.box(x,.75f,2.5f,x+3.5f,4,3.25f,VRedPaint);
            for(float y=1;y<4;y+=.5f)c.box(x,y,2.25f,x+3.5f,y+.25f,2.5f,VRedPaint);
            for(float X=x+.25f;X<x+3.25f;X+=.75f)c.box(X,2.75f,2.25f,X+.5f,3.5f,3.25f,VGlass);
            c.box(x,0,.75f,x+.25f,.25f,2.25f,VMarking);
            c.box(x+3.25f,0,.75f,x+3.5f,.25f,2.25f,VMarking);
        }
        c.box(11.25f,.75f,2.75f,12.75f,3.5f,3.25f,VMetal);
        c.box(11.5f,1,2.75f,12.5f,3.25f,3.25f,VGlass);
        c.box(12.25f,1.75f,2.5f,12.5f,2,2.75f,VMetal);
        c.box(4.5f,4.25f,2.5f,11,5.75f,2.75f,VRedPaint);
        const char* letters[4]={"111100110100100","111010010010111","110101110101101","111100110100111"};
        for(int letter=0;letter<4;++letter)for(int row=0;row<5;++row)for(int col=0;col<3;++col)
            if(letters[letter][row*3+col]=='1')c.box(5+letter*1.5f+col*.25f,5.25f-row*.25f,2.25f,5.25f+letter*1.5f+col*.25f,5.5f-row*.25f,2.5f,VTrim);
        c.box(2.25f,h,2.5f,13.75f,h+.25f,13.5f,VConcrete);
        c.box(9,h+.25f,8,13.5f,h+3.25f,13,VPlaster);
        c.box(9.25f,h+.5f,8.25f,13.25f,h+3,12.75f,VEmpty);
        for(float z:{7.75f,12.75f}){
            c.box(9.5f,h+.75f,z,13,h+2.75f,z+.5f,VTrim);
            c.box(9.75f,h+1,z,12.75f,h+2.5f,z+.5f,VGlass);
            c.box(11.25f,h+1,z,11.5f,h+2.5f,z+.5f,VMetal);
        }
        c.box(13.25f,h+1,9,13.75f,h+2.5f,11.75f,VGlass);
        c.box(8.75f,h+3.25f,7.75f,13.75f,h+3.5f,13.25f,VRoof);
        c.box(3,h,10,5.5f,h+6,12.75f,VBrick);
        c.box(3.25f,h+.25f,10.25f,5.25f,h+5.75f,12.5f,VEmpty);
        c.box(3.75f,h+2,9.75f,4.75f,h+4.5f,10.25f,VGlass);
        c.box(2.75f,h+6,9.75f,5.75f,h+6.25f,13,VConcrete);
        for(float y=h+1;y<h+6;y+=1.5f)c.box(2.75f,y,9.75f,5.75f,y+.25f,13,VConcrete);
        return c.finish();
    }
    if(kind==3){
        // Low factory halls with north-light roofs, rather than office towers.
        float eave=4.5f+p.level*.75f;
        Canvas roof(128,32,128,0,0,.125f);
        Canvas shutters(96,40,12,2,2,.125f);
        Canvas intake(8,32,24,14,4,.125f);
        Canvas ladder(8,int((eave+3)*8),16,14,9,.125f);
        c.box(2,.25f,3,14,eave,13,VIndustrial);
        c.box(2.25f,.5f,3.25f,13.75f,eave,12.75f,VEmpty);
        c.box(3,.5f,8,5,2,10,VWood);c.box(8,.5f,7,10,1.5f,9,VMetal);
        c.box(1.75f,.25f,2.75f,14.25f,.75f,13.25f,VConcrete);
        for(float start:{2.f,6.f,10.f}){
            for(float r=0;r<4;r+=.125f){float y=std::floor(r*4)*.125f;
                roof.box(start+r,y,2.75f,start+r+.125f,y+.125f,13.25f,VSheetRoof);
                roof.box(start+r,0,3,start+r+.125f,y,3.25f,VIndustrial);
                roof.box(start+r,0,12.75f,start+r+.125f,y,13,VIndustrial);
            }
            // Raised vertical glazing admits the visual rhythm of a sawtooth roof.
            roof.box(start+3.875f,.125f,3.25f,start+4,1.875f,12.75f,VGlass);
            for(float z=3;z<13;z+=1.5f)roof.box(start+3.875f,0,z,start+4,2,z+.125f,VMetal);
        }
        for(float x:{3.f,7.f}){
            c.box(x,.75f,2.5f,x+3,4,3,VMetal);
            // Shallow ribs and narrow guide tracks read as a roll-up sheet.
            for(float y=1;y<4;y+=.25f)
                shutters.box(x,y,2.375f,x+3,y+.125f,2.5f,VMetal);
            for(float side:{x-.125f,x+3})
                shutters.box(side,.75f,2.375f,side+.125f,4.125f,2.875f,VMetal);
            shutters.box(x,.75f,2.375f,x+3,.875f,2.5f,VRubber);
            shutters.box(x+1.25f,1.125f,2.25f,x+1.75f,1.25f,2.375f,VRubber);
            shutters.box(x-.125f,4,2.375f,x+3.125f,4.375f,3.125f,VMetal);
        }
        c.box(10.75f,.75f,2.5f,12.25f,3.5f,3.25f,VMetal);
        c.box(11,.75f,2.5f,12,3.25f,3.25f,VPaint);
        c.box(11.25f,2.25f,2.5f,11.75f,3,3.25f,VGlass);
        c.box(11.75f,1.5f,2.25f,12,1.75f,2.5f,VMetal);
        // Service fittings give the blank hall walls a readable human scale.
        for(float z:{3.5f,7.5f,12.5f})for(float x:{1.75f,14.f})
            c.box(x,.75f,z,x+.25f,eave,z+.5f,VConcrete);
        c.box(14,2,4.5f,14.5f,3.5f,6.75f,VMetal);
        intake.box(14.5f,2.125f,4.625f,14.625f,3.375f,6.625f,VRubber);
        for(float y=2.25f;y<3.375f;y+=.25f)
            intake.box(14.625f,y,4.625f,14.875f,y+.125f,6.625f,VMetal);
        for(float z:{4.5f,6.625f})
            intake.box(14.5f,2, z,14.875f,3.5f,z+.125f,VMetal);
        for(float y:{2.f,3.375f})
            intake.box(14.5f,y,4.5f,14.875f,y+.125f,6.75f,VMetal);
        // Narrow rails extend above the roof as handholds; stand-offs expose
        // the wall behind the ladder instead of merging it into a solid rack.
        for(float z:{9.5f,10.375f}){
            ladder.box(14.625f,.375f,z,14.75f,eave+2.5f,z+.125f,VMetal);
            for(float y=1.5f;y<eave;y+=2)
                ladder.box(14,y,z,14.625f,y+.125f,z+.125f,VMetal);
        }
        for(float y=.625f;y<eave+1.875f;y+=.375f)
            ladder.box(14.625f,y,9.5f,14.75f,y+.125f,10.5f,VMetal);
        c.box(1.5f,2.25f,4,1.75f,2.5f,12,VMetal);
        c.box(1.5f,2.25f,4,1.75f,eave+.75f,4.25f,VMetal);
        for(float z:{5.f,8.f,11.f})c.box(1.5f,2, z,2,2.75f,z+.25f,VMetal);
        for(float x:{2.25f,6.25f,10.25f}){
            c.box(x,.25f,1.5f,x+.25f,1.5f,1.75f,VRubber);
            c.box(x,.75f,1.5f,x+.25f,1.25f,1.75f,VMarking);
        }
        c.box(12.5f,.25f,10,13.5f,eave+4,11,VBrick);
        c.box(12.25f,eave+4,9.75f,13.75f,eave+4.25f,11.25f,VMetal);
        // Cut a recessed flue through the cap, retaining a thick weather rim.
        c.box(12.75f,eave+3.25f,10.25f,13.25f,eave+4.25f,10.75f,VEmpty);
        c.box(12.75f,eave+3.25f,10.25f,13.25f,eave+3.5f,10.75f,VRubber);
        c.box(2,.25f,13.5f,6,.5f,15,VWood);
        c.box(2.25f,.5f,13.5f,3.75f,2,14.75f,VWood);
        c.box(4.25f,.5f,13.75f,5.5f,1.75f,14.75f,VMetal);
        // Pallet slats and crate bands break up the storage blocks at the rear.
        for(float x=2;x<6;x+=.5f)c.box(x,.5f,13.5f,x+.25f,.75f,15,VWood);
        for(float y:{.75f,1.5f})c.box(2.25f,y,13.25f,3.75f,y+.25f,15,VMetal);
        c.box(8,.25f,13.75f,10.5f,1.5f,15,VPaint);
        c.box(7.75f,1.5f,13.5f,10.75f,1.75f,15.25f,VMetal);
        for(float x:{8.f,10.f})c.box(x,.25f,13.5f,x+.5f,.75f,14,VRubber);
        // A pair of hooped drums occupies the spare rear corner, clear of the ladder.
        // Chamfered quarter-unit silhouettes keep them readable at street scale.
        for(float x:{11.75f,13.5f}){
            c.box(x,.25f,13.75f,x+1.25f,.5f,15,VWood);
            for(int iz=0;iz<5;++iz)for(int ix=0;ix<5;++ix){
                if((ix==0||ix==4)&&(iz==0||iz==4))continue;
                float dx=x+ix*.25f,dz=13.75f+iz*.25f;
                c.box(dx,.5f,dz,dx+.25f,2.25f,dz+.25f,x<13?VRedPaint:VMetal);
                for(float y:{.5f,1.f,1.75f,2.f})
                    c.box(dx,y,dz,dx+.25f,y+.25f,dz+.25f,VMetal);
            }
            // Inset lid color and dark filler cap distinguish the top from a crate.
            c.box(x+.25f,2.f,14,x+1,2.25f,14.75f,x<13?VRedPaint:VMetal);
            c.box(x+.25f,2.25f,14,x+.5f,2.5f,14.25f,VRubber);
        }
        // Keep the chimney opening free of overlapping roof voxels. Only the
        // roof and loading shutters use finer cells; yard props retain their grid.
        roof.box(12.5f,0,10,13.5f,4,11,VEmpty);
        auto model=c.finish(),detail=roof.finish();
        uint32_t wordOffset=uint32_t(model.words.size());
        for(auto brick:detail.primitives){
            brick.lo[1]+=eave;brick.hi[1]+=eave;
            if(!brick.material)brick.data=(brick.data&0xff000000u)|((brick.data&0xffffffu)+wordOffset);
            model.primitives.push_back(brick);
        }
        model.words.insert(model.words.end(),detail.words.begin(),detail.words.end());
        for(Canvas* fixture:{&shutters,&intake,&ladder}){
            auto fixtureModel=fixture->finish(true);wordOffset=uint32_t(model.words.size());
            for(auto brick:fixtureModel.primitives){
                if(!brick.material)brick.data=(brick.data&0xff000000u)|((brick.data&0xffffffu)+wordOffset);
                model.primitives.push_back(brick);
            }
            model.words.insert(model.words.end(),fixtureModel.words.begin(),fixtureModel.words.end());
        }
        return model;
    }
    if(kind==1&&p.variant==2){
        // Narrow house with a low side wing and bay: a distinct L-shaped mass.
        float h=5.f+p.level*3.f;
        c.box(4,.25f,3,11,h,12,VBrick);
        c.rooms(4.25f,3.25f,10.75f,11.75f,h);
        c.box(3.75f,.25f,2.75f,11.25f,.75f,12.25f,VConcrete);
        for(float y=2;y<h-1;y+=3){
            for(float x:{5.f,8.f})for(float z:{2.75f,11.75f}){
                c.box(x-.25f,y-.25f,z,x+1.75f,y+1.75f,z+.5f,VTrim);
                c.box(x,y,z,x+1.5f,y+1.5f,z+.5f,VGlass);
                c.box(x+.5f,y,z,x+.75f,y+1.5f,z+.5f,VWood);
            }
            for(float z:{4.f,7.f,10.f})for(float x:{3.75f,10.75f}){
                c.box(x,y-.25f,z-.25f,x+.5f,y+1.75f,z+1.5f,VTrim);
                c.box(x,y,z,x+.5f,y+1.5f,z+1.25f,VGlass);
            }
        }
        // Thin roof courses sit over masonry gables, with an open central loft.
        for(float r=0;r<4;r+=.25f){float y=h+std::round(r*.75f*4)*.25f;
            c.box(3.75f+r,y,3,11.25f-r,y+.25f,12,VBrick);
            c.box(3.5f+r,y,2.5f,3.75f+r,y+.25f,12.5f,VRoof);
            c.box(11.25f-r,y,2.5f,11.5f-r,y+.25f,12.5f,VRoof);
        }
        c.box(7.25f,h+3,2.5f,7.75f,h+3.25f,12.5f,VRoof);
        c.box(6.25f,h+.25f,3.25f,8.75f,h+1.75f,11.75f,VEmpty);
        c.box(6.25f,h,3.25f,8.75f,h+.25f,11.75f,VWood);
        for(float z:{2.75f,11.75f}){
            c.box(6.5f,h+.25f,z,8.5f,h+2,z+.5f,VTrim);
            c.box(6.75f,h+.5f,z,8.25f,h+1.75f,z+.5f,VGlass);
            c.box(7.25f,h+.5f,z,7.5f,h+1.75f,z+.5f,VWood);
        }
        for(float x:{3.5f,11.25f})c.box(x,h-.25f,2.5f,x+.25f,h+.25f,12.5f,VMetal);
        c.box(3.5f,.25f,11.75f,3.75f,h,12,VMetal);
        c.box(9,h,9,10,h+3.5f,10,VBrick);c.box(8.75f,h+3.5f,8.75f,10.25f,h+3.75f,10.25f,VConcrete);
        c.box(9.25f,h+3.75f,9.25f,9.75f,h+4,9.75f,VRubber);
        c.box(10.75f,.25f,7,14,4,13,VPlaster);
        c.rooms(11,7.25f,13.75f,12.75f,4);
        c.box(10.75f,.75f,8,11.25f,2.75f,9.25f,VEmpty);
        c.box(10.75f,4,6.75f,14.25f,4.25f,13.25f,VRoof);
        c.box(11.5f,1,6.75f,13.5f,3,7.25f,VTrim);
        c.box(11.75f,1.25f,6.75f,13.25f,2.75f,7.25f,VGlass);
        c.box(13.75f,1,7.75f,14.25f,3,12,VTrim);
        c.box(13.75f,1.25f,8,14.25f,2.75f,11.75f,VGlass);
        c.box(13.75f,1.25f,9.75f,14.25f,2.75f,10,VWood);
        c.box(6,.75f,2,9,3.5f,3,VPlaster);
        // The bay opens into the main room, instead of enclosing solid plaster.
        c.box(6.25f,1,2.25f,8.75f,3.25f,3.25f,VEmpty);
        c.box(6.25f,1.25f,1.75f,8.75f,3,2.25f,VGlass);
        c.box(6,1.25f,2.25f,6.25f,3,2.75f,VGlass);
        c.box(8.75f,1.25f,2.25f,9,3,2.75f,VGlass);
        c.box(7.25f,1.25f,1.75f,7.5f,3,2.25f,VTrim);
        c.box(6.25f,1,2.5f,8.75f,1.25f,3,VWood);
        c.box(5.75f,3.5f,1.75f,9.25f,3.75f,3.25f,VRoof);
        c.box(9.5f,.25f,2.75f,10.5f,2.75f,3,VWood);
        c.box(1,.25f,3,3.5f,.5f,14,VGrass);
        c.box(1.25f,.5f,6,3,1,7,VBrick);c.foliage(1.25f,.75f,6,3,2,7);
        c.box(1.25f,.5f,10,3,1,11,VBrick);c.foliage(1.25f,.75f,10,3,2,11);
        c.box(4,.25f,13,9,.5f,15,VWood);
        c.box(5,.5f,13.5f,7,1,14,VWood);c.box(5,1,14,7,1.75f,14.25f,VWood);
        return c.finish();
    }
    float h=5.f+p.level*3.f+(p.variant%3)*1.5f;
    VoxelModel roofDetail;
    bool hasShutters=kind==1&&p.variant==0;
    bool fineWindows=kind==1||kind==2;
    Canvas frontShutters(fineWindows?128:0,256,8,0,2,.125f);
    Canvas ivy(hasShutters?48:0,256,24,2,2,.125f);
    Canvas garden(kind==1?128:0,16,8,0,1,.125f);
    Canvas leftHedge(kind==1?8:0,16,80,.75f,4.75f,.125f);
    Canvas rightHedge(kind==1?8:0,16,80,13.75f,4.75f,.125f);
    Canvas balcony(kind==1&&(p.variant==1||p.variant==3)?128:0,128,16,0,1,.125f);
    Canvas rearShutters(fineWindows?128:0,256,8,0,13,.125f);
    Canvas leftSash(fineWindows?8:0,256,128,2,0,.125f);
    Canvas rightSash(fineWindows?8:0,256,128,13,0,.125f);
    Canvas cafeSeating(kind==2?128:0,48,24,0,0,.125f);
    Canvas shopYard(kind==2?16:0,24,80,13,3,.125f);
    Canvas houseYard(kind==1&&(p.variant&1)?12:0,24,80,1.5f,4,.125f);
    uint8_t wall=kind==1?(p.variant%2?VPlaster:VBrick):kind==2?(p.variant%2?VCommercial:VBrick):kind==3?VIndustrial:VService;
    c.box(3,.25f,3,13,h,13,wall);
    c.rooms(3.25f,3.25f,12.75f,12.75f,h,kind==2?4.5f:3.f);
    c.box(2.75f,.25f,2.75f,13.25f,1,13.25f,VConcrete);
    // Shops have a taller ground storey and a clear fascia below the upper windows.
    bool wideCommercial=kind==2&&(p.variant&1);
    float windowWidth=wideCommercial?3.f:1.5f;
    for(float y=kind==2?6.f:2.f;y<h-1;y+=3){
        for(float x=wideCommercial?3.75f:4.f;x<12;x+=wideCommercial?4.5f:3.f){
            float end=x+windowWidth;
            if(!fineWindows)c.box(x-.25f,y-.25f,2.75f,end+.25f,y+1.75f,3.25f,VTrim);c.box(x,y,2.75f,end,y+1.5f,3,VEmpty);c.box(x,y,3,end,y+1.5f,3.25f,VGlass);
            if(!fineWindows)c.box(x-.25f,y-.25f,12.75f,end+.25f,y+1.75f,13.25f,VTrim);c.box(x,y,13,end,y+1.5f,13.25f,VEmpty);c.box(x,y,12.75f,end,y+1.5f,13,VGlass);
            if(!fineWindows)c.box(2.75f,y-.25f,x-.25f,3.25f,y+1.75f,end+.25f,VTrim);c.box(2.75f,y,x,3,y+1.5f,end,VEmpty);c.box(3,y,x,3.25f,y+1.5f,end,VGlass);
            if(!fineWindows)c.box(12.75f,y-.25f,x-.25f,13.25f,y+1.75f,end+.25f,VTrim);c.box(13,y,x,13.25f,y+1.5f,end,VEmpty);c.box(12.75f,y,x,13,y+1.5f,end,VGlass);
            if(fineWindows){
                float middle=x+windowWidth*.5f;
                uint8_t sashMaterial=kind==2?VMetal:VWood;
                // Projecting stone sills and timber sash divide the panes at
                // the native voxel scale, rather than painting lines on glass.
                for(Canvas* sash:{&frontShutters,&rearShutters}){
                    // The entrance replaces the middle ground-floor opening.
                    if(sash==&frontShutters&&y<3.5f&&x==7.f)continue;
                    float z=sash==&frontShutters?2.875f:13.f;
                    sash->box(x-.125f,y-.125f,z,end+.125f,y+1.625f,z+.125f,VTrim);
                    sash->box(x,y,z,end,y+1.5f,z+.125f,VEmpty);
                    float sillZ=sash==&frontShutters?2.625f:13.f;
                    sash->box(x-.25f,y-.125f,sillZ,end+.25f,y,sillZ+.375f,VConcrete);
                    sash->box(middle,y,z,middle+.125f,y+1.5f,z+.125f,sashMaterial);
                    if(p.variant%2)sash->box(x,y+.75f,z,end,y+.875f,z+.125f,sashMaterial);
                }
                for(Canvas* sash:{&leftSash,&rightSash}){
                    float X=sash==&leftSash?2.875f:13.f;
                    sash->box(X,y-.125f,x-.125f,X+.125f,y+1.625f,end+.125f,VTrim);
                    sash->box(X,y,x,X+.125f,y+1.5f,end,VEmpty);
                    float sillX=sash==&leftSash?2.625f:13.f;
                    sash->box(sillX,y-.125f,x-.25f,sillX+.375f,y,end+.25f,VConcrete);
                    sash->box(X,y,middle,X+.125f,y+1.5f,middle+.125f,sashMaterial);
                    if(p.variant%2)sash->box(X,y+.75f,x,X+.125f,y+.875f,end,sashMaterial);
                }
            }
            if(hasShutters){
                // Fine timber frames and recessed louvers on both street and garden faces.
                for(float sx:{x-.75f,x+1.75f}){
                    auto shutterBox=[&](float x0,float y0,float depth0,float x1,float y1,float depth1,uint8_t mat){
                        frontShutters.box(x0,y0,2.5f+depth0,x1,y1,2.5f+depth1,mat);
                        rearShutters.box(x0,y0,13.5f-depth1,x1,y1,13.5f-depth0,mat);
                    };
                    shutterBox(sx,y,0,sx+.125f,y+1.5f,.25f,VWood);
                    shutterBox(sx+.375f,y,0,sx+.5f,y+1.5f,.25f,VWood);
                    for(float rail:{y,y+1.375f})shutterBox(sx,rail,0,sx+.5f,rail+.125f,.25f,VWood);
                    shutterBox(sx+.125f,y+.125f,.125f,sx+.375f,y+1.375f,.25f,VWood);
                    for(float slat=y+.25f;slat<y+1.375f;slat+=.25f)
                        shutterBox(sx+.125f,slat,0,sx+.375f,slat+.125f,.125f,VWood);
                    for(float hinge:{y+.25f,y+1.125f})
                        shutterBox(sx,hinge,-.125f,sx+.125f,hinge+.125f,0,VMetal);
                }
            }
        }
        if(kind!=1)c.box(2.75f,y+2,2.75f,13.25f,y+2.25f,13.25f,VConcrete);
        if(kind==1&&(p.variant==1||p.variant==3)&&y>=5){
            float left=p.variant==1?6.5f:3.75f,right=p.variant==1?9.f:12.25f;
            c.box(left,y-.5f,1.5f,right,y-.25f,3,VConcrete);
            balcony.box(left,y+.625f,1.5f,right,y+.75f,1.625f,VMetal);
            balcony.box(left,y-.125f,1.5f,right,y,1.625f,VMetal);
            for(float x=left;x<right;x+=.5f)
                balcony.box(x,y-.25f,1.5f,x+.125f,y+.625f,1.625f,VMetal);
            for(float x:{left,right-.125f}){
                balcony.box(x,y+.625f,1.5f,x+.125f,y+.75f,3,VMetal);
                balcony.box(x,y-.125f,1.5f,x+.125f,y,3,VMetal);
                for(float z=2;z<3;z+=.5f)
                    balcony.box(x,y-.25f,z,x+.125f,y+.625f,z+.125f,VMetal);
            }
        }
    }
    c.box(7,.25f,2.75f,9,3,3.25f,VWood);c.box(6.5f,.25f,2,9.5f,.5f,3,VConcrete);
    if(kind==1){
        // Recessed paneled entrance with a transom, threshold and small lamp.
        c.box(6.75f,.5f,2.5f,9.25f,3.75f,3.25f,VTrim);
        c.box(7,.5f,2.5f,9,3.5f,3.25f,VEmpty);
        c.box(7,.5f,3,9,3,3.25f,VWood);
        for(float x:{7.f,8.75f})c.box(x,.5f,2.75f,x+.25f,3,3,VWood);
        for(float y:{.5f,1.5f,2.75f})c.box(7,y,2.75f,9,y+.25f,3,VWood);
        c.box(7.25f,1.75f,3,8.75f,2.75f,3.25f,VGlass);
        c.box(7.75f,1.75f,2.75f,8,2.75f,3.25f,VWood);
        c.box(7,3,3,9,3.5f,3.25f,VGlass);
        c.box(7,3,2.75f,9,3.25f,3,VWood);
        c.box(8.5f,1.25f,2.5f,8.75f,1.5f,2.75f,VMetal);
        c.box(6.5f,3.75f,2.25f,9.5f,4,3.25f,VConcrete);
        c.box(9.75f,2.25f,2.5f,10,3,3,VMetal);
        c.box(9.75f,2.5f,2.25f,10,2.75f,2.5f,VLight);
        // Alternate ridge direction using the existing cached art variant.
        // Eighth-unit cells across the slope, quarter-unit along the ridge.
        Canvas roof((p.variant&2)?64:128,48,(p.variant&2)?128:64,0,0,.125f);
        auto roofBox=[&](float x0,float y0,float z0,float x1,float y1,float z1,uint8_t material){
            if(p.variant&2){std::swap(x0,z0);std::swap(x1,z1);}
            // The downpipe reaches ground level and stays in the wall canvas.
            if(y0<h-.25f){
                c.box(x0,y0,z0,x1,y1,z1,material);
            }else if(p.variant&2)roof.box(x0*.5f,y0-h+.25f,z0,x1*.5f,y1-h+.25f,z1,material);
            else roof.box(x0,y0-h+.25f,z0*.5f,x1,y1-h+.25f,z1*.5f,material);
        };
        // Masonry gable ends sit beneath a thin stepped roof shell.
        for(float r=0;r<5.75f;r+=.125f){float y=h+std::round(r*.75f*8)*.125f;
            roofBox(2.375f+r,y,3,13.625f-r,y+.125f,13,wall);
            roofBox(2.25f+r,y,2.25f,2.375f+r,y+.125f,13.75f,VRoof);
            roofBox(13.625f-r,y,2.25f,13.75f-r,y+.125f,13.75f,VRoof);
        }
        roofBox(7.875f,h+4.25f,2.25f,8.125f,h+4.375f,13.75f,VRoof);
        roofBox(6.5f,h+.25f,3.25f,9.5f,h+2.25f,12.75f,VEmpty);
        roofBox(6.5f,h,3.25f,9.5f,h+.25f,12.75f,VWood);
        for(float z:{2.75f,12.75f}){
            roofBox(7.125f,h+.625f,z,8.875f,h+2.125f,z+.5f,VTrim);
            roofBox(7.25f,h+.75f,z,8.75f,h+2,z+.5f,VGlass);
            roofBox(7.875f,h+.75f,z,8,h+2,z+.5f,VWood);
        }
        if(p.variant==3){
            // Paired street-facing dormers break the long roof plane.
            for(float z:{4.f,9.75f}){
                roofBox(3.5f,h+1.f,z,6.75f,h+3.25f,z+2.25f,wall);
                roofBox(3.75f,h+1.25f,z+.25f,6.5f,h+3.f,z+2.f,VEmpty);
                roofBox(3.375f,h+1.5f,z+.25f,3.625f,h+3.f,z+2.f,VTrim);
                roofBox(3.375f,h+1.75f,z+.5f,3.875f,h+2.75f,z+1.75f,VEmpty);
                roofBox(3.5f,h+1.75f,z+.5f,3.625f,h+2.75f,z+1.75f,VGlass);
                roofBox(3.25f,h+1.75f,z+1.f,3.375f,h+2.75f,z+1.125f,VWood);
                roofBox(3.25f,h+1.375f,z+.125f,3.75f,h+1.5f,z+2.125f,VTrim);
                // A small zinc gable replaces the broad flat dormer lid.
                // Quarter-unit courses match the roof canvas along its ridge.
                for(int course=0;course<10;++course){
                    float side=z-.125f+course*.25f;
                    float top=h+3.25f+std::min(course,9-course)*.125f;
                    if(course>0&&course<9)
                        roofBox(3.5f,h+3.25f,side,3.75f,top,side+.25f,wall);
                    roofBox(3.25f,top,side,6.75f,top+.125f,side+.25f,VMetal);
                }
                roofBox(3.125f,h+3.75f,z+1.f,6.75f,h+3.875f,z+1.25f,VMetal);
            }
        }
        // Fascia and rain gutters give the overhang a readable edge.
        roofBox(2.25f,h-.25f,2.25f,2.5f,h+.25f,13.75f,VMetal);
        roofBox(13.5f,h-.25f,2.25f,13.75f,h+.25f,13.75f,VMetal);
        roofBox(2.5f,.25f,12.75f,2.75f,h,13,VMetal);
        roofBox(10,h,9,11.25f,h+4,10.25f,VBrick);
        roofBox(9.75f,h+4,8.75f,11.5f,h+4.25f,10.5f,VConcrete);
        roofBox(10.25f,h+4.25f,9.25f,10.75f,h+4.5f,9.75f,VRubber);
        roofDetail=roof.finish();
        for(auto& brick:roofDetail.primitives){
            int ridgeAxis=(p.variant&2)?0:2;
            brick.lo[ridgeAxis]*=2;brick.hi[ridgeAxis]*=2;
        }
        // Long roof strips repeat exactly along the ridge. Join those bricks
        // without changing any occupied volume or the slope's fine cell size.
        int ridgeAxis=(p.variant&2)?0:2;
        auto materialCell=[&](const VoxelPrimitive& brick,int index){
            return brick.material?brick.material:(roofDetail.words[(brick.data&0xffffffu)+index/4]>>((index%4)*8))&255u;
        };
        auto extruded=[&](const VoxelPrimitive& brick){
            if(brick.material)return true;
            for(int index=0;index<512;++index){
                int base=ridgeAxis==0?(index/8)*8:index%64;
                if(materialCell(brick,index)!=materialCell(brick,base))return false;
            }
            return true;
        };
        auto& bricks=roofDetail.primitives;
        for(size_t i=0;i<bricks.size();++i){
            if(!extruded(bricks[i]))continue;
            for(size_t j=i+1;j<bricks.size();){
                bool same=bricks[i].hi[ridgeAxis]==bricks[j].lo[ridgeAxis];
                for(int axis=0;axis<3;++axis)if(axis!=ridgeAxis)
                    same&=bricks[i].lo[axis]==bricks[j].lo[axis]&&bricks[i].hi[axis]==bricks[j].hi[axis];
                if(same&&extruded(bricks[j])){
                    for(int index=0;index<512&&same;++index)same=materialCell(bricks[i],index)==materialCell(bricks[j],index);
                }else same=false;
                if(same){bricks[i].hi[ridgeAxis]=bricks[j].hi[ridgeAxis];bricks.erase(bricks.begin()+j);}
                else ++j;
            }
        }
    }else{
        if(kind==2){
            // Detail coordinates are raised by h-.25 when the roof is merged.
            // A narrow slab lip, recessed membrane and capped parapet replace
            // the broad tiled-looking fascia on the flat shop roof.
            Canvas roof(128,16,128,0,0,.125f);
            roof.box(3,.25f,3,13,.75f,13,VConcrete);
            roof.box(2.75f,.625f,2.75f,13.25f,.75f,13.25f,VConcrete);
            roof.box(3.25f,.75f,3.25f,12.75f,.875f,12.75f,VMembrane);
            for(float z:{3.f,12.75f}){
                roof.box(3,.75f,z,13,1.25f,z+.25f,wall);
                roof.box(2.875f,1.25f,z-.125f,13.125f,1.375f,z+.375f,VMetal);
            }
            for(float x:{3.f,12.75f}){
                roof.box(x,.75f,3,x+.25f,1.25f,13,wall);
                roof.box(x-.125f,1.25f,2.875f,x+.375f,1.375f,13.125f,VMetal);
            }
            // Compact twin-fan condenser on a raised curb, with front louvers.
            roof.box(4.125f,.875f,5.125f,6.875f,1,6.625f,VRubber);
            roof.box(4,1,5,7,1.75f,6.75f,VMetal);
            for(float y:{1.125f,1.375f})
                roof.box(4.125f,y,4.875f,6.875f,y+.125f,5,VRubber);
            for(float x:{4.875f,6.125f}){
                for(float dz=-.5f;dz<.5f;dz+=.125f){
                    float span=std::sqrt(.25f-(dz+.0625f)*(dz+.0625f));
                    roof.box(x-span,1.625f,5.875f+dz,x+span,1.75f,6+dz,VRubber);
                }
                roof.box(x-.375f,1.75f,5.8125f,x+.375f,1.875f,5.9375f,VMetal);
                roof.box(x-.0625f,1.75f,5.5f,x+.0625f,1.875f,6.25f,VMetal);
                roof.box(x-.125f,1.875f,5.75f,x+.125f,2,6,VMetal);
            }
            // A separate capped exhaust has a dark opening below its overhang.
            roof.box(9.125f,.875f,9.125f,10.875f,1.375f,10.875f,VMetal);
            roof.box(9.25f,1.375f,9.25f,10.75f,1.625f,10.75f,VRubber);
            for(float x:{9.125f,10.75f})for(float z:{9.125f,10.75f})
                roof.box(x,1.375f,z,x+.125f,1.625f,z+.125f,VMetal);
            roof.box(9,1.625f,9,11,1.75f,11,VMetal);
            roof.box(7,.875f,6,9.75f,1,6.125f,VMetal);
            roof.box(9.625f,.875f,6.125f,9.75f,1,9.125f,VMetal);
            roofDetail=roof.finish(true);
        }else{
            c.box(2.5f,h,2.5f,13.5f,h+.5f,13.5f,VRoof);
            for(float z:{2.5f,13.25f})c.box(2.5f,h+.5f,z,13.5f,h+1,z+.25f,VConcrete);
            for(float x:{2.5f,13.25f})c.box(x,h+.5f,2.5f,x+.25f,h+1,13.5f,VConcrete);
        }
        if(kind!=2){
            c.box(4,h+.5f,5,7,h+1.5f,8,VMetal);c.box(9,h+.5f,9,11,h+1.25f,11,VMetal);
            for(float x=4.25f;x<7;x+=.5f)c.box(x,h+1.5f,5.25f,x+.25f,h+1.75f,7.75f,VRubber);
        }
        if(kind==2){
            if(p.variant==1){
                // Masonry piers and a dentilled cornice distinguish older shops
                // from the plain brick storefronts without covering the glazing.
                for(float x:{3.f,7.75f,12.625f}){
                    float width=x==7.75f?.5f:.375f;
                    frontShutters.box(x,5.875f,2.875f,x+width,h-.625f,3,VTrim);
                    frontShutters.box(x-.125f,5.75f,2.75f,x+width+.125f,6,3,VTrim);
                    frontShutters.box(x-.125f,h-.875f,2.75f,x+width+.125f,h-.625f,3,VTrim);
                }
                frontShutters.box(2.875f,h-1.125f,2.875f,13.125f,h-1,3,VTrim);
                frontShutters.box(2.75f,h-.625f,2.625f,13.25f,h-.375f,3,VTrim);
                frontShutters.box(2.625f,h-.375f,2.5f,13.375f,h-.25f,3,VTrim);
                for(float x=3.125f;x<13;x+=.5f)
                    frontShutters.box(x,h-.875f,2.625f,x+.25f,h-.625f,2.875f,VTrim);
            }
            // Cut through the existing wall and domestic window frames before
            // fitting the display panes. Glass in front of masonry is not a window.
            c.box(3.25f,.75f,2.5f,12.75f,3.75f,3.25f,VEmpty);
            for(float x:{3.25f,6.5f,9.25f,12.5f})c.box(x,.75f,2.75f,x+.25f,3.75f,3.25f,VMetal);
            for(float x:{3.5f,9.5f}){
                c.box(x,1,3,x+3,3.5f,3.25f,VGlass);
                c.box(x,.75f,2.5f,x+3,1,3.25f,VConcrete);
                c.box(x,3.5f,2.75f,x+3,3.75f,3.25f,VMetal);
                // Display shelves and small goods sit behind the glazing.
                for(float y:{1.25f,2.25f}){
                    c.box(x,y,4,x+3,y+.25f,4.75f,VWood);
                    for(int i=0;i<4;++i)c.box(x+.25f+i*.75f,y+.25f,4.25f,x+.75f+i*.75f,y+.5f+(i%2)*.25f,4.75f,i%2?VTrim:VPaint);
                }
            }
            c.box(6.75f,.5f,3,9.25f,3.5f,3.25f,VGlass);
            for(float x:{6.75f,8.f,9.f})c.box(x,.5f,2.75f,x+.25f,3.5f,3.25f,VMetal);
            c.box(6.75f,.5f,2.75f,9.25f,.75f,3.25f,VMetal);
            c.box(7.5f,1.5f,2.5f,7.75f,2,2.75f,VMetal);
            c.box(8.5f,1.5f,2.5f,8.75f,2,2.75f,VMetal);
            // Sloped striped canvas and hanging valance, with wall brackets.
            for(int step=0;step<12;++step){float z=1.5f+step*.125f,y=3.5f+step*.0625f;
                for(float x=3;x<13;x+=.5f)cafeSeating.box(x,y,z,x+.5f,y+.125f,z+.125f,int((x-3)*2)%2?VTrim:VPaint);
            }
            for(float x=3;x<13;x+=.5f){
                uint8_t cloth=int((x-3)*2)%2?VTrim:VPaint;
                cafeSeating.box(x,3.375f,1.5f,x+.5f,3.625f,1.625f,cloth);
                cafeSeating.box(x+.125f,3.25f,1.5f,x+.375f,3.375f,1.625f,cloth);
            }
            for(float x:{3.f,12.875f}){
                cafeSeating.box(x,3.375f,1.625f,x+.125f,3.5f,3,VMetal);
                for(int step=0;step<10;++step){float z=1.75f+step*.125f,y=3.5f-step*.0625f;
                    cafeSeating.box(x,y-.125f,z,x+.125f,y,z+.125f,VMetal);
                }
            }
            c.box(4.75f,3.75f,2.5f,11.25f,5.75f,2.75f,VWood);
            c.box(5,4,2.25f,11,5.5f,2.5f,VPaint);
            // Raised block lettering stays legible without a texture dependency.
            const char* letters[4]={"111100100100111","010101111101101","111100110100100","111100110100111"};
            for(int letter=0;letter<4;++letter)for(int row=0;row<5;++row)for(int col=0;col<3;++col)
                if(letters[letter][row*3+col]=='1')c.box(5.5f+letter*1.25f+col*.25f,5.25f-row*.25f,2,5.75f+letter*1.25f+col*.25f,5.5f-row*.25f,2.25f,VTrim);
            // Delivery supplies occupy the existing paved side strip.
            for(float z:{10.f,11.5f}){
                shopYard.box(13.625f,.5f,z,14.5f,1.625f,z+.875f,VCommercial);
                shopYard.box(13.5f,1.625f,z-.125f,14.625f,1.75f,z+1,VRubber);
                for(float x:{13.625f,14.375f})
                    shopYard.box(x,.25f,z+.625f,x+.125f,.625f,z+.875f,VRubber);
                shopYard.box(14.5f,1.375f,z+.25f,14.625f,1.5f,z+.625f,VMetal);
            }
            for(float z:{5.f,6.25f})
                shopYard.box(13.5f,.25f,z,14.75f,.5f,z+.25f,VWood);
            for(int slat=0;slat<5;++slat){float x=13.5f+slat*.25f;
                shopYard.box(x,.5f,5,x+.125f,.625f,6.5f,VWood);
            }
            for(int tier=0;tier<1+int(p.variant%2);++tier){
                float x=13.625f+(tier%2)*.125f,y=.625f+tier*.75f,z=5.125f;
                shopYard.box(x,y,z,x+.875f,y+.125f,z+1.125f,VWood);
                for(float dx:{0.f,.75f})for(float dz:{0.f,1.f})
                    shopYard.box(x+dx,y,z+dz,x+dx+.125f,y+.75f,z+dz+.125f,VWood);
                for(float dy:{.25f,.5f}){
                    for(float dz:{0.f,1.f})shopYard.box(x,y+dy,z+dz,x+.875f,y+dy+.125f,z+dz+.125f,VWood);
                    for(float dx:{0.f,.75f})shopYard.box(x+dx,y+dy,z,x+dx+.125f,y+dy+.125f,z+1.125f,VWood);
                }
            }
            // Round bistro tables and open metal chairs leave the doors clear.
            for(float x:{5.25f,10.75f}){
                constexpr float z=1.375f;
                for(float dz=-.625f;dz<.625f;dz+=.125f){
                    float span=std::sqrt(.625f*.625f-(dz+.0625f)*(dz+.0625f));
                    cafeSeating.box(x-span,1.125f,z+dz,x+span,1.25f,z+dz+.125f,VWood);
                }
                cafeSeating.box(x-.0625f,.25f,z-.0625f,x+.0625f,1.125f,z+.0625f,VMetal);
                cafeSeating.box(x-.375f,.25f,z-.0625f,x+.375f,.375f,z+.0625f,VMetal);
                cafeSeating.box(x-.0625f,.25f,z-.375f,x+.0625f,.375f,z+.375f,VMetal);
                for(int side:{-1,1}){
                    float X=x+side*.9375f,back=X+side*.25f;
                    cafeSeating.box(X-.25f,.625f,z-.25f,X+.25f,.75f,z+.25f,VWood);
                    for(float dx:{-.25f,.125f})for(float dz:{-.25f,.125f})
                        cafeSeating.box(X+dx,.25f,z+dz,X+dx+.125f,.625f,z+dz+.125f,VMetal);
                    for(float dz:{-.25f,.125f})cafeSeating.box(back-.0625f,.625f,z+dz,back+.0625f,1.375f,z+dz+.125f,VMetal);
                    for(float y:{.875f,1.125f})cafeSeating.box(back-.0625f,y,z-.25f,back+.0625f,y+.125f,z+.25f,VWood);
                }
                cafeSeating.box(x-.125f,1.25f,z-.125f,x+.125f,1.5f,z+.125f,VBrick);
                cafeSeating.foliage(x-.25f,1.5f,z-.25f,x+.25f,1.875f,z+.25f);
            }
        }
        if(kind>3){c.box(6,4,2.5f,10,5,3,VTrim);c.box(7.5f,h+.5f,7.5f,8.5f,h+2,8.5f,VService);}
    }
    if(kind==4){
        for(float x:{4.f,7.f}){c.box(x,h,8,x+1.5f,h+6,9.5f,VBrick);c.box(x,h+4.5f,8,x+1.5f,h+5,9.5f,VTrim);}
        c.box(4,.25f,1,12,1.75f,2,VMetal);
    }
    if(kind==7){
        for(float x:{4.f,9.f})c.box(x,.5f,2.75f,x+3,4,3,VMetal);
    }
    if(kind==10){c.box(6,4,2.25f,10,5,2.75f,VCommercial);c.box(7.5f,h+.5f,7.5f,8,h+4,8,VMetal);}
    if(kind==11){c.box(2,.25f,1,14,.5f,2,VMarking);c.box(7,h+.5f,2.75f,9,h+2,3.25f,VTrim);}
    // Low hedges add scale and soften residential lots without changing occupancy.
    if(kind==1){
        if(p.variant==0){
            // A rooted climber wraps the corner and spreads between window rows.
            // Keep the leaves outside the wall so the rooms and panes remain intact.
            ivy.box(2.875f,.25f,2.875f,3,h-.5f,3,VWood);
            for(int i=0;i<int((h-1)/.75f);++i){
                float y=.5f+i*.75f,reach=.25f+(i%3)*.125f;
                // Separated leaf sprays expose the woody runner between them.
                ivy.foliage(2.625f,y,2.375f,3+reach,y+.25f,2.875f);
                ivy.foliage(2.375f,y+.25f,2.75f,2.875f,y+.5f,3+reach);
            }
            for(float y=3.75f;y<h-.5f;y+=3){
                float reach=y<4?6.f:4.25f;
                ivy.box(3,y+.25f,2.875f,reach,y+.375f,3,VWood);
                for(float x=3.25f;x<reach;x+=.5f){
                    float offset=.25f*std::sin(x*5);
                    ivy.foliage(x-.125f,y+.5f+offset,2.375f,x+.25f,y+.75f+offset,2.875f);
                    ivy.foliage(x,y+offset,2.5f,x+.375f,y+.25f+offset,3);
                }
                ivy.box(2.875f,y+.25f,3,3,y+.375f,4.5f,VWood);
                for(float z=3.25f;z<4.75f;z+=.5f){
                    ivy.foliage(2.375f,y+.5f,z,2.875f,y+.75f,z+.375f);
                    ivy.foliage(2.5f,y,z+.125f,3,y+.25f,z+.5f);
                }
            }
            ivy.foliage(2,.25f,2,4.25f,1.5f,3);
            ivy.foliage(2,.25f,2.75f,3,1.25f,4.5f);
        }
        for(Canvas* hedge:{&leftHedge,&rightHedge})for(int i=0;i<9;++i){
            float x=hedge->ox+.5f,z=5.5f+i;
            float top=1.125f+((i*7+p.variant*3)%5)*.125f;
            hedge->foliage(x-.5f,.25f,z-.75f,x+.5f,top,z+.75f);
            // Offset upper growth keeps the trimmed hedge from reading as
            // identical cubes, while retaining a continuous lower body.
            float shift=i%2?.125f:-.125f;
            hedge->foliage(x-.25f+shift,.75f,z-.375f,x+.25f+shift,top+.125f,z+.375f);
        }
        for(float x:{3.5f,10.5f}){
            c.box(x,.25f,1,x+2,.75f,2,VBrick);
            garden.box(x+.125f,.75f,1.125f,x+1.875f,.875f,1.875f,VBark);
            for(int plant=0;plant<3;++plant){
                float px=x+.375f+plant*.625f,pz=1.5f+(plant%2?-.125f:.125f);
                float top=1.25f+(plant%2)*.25f;
                garden.foliage(px-.375f,.875f,pz-.375f,px+.375f,top,pz+.375f);
                garden.foliage(px-.25f,top-.25f,pz-.25f,px+.25f,top+.125f,pz+.25f);
                // Small pale blossoms sit among the leaves, with varied heights.
                garden.box(px-.125f,top,pz-.125f,px,top+.125f,pz,VTrim);
                if(plant!=1)garden.box(px+.125f,top-.125f,pz,px+.25f,top,pz+.125f,VTrim);
            }
        }
        for(float y:{.5f,1.f})c.box(1.5f,y,14,14.5f,y+.25f,14.25f,VWood);
        for(int i=0;i<26;++i){float x=1.5f+i*.5f;
            c.box(x,.25f,14.25f,x+.25f,1.5f,14.5f,VWood);
        }
        for(float x=2;x<14;x+=1.5f){
            c.box(x,.25f,14,x+.25f,1.75f,14.5f,VWood);
            c.box(x-.125f,1.75f,14,x+.375f,2,14.5f,VWood);
        }
        if(p.variant&1){
            // A narrow sitting spot fits between the existing hedge and wall.
            for(float z:{6.125f,7.75f}){
                for(float x:{1.875f,2.5f})
                    houseYard.box(x,.25f,z,x+.125f,.875f,z+.125f,VMetal);
                houseYard.box(1.875f,.75f,z,2.875f,.875f,z+.125f,VMetal);
                houseYard.box(2.75f,.25f,z,2.875f,1.75f,z+.125f,VMetal);
            }
            for(float x:{1.875f,2.125f,2.375f})
                houseYard.box(x,.875f,6,x+.125f,1,8.125f,VWood);
            for(float y:{1.125f,1.375f,1.625f})
                houseYard.box(2.625f,y,6,2.75f,y+.125f,8.125f,VWood);
            // Raised herb bed with soil visible between separate plants.
            houseYard.box(1.875f,.25f,10,2.875f,.625f,12.5f,VWood);
            houseYard.box(2,.5f,10.125f,2.75f,.625f,12.375f,VBark);
            for(int plant=0;plant<4;++plant){
                float z=10.375f+plant*.625f,top=.875f+(plant%2)*.25f;
                houseYard.foliage(2,.625f,z-.25f,2.625f,top,z+.25f);
                houseYard.foliage(2.125f,top-.125f,z-.125f,2.5f,top+.125f,z+.125f);
            }
        }
        // Compact bins sit beside the house, within the original paved parcel.
        for(float z:{10.5f,12.f}){
            c.box(13.25f,.5f,z,14,1.5f,z+.75f,VPaint);
            c.box(13,1.5f,z,14.25f,1.75f,z+1,VMetal);
            c.box(13.25f,.25f,z+.5f,13.5f,.75f,z+.75f,VRubber);
            c.box(13.75f,.25f,z+.5f,14,.75f,z+.75f,VRubber);
        }
        c.box(6,.25f,1,6.25f,1.5f,1.25f,VMetal);c.box(5.75f,1.25f,.75f,6.5f,1.75f,1.5f,VMetal);
    }
    auto model=c.finish();
    uint32_t wordOffset=uint32_t(model.words.size());
    for(auto brick:roofDetail.primitives){
        brick.lo[1]+=h-.25f;brick.hi[1]+=h-.25f;
        if(!brick.material)brick.data=(brick.data&0xff000000u)|((brick.data&0xffffffu)+wordOffset);
        model.primitives.push_back(brick);
    }
    model.words.insert(model.words.end(),roofDetail.words.begin(),roofDetail.words.end());
    for(Canvas* shutters:{&ivy,&garden,&leftHedge,&rightHedge,&balcony,&frontShutters,&rearShutters,&leftSash,&rightSash,&cafeSeating,&shopYard,&houseYard}){
        // Clip fine detail against the facade, then give the climber precedence
        // over shutters wherever their fine cells coincide.
        for(int z=0;z<shutters->nz;++z)for(int y=0;y<shutters->ny;++y)for(int x=0;x<shutters->nx;++x){
            auto& cell=shutters->v[x+shutters->nx*(y+shutters->ny*z)];
            if(!cell)continue;
            int cx=int((shutters->ox+(x+.5f)*shutters->cellSize-c.ox)/c.cellSize);
            int cy=int((y+.5f)*shutters->cellSize/c.cellSize);
            int cz=int((shutters->oz+(z+.5f)*shutters->cellSize-c.oz)/c.cellSize);
            if(cx>=0&&cx<c.nx&&cy>=0&&cy<c.ny&&cz>=0&&cz<c.nz&&c.v[cx+c.nx*(cy+c.ny*cz)])cell=VEmpty;
            if(shutters!=&ivy){
                int ix=int(std::floor((shutters->ox+(x+.5f)*shutters->cellSize-ivy.ox)/ivy.cellSize));
                int iz=int(std::floor((shutters->oz+(z+.5f)*shutters->cellSize-ivy.oz)/ivy.cellSize));
                if(ix>=0&&ix<ivy.nx&&iz>=0&&iz<ivy.nz&&ivy.v[ix+ivy.nx*(y+ivy.ny*iz)])cell=VEmpty;
            }
            if(cell==VMetal){
                int supportZ=cz+(shutters==&frontShutters?1:-1);
                if(cx>=0&&cx<c.nx&&cy>=0&&cy<c.ny&&supportZ>=0&&supportZ<c.nz){
                    uint8_t support=c.v[cx+c.nx*(cy+c.ny*supportZ)];
                    if(support&&support!=VGlass)cell=VEmpty;
                }
            }
        }
        auto detail=shutters->finish(true);
        uint32_t offset=uint32_t(model.words.size());
        for(auto brick:detail.primitives){
            if(!brick.material)brick.data=(brick.data&0xff000000u)|((brick.data&0xffffffu)+offset);
            model.primitives.push_back(brick);
        }
        model.words.insert(model.words.end(),detail.words.begin(),detail.words.end());
    }
    return model;
}
VoxelModel trainVoxels(unsigned kind){VoxelModel model;for(auto b:World::trainBoxes(kind))model.primitives.push_back({{b.x0,b.y0,b.z0},0,{b.x1,b.y1,b.z1},b.material==Material::Window?VGlass:b.material==Material::Asphalt?VRubber:b.material==Material::Marking?VLight:b.material==Material::Roof?VMetal:b.material==Material::Curb?VTrim:VPaint});return model;}
VoxelModel carVoxels(){
    Canvas c(32,32,48,-2,-3,.125f);
    c.box(-1.5f,.5f,-3,1.5f,1.75f,3,VPaint);
    c.box(-1.5f,1.5f,-3,1.5f,1.75f,-2.25f,VEmpty);
    c.box(-1.5f,1.5f,2.5f,1.5f,1.75f,3,VEmpty);
    // Hollow cabin with thin, stepped glazing and continuous painted pillars.
    c.box(-1.25f,1,-1.625f,1.25f,1.75f,1.625f,VEmpty);
    for(int step=0;step<6;++step){
        float y=1.75f+step*.125f,front=1.875f-step*.125f;
        c.box(-1.125f,y,-front,1.125f,y+.125f,-front+.125f,VGlass);
        c.box(-1.125f,y,front-.125f,1.125f,y+.125f,front,VGlass);
        for(float x:{-1.25f,1.125f}){
            c.box(x,y,-front+.125f,x+.125f,y+.125f,front-.125f,VGlass);
            c.box(x,y,-front,x+.125f,y+.125f,-front+.125f,VPaint);
            c.box(x,y,front-.125f,x+.125f,y+.125f,front,VPaint);
            c.box(x,y,-.125f,x+.125f,y+.125f,0,VPaint);
        }
    }
    c.box(-1.375f,2.5f,-1.25f,1.375f,2.625f,1.25f,VPaint);
    c.box(-1.25f,1,-1.625f,1.25f,1.125f,1.625f,VRubber);
    for(float x:{-1.f,.25f}){
        c.box(x,1.125f,-.25f,x+.75f,1.375f,.625f,VRubber);
        c.box(x,1.375f,-.375f,x+.75f,1.875f,-.125f,VRubber);
        c.box(x+.125f,1.875f,-.375f,x+.625f,2.125f,-.125f,VRubber);
    }
    c.box(-1.125f,1.125f,-1.5f,1.125f,1.375f,-.75f,VRubber);
    c.box(-1.125f,1.375f,-1.5f,1.125f,1.875f,-1.25f,VRubber);
    c.box(-1.125f,1.5f,1.125f,1.125f,1.75f,1.625f,VRubber);
    // Steering column and a small squared wheel sit behind the dashboard.
    c.box(-.75f,1.375f,.625f,-.625f,1.75f,1.25f,VPolishedMetal);
    c.box(-1.f,1.75f,.625f,-.375f,1.875f,.75f,VRubber);
    c.box(-1.75f,1.75f,1,-1.25f,2,1.5f,VPaint);c.box(1.25f,1.75f,1,1.75f,2,1.5f,VPaint);
    c.box(-1.5f,.5f,-3,1.5f,.75f,-2.75f,VPolishedMetal);c.box(-1.5f,.5f,2.75f,1.5f,.75f,3,VPolishedMetal);
    for(float x:{-1.75f,1.25f})for(float center:{-1.625f,1.625f}){
        for(float z=center-.5f;z<center+.5f;z+=.125f){float dz=z+.0625f-center;
            float span=std::sqrt(std::max(0.f,.25f-dz*dz));
            c.box(x,.5f-span,z,x+.5f,.5f+span,z+.125f,VRubber);
        }
        float outer=x<0?x:x+.375f;
        for(float z=center-.25f;z<center+.25f;z+=.125f){float dz=z+.0625f-center;
            float span=std::sqrt(std::max(0.f,.0625f-dz*dz));
            c.box(outer,.5f-span,z,outer+.125f,.5f+span,z+.125f,VPolishedMetal);
        }
    }
    for(float x:{-1.625f,1.5f})c.box(x,1.5f,-.5f,x+.125f,1.625f,-.125f,VPolishedMetal);
    for(float x:{-1.25f,.75f})c.box(x,1,2.75f,x+.5f,1.5f,3,VLight);
    for(float x:{-1.25f,.75f})c.box(x,1,-3,x+.5f,1.5f,-2.75f,VTailLight);
    c.box(-.5f,.75f,2.75f,.5f,1.25f,3,VRubber);
    c.box(-.5f,.75f,-3,.5f,1,-2.75f,VTrim);
    return c.finish();
}
}
