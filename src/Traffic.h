#pragma once
#include "World.h"
#include <memory>
#include <span>

namespace vc {
struct CarInstance { float x,z,dx,dz; uint32_t color; uint64_t id=0;uint8_t lane=0; float y=0;uint32_t vehicle=0; };
struct TrafficCarState {uint64_t id;Cell tile,destination;float x,z,speed;bool awaitingRoute;uint8_t lane=0;};
struct TrafficStats {
    size_t active=0, pending=0, capacity=0, memoryBytes=0;
    uint64_t completed=0, removed=0, ticks=0, routeExpansions=0;
    double tickMs=0, routingMs=0, routeLatencyMs=0;
    // Age of the submitted worker batch, excluding requests not yet submitted.
    double routeBatchAgeMs=0;
    uint64_t staleRoutes=0;
};
struct TripRequest { uint64_t id=0, owner=0; Cell from,to; uint8_t kind=0; };
struct TripEvent { uint64_t id=0,owner=0; bool completed=false; uint64_t ticks=0; };
struct TrafficConfig { size_t target=50000; uint32_t seed=1; unsigned routeBudget=20000, spawnBudget=256; bool purposeful=false; };
// All float arrays must be 32-byte aligned, allocated to a multiple of eight.
// Inactive lanes are masked; no AVX2 load extends beyond the padded allocation.
void integrateCars(size_t count,float dt,float* speed,const float* limit,float* distance,const float* speedCaps=nullptr);
class TrafficSimulation {
public:
    explicit TrafficSimulation(TrafficConfig config={});
    ~TrafficSimulation();
    TrafficSimulation(const TrafficSimulation&)=delete;
    TrafficSimulation& operator=(const TrafficSimulation&)=delete;
    void update(World& world,double seconds);
    void synchronize(World& world);
    bool requestTrip(TripRequest request);
    std::vector<TripEvent> takeEvents();
    size_t outstandingTrips() const;
    std::vector<uint64_t> ownedTripIds() const;
    void saveState(std::ostream& out) const;
    void loadState(std::istream& in,World& world);
    void swap(TrafficSimulation& other) noexcept;
    void tick(World& world);
    void setTarget(size_t target);
    size_t target() const;
    bool paused=false;
    std::span<const CarInstance> instances();
    const TrafficStats& stats() const;
    // Expensive invariant check for headless tests, never in the frame loop.
    void validate(const World& world) const;
    std::vector<TrafficCarState> debugSnapshot() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
