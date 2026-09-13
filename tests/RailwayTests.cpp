#include "City.h"
#include "Voxels.h"
#include "LegacyCityFixture.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("Railway check failed at ")+std::to_string(__LINE__)+": " #x);}while(0)
using namespace vc;
int main()try{
    NetworkWorker::blocking=true;std::string message;
    auto w=std::make_unique<World>();CitySimulation city;TrafficSimulation traffic({0,42,20000,64,true});city.newCity(*w,traffic);
    CHECK(city.treasury==100000);CHECK(city.railway.trains().size()==6);
    for(int x=0;x<MapSize;++x){CHECK(w->hasRail({x,RegionalRailRow}));CHECK(w->rail({x,RegionalRailRow}).flags&2);}
    CHECK(!city.bulldoze(*w,{50,RegionalRailRow},message));CHECK(!city.place(*w,{50,RegionalRailRow},BuildingKind::Residential,message));
    CHECK(city.buildRail(*w,{50,RegionalRailRow},{50,190},false,message));CHECK(w->rail({50,RegionalRailRow}).links&North);
    CHECK(city.buildRail(*w,{50,191},{50,190},true,message));CHECK(!(w->rail({50,RegionalRailRow}).links&North));CHECK(w->rail({50,RegionalRailRow}).links==(East|West));
    auto before=city.treasury;auto revision=w->railRevision();CHECK(!city.buildRail(*w,{1,1},{3,3},false,message));CHECK(city.treasury==before&&w->railRevision()==revision);
    city.treasury=1;CHECK(!city.buildRail(*w,{1,1},{1,3},false,message));CHECK(!w->hasRail({1,1}));city.treasury=before;
    CHECK(city.placeRailFacility(*w,{100,191},BuildingKind::PassengerStation,0,message));CHECK(city.treasury==before-4000);
    auto stationId=city.at({100,191})->id;CHECK(city.at({103,192})->id==stationId);CHECK(!city.placeRailFacility(*w,{103,193},BuildingKind::CargoTerminal,2,message));
    CHECK(!city.buildRail(*w,{101,192},{101,195},false,message));CHECK(city.buildRail(*w,{103,192},{105,192},false,message));
    for(int r=0;r<4;++r){Cell c{150+r*10,150};CHECK(city.placeRailFacility(*w,c,BuildingKind::TrainDepot,r,message));auto b=*city.at(c);for(auto p:CitySimulation::occupiedFootprint(b))CHECK(city.at(p)->id==b.id);CHECK(city.bulldoze(*w,CitySimulation::facilityFootprint(b,true)[2],message));for(auto p:CitySimulation::facilityFootprint(b))CHECK(!city.at(p));}
    CHECK(city.buildRoad(*w,{200,180},{200,200},false,message));CHECK(w->hasRail({200,192}));CHECK(w->road(200,192));
    CHECK(!city.buildRoad(*w,{190,192},{210,192},false,message));
    before=city.treasury;CHECK(city.buildRailOverpass(*w,{100,206},message));CHECK(city.treasury==before-3000);CHECK(w->rail({100,204}).height==8);CHECK(w->rail({100,208}).height==8);CHECK(w->canTravel({100,204},{99,204}));
    CHECK(!city.buildRailOverpass(*w,{HighwayJunctionX,206},message));CHECK(city.bulldoze(*w,{100,206},message));for(int z=200;z<=212;++z)CHECK(!w->hasRail({100,z}));CHECK(w->highwayCount()==1024);
    auto snapshot=std::make_unique<World>(*w);CHECK(city.buildRail(*w,{15,30},{17,30},false,message));auto chunks=w->changedVisualChunks(snapshot.get());CHECK(chunks.contains(32)&&chunks.contains(33));CHECK(!w->chunkEmpty(32));CHECK(!w->railBoxes(32).empty());CHECK(!terrainVoxels(*w,32).primitives.empty());
    for(unsigned i=1;i<=3;++i)CHECK(!trainVoxels(i).primitives.empty());for(uint8_t i=13;i<=15;++i)for(uint8_t r=0;r<4;++r){CHECK(!buildingVoxels({i,1,r,0}).primitives.empty());CHECK(!World::parcelMesh({i,1,r,0}).indices.empty());}
    auto file=std::filesystem::temp_directory_path()/("railway-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".vcity");
    struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove(p,ec);}}cleanup{file};
    city.save(*w,traffic,file);auto restored=std::make_unique<World>();CitySimulation loaded;TrafficSimulation other({0,42,20000,64,true});loaded.load(*restored,other,file);CHECK(loaded.at({103,192})->id==stationId);CHECK(loaded.railway.trains().size()==city.railway.trains().size());CHECK(restored->rail({50,192}).links==w->rail({50,192}).links);
    // A checksum-valid but invalid rail payload must not replace the live city.
    {std::ifstream saved(file,std::ios::binary);std::string bytes{std::istreambuf_iterator<char>(saved),{}};saved.close();std::ostringstream tail(std::ios::binary);w->writeRails(tail);city.railway.save(tail);size_t railOffset=bytes.size()-tail.str().size()-city.buildings().size()*4-4;bytes[railOffset+1]=char(255);
        uint32_t sum=2166136261u;for(size_t i=32;i<bytes.size();++i){sum^=uint8_t(bytes[i]);sum*=16777619u;}std::memcpy(bytes.data()+20,&sum,4);std::ofstream bad(file,std::ios::binary|std::ios::trunc);bad.write(bytes.data(),bytes.size());bad.close();double money=loaded.treasury;bool rejected=false;try{loaded.load(*restored,other,file);}catch(const std::exception&){rejected=true;}CHECK(rejected);CHECK(loaded.treasury==money);CHECK(loaded.at({103,192})->id==stationId);
    }
    // Pausing freezes train positions; speed changes use the existing simulation clock.
    loaded.paused=true;float position=loaded.railway.trains()[0].distance;loaded.update(*restored,other,.25);CHECK(loaded.railway.trains()[0].distance==position);loaded.paused=false;loaded.speed=3;loaded.update(*restored,other,.1);CHECK(loaded.railway.trains()[0].distance>position);
    // Local automatic shuttles require a reachable depot and carry transfers over a station tree.
    auto localWorld=std::make_unique<World>();CitySimulation builder;builder.sandbox=true;CHECK(builder.buildRail(*localWorld,{10,20},{40,20},false,message));RailSimulation rail;TrafficSimulation empty({0,42,20000,64,true});
    rail.setStations({{1,{11,20},1,true},{2,{25,20},1,true},{3,{39,20},1,true}});rail.tick(*localWorld,empty);CHECK(!rail.local(1,3));
    rail.setStations({{1,{11,20},1,true},{2,{25,20},1,true},{3,{39,20},1,true},{4,{10,20},3,true}});rail.tick(*localWorld,empty);CHECK(rail.local(1,3));CHECK(rail.requestPassenger(101,1,3));
    bool delivered=false;for(int i=0;i<9000&&!delivered;++i){rail.tick(*localWorld,empty);for(auto e:rail.takeEvents())if(e.journey==101){CHECK(e.completed);delivered=true;}}CHECK(delivered);CHECK(rail.stats().passengers==1);
    // Save an in-flight local service and resume it without losing passengers or dispatch state.
    CHECK(rail.requestPassenger(102,3,1));for(int i=0;i<50;++i)rail.tick(*localWorld,empty);std::ostringstream out(std::ios::binary);rail.save(out);RailSimulation reloaded;std::istringstream in(out.str(),std::ios::binary);reloaded.load(in,*localWorld);CHECK(reloaded.trains().size()==rail.trains().size());CHECK(reloaded.journeys().size()==rail.journeys().size());
    CHECK(builder.buildRail(*localWorld,{20,20},{20,20},true,message));rail.tick(*localWorld,empty);for(auto& t:rail.trains())for(auto c:t.route)CHECK(localWorld->hasRail(c));
    // Regional station service actually stops and produces a cargo delivery event.
    auto regional=std::make_unique<World>();CHECK(builder.buildRail(*regional,{0,10},{30,10},false,message));RailSimulation freight;freight.setStations({{9,{12,10},2,true}});bool cargo=false,exported=false;
    for(int i=0;i<4000;++i){freight.tick(*regional,empty);for(auto e:freight.takeEvents()){cargo|=e.station==9&&e.kind==2;exported|=e.kind==4;}}CHECK(cargo&&exported);CHECK(freight.regional(9));
    // Gates hold traffic outside the crossing and release after the final carriage.
    CHECK(builder.buildRoad(*regional,{6,5},{6,15},false,message));RailSimulation gateRail;gateRail.setStations({{9,{12,10},2,true}});bool closed=false,reopened=false;
    for(int i=0;i<3500;++i){gateRail.tick(*regional,empty);if(regional->crossingClosed({6,10}))closed=true;else if(closed)reopened=true;}CHECK(closed&&reopened);

    // Rail-only outside access supports genuine city growth and truck/train freight transfers.
    auto economy=std::make_unique<World>();CitySimulation town;TrafficSimulation townTraffic({0,42,20000,64,true});town.sandbox=true;
    CHECK(town.buildRail(*economy,{0,30},{100,30},false,message));CHECK(town.buildRoad(*economy,{8,28},{90,28},false,message));
    CHECK(town.placeRailFacility(*economy,{10,29},BuildingKind::PassengerStation,0,message));CHECK(town.placeRailFacility(*economy,{80,29},BuildingKind::PassengerStation,0,message));CHECK(town.placeRailFacility(*economy,{18,29},BuildingKind::CargoTerminal,0,message));CHECK(town.placeRailFacility(*economy,{14,29},BuildingKind::TrainDepot,0,message));
    CHECK(town.place(*economy,{10,27},BuildingKind::Power,message));CHECK(town.place(*economy,{11,27},BuildingKind::Water,message));CHECK(town.place(*economy,{12,27},BuildingKind::Sewage,message));
    CHECK(town.place(*economy,{9,27},BuildingKind::Residential,message));CHECK(town.place(*economy,{80,27},BuildingKind::Industrial,message));CHECK(town.place(*economy,{82,27},BuildingKind::Commercial,message));
    bool commuters=false,goods=false;for(int i=0;i<30000;++i){town.tick(*economy,townTraffic);commuters|=!town.railway.journeys().empty();goods|=town.at({82,27})->inventory>0;}
    CHECK(town.stats().population>0);CHECK(town.at({9,27})->external);CHECK(goods);CHECK(town.railway.stats().cargo>=40);CHECK(commuters);town.validate(*economy);townTraffic.validate(*economy);
    // A platform touching an outside endpoint can turn around and leave without a degenerate route.
    auto edge=std::make_unique<World>();CHECK(builder.buildRail(*edge,{0,50},{10,50},false,message));RailSimulation edgeRail;edgeRail.setStations({{1,{1,50},2,true,{0,50},{3,50}}});bool departed=false;
    for(int i=0;i<2400;++i){edgeRail.tick(*edge,empty);for(auto e:edgeRail.takeEvents())departed|=e.kind==4;for(auto& train:edgeRail.trains())CHECK(train.route.size()>=2);}CHECK(departed);
    // Closing a railway crossing prevents new road vehicles from entering it.
    auto crossing=std::make_unique<World>();CHECK(builder.buildRail(*crossing,{0,40},{30,40},false,message));CHECK(builder.buildRoad(*crossing,{6,35},{6,45},false,message));TrafficSimulation cars({0,42,20000,64,true});CHECK(cars.requestTrip({1,1,{6,35},{6,45},0}));crossing->closeCrossings({{6,40}});
    for(int i=0;i<600;++i){cars.tick(*crossing);for(auto car:cars.debugSnapshot())CHECK(car.tile.z<40);}CHECK(cars.stats().active==1);crossing->closeCrossings({});for(int i=0;i<600;++i)cars.tick(*crossing);CHECK(cars.stats().completed==1);
    // A full network queue leaves construction and train simulation responsive; later results discard stale topology.
    NetworkWorker::blocking=false;auto release=std::make_shared<std::promise<void>>();auto barrier=release->get_future().share();auto entered=std::make_shared<std::promise<void>>();auto started=entered->get_future();auto blocked=NetworkWorker::instance().submit([barrier,entered]{entered->set_value();barrier.wait();return true;});started.wait();std::vector<std::future<bool>> queued;for(int i=0;i<64;++i)queued.push_back(NetworkWorker::instance().submit([]{return true;}));
    RailSimulation pending;pending.setStations({{1,{11,20},1,true},{3,{39,20},1,true},{4,{10,20},3,true}});pending.tick(*localWorld,empty);CHECK(builder.buildRail(*localWorld,{20,20},{20,20},false,message));pending.tick(*localWorld,empty);CHECK(!pending.local(1,3));release->set_value();blocked.wait();NetworkWorker::blocking=true;pending.tick(*localWorld,empty); // the isolated repaired tile does not reconnect until explicitly linked
    CHECK(builder.buildRail(*localWorld,{19,20},{21,20},false,message));pending.tick(*localWorld,empty);CHECK(pending.local(1,3));
    // Railway trees are cleared and do not regrow after tracks are removed.
    economy->initializeVegetation();for(int chunk=0;chunk<ChunkCount;++chunk)for(auto tree:economy->trees(chunk))CHECK(!economy->hasRail({int(tree.tile)%MapSize,int(tree.tile)/MapSize}));
    std::cout<<"Railway tests passed: regional line, construction, station footprints, overpass, service routing, transfers, gates and persistence\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
