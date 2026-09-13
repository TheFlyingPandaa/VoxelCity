#include "RoadNetwork.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#define CHECK(x) do {if(!(x))throw std::runtime_error("Failed: " #x);}while(0)
using namespace vc;
static void equivalent(const World& world,const RoadNetwork& network){
    std::set<NetworkId> nodes,segments,lanes;
    for(auto n:network.nodes())CHECK(nodes.insert(n.id).second);
    for(auto s:network.segments()){CHECK(segments.insert(s.id).second);CHECK(nodes.contains(s.from));CHECK(nodes.contains(s.to));CHECK(s.lengthMeters>0);}
    for(auto l:network.lanes()){CHECK(lanes.insert(l.id).second);CHECK(segments.contains(l.segment));CHECK(nodes.contains(l.from));CHECK(nodes.contains(l.to));CHECK(l.speedMetersPerSecond>0);}
    for(int z=0;z<MapSize;++z)for(int x=0;x<MapSize;++x)for(int d=0;d<8;++d){
        Cell a{x,z},b{x+RoadDX[d],z+RoadDZ[d]};
        CHECK(world.canTravel(a,b)==network.canTravel(a,b));
    }
    CHECK(!network.canTravel({-1,0},{0,0}));CHECK(!network.canTravel({10,10},{10,10}));
}
int main()try{
    auto world=std::make_unique<World>();
    world->stroke({10,10},{14,10},true);
    RoadNetwork straight(*world);CHECK(straight.nodes().size()==5);CHECK(straight.segments().size()==4);CHECK(straight.lanes().size()==8);
    for(auto s:straight.segments())CHECK(s.lengthMeters==TileSize/WorldUnitsPerMeter);
    auto original=straight.segments().front().id;
    world->stroke({10,10},{10,14},true);
    world->setRoadRule({12,10},2);
    CHECK(world->placeRoundabout({30,30}));
    for(auto c:World::diagonalLine({60,60},{65,65}))CHECK(world->setDiagonalRoad(c,false));
    world->generateRegionalHighway();
    RoadNetwork mixed(*world);equivalent(*world,mixed);
    CHECK(mixed.segments().front().id==original);
    CHECK(!mixed.canTravel({12,10},{11,10}));CHECK(mixed.canTravel({12,10},{13,10}));
    // Snapshot ownership: subsequent edits cannot mutate a published graph.
    world->setRoad(13,10,false);CHECK(mixed.canTravel({12,10},{13,10}));
    RoadNetwork edited(*world);CHECK(!edited.canTravel({12,10},{13,10}));equivalent(*world,edited);
    std::stringstream bytes(std::ios::in|std::ios::out|std::ios::binary);world->writeRoads(bytes);
    auto restored=std::make_unique<World>();restored->readRoads(bytes,true);
    RoadNetwork loaded(*restored);equivalent(*restored,loaded);
    CHECK(loaded.nodes().size()==edited.nodes().size());CHECK(loaded.segments().size()==edited.segments().size());CHECK(loaded.lanes().size()==edited.lanes().size());
    for(size_t i=0;i<loaded.segments().size();++i)CHECK(loaded.segments()[i].id==edited.segments()[i].id);
    for(size_t i=0;i<loaded.lanes().size();++i)CHECK(loaded.lanes()[i].id==edited.lanes()[i].id);
    std::cout<<"Road network adapter equivalence passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
