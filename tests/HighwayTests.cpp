#include "NetworkWorker.h"
#include "City.h"
#include "LegacyCityFixture.h"
#include "Binary.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do {if(!(x))throw std::runtime_error("Failed: " #x);}while(0)
using namespace vc;
int main()try {
    vc::NetworkWorker::blocking=true;
    auto w=std::make_unique<World>();CitySimulation city;TrafficSimulation traffic({0,42,20000,64,true});std::string message;
    city.newCity(*w,traffic);
    CHECK(w->highwayCount()==MapSize*2);CHECK(city.treasury==100000);CHECK(city.buildings().empty());CHECK(city.regionalTrips()>0);
    CHECK(w->canTravel({10,HighwayEastRow},{11,HighwayEastRow}));CHECK(!w->canTravel({11,HighwayEastRow},{10,HighwayEastRow}));
    CHECK(w->canTravel({11,HighwayWestRow},{10,HighwayWestRow}));CHECK(!w->canTravel({10,HighwayWestRow},{11,HighwayWestRow}));
    CHECK(w->column(10*TileSize+8,HighwayEastRow*TileSize).material==Material::Curb);
    CHECK(w->column(10*TileSize+8,HighwayEastRow*TileSize+2).material==Material::HighwayShoulder);
    auto money=city.treasury;auto roads=w->roadCount();
    CHECK(!city.bulldoze(*w,{10,HighwayEastRow},message));CHECK(!city.roadRule(*w,{10,HighwayEastRow},4,message));
    CHECK(!city.buildRoad(*w,{10,HighwayEastRow-1},{10,HighwayEastRow+1},true,message));CHECK(city.treasury==money);CHECK(w->roadCount()==roads);
    // Side streets cannot connect through a carriageway's shoulder.
    CHECK(city.buildRoad(*w,{100,HighwayEastRow-1},{110,HighwayEastRow-1},false,message));
    CHECK(!(w->connections(100,HighwayEastRow-1)&South));CHECK(!(w->connections(100,HighwayEastRow)&North));
    CHECK(city.place(*w,{100,HighwayEastRow-2},BuildingKind::Residential,message));
    CHECK(city.place(*w,{110,HighwayEastRow+1},BuildingKind::Residential,message));city.refresh(*w);
    CHECK(!city.at({100,HighwayEastRow-2})->external);CHECK(city.at({110,HighwayEastRow+1})->component==-1);
    // The authored access road reaches both neighboring cities.
    CHECK(city.buildRoad(*w,{HighwayJunctionX,216},{HighwayJunctionX,223},false,message));
    CHECK(city.place(*w,{HighwayJunctionX-1,222},BuildingKind::Residential,message));city.refresh(*w);CHECK(city.at({HighwayJunctionX-1,222})->external);
    float topSpeed=0;for(int i=0;i<300;++i){city.tick(*w,traffic);for(const auto& c:traffic.debugSnapshot())topSpeed=std::max(topSpeed,c.speed);}
    CHECK(topSpeed>25);traffic.validate(*w);city.validate(*w);
    auto save=std::filesystem::temp_directory_path()/("VoxelCity-highway-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".vcity");
    struct Cleanup {std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove(p,ec);}}cleanup{save};
    city.save(*w,traffic,save);auto restoredWorld=std::make_unique<World>();CitySimulation restored;TrafficSimulation restoredTraffic({0,42,20000,64,true});restored.load(*restoredWorld,restoredTraffic,save);
    CHECK(restoredWorld->highwayCount()==1024);CHECK(restored.regionalTrips()==city.regionalTrips());
    for(int i=0;i<120;++i){city.tick(*w,traffic);restored.tick(*restoredWorld,restoredTraffic);}
    auto a=traffic.debugSnapshot(),b=restoredTraffic.debugSnapshot();CHECK(a.size()==b.size());
    for(size_t i=0;i<a.size();++i){CHECK(a[i].id==b[i].id);CHECK(a[i].x==b[i].x);CHECK(a[i].z==b[i].z);CHECK(a[i].speed==b[i].speed);}
    // Both directional trade endpoints and the cross-carriageway access are routable.
    TrafficSimulation routes({0,99,20000,64,true});
    for(auto r:{TripRequest{1,1,{0,HighwayEastRow},{HighwayJunctionX,223},1},TripRequest{2,1,{MapSize-1,HighwayWestRow},{HighwayJunctionX,223},1},TripRequest{3,1,{HighwayJunctionX,223},{0,HighwayWestRow},1},TripRequest{4,1,{HighwayJunctionX,223},{MapSize-1,HighwayEastRow},1}})CHECK(routes.requestTrip(r));
    int completed=0;for(int i=0;i<12000;++i){routes.tick(*w);for(const auto& e:routes.takeEvents()){CHECK(e.completed);++completed;}if(completed==4)break;}
    CHECK(completed==4);routes.validate(*w);
    // Genuine gameplay imports reach a roadside shop through the correct entry.
    CHECK(city.place(*w,{HighwayJunctionX+1,220},BuildingKind::Power,message));CHECK(city.place(*w,{HighwayJunctionX+1,221},BuildingKind::Water,message));CHECK(city.place(*w,{HighwayJunctionX+1,222},BuildingKind::Sewage,message));CHECK(city.place(*w,{HighwayJunctionX-1,221},BuildingKind::Commercial,message));
    for(int i=0;i<15000&&city.stats().deliveries==0;++i)city.tick(*w,traffic);CHECK(city.stats().deliveries>0);
    // Old v3 city payloads remain readable without injecting a highway into saved layouts.
    auto legacyWorld=std::make_unique<World>();CitySimulation legacy;TrafficSimulation legacyTraffic({0,42,20000,64,true});legacy.save(*legacyWorld,legacyTraffic,save);
    {std::ifstream file(save,std::ios::binary);std::string bytes{std::istreambuf_iterator<char>(file),{}};file.close();
     // v3 predates the trailing vegetation block; update length and checksum too.
     std::string payload=preRailPayload(bytes,*legacyWorld,legacy);payload.resize(payload.size()-8-MapSize*MapSize/8);
     uint32_t checksum=2166136261u;for(unsigned char c:payload){checksum^=c;checksum*=16777619u;}
     std::ofstream old(save,std::ios::binary|std::ios::trunc);
     for(uint32_t v:{0x59544356u,3u,uint32_t(MapSize),uint32_t(MapSize),uint32_t(TileSize),checksum})binary::write(old,v);
     binary::write(old,uint64_t(payload.size()));old.write(payload.data(),payload.size());}
    restored.load(*restoredWorld,restoredTraffic,save);CHECK(restoredWorld->highwayCount()==0);CHECK(restoredWorld->roadCount()==0);
    std::cout<<"Highway tests passed: protected geometry, limited access, speed, four trade routes, imports, active-trip persistence and v3 compatibility\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
