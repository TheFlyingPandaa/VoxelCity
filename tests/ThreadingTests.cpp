#include "City.h"
#include "NetworkWorker.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>
#include <stdexcept>
using namespace vc;
#define CHECK(x) do{if(!(x))throw std::runtime_error("Threading check failed: " #x);}while(0)
struct Gate {
    std::promise<void> release;
    std::shared_future<void> signal=release.get_future().share();
    std::future<bool> job;
    Gate(){auto wait=signal;auto started=std::make_shared<std::promise<void>>();auto ready=started->get_future();job=NetworkWorker::instance().submit([wait,started]{started->set_value();wait.wait();return NetworkWorker::instance().isWorkerThread();});CHECK(job.valid());ready.get();}
    void open(){if(job.valid()){release.set_value();CHECK(job.get());}}
    ~Gate(){open();}
};
static bool complete(TrafficSimulation& sim,World& world,uint64_t id) {
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(std::chrono::steady_clock::now()<deadline) {
        sim.tick(world);auto events=sim.takeEvents();
        for(auto e:events)if(e.id==id){CHECK(sim.outstandingTrips()==0);sim.validate(world);return e.completed;}
        if(sim.stats().active==0)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("Asynchronous trip timed out: active="+std::to_string(sim.stats().active)+" pending="+std::to_string(sim.outstandingTrips())+" ticks="+std::to_string(sim.stats().ticks));
}
int main(int argc,char** argv)try {
    if(argc>1) {
        auto legacyWorld=std::make_unique<World>();CitySimulation legacyCity;TrafficSimulation legacyTraffic({0,42,100,8,true});
        legacyCity.load(*legacyWorld,legacyTraffic,argv[1]);legacyCity.validate(*legacyWorld);legacyTraffic.validate(*legacyWorld);
        std::cout<<"Existing city save validated without modification\n";
    }
    // Dirty chunks are compared with the consumed snapshot, including skipped edits and chunk borders.
    {
        auto baseline=std::make_unique<World>();auto current=std::make_unique<World>(*baseline);
        current->setRoad(15,15,true);current->setRoad(63,63,true);
        auto changed=current->changedVisualChunks(baseline.get());
        CHECK(changed.contains(0));CHECK(changed.contains(1));CHECK(changed.contains(ChunksAcross));CHECK(changed.contains(ChunksAcross+1));
        CHECK(changed.contains(3*ChunksAcross+3));CHECK(current->changedVisualChunks(current.get()).empty());
    }
    CHECK(!NetworkWorker::instance().isWorkerThread());
    auto world=std::make_unique<World>();world->stroke({10,10},{30,10},true);
    TrafficSimulation sim({0,42,20,64,true});NetworkWorker::blocking=true;sim.synchronize(*world);NetworkWorker::blocking=false;
    std::stringstream saved;
    {
        Gate gate;
        CHECK(sim.requestTrip({1,1,{10,10},{30,10},0}));
        auto start=std::chrono::steady_clock::now();sim.tick(*world);sim.saveState(saved);
        CHECK(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(250));
        CHECK(sim.outstandingTrips()==1);CHECK(sim.ownedTripIds()==std::vector<uint64_t>{1});
        // Invalidate a queued route. Its logical trip must survive and fail on the new graph.
        world->setRoad(20,10,false);sim.tick(*world);CHECK(sim.outstandingTrips()==1);
        gate.open();
    }
    CHECK(!complete(sim,*world,1));
    // A saved, unfinished request restarts on the worker and completes once.
    world->setRoad(20,10,true);TrafficSimulation loaded({0,42,20,64,true});loaded.loadState(saved,*world);
    CHECK(loaded.ownedTripIds()==std::vector<uint64_t>{1});CHECK(complete(loaded,*world,1));
    for(int i=0;i<10;++i)loaded.tick(*world);CHECK(loaded.takeEvents().empty());
    // Destroying a simulation while a search is queued never waits on the worker.
    {
        auto discarded=std::make_unique<TrafficSimulation>(TrafficConfig{0,9,1,8,true});NetworkWorker::blocking=true;discarded->synchronize(*world);NetworkWorker::blocking=false;
        Gate gate;discarded->requestTrip({7,7,{10,10},{30,10},0});discarded->tick(*world);
        auto start=std::chrono::steady_clock::now();discarded.reset();CHECK(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(250));gate.open();
    }
    // Queue saturation is explicit and bounded, and retains work for callers to retry.
    {
        Gate gate;std::vector<std::future<bool>> queued;
        for(int i=0;i<64;++i){auto job=NetworkWorker::instance().submit([]{return true;});if(job.valid())queued.push_back(std::move(job));}
        CHECK(!NetworkWorker::instance().submit([]{return true;}).valid());CHECK(NetworkWorker::instance().pending()<=64);
        gate.open();for(auto& job:queued)CHECK(job.get());
    }
    // Paused coverage is asynchronous, rejects obsolete facility snapshots, and eventually publishes.
    {
        auto map=std::make_unique<World>();map->stroke({10,0},{10,20},true);CitySimulation city;city.sandbox=true;TrafficSimulation traffic({0,3,100,8,true});std::string message;
        NetworkWorker::blocking=true;CHECK(city.place(*map,{9,10},BuildingKind::Residential,message));CHECK(city.place(*map,{9,5},BuildingKind::Park,message));city.refresh(*map);NetworkWorker::blocking=false;
        CHECK(city.at({9,10})->services[5]==1);city.paused=true;
        Gate gate;CHECK(city.bulldoze(*map,{9,5},message));auto start=std::chrono::steady_clock::now();city.update(*map,traffic,0.1);CHECK(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(250));
        CHECK(city.place(*map,{9,6},BuildingKind::Park,message));city.update(*map,traffic,0.1);
        auto pendingSave=std::filesystem::temp_directory_path()/("vc-threading-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".vcity");
        city.save(*map,traffic,pendingSave); // Must not wait for the deliberately blocked network worker.
        gate.open();
        auto restored=std::make_unique<World>();CitySimulation restoredCity;TrafficSimulation restoredTraffic({0,3,100,8,true});
        restoredCity.load(*restored,restoredTraffic,pendingSave);restoredCity.validate(*restored);restoredTraffic.validate(*restored);std::filesystem::remove(pendingSave);
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(std::chrono::steady_clock::now()<deadline&&city.at({9,6})->component<0){city.update(*map,traffic,0.1);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        CHECK(city.at({9,6})->component>=0);CHECK(city.at({9,10})->services[5]==1);CHECK(city.ticks()==0);city.validate(*map);
    }
    std::cout<<"Threading tests passed: worker ownership, async handoff, stale routes, pending saves, cancellation, queue bounds, coverage\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
