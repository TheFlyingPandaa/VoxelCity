#include "World.h"
#include "Binary.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace vc {
void World::setRail(Cell c,RailTile v){
    if(!valid(c.x,c.z))return;
    rails_[c.z*MapSize+c.x]=v;++railRevision_;
    if(v.flags)clearConstructionVegetation(c);
    for(int z=-1;z<=1;++z)for(int x=-1;x<=1;++x)if(valid(c.x+x,c.z+z))dirty_.insert((c.z+z)/ChunkTiles*ChunksAcross+(c.x+x)/ChunkTiles);
}
void World::clearRails(){closedCrossingTiles_.clear();std::fill(rails_.begin(),rails_.end(),RailTile{});std::fill(closedCrossings_.begin(),closedCrossings_.end(),uint8_t(0));++railRevision_;}
void World::generateRegionalRailway(){for(int x=0;x<MapSize;++x)setRail({x,RegionalRailRow},{uint8_t((x?West:0)|(x+1<MapSize?East:0)),3,0,-1});}
double World::railUpkeep()const{double n=0;for(int i=0;i<int(rails_.size());++i){auto r=rails_[i];if((r.flags&1)&&!(r.flags&2))n+=.02;if(r.bridge==i)n+=10;}return n;}
void World::closeCrossings(const std::vector<Cell>& cells){
    std::vector<int> next;for(auto c:cells)if(valid(c.x,c.z)&&hasRail(c)&&road(c.x,c.z)&&rail(c).height<1)next.push_back(c.z*MapSize+c.x);
    std::sort(next.begin(),next.end());next.erase(std::unique(next.begin(),next.end()),next.end());if(next==closedCrossingTiles_)return;
    auto dirty=[&](int i){dirty_.insert((i/MapSize)/ChunkTiles*ChunksAcross+(i%MapSize)/ChunkTiles);};
    for(int i:closedCrossingTiles_){closedCrossings_[i]=0;dirty(i);}for(int i:next){closedCrossings_[i]=1;dirty(i);}closedCrossingTiles_=std::move(next);++styleRevision_;
}
void World::writeRails(std::ostream& out)const{using namespace binary;for(auto r:rails_){write(out,r.links);write(out,r.flags);write(out,r.height);write(out,r.bridge);}}
void World::readRails(std::istream& in){using namespace binary;for(auto& r:rails_){r.links=read<uint8_t>(in);r.flags=read<uint8_t>(in);r.height=read<float>(in);r.bridge=read<int>(in);
    if(r.links>15||(r.flags!=0&&r.flags!=1&&r.flags!=3)||!std::isfinite(r.height)||r.height<0||r.height>12||r.bridge< -1||r.bridge>=MapSize*MapSize||(!r.flags&&(r.links||r.height||r.bridge!=-1)))throw std::runtime_error("Invalid railway tile");}
    for(int i=0;i<int(rails_.size());++i)for(int d=0;d<4;++d)if(rails_[i].links&(1<<d)){Cell n{i%MapSize+RoadDX[d],i/MapSize+RoadDZ[d]};if(!hasRail(n)||!(rail(n).links&(1<<oppositeDirection(d))))throw std::runtime_error("Broken railway connection");}
    for(int i=0;i<int(rails_.size());++i){auto r=rails_[i];if(r.bridge>=0&&(rails_[r.bridge].bridge!=r.bridge||!rails_[r.bridge].flags))throw std::runtime_error("Invalid rail overpass identity");}
    ++railRevision_;dirtyAll();
}
std::vector<RailBox> World::railBoxes(int chunk)const{
    std::vector<RailBox> out;int originX=chunk%ChunksAcross*ChunkTiles,originZ=chunk/ChunksAcross*ChunkTiles;
    for(int z=0;z<ChunkTiles;++z)for(int x=0;x<ChunkTiles;++x){Cell c{originX+x,originZ+z};auto r=rail(c);if(!r.flags)continue;
        auto box=[&](float a,float y,float b,float d,float h,float e,Material m){out.push_back({float(x*16)+a,y,float(z*16)+b,float(x*16)+d,h,float(z*16)+e,m});};
        float y=r.height;bool horizontal=(r.links&(East|West))!=0,vertical=(r.links&(North|South))!=0;
        if(!r.links)horizontal=true;
        if(y>0){if(!road(c.x,c.z)&&y>3)for(float a:{2.f,13.f})box(a,0,7,a+1,y,9,Material::Curb);}
        // Each connected arm has two running tracks; short sleepers make the rail silhouette readable at city scale.
        auto arm=[&](int d){bool ns=d%2==0;float begin=(d==0||d==3)?0.f:8.f,end=(d==0||d==3)?8.f:16.f;
            Cell neighbor{c.x+RoadDX[d],c.z+RoadDZ[d]};float slope=hasRail(neighbor)?rail(neighbor).height-y:0;auto height=[&](float t){return y+slope*std::abs(t-8)/16;};
            for(float t=begin;t<end;t+=.5f){float h=height(t+.25f);if(r.bridge>=0){if(ns)box(1,h-.65f,t,15,h,t+.5f,Material::Curb);else box(t,h-.65f,1,t+.5f,h,15,Material::Curb);}
                if(!road(c.x,c.z)||y>0){if(ns)box(1,h,t,15,h+.18f,t+.5f,Material::HighwayShoulder);else box(t,h,1,t+.5f,h+.18f,15,Material::HighwayShoulder);}
            }
            for(float t=begin+.6f;t<end;t+=2){float h=height(t);if(ns)box(1.5f,h+.18f,t,14.5f,h+.35f,t+.55f,Material::Roof);else box(t,h+.18f,1.5f,t+.55f,h+.35f,14.5f,Material::Roof);}
            float step=slope==0?8:.5f;for(float a=begin;a<end;a+=step)for(float t:{3.f,5.f,11.f,13.f}){float h=height(a+step*.5f);if(ns)box(t,h+.35f,a,t+.22f,h+.55f,a+step,Material::Marking);else box(a,h+.35f,t,a+step,h+.55f,t+.22f,Material::Marking);}
        };
        int degree=0;for(int d=0;d<4;++d)degree+=(r.links>>d)&1;
        if(!(degree==2&&horizontal&&vertical))for(int d=0;d<4;++d)if(r.links&(1<<d))arm(d);if(!r.links){arm(1);arm(3);}
        // Quarter-circle running rails also form the diverging paths of automatic switches.
        for(int a=0;a<4;++a)for(int b=a+1;b<4;++b)if((r.links&(1<<a))&&(r.links&(1<<b))&&a%2!=b%2){
            float ix=float(-RoadDX[a]),iz=float(-RoadDZ[a]),ox=float(RoadDX[b]),oz=float(RoadDZ[b]);float cx=8-ix*8+ox*8,cz=8-iz*8+oz*8;
            for(int segment=0;segment<24;++segment){float t=segment*1.57079632679f/24,T=(segment+1)*1.57079632679f/24;
                for(float radius:{3.f,5.f,11.f,13.f}){float x0=cx-ox*radius*std::cos(t)+ix*radius*std::sin(t),z0=cz-oz*radius*std::cos(t)+iz*radius*std::sin(t),x1=cx-ox*radius*std::cos(T)+ix*radius*std::sin(T),z1=cz-oz*radius*std::cos(T)+iz*radius*std::sin(T);box(std::min(x0,x1)-.1f,y+.35f,std::min(z0,z1)-.1f,std::max(x0,x1)+.1f,y+.55f,std::max(z0,z1)+.1f,Material::Marking);}
                if(segment%3==0){float vx=-ox*std::cos(t)+ix*std::sin(t),vz=-oz*std::cos(t)+iz*std::sin(t);for(float radius=1.5f;radius<14.5f;radius+=.4f){float px=cx+vx*radius,pz=cz+vz*radius;box(px-.25f,y+.18f,pz-.25f,px+.25f,y+.35f,pz+.25f,Material::Roof);}}
            }
        }
        if(road(c.x,c.z)&&y<1){bool closed=crossingClosed(c);for(float side:{1.f,14.f}){
            if(horizontal&&!vertical){box(1,0,side,1.6f,3,side+.5f,Material::Marking);box(1,2.7f,side,closed?14.f:1.5f,closed?3.f:9.f,side+.35f,Material::Warning);}
            else {box(side,0,1,side+.5f,3,1.6f,Material::Marking);box(side,2.7f,1,side+.35f,closed?3.f:9.f,closed?14.f:1.5f,Material::Warning);}
        }}
    }return out;
}
std::vector<RailBox> World::railFacilityBoxes(ParcelVisual p){
    std::vector<RailBox> boxes;auto box=[&](float x,float y,float z,float X,float Y,float Z,Material m){for(int i=0;i<p.variant%4;++i){float old=x;x=16-Z;Z=X;X=16-z;z=old;}boxes.push_back({x,y,z,X,Y,Z,m});};
    box(0,0,0,16,.6f,16,Material::Curb);box(0,.6f,14,16,.7f,15,Material::Warning);
    if(p.kind==13){ // A glazed concourse and platform canopy.
        box(1,.6f,2,15,5,8,Material::Service);box(2,1.2f,7.9f,14,4.5f,8.1f,Material::Window);
        for(float x:{2.f,13.f})box(x,.6f,12,x+.4f,5,12.4f,Material::Curb);
        box(0,5,1,16,5.5f,15,Material::Roof);box(5,2,11,11,2.5f,12,Material::Roof);
        box(6,5.5f,3,10,7.5f,4,Material::Service);box(7,6,2.9f,9,7,3,Material::Marking);
    }else if(p.kind==14){
        for(float z:{2.f,8.f}){box(1,.6f,z,11,4,z+4,Material::Industrial);for(float x=2;x<11;x+=2)box(x,.7f,z-.1f,x+.15f,4,z,Material::Roof);}
        box(13,.6f,1,14,10,15,Material::Warning);box(1,9,1,14,10,2,Material::Warning);
    }else {
        box(1,.6f,1,15,7,12,Material::Utility);box(0,7,0,16,8,13,Material::Roof);
        box(3,.6f,11.9f,13,5.5f,12.1f,Material::Window);box(2,5.5f,12,14,6,12.3f,Material::Warning);
    }return boxes;
}
std::vector<RailBox> World::trainBoxes(unsigned kind){
    std::vector<RailBox> b;auto box=[&](float x,float y,float z,float X,float Y,float Z,Material m){b.push_back({x,y,z,X,Y,Z,m});};
    box(-1.65f,.65f,-5.5f,1.65f,1.3f,5.5f,Material::Roof);
    for(float x:{-1.35f,.9f})for(float z:{-4.f,3.f})box(x,.55f,z,x+.45f,1.55f,z+1,Material::Asphalt);
    box(-.3f,.75f,-6,.3f,1.1f,6,Material::Roof);
    if(kind==1){box(-1.5f,1.3f,-5,1.5f,3.1f,5,Material::Service);box(-1.5f,3.1f,1.5f,1.5f,4.5f,4.6f,Material::Service);box(-1.3f,3.3f,4.6f,1.3f,4.2f,4.7f,Material::Window);box(-.6f,3.1f,-2,.6f,4.1f,-1,Material::Roof);box(-1.2f,2.1f,5,1.2f,2.5f,5.1f,Material::Marking);}
    else if(kind==2){box(-1.5f,1.3f,-5,1.5f,4.2f,5,Material::Service);for(float z=-4;z<4;z+=1.8f){box(-1.51f,2.4f,z,-1.49f,3.6f,z+1.2f,Material::Window);box(1.49f,2.4f,z,1.51f,3.6f,z+1.2f,Material::Window);}box(-1.4f,4.2f,-5,1.4f,4.4f,5,Material::Curb);}
    else {box(-1.5f,1.3f,-5,1.5f,4.5f,5,Material::Industrial);for(float z=-4.8f;z<5;z+=.8f){box(-1.6f,1.3f,z,-1.5f,4.5f,z+.1f,Material::Roof);box(1.5f,1.3f,z,1.6f,4.5f,z+.1f,Material::Roof);}}
    return b;
}

}
