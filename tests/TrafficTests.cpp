#include "NetworkWorker.h"
#include "Traffic.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <fstream>
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string("Line ")+std::to_string(__LINE__)+" failed: " #x);}while(0)
using namespace vc;
static void checkSeparated(std::span<const CarInstance> cars) {
    // Independent oriented-box collision oracle using the rendered 6 x 3 car body.
    for(size_t i=0;i<cars.size();++i)for(size_t j=i+1;j<cars.size();++j){
        auto a=cars[i],b=cars[j];float dx=b.x-a.x,dz=b.z-a.z;
        if(dx*dx+dz*dz>49)continue;
        bool separate=false;
        for(auto axis:std::array<std::array<float,2>,4>{{{a.dx,a.dz},{-a.dz,a.dx},{b.dx,b.dz},{-b.dz,b.dx}}}){
            float extent=3*(std::abs(a.dx*axis[0]+a.dz*axis[1])+std::abs(b.dx*axis[0]+b.dz*axis[1]))+
                         1.5f*(std::abs(-a.dz*axis[0]+a.dx*axis[1])+std::abs(-b.dz*axis[0]+b.dx*axis[1]));
            if(std::abs(dx*axis[0]+dz*axis[1])>=extent-.01f){separate=true;break;}
        }
        CHECK(separate);
    }
}
int main(int argc,char** argv)try {
    vc::NetworkWorker::blocking=true;
    {
        auto avenue=std::make_unique<World>();for(int x=10;x<=40;++x){avenue->setRoad(x,10,true);avenue->setRoadClass({x,10},RoadClass::Avenue);avenue->setRoadRule({x,10},2);}
        TrafficSimulation lanes({0,17,20000,64,true});for(uint64_t id=1;id<=12;++id)CHECK(lanes.requestTrip({id,1,{10,10},{40,10},1}));
        bool usedPassingLane=false;for(int tick=0;tick<300;++tick){lanes.tick(*avenue);for(auto car:lanes.debugSnapshot())usedPassingLane|=car.lane==1;checkSeparated(lanes.instances());}
        CHECK(usedPassingLane);lanes.validate(*avenue);
    }
    {
        // All four diagonal headings, two-way traffic and a cardinal junction.
        auto map=std::make_unique<World>();
        for(auto c:World::diagonalLine({10,10},{30,30}))map->setDiagonalRoad(c,false);
        for(auto c:World::diagonalLine({10,30},{30,10}))map->setDiagonalRoad(c,true);
        map->stroke({20,20},{20,36},true);
        CHECK(map->connections(15,15)==(SouthEast|NorthWest));
        CHECK(map->connections(15,25)==(NorthEast|SouthWest));
        CHECK(map->canTravel({15,15},{16,16}));CHECK(map->canTravel({16,16},{15,15}));
        TrafficSimulation diagonal({0,42,20000,64,true});
        Cell ends[]={{10,10},{30,30},{10,30},{30,10},{20,36}};
        uint64_t id=1;
        for(auto a:ends)for(auto b:ends)if(a!=b){
            CHECK(diagonal.requestTrip({id++,1,a,b,1}));
            for(int tick=0;tick<2400&&diagonal.outstandingTrips();++tick){
                diagonal.tick(*map);checkSeparated(diagonal.instances());
                for(auto car:diagonal.instances()){auto mat=map->column(int(car.x),int(car.z)).material;CHECK(mat==Material::Asphalt||mat==Material::Marking);}
            }
            auto events=diagonal.takeEvents();CHECK(events.size()==1);CHECK(events[0].completed);
        }
        // Concurrent opposing diagonal lanes and merging movements stay separated.
        for(auto a:ends)for(auto b:ends)if(a!=b)CHECK(diagonal.requestTrip({id++,1,a,b,1}));
        for(int tick=0;tick<3600;++tick){diagonal.tick(*map);checkSeparated(diagonal.instances());}
        diagonal.validate(*map);
        auto events=diagonal.takeEvents();CHECK(events.size()==20);for(auto e:events)CHECK(e.completed);
    }
    {
        auto map=std::make_unique<World>();map->placeRoundabout({20,20});
        Cell ends[]={{21,16},{26,21},{21,26},{16,21}},mouths[]={{21,19},{23,21},{21,23},{19,21}};
        for(int i=0;i<4;++i)map->stroke(ends[i],mouths[i],true);
        TrafficSimulation circular({0,42,20000,64,true});uint64_t id=1;
        for(auto a:ends)for(auto b:ends)if(a!=b)CHECK(circular.requestTrip({id++,1,a,b,1}));
        for(int tick=0;tick<3600;++tick){circular.tick(*map);checkSeparated(circular.instances());
            for(auto car:circular.instances()){auto mat=map->column(int(car.x),int(car.z)).material;CHECK(mat==Material::Asphalt||mat==Material::Marking);}
        }
        auto events=circular.takeEvents();CHECK(events.size()==12);for(auto e:events)CHECK(e.completed);circular.validate(*map);
    }
    if(argc>1) {
        size_t count=std::stoul(argv[1]);auto w=std::make_unique<World>();w->generateScenario(2);
        TrafficSimulation traffic({count,42});std::vector<double> times;
        auto start=std::chrono::steady_clock::now();unsigned ticks=0;
        while(traffic.stats().active<count && ticks<10000){traffic.tick(*w);++ticks;}
        double ramp=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        CHECK(traffic.stats().active==count);traffic.validate(*w);
        size_t minimum=count;
        for(int i=0;i<600;++i){traffic.tick(*w);times.push_back(traffic.stats().tickMs);minimum=std::min(minimum,traffic.stats().active);}
        traffic.validate(*w);std::sort(times.begin(),times.end());
        std::cout<<"cars="<<traffic.stats().active<<" min="<<minimum<<" ramp_ticks="<<ticks<<" ramp_seconds="<<ramp<<" p50_ms="<<times[300]<<" p95_ms="<<times[570]<<" completed="<<traffic.stats().completed<<" memory_mib="<<double(traffic.stats().memoryBytes)/1048576<<'\n';return 0;
    }
    // Independent scalar oracle including nonmultiples of eight and untouched padding.
    for(size_t n:std::array<size_t,9>{0,1,7,8,9,15,16,17,31}) {
        alignas(32) std::array<float,32> speed{},limit{},distance{};auto expected=speed;
        for(size_t i=0;i<32;++i){speed[i]=float(i%17);limit[i]=float(i)*.037f;distance[i]=-123;expected[i]=speed[i];}
        for(size_t i=0;i<n;++i){float v=std::min(16.f,std::min(std::sqrt(12*limit[i]),speed[i]+6.f/30));float d=std::min(limit[i],v/30);expected[i]=std::min(v,d/(1.f/30));}
        integrateCars(n,1.f/30,speed.data(),limit.data(),distance.data());
        for(size_t i=0;i<n;++i){CHECK(std::abs(speed[i]-expected[i])<.0001f);CHECK(distance[i]<=limit[i]);}
        for(size_t i=n;i<32;++i){CHECK(distance[i]==-123);CHECK(speed[i]==float(i%17));}
    }
    auto w=std::make_unique<World>();TrafficSimulation traffic({100,7});
    // Opposing cars must be able to pass through consecutive alternating bends.
    for(int rotation=0;rotation<4;++rotation)for(uint32_t seed=1;seed<=16;++seed) {
        auto chicane=std::make_unique<World>();
        for(Cell p:std::array<Cell,8>{{{10,10},{11,10},{11,11},{12,11},{12,12},{13,12},{13,13},{14,13}}}){
            for(int r=0;r<rotation;++r)p={30-p.z,p.x};chicane->setRoad(p.x,p.z,true);
        }
        TrafficSimulation pair({2,seed});pair.tick(*chicane);CHECK(pair.stats().active==2);pair.setTarget(0);
        checkSeparated(pair.instances());
        for(int tick=0;tick<1800 && pair.stats().active;++tick){pair.tick(*chicane);checkSeparated(pair.instances());}
        if(pair.stats().active){std::cerr<<"Chicane deadlock, seed "<<seed<<'\n';for(auto car:pair.debugSnapshot())std::cerr<<car.id<<": "<<car.tile.x<<","<<car.tile.z<<" to "<<car.destination.x<<","<<car.destination.z<<" speed="<<car.speed<<'\n';}
        CHECK(pair.stats().active==0);CHECK(pair.stats().completed==2);
    }
    // Two turning/merging cars at a crossroads must drain, including an older
    // follower behind a newer car on the same approach. Check real body separation.
    for(uint32_t seed=1;seed<=128;++seed){
        auto cross=std::make_unique<World>();cross->stroke({8,10},{12,10},true);cross->stroke({10,8},{10,12},true);
        TrafficSimulation pair({2,seed});pair.tick(*cross);CHECK(pair.stats().active==2);pair.setTarget(0);
        checkSeparated(pair.instances());
        for(int tick=0;tick<1200 && pair.stats().active;++tick){pair.tick(*cross);checkSeparated(pair.instances());}
        if(pair.stats().active)std::cerr<<"Intersection deadlock, seed "<<seed<<'\n';
        CHECK(pair.stats().active==0);CHECK(pair.stats().completed==2);
    }
    bool coveredOlderFollower=false;
    for(uint32_t seed=1;seed<=128;++seed){
        auto cross=std::make_unique<World>();cross->stroke({8,10},{12,10},true);cross->stroke({10,8},{10,12},true);
        TrafficSimulation pair({1,seed});pair.tick(*cross);pair.setTarget(0);
        for(int tick=0;tick<600 && pair.stats().active;++tick){
            auto state=pair.debugSnapshot()[0];auto pose=pair.instances()[0];
            float progress=8+(pose.x-(state.tile.x*16+8))*pose.dx+(pose.z-(state.tile.z*16+8))*pose.dz;
            bool approach=std::abs(state.tile.x-10)+std::abs(state.tile.z-10)==1 && (168-pose.x)*pose.dx+(168-pose.z)*pose.dz>0;
            if(approach && progress<2 && state.tile!=state.destination){
                pair.setTarget(2);pair.tick(*cross);pair.setTarget(0);
                auto states=pair.debugSnapshot();auto poses=pair.instances();
                if(states.size()==2 && states[0].tile==states[1].tile && poses[0].dx*poses[1].dx+poses[0].dz*poses[1].dz>.99f &&
                   (poses[1].x-poses[0].x)*poses[0].dx+(poses[1].z-poses[0].z)*poses[0].dz>0)coveredOlderFollower=true;
                break;
            }
            pair.tick(*cross);
        }
        for(int tick=0;tick<1200 && pair.stats().active;++tick){pair.tick(*cross);checkSeparated(pair.instances());}
        CHECK(pair.stats().active==0);
    }
    CHECK(coveredOlderFollower);
    // Adjacent junctions exercise cars turning out of one controlled tile into another.
    for(uint32_t seed=1;seed<=64;++seed){
        auto adjacent=std::make_unique<World>();for(int z=10;z<13;++z)adjacent->stroke({10,z},{12,z},true);
        TrafficSimulation pair({2,seed});pair.tick(*adjacent);CHECK(pair.stats().active==2);pair.setTarget(0);
        for(int tick=0;tick<1200 && pair.stats().active;++tick){pair.tick(*adjacent);checkSeparated(pair.instances());}
        if(pair.stats().active){std::cerr<<"Adjacent junction deadlock, seed "<<seed<<'\n';for(auto car:pair.debugSnapshot())std::cerr<<car.id<<": "<<car.tile.x<<","<<car.tile.z<<" to "<<car.destination.x<<","<<car.destination.z<<" speed="<<car.speed<<'\n';}
        CHECK(pair.stats().active==0);CHECK(pair.stats().completed==2);
    }
    for(int i=0;i<10;++i)traffic.tick(*w);CHECK(traffic.stats().active==0);
    w->setRoad(20,20,true);traffic.tick(*w);CHECK(traffic.stats().active==0);
    // Straight road, bends, dead ends and a four-way junction, plus a disconnected component.
    w->stroke({10,20},{40,20},true);w->stroke({25,10},{25,35},true);
    w->stroke({40,20},{40,40},true);w->stroke({40,40},{30,40},true);w->stroke({100,100},{110,100},true);
    for(int i=0;i<1200;++i){traffic.tick(*w);if(i%30==0)traffic.validate(*w);}
    CHECK(traffic.stats().active>0);CHECK(traffic.stats().active<=100);CHECK(traffic.stats().completed>0);
    auto completed=traffic.stats().completed;
    traffic.setTarget(0);for(int i=0;i<2400;++i)traffic.tick(*w);
    CHECK(traffic.stats().completed>completed);CHECK(traffic.stats().active<100);
    traffic.setTarget(100);for(int i=0;i<100;++i)traffic.tick(*w);
    auto active=traffic.stats().active;traffic.paused=true;traffic.update(*w,100);CHECK(traffic.stats().active==active);
    w->setRoad(25,20,false);traffic.update(*w,0);traffic.validate(*w);traffic.paused=false;
    for(int i=0;i<600;++i){traffic.tick(*w);if(i%30==0)traffic.validate(*w);}
    w->generateScenario(0);traffic.update(*w,0);CHECK(traffic.stats().active==0);
    // Fixed seeds and fixed ticks reproduce snapshots exactly.
    w->generateScenario(3);TrafficSimulation a({30,123}),b({30,123});
    for(int i=0;i<300;++i){a.tick(*w);b.tick(*w);}
    auto aa=a.instances(),bb=b.instances();CHECK(aa.size()==bb.size());
    for(size_t i=0;i<aa.size();++i){CHECK(aa[i].x==bb[i].x);CHECK(aa[i].z==bb[i].z);}
    // Catch-up bound and invalid deltas.
    auto ticks=a.stats().ticks;a.update(*w,1000);CHECK(a.stats().ticks<=ticks+4);
    ticks=a.stats().ticks;a.update(*w,-1);CHECK(a.stats().ticks==ticks);
    // Uncongested trips drain completely, including two-tile dead-end U-turns.
    w->generateScenario(0);w->stroke({2,2},{3,2},true);TrafficSimulation tiny({1,91});
    tiny.tick(*w);CHECK(tiny.stats().active==1);tiny.setTarget(0);
    for(int i=0;i<300;++i)tiny.tick(*w);CHECK(tiny.stats().active==0);CHECK(tiny.stats().completed==1);
    // A single car must make progress through every corner and junction without starvation.
    w->generateScenario(3);TrafficSimulation moving({1,71});
    std::unordered_map<uint64_t,TrafficCarState> last;
    for(int i=0;i<1800;++i){moving.tick(*w);for(auto car:moving.debugSnapshot()){
        if(last.contains(car.id)){auto old=last.at(car.id);CHECK(std::hypot(car.x-old.x,car.z-old.z)<.54f);}
        last[car.id]=car;
    }}std::cout<<"Single-car completed "<<moving.stats().completed<<"\n";CHECK(moving.stats().completed>0);
    // A distant edit preserves all existing identities and coordinates when paused.
    auto before=moving.debugSnapshot();moving.paused=true;w->setRoad(400,400,true);moving.update(*w,0);auto after=moving.debugSnapshot();
    CHECK(before.size()==after.size());for(size_t i=0;i<before.size();++i){CHECK(before[i].id==after[i].id);CHECK(before[i].x==after[i].x);CHECK(before[i].z==after[i].z);}
    // Failed loading cannot clear traffic; successful map replacement must clear it.
    auto file=std::filesystem::temp_directory_path()/("traffic-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".vcity");
    {std::ofstream f(file);f<<"invalid";}bool failed=false;try{w->load(file);}catch(...){failed=true;}CHECK(failed);moving.update(*w,0);CHECK(moving.stats().active==before.size());
    w->save(file);w->load(file);moving.update(*w,0);CHECK(moving.stats().active==0);std::filesystem::remove(file);
    // Very small routing budgets resume searches across ticks and still complete trips.
    w->generateScenario(3);TrafficSimulation budgeted({5,19,1,1});
    for(int i=0;i<1800;++i)budgeted.tick(*w);CHECK(budgeted.stats().completed>0);budgeted.validate(*w);
    // Repair trips on a still-connected network. Only cars on the erased tile may vanish
    // at the edit boundary; unaffected cars retain their world position and identity.
    w->generateScenario(0);for(int z=10;z<20;++z)w->stroke({10,z},{19,z},true);
    TrafficSimulation edited({20,29});for(int i=0;i<100;++i)edited.tick(*w);
    auto original=edited.debugSnapshot();w->setRoad(15,15,false);edited.paused=true;edited.update(*w,0);
    auto retained=edited.debugSnapshot();size_t expected=0;
    for(auto car:original)if(car.tile!=Cell{15,15} && car.destination!=Cell{15,15})++expected;
    CHECK(retained.size()==expected);edited.paused=false;
    for(int i=0;i<900;++i){edited.tick(*w);if(i%30==0)edited.validate(*w);}
    CHECK(edited.stats().completed>0);
    std::cout<<"Traffic tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

