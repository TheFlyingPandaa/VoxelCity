#include "NetworkWorker.h"
#include "City.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#define CHECK(x) do {if(!(x))throw std::runtime_error("Failed: " #x);}while(0)
using namespace vc;
static void advance(CitySimulation& c,World& w,TrafficSimulation& t,int ticks){for(int i=0;i<ticks;++i)c.tick(w,t);}
static std::string bytes(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv) try {
    vc::NetworkWorker::blocking=true;
    auto w=std::make_unique<World>();CitySimulation city;TrafficSimulation traffic({0,42,20000,64,true});std::string message;
    auto folder=std::filesystem::temp_directory_path()/("VoxelCityTests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(folder);
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}}cleanup{folder};
    {
        auto map=std::make_unique<World>();CitySimulation c;TrafficSimulation sim({0,42,20000,64,true});
        CHECK(!c.buildRoundabout(*map,{511,511},message));
        c.treasury=159;CHECK(!c.buildRoundabout(*map,{20,20},message));CHECK(map->roadCount()==0);
        c.treasury=1000;CHECK(c.buildRoundabout(*map,{20,20},message));CHECK(c.treasury==840);CHECK(map->roadCount()==8);
        CHECK(!map->road(21,21));CHECK(!map->setRoad(21,21,true));
        CHECK(!c.place(*map,{21,21},BuildingKind::Park,message));CHECK(!c.roadRule(*map,{20,20},2,message));
        CHECK(!c.buildRoundabout(*map,{21,21},message));CHECK(!c.buildRoad(*map,{20,20},{20,20},true,message));
        Cell ring[]={{20,20},{20,21},{20,22},{21,22},{22,22},{22,21},{22,20},{21,20}};
        for(int i=0;i<8;++i){CHECK(map->canTravel(ring[i],ring[(i+1)%8]));CHECK(!map->canTravel(ring[(i+1)%8],ring[i]));}
        Cell ports[]={{21,16},{26,21},{21,26},{16,21}};
        Cell mouths[]={{21,19},{23,21},{21,23},{19,21}};
        Cell entries[]={{21,20},{22,21},{21,22},{20,21}};
        for(int i=0;i<4;++i){map->stroke(ports[i],mouths[i],true);CHECK(map->canTravel(mouths[i],entries[i]));CHECK(map->canTravel(entries[i],mouths[i]));}
        map->setRoad(20,19,true);CHECK(!map->canTravel({20,19},{20,20}));
        // Every entrance reaches every other exit through the fixed circulation path.
        uint64_t id=1;for(auto a:ports)for(auto b:ports)if(a!=b){CHECK(sim.requestTrip({id++,1,a,b,1}));}
        for(int i=0;i<3600;++i)sim.tick(*map);
        auto events=sim.takeEvents();CHECK(events.size()==12);for(auto e:events)CHECK(e.completed);sim.validate(*map);
        c.save(*map,sim,folder/"roundabout.vcity");
        auto restored=std::make_unique<World>();CitySimulation restoredCity;TrafficSimulation restoredTraffic({0,42,20000,64,true});
        restoredCity.load(*restored,restoredTraffic,folder/"roundabout.vcity");
        CHECK(restored->roundaboutOrigin({21,21})==Cell(20,20));CHECK(restored->canTravel(ring[0],ring[1]));CHECK(!restored->canTravel(ring[1],ring[0]));
        CHECK(restoredCity.bulldoze(*restored,{21,21},message));CHECK(restoredCity.treasury==800);CHECK(restored->roadCount()==17);
        CHECK(restoredCity.buildRoundabout(*restored,{20,20},message));
    }
    {
        auto map=std::make_unique<World>();CitySimulation c;
        CHECK(c.buildRoad(*map,{20,20},{24,20},false,message,RoadClass::Avenue));
        CHECK(c.treasury==99700);for(int x=20;x<=24;++x){CHECK(map->roadClass({x,20})==RoadClass::Avenue);CHECK(map->roadClass({x,21})==RoadClass::Avenue);CHECK(map->lanesPerDirection({x,20})==2);}
        CHECK(map->canTravel({20,20},{21,20}));CHECK(!map->canTravel({21,20},{20,20}));
        CHECK(map->canTravel({21,21},{20,21}));CHECK(!map->canTravel({20,21},{21,21}));
        CHECK(c.bulldoze(*map,{22,21},message));CHECK(!map->road(22,20));CHECK(!map->road(22,21));
        CHECK(c.buildRoad(*map,{40,30},{50,30},false,message,RoadClass::Highway6));
        CHECK(map->roadClass({45,30})==RoadClass::Median);CHECK(map->connections(45,30)==0);CHECK(map->lanesPerDirection({45,29})==3);CHECK(!map->zoningFrontage({45,29}));
        CHECK(c.buildRoad(*map,{45,27},{45,28},false,message));CHECK(!map->canTravel({45,28},{45,29}));
        CHECK(c.buildDiamondInterchange(*map,{45,30},message));CHECK(map->canTravel({45,28},{45,29}));CHECK(map->canTravel({45,31},{45,32}));
        CHECK(c.buildRoad(*map,{60,60},{65,65},false,message,RoadClass::OneWay));CHECK(map->canTravel({62,62},{63,63}));CHECK(!map->canTravel({63,63},{62,62}));
    }
    {
        auto map=std::make_unique<World>();CitySimulation c;TrafficSimulation sim({0,42,20000,64,true});
        CHECK(c.buildDiagonalRoad(*map,{10,10},{20,20},message));CHECK(map->roadCount()==11);CHECK(c.treasury==99780);
        CHECK(!c.buildDiagonalRoad(*map,{10,11},{11,10},message));CHECK(!map->road(10,11));
        CHECK(map->roadOccupies({15,16}));CHECK(!c.place(*map,{15,16},BuildingKind::Park,message));
        CHECK(c.place(*map,{30,31},BuildingKind::Residential,message));auto money=c.treasury;
        CHECK(!c.buildDiagonalRoad(*map,{30,30},{35,35},message));CHECK(c.treasury==money);CHECK(!map->road(30,30));
        CHECK(sim.requestTrip({1,1,{10,10},{20,20},1}));for(int i=0;i<60;++i)sim.tick(*map);
        std::stringstream savedTraffic;sim.saveState(savedTraffic);TrafficSimulation idle({0,42,20000,64,true});c.save(*map,idle,folder/"diagonal.vcity");
        auto restored=std::make_unique<World>();CitySimulation loaded;TrafficSimulation moving({0,42,20000,64,true});
        loaded.load(*restored,moving,folder/"diagonal.vcity");moving.loadState(savedTraffic,*restored);CHECK(restored->diagonalCount()==11);CHECK(restored->canTravel({15,15},{16,16}));
        for(int i=0;i<2400;++i)moving.tick(*restored);moving.validate(*restored);auto events=moving.takeEvents();CHECK(events.size()==1&&events[0].completed);
        CHECK(loaded.bulldoze(*restored,{15,15},message));CHECK(!restored->canTravel({14,14},{15,15}));
        map->save(folder/"diagonal-map.vcity");restored->load(folder/"diagonal-map.vcity");CHECK(restored->diagonalCount()==11);
    }
    CHECK(city.buildRoad(*w,{10,0},{10,30},false,message));CHECK(city.treasury==100000-31*20);
    auto roads=w->roadCount();auto money=city.treasury;city.treasury=1;CHECK(!city.buildRoad(*w,{11,20},{15,20},false,message));CHECK(w->roadCount()==roads);CHECK(city.treasury==1);city.treasury=money;
    CHECK(city.place(*w,{9,1},BuildingKind::Power,message));CHECK(city.place(*w,{9,2},BuildingKind::Water,message));CHECK(city.place(*w,{9,3},BuildingKind::Sewage,message));
    CHECK(!city.place(*w,{9,3},BuildingKind::Residential,message));CHECK(!city.place(*w,{10,3},BuildingKind::Residential,message));CHECK(!city.place(*w,{9,4},BuildingKind::Police,message));
    CHECK(city.place(*w,{9,10},BuildingKind::Residential,message));CHECK(city.place(*w,{9,11},BuildingKind::Industrial,message));CHECK(city.place(*w,{9,12},BuildingKind::Commercial,message));
    CHECK(city.place(*w,{9,13},BuildingKind::HighDensityResidential,message));CHECK(city.place(*w,{9,14},BuildingKind::HighDensityCommercial,message));
    CHECK(city.definition(BuildingKind::HighDensityResidential).capacity==3*city.definition(BuildingKind::LowDensityResidential).capacity);
    CHECK(city.definition(BuildingKind::HighDensityCommercial).capacity==3*city.definition(BuildingKind::LowDensityCommercial).capacity);
    advance(city,*w,traffic,900);city.validate(*w);traffic.validate(*w);CHECK(city.stats().population>0);CHECK(city.at({9,10})->utilities[0]==1);CHECK(city.stats().jobs>0);
    CHECK(city.buildRoad(*w,{10,5},{10,5},true,message));advance(city,*w,traffic,30);CHECK(!city.at({9,10})->external);CHECK(city.at({9,10})->utilities[0]==0);
    CHECK(city.buildRoad(*w,{10,5},{10,5},false,message));advance(city,*w,traffic,30);CHECK(city.at({9,10})->external);CHECK(city.at({9,10})->utilities[0]==1);
    auto save=folder/"city.vcity";city.save(*w,traffic,save);
    {std::ifstream saved(save,std::ios::binary);uint32_t magic=0,version=0;saved.read(reinterpret_cast<char*>(&magic),4);saved.read(reinterpret_cast<char*>(&version),4);CHECK(magic==0x59544356u);CHECK(version==7);}
    auto otherWorld=std::make_unique<World>();CitySimulation other;TrafficSimulation otherTraffic({0,42,20000,64,true});other.load(*otherWorld,otherTraffic,save);other.validate(*otherWorld);otherTraffic.validate(*otherWorld);
    CHECK(other.at({9,13})->kind==BuildingKind::HighDensityResidential);CHECK(other.at({9,14})->kind==BuildingKind::HighDensityCommercial);CHECK(otherWorld->parcels()[13*MapSize+9].variant>=4);
    CHECK(other.stats().population==city.stats().population);CHECK(other.treasury==city.treasury);CHECK(otherTraffic.outstandingTrips()==traffic.outstandingTrips());
    advance(city,*w,traffic,180);advance(other,*otherWorld,otherTraffic,180);
    city.save(*w,traffic,folder/"a.vcity");other.save(*otherWorld,otherTraffic,folder/"b.vcity");CHECK(bytes(folder/"a.vcity")==bytes(folder/"b.vcity"));
    auto corrupt=bytes(save);corrupt.back()^=1;{std::ofstream f(folder/"bad.vcity",std::ios::binary);f.write(corrupt.data(),corrupt.size());}
    auto oldPop=other.stats().population;auto oldMoney=other.treasury;bool rejected=false;try{other.load(*otherWorld,otherTraffic,folder/"bad.vcity");}catch(...){rejected=true;}CHECK(rejected);CHECK(other.stats().population==oldPop);CHECK(other.treasury==oldMoney);
    auto legacy=folder/"legacy.vcity";w->save(legacy);other.load(*otherWorld,otherTraffic,legacy);CHECK(other.buildings().empty());CHECK(other.treasury==100000);CHECK(otherWorld->roadCount()==w->roadCount());
    CHECK(city.takeLoan());money=city.treasury;CHECK(!city.takeLoan());CHECK(city.treasury==money);
    // Saving a paused city immediately after an edit reconciles traffic without moving time.
    city.scenario(*w,traffic,500);advance(city,*w,traffic,900);city.paused=true;
    auto carsBefore=traffic.debugSnapshot();CHECK(!carsBefore.empty());auto localCar=std::find_if(carsBefore.begin(),carsBefore.end(),[&](const auto& car){return !w->highway(car.tile);});CHECK(localCar!=carsBefore.end());Cell edited=localCar->tile;
    CHECK(city.bulldoze(*w,edited,message));auto clockBefore=city.ticks();city.save(*w,traffic,folder/"paused-edit.vcity");CHECK(city.ticks()==clockBefore);
    other.load(*otherWorld,otherTraffic,folder/"paused-edit.vcity");otherTraffic.validate(*otherWorld);CHECK(!otherWorld->road(edited.x,edited.z));
    // Restore the small fixture used by the following outage checks.
    city.load(*w,traffic,save);
    // One-way traffic has real endpoint ownership and rejects the reverse journey.
    auto line=std::make_unique<World>();line->stroke({20,20},{30,20},true);for(int x=20;x<=30;++x)line->setRoadRule({x,20},2);
    TrafficSimulation directed({0,9,100,8,true});CHECK(directed.requestTrip({1,10,{20,20},{30,20},1}));CHECK(!directed.requestTrip({1,10,{20,20},{30,20},1}));CHECK(directed.requestTrip({2,10,{30,20},{20,20},1}));
    for(int i=0;i<1800;++i)directed.tick(*line);auto events=directed.takeEvents();CHECK(events.size()==2);CHECK(std::any_of(events.begin(),events.end(),[](auto e){return e.id==1&&e.completed;}));CHECK(std::any_of(events.begin(),events.end(),[](auto e){return e.id==2&&!e.completed;}));directed.validate(*line);
    CHECK(directed.requestTrip({3,10,{20,20},{30,20},1}));directed.tick(*line);line->setRoad(25,20,false);for(int i=0;i<90;++i)directed.tick(*line);events=directed.takeEvents();CHECK(std::any_of(events.begin(),events.end(),[](auto e){return e.id==3&&!e.completed;}));

    // The same origin/destination completes sooner after a player-built bypass.
    auto journey=[](bool bypass){auto map=std::make_unique<World>();map->stroke({10,10},{10,30},true);map->stroke({10,30},{30,30},true);map->stroke({30,30},{30,10},true);if(bypass)map->stroke({10,10},{30,10},true);
        TrafficSimulation sim({0,42,20000,64,true});CHECK(sim.requestTrip({1,1,{10,10},{30,10},1}));
        for(int tick=0;tick<4000;++tick){sim.tick(*map);auto result=sim.takeEvents();if(!result.empty()){CHECK(result[0].completed);return tick;}}
        throw std::runtime_error("Bypass test trip did not finish");};
    CHECK(journey(true)<journey(false)/2);
    // Pausing and speed changes do not change fixed-tick outcomes.
    auto beforeTicks=city.ticks();city.paused=true;city.update(*w,traffic,1);CHECK(city.ticks()==beforeTicks);city.paused=false;city.speed=3;city.update(*w,traffic,0.1);CHECK(city.ticks()==beforeTicks+9);city.speed=1;
    // Missing utility capacity deteriorates a home; restoring it permits recovery.
    CHECK(city.bulldoze(*w,{9,1},message));advance(city,*w,traffic,30*130);CHECK(city.at({9,10})->distress>=120);
    CHECK(city.place(*w,{9,1},BuildingKind::Power,message));advance(city,*w,traffic,30*80);CHECK(city.at({9,10})->utilities[0]==1);CHECK(city.at({9,10})->distress<120);
    // Starter towns must grow without manually setting resident counts.
    city.scenario(*w,traffic,0);advance(city,*w,traffic,18000);city.validate(*w);traffic.validate(*w);
    std::cout<<"starter_population="<<city.stats().population<<" balance="<<city.stats().balance<<" deliveries="<<city.stats().deliveries<<" active="<<traffic.stats().active<<" pending="<<traffic.stats().pending<<"\n";
    CHECK(city.stats().population>=500);CHECK(city.stats().balance>0);
    if(argc>1&&std::string(argv[1])=="growth"){
        city.scenario(*w,traffic,10000);std::vector<std::pair<Cell,BuildingKind>> blueprint;for(const auto& b:city.buildings())blueprint.push_back({b.cell,b.kind});
        city=CitySimulation();city.newCity(*w,traffic);
        CHECK(city.buildRoad(*w,{248,216},{248,300},false,message));
        for(int r=0;r<18;++r)CHECK(city.buildRoad(*w,{240,220+r*3},{335,220+r*3},false,message));
        CHECK(city.buildRoad(*w,{240,220},{240,271},false,message));CHECK(city.buildRoad(*w,{335,220},{335,271},false,message));
        for(int x:{272,296,320})CHECK(city.buildRoad(*w,{x,220},{x,271},false,message));
        std::vector<std::pair<Cell,BuildingKind>> facilities;
        for(auto [cell,kind]:blueprint)if(kind<=BuildingKind::Industrial)CHECK(city.place(*w,cell,kind,message));else facilities.push_back({cell,kind});
        int stable=0,peak=0;
        for(int second=0;second<7200;++second){
            if(second%5==0)for(auto [cell,kind]:facilities)if(!city.at(cell))city.place(*w,cell,kind,message);
            advance(city,*w,traffic,30);peak=std::max(peak,city.stats().population);
            if(second%300==0)std::cout<<"growth second="<<second<<" pop="<<city.stats().population<<" cash="<<city.treasury<<" balance="<<city.stats().balance<<" demand="<<city.stats().demand[0]<<" supply="<<city.stats().capacity[0]<<"/"<<city.stats().consumption[0]<<" facilities="<<city.buildings().size()-blueprint.size()+facilities.size()<<"\n"<<std::flush;
            if(second%60==0){city.validate(*w);stable=city.stats().population>=10000&&city.stats().balance>0?stable+1:0;if(stable>=30)break;}
        }
        for(const auto& b:city.buildings())if(b.kind==BuildingKind::Residential&&b.level<2){std::cout<<"first unupgraded home: happiness="<<b.happiness<<" land="<<b.landValue<<" waste="<<b.waste<<" school="<<b.services[4]<<" park="<<b.services[5]<<"\n";break;}
        std::cout<<"growth peak="<<peak<<" final="<<city.stats().population<<" stable_periods="<<stable<<"\n";CHECK(peak>=10000);CHECK(stable>=30);traffic.validate(*w);
    }
    else if(argc>1){bool soak=std::string(argv[1])=="soak";int seconds=soak?7200:std::stoi(argv[1]);city.scenario(*w,traffic,10000);std::vector<double> times;auto start=std::chrono::steady_clock::now();
        for(int i=0;i<seconds*30;++i){
            if(soak&&i%1800==0)city.bulldoze(*w,{272,250},message);
            if(soak&&i%1800==90)city.buildRoad(*w,{272,250},{272,250},false,message);
            if(i&&i%9000==0){auto path=folder/("soak-"+std::to_string((i/9000)%3)+".vcity");city.save(*w,traffic,path);city.load(*w,traffic,path);}
            auto a=std::chrono::steady_clock::now();city.tick(*w,traffic);times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-a).count());if(i%1800==0)city.validate(*w);}
        city.validate(*w);traffic.validate(*w);std::sort(times.begin(),times.end());std::cout<<"population="<<city.stats().population<<" balance="<<city.stats().balance<<" active="<<traffic.stats().active<<" pending="<<traffic.stats().pending<<" deliveries="<<city.stats().deliveries<<" p95_ms="<<times[size_t(times.size()*.95)]<<" wall_seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<"\n";
    }
    std::cout<<"City tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
