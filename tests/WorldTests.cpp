#include "World.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <chrono>
#define CHECK(x) do { if(!(x)) throw std::runtime_error("Failed: " #x); } while(0)
using namespace vc;
int main() try {
    auto w=std::make_unique<World>();
    for(int mask=0;mask<16;++mask) {
        w->setRoad(10,10,true);
        w->setRoad(10,9,mask&North); w->setRoad(11,10,mask&East);
        w->setRoad(10,11,mask&South); w->setRoad(9,10,mask&West);
        CHECK(w->connections(10,10)==mask);
        constexpr int origin=10*TileSize,mid=origin+TileSize/2,last=origin+TileSize-1;
        CHECK(w->column(mid,origin).height==((mask&North)?0:1));
        CHECK(w->column(last,mid).height==((mask&East)?0:1));
        CHECK(w->column(mid,last).height==((mask&South)?0:1));
        CHECK(w->column(origin,mid).height==((mask&West)?0:1));
        if(!(mask&North)) {
            CHECK(w->column(mid,origin).material==Material::Sidewalk);
            CHECK(w->column(mid,origin+1).material==Material::Curb);
            CHECK(w->column(mid,origin+2).height==0);
        }
        CHECK(!w->mesh(0).indices.empty());
        for(const auto& vertex:w->mesh(0).vertices)CHECK(vertex.y==0||vertex.y==.25f);
    }
    w=std::make_unique<World>();
    CHECK(TileSize==2*GridSize);
    for(int z=0;z<2;++z) for(int x=0;x<2;++x) {
        const Cell expected{10,10};
        CHECK(World::cellAt(float(10*TileSize+x*GridSize+4),float(10*TileSize+z*GridSize+4))==expected);
    }
    CHECK(World::cellAt(-0.1f,0).x==-1); CHECK(World::cellAt(float(WorldSize),0).x==-1);
    CHECK(w->mesh(0).vertices.size()==4);
    CHECK(w->mesh(0).indices.size()==6);
    w->stroke({0,0},{511,511},true);
    CHECK(w->roadCount()==1023);
    for(int i=0;i<511;++i) { CHECK(w->road(i,i)); CHECK(w->road(i+1,i)); }
    w->stroke({0,0},{511,511},false); CHECK(w->roadCount()==0);
    CHECK(!w->setRoad(-1,0,true)); CHECK(!w->setRoad(512,511,true));
    w->takeDirty(); w->setRoad(15,15,true);
    auto dirty=w->takeDirty(); CHECK(dirty.contains(0)); CHECK(dirty.contains(1)); CHECK(dirty.contains(32)); CHECK(dirty.contains(33));
    w->setRoad(16,15,true); CHECK(w->connections(15,15)==East); CHECK(w->connections(16,15)==West);
    CHECK(w->column(16*TileSize-1,15*TileSize+TileSize/2).height==0);
    CHECK(w->column(16*TileSize,15*TileSize+TileSize/2).height==0);
    // Sidewalks continue across chunk seams without blocking connected asphalt.
    CHECK(w->column(16*TileSize-1,15*TileSize).material==Material::Sidewalk);
    CHECK(w->column(16*TileSize,15*TileSize).material==Material::Sidewalk);
    auto path=std::filesystem::temp_directory_path()/("voxelcity-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".vcity");
    w->save(path); auto loaded=std::make_unique<World>(); loaded->load(path);
    CHECK(loaded->roadCount()==2); CHECK(loaded->connections(15,15)==East);
    // Legacy maps keep their tile coordinates and acquire the new footprint.
    {std::fstream legacy(path,std::ios::binary|std::ios::in|std::ios::out);
     uint32_t version=1,oldSize=8;legacy.seekp(4);legacy.write(reinterpret_cast<char*>(&version),4);
     legacy.seekp(16);legacy.write(reinterpret_cast<char*>(&oldSize),4);}
    loaded->load(path); CHECK(loaded->roadCount()==2); CHECK(loaded->connections(15,15)==East);
    CHECK(loaded->column(15*TileSize,15*TileSize).material==Material::Sidewalk);
    loaded->save(path);
    {std::ifstream current(path,std::ios::binary);uint32_t version=0;current.seekg(4);current.read(reinterpret_cast<char*>(&version),4);CHECK(version==2);}
    {std::ofstream bad(path,std::ios::binary|std::ios::trunc); bad<<"bad";}
    bool rejected=false; try {loaded->load(path);}catch(...){rejected=true;}
    CHECK(rejected); CHECK(loaded->roadCount()==2);
    w->save(path);
    {std::fstream bad(path,std::ios::binary|std::ios::in|std::ios::out); bad.seekp(4); uint32_t version=99; bad.write(reinterpret_cast<char*>(&version),4);}
    rejected=false; try {loaded->load(path);}catch(...){rejected=true;} CHECK(rejected); CHECK(loaded->roadCount()==2);
    w->save(path);
    {std::fstream bad(path,std::ios::binary|std::ios::in|std::ios::out); bad.seekp(30); bad.put(1);}
    rejected=false; try {loaded->load(path);}catch(...){rejected=true;} CHECK(rejected); CHECK(loaded->roadCount()==2);
    std::filesystem::remove(path);
    std::cout<<"World tests passed: masks, curbs, meshing, strokes, bounds, chunk seams, persistence.\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
