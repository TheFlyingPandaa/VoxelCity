#pragma once
#include "Traffic.h"
#include "NetworkWorker.h"
#include "Rail.h"
#include <array>
#include <string>
#include <unordered_map>
namespace vc {
enum class BuildingKind:uint8_t {
    None,
    LowDensityResidential,HighDensityResidential,
    LowDensityCommercial,HighDensityCommercial,
    Industrial,Power,Water,Sewage,Garbage,Clinic,Fire,Police,School,Park,PassengerStation,CargoTerminal,TrainDepot,
    Count,
    // Source compatibility for tools/tests which predate explicit density.
    Residential=LowDensityResidential,Commercial=LowDensityCommercial
};
constexpr bool railFacility(BuildingKind k){return k>=BuildingKind::PassengerStation&&k<=BuildingKind::TrainDepot;}
constexpr bool residential(BuildingKind k){return k==BuildingKind::LowDensityResidential||k==BuildingKind::HighDensityResidential;}
constexpr bool commercial(BuildingKind k){return k==BuildingKind::LowDensityCommercial||k==BuildingKind::HighDensityCommercial;}
constexpr bool highDensity(BuildingKind k){return k==BuildingKind::HighDensityResidential||k==BuildingKind::HighDensityCommercial;}
constexpr bool zone(BuildingKind k){return residential(k)||commercial(k)||k==BuildingKind::Industrial;}
constexpr int demandCategory(BuildingKind k){return residential(k)?0:commercial(k)?1:2;}
constexpr int visualKind(BuildingKind k){
    if(residential(k))return 1;if(commercial(k))return 2;if(k==BuildingKind::Industrial)return 3;
    return 4+int(k)-int(BuildingKind::Power);
}
enum class CityOverlay:uint8_t { None,Access,Traffic,Power,Water,Sewage,Services,Pollution,LandValue,Healthcare,Fire,Police,Education,Parks };
struct BuildingDefinition { std::string name; int cost=0,upkeep=0,capacity=0,unlock=0,radius=0; };
struct Building {
    uint64_t id=0; Cell cell; BuildingKind kind=BuildingKind::None; uint8_t level=0,variant=0;
    int residents=0,workers=0,inventory=0,distress=0,age=0;
    float productivity=1;
    float waste=0,health=100,fire=0,crime=0,education=0,happiness=70,landValue=50,pollution=0,noise=0;
    std::array<float,3> utilities{};
    Cell access; int component=-1; bool external=false;
    std::array<float,6> services{};
    int railPassengers=0;
    std::string problem;
};
struct Household {uint64_t id=0,home=0,workplace=0;int members=0;float commuteQuality=1,travelSeconds=0;};
struct CityTrip {uint64_t id=0,source=0,destination=0,household=0;uint8_t kind=0;int cargo=0;uint64_t requested=0;};
// Diagnostic wall-clock costs only; never serialized or used for simulation decisions.
struct CityPerformance {
    uint64_t ticks=0,steps=0,deferredSteps=0;
    double accessMs=0,railMs=0,trafficMs=0,eventsMs=0;
    double servicesMs=0,employmentMs=0,developmentMs=0,dispatchMs=0,financeMs=0,publishMs=0;
};
struct CityStats {
    int population=0,jobs=0,employed=0,milestone=0;
    double income=0,expenses=0,balance=0,tickMs=0,averageTripSeconds=0;
    float happiness=0,unemployment=0;
    std::array<float,3> demand{1,1,1};
    std::array<int,3> capacity{},consumption{};
    uint64_t deliveries=0,failedTrips=0,completedTrips=0;
};
class CitySimulation {
public:
    explicit CitySimulation(uint32_t seed=42);
    const std::vector<Building>& buildings() const {return buildings_;}
    const std::vector<Household>& households() const {return households_;}
    const CityStats& stats() const {return stats_;}
    const CityPerformance& performance() const {return performance_;}
    void resetPerformance() {performance_={};}
    const BuildingDefinition& definition(BuildingKind k) const {return definitions_.at(size_t(k));}
    const Building* at(Cell c) const;
    const Building* find(uint64_t id) const;
    RailSimulation railway;
    bool buildRail(World&,Cell from,Cell to,bool erase,std::string& message);
    bool buildRailOverpass(World&,Cell center,std::string& message);
    bool placeRailFacility(World&,Cell origin,BuildingKind kind,int rotation,std::string& message);
    static std::vector<Cell> facilityFootprint(const Building& b,bool platform=false);
    static std::vector<Cell> occupiedFootprint(const Building&);
    void updateRailServices(World&);
    void railEvents(World&,TrafficSimulation&);
    int waitingRailCargo(uint64_t station)const;
    bool buildRoad(World& world,Cell from,Cell to,bool erase,std::string& message,RoadClass roadClass=RoadClass::Street);
    bool buildRoundabout(World& world,Cell origin,std::string& message);
    bool buildDiamondInterchange(World& world,Cell center,std::string& message);
    bool buildDiagonalRoad(World& world,Cell from,Cell to,std::string& message);
    bool place(World& world,Cell c,BuildingKind kind,std::string& message);
    bool bulldoze(World& world,Cell c,std::string& message);
    bool roadRule(World& world,Cell c,uint8_t rule,std::string& message);
    bool takeLoan();
    void update(World& world,TrafficSimulation& traffic,double elapsed);
    void tick(World& world,TrafficSimulation& traffic);
    void refresh(World& world);
    void setOverlay(World& world,CityOverlay overlay);
    void save(World& world,TrafficSimulation& traffic,const std::filesystem::path& path);
    void load(World& world,TrafficSimulation& traffic,const std::filesystem::path& path);
    void scenario(World& world,TrafficSimulation& traffic,int population=0);
    void newCity(World& world,TrafficSimulation& traffic);
    void railwayScenario(World&,TrafficSimulation&);
    size_t regionalTrips() const;
    void prepareTrafficStress(World& world);
    void replenishTrafficStress(TrafficSimulation& traffic,size_t target);
    void validate(const World& world) const;
    void loadDefinitions(const std::filesystem::path& path);
    uint64_t ticks() const {return ticks_;}
    double treasury=100000;
    std::array<int,3> taxes{9,9,9};
    bool paused=false,sandbox=false,loanTaken=false,bankrupt=false;
    int speed=1;
    uint64_t revision=0;
private:
    std::array<BuildingDefinition,size_t(BuildingKind::Count)> definitions_;
    std::vector<Building> buildings_;
    std::vector<Household> households_;
    std::vector<CityTrip> trips_;
    std::vector<int> parcel_=std::vector<int>(MapSize*MapSize,-1),components_=std::vector<int>(MapSize*MapSize,-1);
    std::vector<Cell> exits_;
    std::vector<std::vector<Cell>> arrivals_,departures_;
    std::unordered_map<uint64_t,size_t> ids_;
    struct RailExport {uint64_t source=0,terminal=0,train=0;int amount=0;};
    std::vector<RailExport> railExports_;
    CityStats stats_;
    CityPerformance performance_;
    std::vector<float> roadPressure_=std::vector<float>(MapSize*MapSize);
    uint64_t ticks_=0,nextId_=1,nextTrip_=1,topology_=~uint64_t(0);
    uint32_t random_=42;
    int insolvency_=0;
    double accumulator_=0,tripSeconds_=0;
    CityOverlay overlay_=CityOverlay::None;
    bool accessDirty_=true;
    uint32_t random();
    Building* mutableFind(uint64_t id);
    bool rebuild(World& world,bool wait=false);
    bool computeAccess(World& world,size_t& cursor);
    struct AccessJob;
    std::shared_ptr<AccessJob> accessJob_;
    uint64_t accessRevision_=0;
    void cityStep(World& world,TrafficSimulation& traffic);
    void publish(World& world);
    bool sendTrip(TrafficSimulation& traffic,uint64_t source,uint64_t destination,uint8_t kind,int cargo=0,uint64_t household=0);
    void events(TrafficSimulation& traffic);
    void regionalTraffic(World& world,TrafficSimulation& traffic,bool seed=false);
};
}
