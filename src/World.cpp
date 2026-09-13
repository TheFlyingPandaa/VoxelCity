#include "World.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <windows.h>

namespace vc {
namespace {
bool roundRule(uint8_t r){return r>=64&&r<=72&&r!=68;}
int roadDirection(uint8_t r){return r&ExtendedDirectionRule?r&15:r&7;}
bool validRule(uint8_t r){return roundRule(r)||(r&ExtendedDirectionRule?(r&15)<=8:((r&7)<=4&&r<=63));}
constexpr int ringNext[9]={3,0,1,6,-1,2,7,8,5};
constexpr int ringMask[9]={6,11,12,13,0,7,3,14,9};
uint32_t ruleStyle(uint8_t r){return uint32_t(r)<<2;}
}
Cell World::roundaboutOrigin(Cell c) const {
    if(!valid(c.x,c.z))return {};
    auto r=roadRule(c);
    if(roundRule(r))return {c.x-(r-64)%3,c.z-(r-64)/3};
    if(roadRule({c.x,c.z-1})==65)return {c.x-1,c.z-1};
    return {};
}
bool World::placeRoundabout(Cell o) {
    if(!valid(o.x,o.z)||!valid(o.x+2,o.z+2))return false;
    for(int z=0;z<3;++z)for(int x=0;x<3;++x)if(roadOccupies({o.x+x,o.z+z})||roundaboutOrigin({o.x+x,o.z+z}).x>=0||parcels_[(o.z+z)*MapSize+o.x+x].kind)return false;
    for(int i=0;i<9;++i)if(i!=4)setRoad(o.x+i%3,o.z+i/3,true);
    for(int i=0;i<9;++i)if(i!=4)setRoadRule({o.x+i%3,o.z+i/3},uint8_t(64+i));
    for(int z=0;z<3;++z)for(int x=0;x<3;++x)clearVegetation({o.x+x,o.z+z});
    return true;
}
bool World::removeRoundabout(Cell c) {
    Cell o=roundaboutOrigin(c);if(o.x<0)return false;
    for(int i=0;i<9;++i)if(i!=4)rules_[(o.z+i/3)*MapSize+o.x+i%3]=0;
    for(int i=0;i<9;++i)if(i!=4)setRoad(o.x+i%3,o.z+i/3,false);
    return true;
}
Cell World::cellAt(float x,float z) {
    if(!std::isfinite(x) || !std::isfinite(z) || x<0 || z<0 || x>=WorldSize || z>=WorldSize) return {};
    return {int(x)/TileSize,int(z)/TileSize};
}
bool World::road(int x, int z) const { return valid(x,z) && roads_[z*MapSize+x] != 0; }
uint8_t World::connections(int x, int z) const {
    if(!road(x,z))return 0;
    if(roadClass({x,z})==RoadClass::Median)return 0;
    auto permits=[&](Cell c,int d){
        uint8_t rule=roadRule(c);int oneWay=roadDirection(rule);
        if(roundRule(rule))return d<4&&(ringMask[rule-64]&(1<<d));
        if(rule&HighwayAccessRule)return d<4;
        if(highway(c)||roadDefinition(c).oneWay)return oneWay==0||oneWay-1==d||oppositeDirection(oneWay-1)==d;
        return true;
    };
    uint8_t mask=0;
    for(int d=0;d<8;++d){Cell a{x,z},b{x+RoadDX[d],z+RoadDZ[d]};
        if(!road(b.x,b.z)||roadClass(b)==RoadClass::Median||!permits(a,d)||!permits(b,oppositeDirection(d)))continue;
        if(highway(a)!=highway(b)&&!((roadRule(a)|roadRule(b))&HighwayAccessRule))continue;
        if(d>=4){
            auto supports=[&](Cell c){auto t=roadType(c);return t==4||(t==2&&(d==5||d==7))||(t==3&&(d==4||d==6));};
            if(!supports(a)&&!supports(b))continue;
            // Crossing a parcel corner needs both shoulders to be clear.
            if(parcels_[z*MapSize+b.x].kind||parcels_[b.z*MapSize+x].kind||roundaboutOrigin({b.x,z}).x>=0||roundaboutOrigin({x,b.z}).x>=0)continue;
        }
        mask|=uint8_t(1<<d);
    }
    return mask;
}
bool World::roadOccupies(Cell c) const {
    if(!valid(c.x,c.z))return false;
    if(road(c.x,c.z)||roundaboutOrigin(c).x>=0)return true;
    if(!diagonalCount_)return false;
    for(int d=0;d<4;++d){Cell n{c.x+RoadDX[d],c.z+RoadDZ[d]};auto m=connections(n.x,n.z);
        for(int k=4;k<8;++k)if(m&(1<<k)){
            Cell b{n.x+RoadDX[k],n.z+RoadDZ[k]};
            if(c==Cell{n.x,b.z}||c==Cell{b.x,n.z})return true;
        }
    }
    return false;
}
std::vector<Cell> World::diagonalLine(Cell a,Cell b){
    std::vector<Cell> cells;if(!valid(a.x,a.z)||!valid(b.x,b.z))return cells;
    int dx=std::abs(b.x-a.x),dz=-std::abs(b.z-a.z),sx=a.x<b.x?1:-1,sz=a.z<b.z?1:-1,error=dx+dz;
    for(;;){cells.push_back(a);if(a==b)break;int twice=2*error;if(twice>=dz){error+=dz;a.x+=sx;}if(twice<=dx){error+=dx;a.z+=sz;}}
    return cells;
}
bool World::setDiagonalRoad(Cell c,bool rising){
    if(!valid(c.x,c.z)||roundaboutOrigin(c).x>=0||highway(c))return false;
    auto old=roadType(c);uint8_t type=rising?3:2;
    if(old>=2&&old!=type)type=4;
    if(old==type)return false;
    setRoad(c.x,c.z,true);roads_[c.z*MapSize+c.x]=type;if(old<2)++diagonalCount_;
    clearConstructionVegetation(c);
    ++revision_;trafficChanges_.push_back(c);
    for(int z=-1;z<=1;++z)for(int x=-1;x<=1;++x)if(valid(c.x+x,c.z+z))dirty_.insert((c.z+z)/ChunkTiles*ChunksAcross+(c.x+x)/ChunkTiles);
    return true;
}
std::vector<Cell> World::roadCrossSection(Cell c) const {
    if(!road(c.x,c.z))return {};
    auto cls=roadClass(c);if(cls!=RoadClass::Avenue&&cls!=RoadClass::Highway4&&cls!=RoadClass::Highway6&&cls!=RoadClass::Median)return {c};
    auto oppositeRule=[](int a,int b){return a>=1&&a<=8&&b==oppositeDirection(a-1)+1;};
    if(cls==RoadClass::Avenue){for(int d=0;d<8;++d){Cell n{c.x+RoadDX[d],c.z+RoadDZ[d]};if(roadClass(n)==cls&&oppositeRule(roadDirection(roadRule(c)),roadDirection(roadRule(n))))return {c,n};}return {c};}
    Cell median=c;if(cls!=RoadClass::Median){median={};for(int d=0;d<4;++d){Cell n{c.x+RoadDX[d],c.z+RoadDZ[d]};if(roadClass(n)==RoadClass::Median){median=n;break;}}}
    if(median.x<0)return {c};
    for(int d=0;d<2;++d){Cell a{median.x+RoadDX[d],median.z+RoadDZ[d]},b{median.x-RoadDX[d],median.z-RoadDZ[d]};auto ca=roadClass(a),cb=roadClass(b);if((ca==RoadClass::Highway4||ca==RoadClass::Highway6)&&(cb==RoadClass::Highway4||cb==RoadClass::Highway6))return {a,median,b};}
    return {c};
}
bool World::placeDiamondInterchange(Cell c){
    if(!valid(c.x-4,c.z-4)||!valid(c.x+4,c.z+4)||roadClass(c)!=RoadClass::Median)return false;
    bool horizontal=roadClass({c.x-1,c.z})==RoadClass::Median&&roadClass({c.x+1,c.z})==RoadClass::Median;
    bool vertical=roadClass({c.x,c.z-1})==RoadClass::Median&&roadClass({c.x,c.z+1})==RoadClass::Median;
    if(horizontal==vertical)return false;
    Cell a=horizontal?Cell{c.x,c.z-1}:Cell{c.x-1,c.z},b=horizontal?Cell{c.x,c.z+1}:Cell{c.x+1,c.z};
    if(!highway(a)||!highway(b))return false;
    for(int i=-4;i<=4;++i){Cell p=horizontal?Cell{c.x,c.z+i}:Cell{c.x+i,c.z};if(p==a||p==b){setRoadRule(p,uint8_t(roadRule(p)|HighwayAccessRule));continue;}if(!road(p.x,p.z))setRoad(p.x,p.z,true);setRoadClass(p,RoadClass::Avenue);}
    for(int dz=-4;dz<=4;++dz)for(int dx=-4;dx<=4;++dx)clearVegetation({c.x+dx,c.z+dz});return true;
}
bool World::setRoadClass(Cell c,RoadClass roadClass){
    if(!valid(c.x,c.z)||!road(c.x,c.z)||roadClass<=RoadClass::None||roadClass>RoadClass::Median)return false;
    auto& value=classes_[c.z*MapSize+c.x];auto next=uint8_t(roadClass);if(value==next)return false;
    bool wasHighway=highway(c);value=next;bool isHighway=highway(c);
    if(wasHighway!=isHighway){if(isHighway)++highwayCount_;else --highwayCount_;}
    ++revision_;++styleRevision_;trafficChanges_.push_back(c);
    for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx)if(valid(c.x+dx,c.z+dz))dirty_.insert((c.z+dz)/ChunkTiles*ChunksAcross+(c.x+dx)/ChunkTiles);
    clearConstructionVegetation(c);return true;
}
bool World::setRoad(int x, int z, bool value) {
    if(roundaboutOrigin({x,z}).x>=0)return false;
    if (!valid(x,z) || road(x,z) == value) return false;
    if(!value&&roadType({x,z})>=2)--diagonalCount_;
    roads_[z*MapSize+x] = uint8_t(value);
    if(value)classes_[z*MapSize+x]=uint8_t(RoadClass::Street);
    else {if(highway({x,z}))--highwayCount_;classes_[z*MapSize+x]=0;rules_[z*MapSize+x]=0;styles_[z*MapSize+x]=0;++styleRevision_;}
    ++revision_; trafficChanges_.push_back({x,z});
    if (value) {++count_;clearConstructionVegetation({x,z});} else --count_;
    // Cardinal neighbors change templates; diagonals can expose curb side faces.
    for (int dz=-1; dz<=1; ++dz) for (int dx=-1; dx<=1; ++dx)
        if (valid(x+dx,z+dz)) dirty_.insert((z+dz)/ChunkTiles*ChunksAcross+(x+dx)/ChunkTiles);
    return true;
}
void World::stroke(Cell from, Cell to, bool value) {
    if (!valid(from.x,from.z) || !valid(to.x,to.z)) return;
    int dx=std::abs(to.x-from.x), dz=std::abs(to.z-from.z);
    int sx=to.x>from.x?1:-1, sz=to.z>from.z?1:-1;
    int ix=0, iz=0;
    setRoad(from.x,from.z,value);
    // Visit one axis per step, including ties, to preserve four-way connectivity.
    while (ix<dx || iz<dz) {
        if (ix<dx && (iz==dz || (1+2*ix)*dz <= (1+2*iz)*dx)) { from.x+=sx; ++ix; }
        else { from.z+=sz; ++iz; }
        setRoad(from.x,from.z,value);
    }
}
Column World::column(int x, int z) const {
    if(x<0||z<0||x>=WorldSize||z>=WorldSize)return {0,Material::Grass};
    Cell cell{x/TileSize,z/TileSize};Cell origin=roundaboutOrigin(cell);
    if(roadClass(cell)==RoadClass::Median)return {0,Material::Grass};
    if(origin.x>=0){
        float px=x+.5f-(origin.x*TileSize+24),pz=z+.5f-(origin.z*TileSize+24);
        float ring=std::abs(std::hypot(px,pz)-16.f)-5.f;
        float arms=std::min(std::max(std::abs(px)-6.f,14.f-std::abs(pz)),std::max(std::abs(pz)-6.f,14.f-std::abs(px)));
        float surface=std::min(ring,arms);
        if(surface<=0)return {0,Material::Asphalt};
        if(surface<=1)return {1,Material::Curb};
        if(surface<=2)return {1,Material::Sidewalk};
        return {0,Material::Grass};
    }
    if(diagonalCount_){
        float nearest=1e9f,along=0;bool diagonal=false;
        for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx){Cell n{cell.x+dx,cell.z+dz};
            if(!road(n.x,n.z))continue;auto mask=connections(n.x,n.z);
            if(!(mask&240)&&roadType(n)<2)continue;
            diagonal=true;
            // Include cardinal arms at a diagonal junction so all lane mouths meet.
            if(!mask)mask=roadType(n)==3?80:160;
            for(int d=0;d<8;++d)if(mask&(1<<d)){
                float ax=n.x*16.f+8,az=n.z*16.f+8,bx=RoadDX[d]*16.f,bz=RoadDZ[d]*16.f;
                float t=std::clamp(((x+.5f-ax)*bx+(z+.5f-az)*bz)/(bx*bx+bz*bz),0.f,1.f);
                float distance=std::hypot(x+.5f-ax-t*bx,z+.5f-az-t*bz);
                if(distance<nearest){nearest=distance;along=t*std::hypot(bx,bz);}
            }
        }
        if(diagonal&&nearest<=8&&!(roadType(cell)==1&&(connections(cell.x,cell.z)&240)==0)){
            if(nearest>7)return {1,Material::Sidewalk};
            if(nearest>6)return {1,Material::Curb};
            return {0,nearest<.55f&&std::fmod(along,8.f)<4?Material::Marking:Material::Asphalt};
        }
        if(roadType(cell)>=2)return {0,Material::Grass};
    }
    if(!road(cell.x,cell.z))return {0,Material::Grass};
    int u=x%TileSize, v=z%TileSize;
    uint8_t m=connections(x/TileSize,z/TileSize);
    if(highway({x/TileSize,z/TileSize})){if(x/TileSize==0)m|=West;if(x/TileSize==MapSize-1)m|=East;}
    int edge=TileSize;
    if (!(m&North)) edge=std::min(edge,v);
    if (!(m&East)) edge=std::min(edge,TileSize-1-u);
    if (!(m&South)) edge=std::min(edge,TileSize-1-v);
    if (!(m&West)) edge=std::min(edge,u);
    if(highway({x/TileSize,z/TileSize})) {
        if(edge==0)return {1,Material::Curb};
        if(edge==1)return {0,Material::Marking};
        if(edge==2)return {0,Material::HighwayShoulder};
        int cross=(m&(North|South))?u:v;unsigned lanes=roadDefinition(cell).lanesPerDirection;
        bool laneMark=lanes>1&&cross>2&&cross<TileSize-3&&((cross-2)*int(lanes))%(TileSize-4)<int(lanes);
        return {0,laneMark?Material::Marking:Material::Asphalt};
    }
    if (edge<SidewalkWidth) return {1,Material::Sidewalk};
    if (edge==SidewalkWidth) return {1,Material::Curb};
    constexpr int center=TileSize/2-1;
    auto cls=roadClass(cell);unsigned perDirection=roadDefinition(cell).lanesPerDirection;
    int degree=((m&North)!=0)+((m&East)!=0)+((m&South)!=0)+((m&West)!=0);
    bool marking=false;
    if (degree<3) {
        // Center dashes follow each arm; the central square joins bends.
        marking=((m&North) && u==center && v<=center && v%8<4) ||
                ((m&South) && u==center && v>=center && v%8<4) ||
                ((m&West) && v==center && u<=center && u%8<4) ||
                ((m&East) && v==center && u>=center && u%8<4);
    }
    if((cls==RoadClass::Avenue||cls==RoadClass::OneWay)&&degree<3){
        int cross=(m&(North|South))?u:v;
        if(perDirection>1&&cross>SidewalkWidth+1&&cross<TileSize-SidewalkWidth-2)
            marking|=cross==TileSize/4||cross==3*TileSize/4;
    }
    return {0,marking?Material::Marking:Material::Asphalt};
}
bool World::chunkEmpty(int chunk) const {
    int tx=chunk%ChunksAcross*ChunkTiles, tz=chunk/ChunksAcross*ChunkTiles;
    for (int z=tz; z<tz+ChunkTiles; ++z) for (int x=tx; x<tx+ChunkTiles; ++x)
        if (roadOccupies({x,z})||hasRail({x,z})) return false;
    return true;
}
Mesh World::mesh(int chunk) const {
    Mesh out;
    int ox=chunk%ChunksAcross*ChunkSize, oz=chunk/ChunksAcross*ChunkSize;
    auto quad=[&](std::array<std::array<float,3>,4> p, float nx,float ny,float nz, Material mat) {
        uint32_t b=uint32_t(out.vertices.size());
        for (auto a:p) out.vertices.push_back({a[0],a[1],a[2],nx,ny,nz,uint32_t(mat)});
        out.indices.insert(out.indices.end(),{b,b+1,b+2,b,b+2,b+3});
    };
    std::vector<Column> columns(ChunkSize*ChunkSize);
    std::vector<uint8_t> used(ChunkSize*ChunkSize,0);
    for (int z=0;z<ChunkSize;++z) for (int x=0;x<ChunkSize;++x) columns[z*ChunkSize+x]=column(ox+x,oz+z);
    auto same=[&](int x,int z,Column c) {
        return !used[z*ChunkSize+x] && columns[z*ChunkSize+x].height==c.height && columns[z*ChunkSize+x].material==c.material;
    };
    for (int z=0;z<ChunkSize;++z) for (int x=0;x<ChunkSize;++x) {
        if (used[z*ChunkSize+x]) continue;
        Column c=columns[z*ChunkSize+x];
        int w=1,h=1;
        while (x+w<ChunkSize && same(x+w,z,c)) ++w;
        while (z+h<ChunkSize) {
            bool fits=true;
            for (int i=0;i<w;++i) if (!same(x+i,z+h,c)) {fits=false;break;}
            if (!fits) break;
            ++h;
        }
        for (int j=0;j<h;++j) for (int i=0;i<w;++i) used[(z+j)*ChunkSize+x+i]=true;
        float a=float(x), b=float(z), y=c.height*TerrainStepHeight, r=float(x+w), d=float(z+h);
        quad({{{a,y,b},{a,y,d},{r,y,d},{r,y,b}}},0,1,0,c.material);
    }
    // Merge exposed curb walls along each row/column.
    for (int axis=0;axis<4;++axis) for (int line=0;line<ChunkSize;++line) {
        int run=-1;
        for (int t=0;t<=ChunkSize;++t) {
            int x=axis<2?line:t, z=axis<2?t:line;
            bool exposed=false;
            if (t<ChunkSize) {
                int nx=x+(axis==0?-1:axis==1?1:0), nz=z+(axis==2?-1:axis==3?1:0);
                exposed=columns[z*ChunkSize+x].height==1 && column(ox+nx,oz+nz).height==0;
            }
            if (exposed && run<0) run=t;
            if (!exposed && run>=0) {
                float a=float(run),b=float(t), l=float(line);
                if(axis==0) quad({{{l,0,a},{l,0,b},{l,TerrainStepHeight,b},{l,TerrainStepHeight,a}}},-1,0,0,Material::Curb);
                if(axis==1) quad({{{l+1,0,b},{l+1,0,a},{l+1,TerrainStepHeight,a},{l+1,TerrainStepHeight,b}}},1,0,0,Material::Curb);
                if(axis==2) quad({{{b,0,l},{a,0,l},{a,TerrainStepHeight,l},{b,TerrainStepHeight,l}}},0,0,-1,Material::Curb);
                if(axis==3) quad({{{a,0,l+1},{b,0,l+1},{b,TerrainStepHeight,l+1},{a,TerrainStepHeight,l+1}}},0,0,1,Material::Curb);
                run=-1;
            }
        }
    }
    for(auto b:railBoxes(chunk)) {
        float x=b.x0,z=b.z0,r=b.x1,d=b.z1,y=b.y0,h=b.y1;auto m=b.material;
        quad({{{x,h,z},{x,h,d},{r,h,d},{r,h,z}}},0,1,0,m);
        quad({{{x,y,z},{r,y,z},{r,y,d},{x,y,d}}},0,-1,0,m);
        quad({{{x,y,z},{x,y,d},{x,h,d},{x,h,z}}},-1,0,0,m);
        quad({{{r,y,d},{r,y,z},{r,h,z},{r,h,d}}},1,0,0,m);
        quad({{{r,y,z},{x,y,z},{x,h,z},{r,h,z}}},0,0,-1,m);
        quad({{{x,y,d},{r,y,d},{r,h,d},{x,h,d}}},0,0,1,m);
    }
    return out;
}
Mesh World::parcelMesh(ParcelVisual p,bool simple) {
    Mesh out;
    auto quad=[&](std::array<std::array<float,3>,4> points,float nx,float ny,float nz,Material mat){uint32_t base=uint32_t(out.vertices.size());for(auto a:points)out.vertices.push_back({a[0],a[1],a[2],nx,ny,nz,uint32_t(mat)});out.indices.insert(out.indices.end(),{base,base+1,base+2,base,base+2,base+3});};
    auto box=[&](float x,float z,float w,float d,float bottom,float top,Material mat) {
        quad({{{x,top,z},{x,top,z+d},{x+w,top,z+d},{x+w,top,z}}},0,1,0,mat);
        quad({{{x,bottom,z},{x,bottom,z+d},{x,top,z+d},{x,top,z}}},-1,0,0,mat);
        quad({{{x+w,bottom,z+d},{x+w,bottom,z},{x+w,top,z},{x+w,top,z+d}}},1,0,0,mat);
        quad({{{x+w,bottom,z},{x,bottom,z},{x,top,z},{x+w,top,z}}},0,0,-1,mat);
        quad({{{x,bottom,z+d},{x+w,bottom,z+d},{x+w,top,z+d},{x,top,z+d}}},0,0,1,mat);
    };
    {
        if(p.kind>=13){for(auto b:railFacilityBoxes(p))box(b.x0,b.z0,b.x1-b.x0,b.z1-b.z0,b.y0,b.y1,b.material);return out;}
        if(!p.kind)return out;
        Material mat=p.kind<=3?Material(uint8_t(Material::Residential)+p.kind-1):p.kind<=6?Material::Utility:Material::Service;
        if(p.tint)mat=p.tint==1?Material::Bad:p.tint==2?Material::Warning:Material::Good;
        float px=0,pz=0;
        box(px+1,pz+1,14,14,0,0.15f,mat);
        if(!p.level)return out;
        bool dense=p.variant>=4;uint8_t art=p.variant&3;float h=dense?14.f+p.level*10.f+art*2.f:4.f+p.level*4.f+art*2.f;
        box(px+3,pz+3,10,10,0.15f,h,mat);
        box(px+2,pz+2,12,12,h,h+1,Material::Roof);
        if(p.variant%2)box(px+5,pz+5,5,5,h+1,h+3,mat);
        for(float y=3;!simple&&y<h-1;y+=4) {
            box(px+4,pz+2.9f,2,0.15f,y,y+1,Material::Window);
            box(px+9,pz+2.9f,2,0.15f,y,y+1,Material::Window);
            box(px+12.9f,pz+5,0.15f,3,y,y+1,Material::Window);
        }
    }
    return out;
}
std::set<int> World::takeDirty() { std::set<int> result; result.swap(dirty_); return result; }
void World::dirtyAll() { for(int i=0;i<ChunkCount;++i) dirty_.insert(i); }
static uint32_t checksum(const std::array<uint8_t,MapSize*MapSize>& data) {
    uint32_t h=2166136261u; for(auto b:data) { h^=b; h*=16777619u; } return h;
}
void World::save(const std::filesystem::path& path) const {
    auto temporary=path; temporary += L".tmp";
    try {
        std::ofstream f(temporary,std::ios::binary|std::ios::trunc);
        const std::array<uint32_t,6> header={0x59544356,2,MapSize,MapSize,TileSize,checksum(roads_)};
        f.write(reinterpret_cast<const char*>(header.data()),sizeof(header));
        f.write(reinterpret_cast<const char*>(roads_.data()),roads_.size());
        f.flush(); if(!f) throw std::runtime_error("Could not write map file."); f.close();
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Could not replace map file. Check permissions.");
    } catch (...) { std::error_code ec; std::filesystem::remove(temporary,ec); throw; }
}
void World::load(const std::filesystem::path& path) {
    std::ifstream f(path,std::ios::binary);
    std::array<uint32_t,6> header{};
    f.read(reinterpret_cast<char*>(header.data()),sizeof(header));
    const bool supportedScale=(header[1]==1 && header[4]==8) || (header[1]==2 && header[4]==TileSize);
    if(!f || header[0]!=0x59544356 || !supportedScale || header[2]!=MapSize || header[3]!=MapSize)
        throw std::runtime_error("Unsupported or invalid VoxelCity map.");
    auto candidate=std::vector<uint8_t>(roads_.size());
    f.read(reinterpret_cast<char*>(candidate.data()),candidate.size());
    if(!f || f.peek()!=std::char_traits<char>::eof() || std::any_of(candidate.begin(),candidate.end(),[](auto v){return v>4;}))
        throw std::runtime_error("Truncated or invalid map data.");
    uint32_t h=2166136261u; for(auto b:candidate) { h^=b; h*=16777619u; }
    if(h!=header[5]) throw std::runtime_error("Map checksum does not match.");
    vegetationEnabled_=false;std::fill(clearedTrees_.begin(),clearedTrees_.end(),uint8_t(0));vegetationChunks_.fill(++vegetationRevision_);
    clearRails();std::copy(candidate.begin(),candidate.end(),roads_.begin());
    highwayCount_=0;std::fill(classes_.begin(),classes_.end(),uint8_t(0));for(size_t i=0;i<candidate.size();++i)if(candidate[i])classes_[i]=uint8_t(RoadClass::Street);std::fill(rules_.begin(),rules_.end(),uint8_t(0)); std::fill(styles_.begin(),styles_.end(),0u);++styleRevision_; clearParcelVisuals();
    ++revision_; ++replacement_; trafficChanges_.clear();
    diagonalCount_=std::count_if(roads_.begin(),roads_.end(),[](auto r){return r>=2;});count_=std::count_if(roads_.begin(),roads_.end(),[](auto r){return r!=0;}); dirtyAll();
}
void World::generateScenario(int scenario) {
    vegetationEnabled_=false;std::fill(clearedTrees_.begin(),clearedTrees_.end(),uint8_t(0));vegetationChunks_.fill(++vegetationRevision_);
    ++revision_; ++replacement_; trafficChanges_.clear();
    clearRails();roads_.fill(0); highwayCount_=0;diagonalCount_=0;std::fill(classes_.begin(),classes_.end(),uint8_t(0));std::fill(rules_.begin(),rules_.end(),uint8_t(0)); std::fill(styles_.begin(),styles_.end(),0u);++styleRevision_; clearParcelVisuals(); count_=0;
    for(int z=0;z<MapSize;++z) for(int x=0;x<MapSize;++x) {
        bool value=scenario==2 || (scenario==1 && (x%16==0 || z%16==0));
        roads_[z*MapSize+x]=uint8_t(value);classes_[z*MapSize+x]=value?uint8_t(RoadClass::Street):0; count_+=value;
    }
    if(scenario==5){
        for(auto c:diagonalLine({254,254},{264,264}))setDiagonalRoad(c,false);
        for(auto c:diagonalLine({254,264},{264,254}))setDiagonalRoad(c,true);
        stroke({259,259},{259,266},true);
    }
    if(scenario==4) {
        placeRoundabout({258,258});
        stroke({259,254},{259,257},true);stroke({261,259},{264,259},true);
        stroke({259,261},{259,264},true);stroke({254,259},{257,259},true);
    }
    if(scenario==3) {
        stroke({253,253},{265,253},true);stroke({253,253},{253,264},true);
        stroke({253,264},{258,264},true);stroke({258,264},{258,259},true);
        stroke({258,259},{270,259},true);stroke({263,253},{263,263},true);
        stroke({267,259},{267,255},true);
    }
    dirtyAll();
}
uint8_t World::roadRule(Cell c) const {return valid(c.x,c.z)?rules_[c.z*MapSize+c.x]:0;}
void World::setRoadRule(Cell c,uint8_t rule) {
    if(!road(c.x,c.z)||!validRule(rule))return;
    auto& r=rules_[c.z*MapSize+c.x];if(r==rule)return;bool wasHighway=highway(c);
    if(((r|rule)&HighwayRule)||roundRule(r)||roundRule(rule))for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx)if(valid(c.x+dx,c.z+dz))dirty_.insert((c.z+dz)/ChunkTiles*ChunksAcross+(c.x+dx)/ChunkTiles);
    r=rule;bool isHighway=highway(c);if(wasHighway!=isHighway){if(isHighway)++highwayCount_;else --highwayCount_;}
    styles_[c.z*MapSize+c.x]=(styles_[c.z*MapSize+c.x]&3)|ruleStyle(rule);++styleRevision_;++revision_;trafficChanges_.push_back(c);
}
bool World::canTravel(Cell a,Cell b) const {
    if(!road(a.x,a.z)||!road(b.x,b.z)||std::max(std::abs(a.x-b.x),std::abs(a.z-b.z))!=1)return false;
    int direction=0;for(;direction<8;++direction)if(b==Cell{a.x+RoadDX[direction],a.z+RoadDZ[direction]})break;int d=direction+1;
    int ra=roadDirection(roadRule(a)),rb=roadDirection(roadRule(b));
    if(roundRule(roadRule(a))||roundRule(roadRule(b))){
        if(!(connections(a.x,a.z)&(1<<(d-1))))return false;
        if(roundRule(roadRule(a))&&roundRule(roadRule(b))&&roundaboutOrigin(a)==roundaboutOrigin(b)) {
            int next=ringNext[roadRule(a)-64];Cell o=roundaboutOrigin(a);
            return b==Cell{o.x+next%3,o.z+next/3};
        }
        return (roundRule(roadRule(a))||ra==0||ra==d)&&(roundRule(roadRule(b))||rb==0||rb==d);
    }
    return (connections(a.x,a.z)&(1<<(d-1)))&&((roadRule(a)&HighwayAccessRule)||ra==0||ra==d)&&((roadRule(b)&HighwayAccessRule)||rb==0||rb==d);
}
void World::setParcelVisual(Cell c,ParcelVisual visual) {
    if(!valid(c.x,c.z))return;auto& p=parcels_[c.z*MapSize+c.x];if(p==visual)return;p=visual;++parcelRevision_;if(visual.kind)clearVegetation(c);
}
void World::clearParcelVisuals(){std::fill(parcels_.begin(),parcels_.end(),ParcelVisual{});++parcelRevision_;dirtyAll();}
void World::writeRoads(std::ostream& out) const {
    out.write(reinterpret_cast<const char*>(roads_.data()),roads_.size());
    out.write(reinterpret_cast<const char*>(rules_.data()),rules_.size());
    out.write(reinterpret_cast<const char*>(classes_.data()),classes_.size());
}
double World::roadUpkeep() const {
    double total=0;
    for(size_t i=0;i<classes_.size();++i)if(roads_[i]&&!(rules_[i]&HighwayRule)){auto cls=RoadClass(classes_[i]);double divisor=(cls==RoadClass::Avenue||cls==RoadClass::Highway4||cls==RoadClass::Highway6)?2:1;total+=RoadDefinitions[classes_[i]].upkeep/divisor;}
    return total;
}
void World::readRoads(std::istream& in,bool hasClasses) {
    in.read(reinterpret_cast<char*>(roads_.data()),roads_.size());
    in.read(reinterpret_cast<char*>(rules_.data()),rules_.size());
    if(hasClasses)in.read(reinterpret_cast<char*>(classes_.data()),classes_.size());
    else {std::fill(classes_.begin(),classes_.end(),uint8_t(0));for(size_t i=0;i<roads_.size();++i)if(roads_[i])classes_[i]=uint8_t((rules_[i]&HighwayRule)?RoadClass::LegacyHighway:RoadClass::Street);}
    if(!in)throw std::runtime_error("Truncated road data");
    for(size_t i=0;i<roads_.size();++i)if(roads_[i]>4 || classes_[i]>uint8_t(RoadClass::Median) || (bool(roads_[i])!=(classes_[i]!=0)) || !validRule(rules_[i]) || (!roads_[i]&&rules_[i]))throw std::runtime_error("Invalid road data");
    for(int t=0;t<MapSize*MapSize;++t)if(roundRule(rules_[t])){
        Cell o=roundaboutOrigin({t%MapSize,t/MapSize});
        if(!valid(o.x,o.z)||!valid(o.x+2,o.z+2)||road(o.x+1,o.z+1))throw std::runtime_error("Invalid roundabout footprint");
        for(int i=0;i<9;++i)if(i!=4&&roadRule({o.x+i%3,o.z+i/3})!=64+i)throw std::runtime_error("Incomplete roundabout");
    }
    highwayCount_=0;for(int t=0;t<MapSize*MapSize;++t)if(highway({t%MapSize,t/MapSize}))++highwayCount_;
    for(size_t i=0;i<rules_.size();++i)styles_[i]=ruleStyle(rules_[i]);++styleRevision_;
    diagonalCount_=std::count_if(roads_.begin(),roads_.end(),[](auto r){return r>=2;});count_=std::count_if(roads_.begin(),roads_.end(),[](auto r){return r!=0;});++revision_;++replacement_;trafficChanges_.clear();clearParcelVisuals();
}

void World::setTileTint(Cell c,uint8_t tint){if(!valid(c.x,c.z))return;auto& value=styles_[c.z*MapSize+c.x];uint32_t next=(value&~3u)|(tint&3u);if(next!=value){value=next;++styleRevision_;}}
void World::clearTints(){for(auto& value:styles_)value&=~3u;++styleRevision_;}

void World::generateRegionalHighway() {
    // Two separated, opposing carriageways with a flat central access junction.
    stroke({0,HighwayWestRow},{MapSize-1,HighwayWestRow},true);
    stroke({0,HighwayEastRow},{MapSize-1,HighwayEastRow},true);
    for(int x=0;x<MapSize;++x){setRoadClass({x,HighwayWestRow},RoadClass::Highway6);setRoadClass({x,HighwayEastRow},RoadClass::Highway6);setRoadRule({x,HighwayWestRow},HighwayRule|4);setRoadRule({x,HighwayEastRow},HighwayRule|2);}
    stroke({HighwayJunctionX,HighwayWestRow},{HighwayJunctionX,216},true);
    setRoadRule({HighwayJunctionX,HighwayWestRow},HighwayRule|HighwayAccessRule);
    setRoadRule({HighwayJunctionX,HighwayEastRow},HighwayRule|HighwayAccessRule);
}

}

namespace vc {
std::set<int> World::changedVisualChunks(const World* previous) const {
    std::set<int> result;
    if(!previous||replacement_!=previous->replacement_) {for(int c=0;c<ChunkCount;++c)result.insert(c);return result;}
    if(revision_==previous->revision_&&railRevision_==previous->railRevision_&&closedCrossings_==previous->closedCrossings_)return result;
    for(int z=0;z<MapSize;++z)for(int x=0;x<MapSize;++x) {
        int i=z*MapSize+x;if(roads_[i]==previous->roads_[i]&&rules_[i]==previous->rules_[i]&&classes_[i]==previous->classes_[i]&&rails_[i].links==previous->rails_[i].links&&rails_[i].flags==previous->rails_[i].flags&&rails_[i].height==previous->rails_[i].height&&closedCrossings_[i]==previous->closedCrossings_[i])continue;
        for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx)if(valid(x+dx,z+dz))result.insert(((z+dz)/ChunkTiles)*ChunksAcross+(x+dx)/ChunkTiles);
    }
    return result;
}
}
