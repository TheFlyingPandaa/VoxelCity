#include "World.h"
#include "Voxels.h"
#include "City.h"
#include "LegacyCityFixture.h"
#include "NetworkWorker.h"
#include "Binary.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#define CHECK(x) do {if(!(x))throw std::runtime_error("Failed: " #x);}while(0)
using namespace vc;
static std::vector<TreeInstance> all(const World& w){std::vector<TreeInstance> r;for(int c=0;c<ChunkCount;++c){auto v=w.trees(c);r.insert(r.end(),v.begin(),v.end());}return r;}
static uint32_t checksum(const std::string& s){uint32_t h=2166136261u;for(unsigned char c:s){h^=c;h*=16777619u;}return h;}
static Cell cell(TreeInstance t){return {int(t.tile%MapSize),int(t.tile/MapSize)};}
static bool present(const World& w,TreeInstance t){auto v=w.trees(cell(t).z/ChunkTiles*ChunksAcross+cell(t).x/ChunkTiles);return std::find(v.begin(),v.end(),t)!=v.end();}
int main()try{
    NetworkWorker::blocking=true;
    auto w=std::make_unique<World>();CHECK(all(*w).empty());w->initializeVegetation();
    auto original=all(*w);CHECK(original.size()>5000&&original.size()<35000);
    auto identical=std::make_unique<World>();identical->initializeVegetation();CHECK(all(*identical)==original);
    std::array<size_t,TreeVariantCount> variants{};size_t treelessChunks=0;
    // Low scrub may occupy open chunks; tall woodland still leaves clearings.
    for(int c=0;c<ChunkCount;++c){auto plants=w->trees(c);
        if(std::none_of(plants.begin(),plants.end(),[](auto p){return p.variant<4;}))++treelessChunks;
    }
    CHECK(treelessChunks>0);
    for(auto t:original){++variants[t.variant];auto c=cell(t);CHECK(World::valid(c.x,c.z));
        for(auto b:World::treeBoxes(t.variant)){CHECK(t.x+b.x0>=c.x*TileSize);CHECK(t.x+b.x1<=(c.x+1)*TileSize);CHECK(t.z+b.z0>=c.z*TileSize);CHECK(t.z+b.z1<=(c.z+1)*TileSize);}
    }
    for(auto n:variants)CHECK(n>100);
    CHECK(original.size()-variants[4]<20000);CHECK(variants[4]>1000);
    for(uint8_t variant=0;variant<TreeVariantCount;++variant){auto model=treeVoxels(variant);CHECK(!model.primitives.empty());CHECK(!World::treeMesh(variant).indices.empty());
        // Even padded brick bounds stay in the parcel with maximum placement jitter.
        for(auto p:model.primitives){CHECK(p.lo[0]>=2&&p.hi[0]<=14);CHECK(p.lo[2]>=2&&p.hi[2]<=14);}
        if(variant==4){for(auto p:model.primitives)CHECK(p.hi[1]<=3);continue;}
        float o[]={8,20,8},d[]={0,-1,0};VoxelRayHit closest;closest.t=100;
        for(auto p:model.primitives){VoxelRayHit hit;if(intersectVoxel(p,model.words,o,d,.003f,closest.t,hit))closest=hit;}
        CHECK(closest.material==VLeaf);CHECK(closest.t>5&&closest.t<15);
        // Probe the trunk from inside the surrounding shrub layer.
        float trunkOrigin[]={6.5f,1,8},trunkDirection[]={1,0,0};closest.t=100;
        for(auto p:model.primitives){VoxelRayHit hit;if(intersectVoxel(p,model.words,trunkOrigin,trunkDirection,.003f,closest.t,hit))closest=hit;}
        CHECK(closest.material==VBark);
    }
    CitySimulation city;TrafficSimulation traffic({0,42,20000,64,true});std::string message;
    auto a=original[0],b=original[1];auto snapshot=std::make_unique<World>(*w);
    city.treasury=0;CHECK(!city.buildRoad(*w,cell(a),cell(a),false,message));CHECK(present(*w,a));
    CHECK(!city.place(*w,cell(a),BuildingKind::Power,message));CHECK(present(*w,a));
    CHECK(w->vegetationRevision()==snapshot->vegetationRevision());
    city.treasury=100000;CHECK(city.buildRoad(*w,cell(a),cell(a),false,message));CHECK(!present(*w,a));
    CHECK(city.bulldoze(*w,cell(a),message));CHECK(!present(*w,a));
    CHECK(city.place(*w,cell(b),BuildingKind::Residential,message));CHECK(!present(*w,b));
    CHECK(city.bulldoze(*w,cell(b),message));CHECK(!present(*w,b));
    CHECK(present(*snapshot,a)&&present(*snapshot,b));
    CHECK(w->vegetationRevision()>snapshot->vegetationRevision());
    auto c=cell(a);int chunk=c.z/ChunkTiles*ChunksAcross+c.x/ChunkTiles;
    CHECK(w->vegetationChunkRevision(chunk)!=snapshot->vegetationChunkRevision(chunk));
    // Consecutive edits on opposite sides of a chunk boundary survive skipped snapshots.
    w->clearVegetation({15,15});w->clearVegetation({16,16});
    CHECK(w->vegetationChunkRevision(0)!=snapshot->vegetationChunkRevision(0));
    CHECK(w->vegetationChunkRevision(ChunksAcross+1)!=snapshot->vegetationChunkRevision(ChunksAcross+1));
    // Clear reservations as well as road centerlines, including after removal.
    w->generateRegionalHighway();w->placeRoundabout({100,100});
    for(auto p:World::diagonalLine({200,200},{210,210}))w->setDiagonalRoad(p,false);
    for(auto t:all(*w))CHECK(!w->roadOccupies(cell(t)));
    std::stringstream vegetation;w->writeVegetation(vegetation);auto encoded=vegetation.str();
    auto bit=[&](Cell p){int tile=p.z*MapSize+p.x;return (uint8_t(encoded[8+tile/8])&(1u<<(tile%8)))!=0;};
    CHECK(bit({101,101}));CHECK(bit({205,206}));CHECK(bit({10,HighwayWestRow}));
    w->removeRoundabout({101,101});for(auto p:World::diagonalLine({200,200},{210,210}))w->setRoad(p.x,p.z,false);
    std::stringstream after;w->writeVegetation(after);CHECK(after.str()==encoded);
    // Vegetation parsing is transactional even before the outer city candidate is published.
    auto savedRevision=w->vegetationRevision();auto before=all(*w);
    for(auto invalid:{std::string(),std::string(8,'\0'),encoded.substr(0,encoded.size()-1)}){
        bool rejected=false;try{std::stringstream in(invalid);w->readVegetation(in);}catch(const std::exception&){rejected=true;}
        CHECK(rejected);CHECK(w->vegetationRevision()==savedRevision);CHECK(all(*w)==before);
    }
    auto folder=std::filesystem::temp_directory_path()/("VegetationTests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(folder);
    struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove_all(p,ec);}}cleanup{folder};
    auto occupied=original[2];CHECK(city.place(*w,cell(occupied),BuildingKind::Residential,message));
    city.save(*w,traffic,folder/"trees.vcity");auto restored=std::make_unique<World>();CitySimulation loaded;TrafficSimulation sim({0,42,20000,64,true});
    loaded.load(*restored,sim,folder/"trees.vcity");CHECK(all(*restored)==all(*w));CHECK(!present(*restored,a));
    // Make genuine old-layout payloads by removing the new trailing vegetation block.
    std::ifstream file(folder/"trees.vcity",std::ios::binary);std::string bytes{std::istreambuf_iterator<char>(file),{}};
    std::string priorPayload=preRailPayload(bytes,*w,city);std::string payload=priorPayload.substr(0,priorPayload.size()-encoded.size());
    // A valid outer checksum must not hide a malformed vegetation generation version.
    {
        std::string bad=priorPayload;bad[bad.size()-encoded.size()]=char(99);
        std::ofstream out(folder/"bad.vcity",std::ios::binary);
        for(uint32_t v:{0x59544356u,5u,uint32_t(MapSize),uint32_t(MapSize),uint32_t(TileSize),checksum(bad)})binary::write(out,v);
        binary::write(out,uint64_t(bad.size()));out.write(bad.data(),bad.size());out.close();
        auto prior=all(*restored);auto money=loaded.treasury;bool rejected=false;
        try{loaded.load(*restored,sim,folder/"bad.vcity");}catch(const std::exception&){rejected=true;}
        CHECK(rejected);CHECK(all(*restored)==prior);CHECK(loaded.treasury==money);
    }
    for(uint32_t version:{3u,4u}){
        std::ofstream old(folder/"old.vcity",std::ios::binary);
        for(uint32_t value:{0x59544356u,version,uint32_t(MapSize),uint32_t(MapSize),uint32_t(TileSize),checksum(payload)})binary::write(old,value);
        binary::write(old,uint64_t(payload.size()));old.write(payload.data(),payload.size());old.close();
        loaded.load(*restored,sim,folder/"old.vcity");CHECK(restored->vegetationEnabled());CHECK(!all(*restored).empty());
        for(auto t:all(*restored))CHECK(!restored->roadOccupies(cell(t)));
        CHECK(!present(*restored,occupied));CHECK(loaded.bulldoze(*restored,cell(occupied),message));CHECK(!present(*restored,occupied));
    }
    for(uint32_t version:{1u,2u}){
        w->save(folder/"road.vcity");if(version==1){std::fstream old(folder/"road.vcity",std::ios::binary|std::ios::in|std::ios::out);old.seekp(4);binary::write(old,version);old.seekp(16);binary::write(old,uint32_t(8));}
        loaded.load(*restored,sim,folder/"road.vcity");CHECK(restored->vegetationEnabled());CHECK(!all(*restored).empty());
    }
    city.newCity(*w,traffic);CHECK(present(*w,a));w->generateScenario(0);CHECK(all(*w).empty());
    std::cout<<"Vegetation tests passed: "<<original.size()<<" deterministic trees\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
