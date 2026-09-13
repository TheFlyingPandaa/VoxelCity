#pragma once
#include "Traffic.h"
#include "NetworkWorker.h"
#include <map>
#include <string>
namespace vc {
struct RailStation {uint64_t id=0;Cell cell;uint8_t kind=0;bool active=false;Cell first,last;bool operator==(const RailStation&)const=default;};
struct RailService {uint64_t from=0,to=0;uint8_t kind=0;std::vector<Cell> route;};
struct RailTrain {uint64_t id=0,from=0,to=0;uint8_t kind=0;std::vector<Cell> route;float distance=0;int dwell=0;bool stopped=false;};
struct RailJourney {uint64_t id=0,from=0,to=0,train=0,requested=0,ready=0;uint32_t egress=0;bool arrived=false;};
struct RailEvent {uint64_t train=0,station=0,journey=0;uint8_t kind=0;bool completed=true;float seconds=0;};
struct RailStats {uint64_t passengers=0,cargo=0,completed=0;};
class RailSimulation {
public:
    void setStations(std::vector<RailStation> stations);
    void tick(World&,TrafficSimulation&);
    bool regional(uint64_t station)const;
    bool local(uint64_t from,uint64_t to)const;
    bool requestPassenger(uint64_t id,uint64_t from,uint64_t to,unsigned accessSeconds=0,unsigned egressSeconds=0);
    std::vector<RailEvent> takeEvents(){auto out=std::move(events_);events_.clear();return out;}
    std::span<const CarInstance> instances(const World&);
    const std::vector<RailTrain>& trains()const{return trains_;}
    const std::vector<RailJourney>& journeys()const{return journeys_;}
    const RailStats& stats()const{return stats_;}
    void recordCargo(unsigned amount){stats_.cargo+=amount;}
    void recordPassenger(){++stats_.passengers;}
    void save(std::ostream&)const;
    void load(std::istream&,World&);
    void seedRegional(const World&);
private:
    struct Network {std::vector<RailService> services;std::map<std::pair<uint64_t,uint64_t>,uint64_t> next;};
    struct Build;
    struct Job {uint64_t revision=0;std::vector<RailStation> stations;std::shared_ptr<Build> work;std::future<bool> future;NetworkLifetime life;};
    std::shared_ptr<Job> job_;
    Network network_;
    std::vector<RailStation> stations_;
    std::vector<RailTrain> trains_;
    std::vector<RailJourney> journeys_;
    std::vector<RailEvent> events_;
    std::vector<CarInstance> instances_;
    std::map<std::pair<uint64_t,uint64_t>,uint64_t> dispatched_;
    uint64_t topology_=~uint64_t(0),ticks_=0,nextId_=1;
    bool dirty_=true;
    RailStats stats_;
    void refresh(const World&);
};
}
