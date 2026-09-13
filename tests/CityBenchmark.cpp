#include "City.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <windows.h>
#include <psapi.h>

// Controlled worker mode measures simulation throughput, not rendered performance.
int main(int argc,char** argv) try {
    const int population=argc>1?std::stoi(argv[1]):10000;
    const int count=argc>2?std::stoi(argv[2]):900;
    const int warmup=argc>3?std::stoi(argv[3]):300;
    if(population<0||count<1||warmup<0)throw std::runtime_error("Expected population >= 0, ticks > 0, warmup >= 0");
    vc::NetworkWorker::blocking=true;
    auto world=std::make_unique<vc::World>();
    vc::CitySimulation city(42);vc::TrafficSimulation traffic({0,42,20000,64,true});
    city.scenario(*world,traffic,population);
    for(int i=0;i<warmup;++i)city.tick(*world,traffic);
    city.resetPerformance();
    std::vector<double> samples;samples.reserve(count);
    int minimumPopulation=city.stats().population;
    double maximumRouteBatchAgeMs=0;
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<count;++i){
        auto before=std::chrono::steady_clock::now();city.tick(*world,traffic);
        samples.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count());
        minimumPopulation=std::min(minimumPopulation,city.stats().population);
        maximumRouteBatchAgeMs=std::max(maximumRouteBatchAgeMs,traffic.stats().routeBatchAgeMs);
    }
    double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    city.validate(*world);traffic.validate(*world);
    std::sort(samples.begin(),samples.end());
    auto percentile=[&](double p){return samples[std::min(samples.size()-1,size_t(p*(samples.size()-1)))];};
    PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);
    bool memoryAvailable=GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory))!=0;
    const auto& m=city.performance();
    std::cout<<"{\n  \"seed\": 42, \"worker_mode\": \"blocking\", \"rendered\": false,"
        <<"\n  \"requested_population\": "<<population<<", \"minimum_population\": "<<minimumPopulation
        <<", \"population_target_met\": "<<(minimumPopulation>=population?"true":"false")
        <<",\n  \"warmup_ticks\": "<<warmup<<", \"measured_ticks\": "<<m.ticks
        <<", \"city_steps\": "<<m.steps<<", \"deferred_city_steps\": "<<m.deferredSteps
        <<",\n  \"wall_seconds\": "<<seconds<<", \"simulation_seconds\": "<<double(m.ticks)/30
        <<", \"ticks_per_wall_second\": "<<m.ticks/seconds<<", \"achieved_1x_ratio\": "<<m.ticks/(30*seconds)
        <<",\n  \"tick_p50_ms\": "<<percentile(.5)<<", \"tick_p95_ms\": "<<percentile(.95)<<", \"tick_p99_ms\": "<<percentile(.99)
        <<",\n  \"active_vehicles\": "<<traffic.stats().active<<", \"pending_routes\": "<<traffic.stats().pending
        <<", \"maximum_route_batch_age_ms\": "<<maximumRouteBatchAgeMs
        <<", \"completed_trips\": "<<city.stats().completedTrips<<", \"failed_trips\": "<<city.stats().failedTrips
        <<",\n  \"working_set_bytes\": ";
    if(memoryAvailable)std::cout<<memory.WorkingSetSize;else std::cout<<"null";
    std::cout<<",\n  \"subsystem_total_ms\": {\"access\": "<<m.accessMs<<", \"rail\": "<<m.railMs
        <<", \"traffic\": "<<m.trafficMs<<", \"events\": "<<m.eventsMs<<", \"services\": "<<m.servicesMs
        <<", \"employment\": "<<m.employmentMs<<", \"development\": "<<m.developmentMs
        <<", \"dispatch\": "<<m.dispatchMs<<", \"finance\": "<<m.financeMs<<", \"publish\": "<<m.publishMs<<"}\n}\n";
    return minimumPopulation>=population?0:2;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
