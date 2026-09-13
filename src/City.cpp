#include "City.h"
#include "Binary.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <climits>
#include <fstream>
#include <queue>
#include <map>
#include <sstream>
#include <unordered_set>
#include <windows.h>
namespace vc {
namespace {
constexpr auto& DX=RoadDX;constexpr auto& DZ=RoadDZ;
constexpr int Cells=MapSize*MapSize;
int tile(Cell c){return c.z*MapSize+c.x;}
int distance(Cell a,Cell b){return std::abs(a.x-b.x)+std::abs(a.z-b.z);}
uint32_t checksum(const std::string& data){uint32_t h=2166136261u;for(unsigned char c:data){h^=c;h*=16777619u;}return h;}
}
CitySimulation::CitySimulation(uint32_t seed):random_(seed?seed:42) {
    definitions_={BuildingDefinition{"None"},
        {"Low-density residential",0,0,8,0,0},{"High-density residential",0,0,24,0,0},
        {"Low-density commercial",0,0,6,0,0},{"High-density commercial",0,0,18,0,0},{"Industrial zone",0,0,10,0,0},
        {"Power plant",2500,30,2000,0,0},{"Water tower",1800,20,2000,0,0},{"Sewage treatment",2200,25,2000,0,0},
        {"Garbage depot",1800,15,2000,500,80},{"Clinic",2000,15,2000,500,64},{"Fire station",2000,15,2000,500,80},
        {"Police station",1800,15,2000,2000,64},{"School",2200,20,2000,2000,64},{"Park",500,5,2000,500,40},{"Passenger station",4000,40,120,0,0},{"Cargo terminal",6000,60,120,0,0},{"Train depot",5000,50,120,0,0}};
}
uint32_t CitySimulation::random(){random_^=random_<<13;random_^=random_>>17;random_^=random_<<5;return random_;}
const Building* CitySimulation::at(Cell c)const {if(!World::valid(c.x,c.z))return nullptr;int i=parcel_[tile(c)];return i>=0?&buildings_[i]:nullptr;}
const Building* CitySimulation::find(uint64_t id)const {auto i=ids_.find(id);return i==ids_.end()?nullptr:&buildings_[i->second];}
Building* CitySimulation::mutableFind(uint64_t id){return const_cast<Building*>(find(id));}
bool CitySimulation::buildRoad(World& w,Cell a,Cell b,bool erase,std::string& message,RoadClass roadClass) {
    if(!World::valid(a.x,a.z)||!World::valid(b.x,b.z))return false;
    Cell start=a,end=b;std::vector<Cell> line=World::diagonalLine(a,b);if(line.empty())return false;
    if(!erase&&(roadClass<=RoadClass::None||roadClass>=RoadClass::LegacyHighway))return false;
    struct Placement{Cell cell;int offset;};std::vector<Placement> placements;std::vector<Cell> cells;unsigned width=erase?1:RoadDefinitions[size_t(roadClass)].footprint;
    for(size_t i=0;i<line.size();++i){Cell before=line[i?i-1:i],after=line[i+1<line.size()?i+1:i];int tx=(after.x>before.x)-(after.x<before.x),tz=(after.z>before.z)-(after.z<before.z);if(!tx&&!tz)tx=1;Cell p{-tz,tx};int first=width==2?0:-int(width/2),last=int(width/2);
        for(int o=first;o<=last;++o){Cell c{line[i].x+p.x*o,line[i].z+p.z*o};if(std::find(cells.begin(),cells.end(),c)==cells.end()){cells.push_back(c);placements.push_back({c,o});}}}
    auto desired=[&](int offset){return (roadClass==RoadClass::Highway4||roadClass==RoadClass::Highway6)&&offset==0?RoadClass::Median:roadClass;};
    int changes=0;for(auto p:placements){Cell c=p.cell;if((w.roadRule(c)&HighwayRule)&&(erase||(!erase&&w.roadClass(c)!=desired(p.offset)))){message="The regional highway is maintained by neighboring cities. Edit local roads instead.";return false;}if(!erase&&w.highway(c)&&!RoadDefinitions[size_t(roadClass)].highway){message="Highways connect to local roads only through an interchange.";return false;}if(!erase&&(!World::valid(c.x,c.z)||at(c))){message="The complete road footprint must be clear of parcels and map edges.";return false;}if(w.road(c.x,c.z)!=!erase||(!erase&&w.roadClass(c)!=desired(p.offset)))++changes;}
    for(auto c:cells)if(w.roundaboutOrigin(c).x>=0){message="Roundabout paths are fixed. Use bulldoze to remove the whole piece.";return false;}
    if(!erase)for(auto c:cells)if(w.rail(c).bridge>=0&&!w.road(c.x,c.z)){message="The rail overpass corridor is reserved.";return false;}
    if(!erase)for(auto c:cells)if(w.hasRail(c)&&w.rail(c).height<1){auto links=w.rail(c).links;bool horizontal=start.z==end.z,straight=(links==(North|South)&&horizontal)||(links==(East|West)&&!horizontal&&start.x==end.x);if(!straight||roadClass!=RoadClass::Street||start==end){message="Rail crossings require a straight perpendicular two-lane street.";return false;}}
    double cost=0;if(erase)cost=changes*5;else for(auto c:line){int old=w.road(c.x,c.z)?RoadDefinitions[size_t(w.roadClass(c))].cost:0;cost+=std::max(0,RoadDefinitions[size_t(roadClass)].cost-old);}
    if(!sandbox&&treasury<cost){message="Insufficient funds for this road stroke.";return false;}
    bool diagonal=start.x!=end.x&&start.z!=end.z,rising=(end.x-start.x)*(end.z-start.z)<0;
    if(!changes)return false;for(auto p:placements){auto c=p.cell;if(erase)w.setRoad(c.x,c.z,false);else {if(diagonal)w.setDiagonalRoad(c,rising);else if(!w.road(c.x,c.z))w.setRoad(c.x,c.z,true);w.setRoadClass(c,desired(p.offset));}}
    if(!erase&&start!=end){int ax=(end.x>start.x)-(end.x<start.x),az=(end.z>start.z)-(end.z<start.z),heading=0;for(;heading<8;++heading)if(DX[heading]==ax&&DZ[heading]==az)break;uint8_t forward=uint8_t(heading+1),backward=uint8_t(oppositeDirection(heading)+1);if(diagonal){forward|=ExtendedDirectionRule;backward|=ExtendedDirectionRule;}
        for(auto p:placements){if(desired(p.offset)==RoadClass::Median)continue;uint8_t rule=0;if(roadClass==RoadClass::OneWay)rule=forward;else if(roadClass==RoadClass::Avenue)rule=p.offset==0?forward:backward;else if(roadClass==RoadClass::Highway4||roadClass==RoadClass::Highway6)rule=p.offset<0?forward:backward;w.setRoadRule(p.cell,rule);}}
    if(!sandbox)treasury-=cost;++revision;accessDirty_=true;++accessRevision_;message=erase?"Road removed.":std::string(RoadDefinitions[size_t(roadClass)].name)+" built.";return true;
}
bool CitySimulation::buildDiagonalRoad(World& w,Cell a,Cell b,std::string& message){
    auto cells=World::diagonalLine(a,b);if(cells.empty())return false;
    if(a.x==b.x||a.z==b.z)return buildRoad(w,a,b,false,message);
    bool rising=(b.x-a.x)*(b.z-a.z)<0;int changes=0;
    auto clear=[&](Cell c){return World::valid(c.x,c.z)&&!at(c)&&!w.hasRail(c)&&!w.highway(c)&&w.roundaboutOrigin(c).x<0;};
    for(size_t i=0;i<cells.size();++i){Cell c=cells[i];
        if(!clear(c)){message="Diagonal road is blocked by a parcel or fixed road.";return false;}
        if(i&&cells[i-1].x!=c.x&&cells[i-1].z!=c.z){Cell p=cells[i-1];Cell shoulder{p.x,c.z},other{c.x,p.z};int d=0;for(;d<8;++d)if(other==Cell{shoulder.x+DX[d],shoulder.z+DZ[d]})break;if(w.connections(shoulder.x,shoulder.z)&(1<<d)){message="Cross diagonal roads at a shared road tile, not between tiles.";return false;}if(!clear({p.x,c.z})||!clear({c.x,p.z})){message="Diagonal road shoulders need clear adjacent parcels.";return false;}}
        auto type=w.roadType(c);if(type!=4&&type!=(rising?3:2))++changes;
    }
    if(!changes)return false;
    if(!sandbox&&treasury<changes*20){message="Insufficient funds for diagonal road.";return false;}
    for(Cell c:cells)w.setDiagonalRoad(c,rising);
    if(!sandbox)treasury-=changes*20;++revision;accessDirty_=true;++accessRevision_;message="Diagonal road built ($20 per tile).";return true;
}
bool CitySimulation::buildRoundabout(World& w,Cell o,std::string& message) {
    if(!World::valid(o.x,o.z)||!World::valid(o.x+2,o.z+2)){message="The entire 6 x 6 roundabout must fit on the map.";return false;}
    for(int z=0;z<3;++z)for(int x=0;x<3;++x){Cell c{o.x+x,o.z+z};if(w.roadOccupies(c)||w.hasRail(c)||at(c)||w.roundaboutOrigin(c).x>=0){message="Roundabout requires an empty 6 x 6 footprint.";return false;}}
    if(!sandbox&&treasury<160){message="Insufficient funds for roundabout ($160).";return false;}
    if(!w.placeRoundabout(o))return false;
    if(!sandbox)treasury-=160;++revision;accessDirty_=true;++accessRevision_;message="Roundabout built. Connect roads at the midpoint of each side.";return true;
}
bool CitySimulation::buildDiamondInterchange(World& w,Cell c,std::string& message){
    if(!World::valid(c.x-4,c.z-4)||!World::valid(c.x+4,c.z+4)){message="The complete 9 x 9 interchange must fit on the map.";return false;}
    for(int z=-4;z<=4;++z)for(int x=-4;x<=4;++x){Cell p{c.x+x,c.z+z};if(w.hasRail(p)||at(p)||w.roundaboutOrigin(p).x>=0){message="Interchange footprint is blocked by a parcel or prefab.";return false;}}
    if(!sandbox&&treasury<2500){message="Insufficient funds for diamond interchange ($2,500).";return false;}
    if(!w.placeDiamondInterchange(c)){message="Place the interchange on the median of a straight divided highway.";return false;}
    if(!sandbox)treasury-=2500;++revision;accessDirty_=true;++accessRevision_;message="Diamond interchange built. Local traffic now uses the fixed highway access points.";return true;
}
bool CitySimulation::place(World& w,Cell c,BuildingKind kind,std::string& message) {
    if(railFacility(kind))return placeRailFacility(w,c,kind,0,message);
    if(w.roundaboutOrigin(c).x>=0){message="Roundabout footprint is reserved.";return false;}
    if(!World::valid(c.x,c.z)||kind<=BuildingKind::None||kind>=BuildingKind::Count)return false;
    if(w.roadOccupies(c)||w.hasRail(c)||at(c)){message="Parcel occupied.";return false;}
    const auto& d=definition(kind);
    if(!sandbox&&stats_.milestone<d.unlock){message="Unlocks at "+std::to_string(d.unlock)+" residents.";return false;}
    if(!sandbox&&treasury<d.cost){message="Insufficient funds.";return false;}
    Building b;b.id=nextId_++;b.cell=c;b.kind=kind;b.variant=uint8_t(random()%4);b.level=zone(kind)?0:1;
    parcel_[tile(c)]=int(buildings_.size());ids_[b.id]=buildings_.size();buildings_.push_back(b);if(!sandbox)treasury-=d.cost;
    ++revision;accessDirty_=true;++accessRevision_;publish(w);message=zone(kind)?"Zone painted. Growth needs road access, utilities and demand.":"Facility built. Connect its adjacent road to your town.";return true;
}
bool CitySimulation::bulldoze(World& w,Cell c,std::string& message) {
    if(w.roundaboutOrigin(c).x>=0){if(!sandbox&&treasury<40){message="Insufficient funds to remove roundabout ($40).";return false;}w.removeRoundabout(c);if(!sandbox)treasury-=40;++revision;accessDirty_=true;++accessRevision_;message="Roundabout removed ($40).";return true;}
    auto b=at(c);if(!b&&w.hasRail(c))return buildRail(w,c,c,true,message);if(!b){auto section=w.roadCrossSection(c);if(section.size()<=1)return buildRoad(w,c,c,true,message);for(auto p:section)if(w.roadRule(p)&HighwayRule){message="The regional highway is maintained by neighboring cities.";return false;}double cost=double(section.size())*5;if(!sandbox&&treasury<cost){message="Insufficient funds.";return false;}for(auto p:section)w.setRoad(p.x,p.z,false);if(!sandbox)treasury-=cost;++revision;accessDirty_=true;++accessRevision_;message="Road corridor cross-section removed.";return true;}
    if(!sandbox&&treasury<5){message="Insufficient funds.";return false;}
    if(railFacility(b->kind))for(auto p:occupiedFootprint(*b)){parcel_[tile(p)]=-1;w.setParcelVisual(p,{});}
    uint64_t id=b->id;size_t i=ids_.at(id);ids_.erase(id);parcel_[tile(c)]=-1;
    if(i+1<buildings_.size()){buildings_[i]=buildings_.back();ids_[buildings_[i].id]=i;for(auto p:occupiedFootprint(buildings_[i]))parcel_[tile(p)]=int(i);}buildings_.pop_back();
    std::erase_if(households_,[&](const Household& h){return h.home==id;});for(auto& h:households_)if(h.workplace==id)h.workplace=0;
    w.setParcelVisual(c,{});w.setTileTint(c,0);if(!sandbox)treasury-=5;++revision;accessDirty_=true;++accessRevision_;message="Parcel cleared.";return true;
}
bool CitySimulation::roadRule(World& w,Cell c,uint8_t rule,std::string& message) {
    if(w.roundaboutOrigin(c).x>=0){message="Roundabout entry, exit and circulation paths are fixed.";return false;}
    if(w.highway(c)){message="Highway lane directions are fixed. Use an interchange for local access.";return false;}
    if(w.roadClass(c)!=RoadClass::Street){message="This road class has fixed lane directions.";return false;}
    if(!w.road(c.x,c.z)||(rule&7)>4||rule>12){message="Select an existing road and a valid direction.";return false;}
    if(w.roadRule(c)==rule)return false;
    w.setRoadRule(c,rule);accessDirty_=true;++accessRevision_;++revision;message="Road direction / intersection control updated.";return true;
}
bool CitySimulation::takeLoan(){if(loanTaken)return false;loanTaken=true;treasury+=25000;bankrupt=false;insolvency_=0;++revision;return true;}
struct CitySimulation::AccessJob {
    NetworkLifetime lifetime;
    uint64_t topology,replacement,access;
    struct Work {std::shared_ptr<World> world;std::shared_ptr<CitySimulation> city;size_t cursor=0;};
    std::shared_ptr<Work> work;
    std::future<bool> result;
};
bool CitySimulation::rebuild(World& w,bool wait) {
    if(accessJob_&&(accessJob_->topology!=w.topologyRevision()||accessJob_->replacement!=w.replacementRevision()||accessJob_->access!=accessRevision_))accessJob_.reset();
    if(!accessJob_) {
        auto job=std::make_shared<AccessJob>();job->topology=w.topologyRevision();job->replacement=w.replacementRevision();job->access=accessRevision_;
        auto snapshot=std::make_shared<World>(w);auto candidate=std::make_shared<CitySimulation>(*this);candidate->accessJob_.reset();
        job->work=std::make_shared<AccessJob::Work>();job->work->world=snapshot;job->work->city=candidate;accessJob_=std::move(job);
    }
    for(;;) {
        if(!accessJob_->result.valid()) {
            auto work=accessJob_->work;
            accessJob_->result=NetworkWorker::instance().submit([work]{return work->city->computeAccess(*work->world,work->cursor);},accessJob_->lifetime.cancelled,wait);
        }
        if(!networkReady(accessJob_->result,wait))return false;
        if(accessJob_->result.get())break;
        if(!wait&&!NetworkWorker::blocking)return false;
    }
    auto data=accessJob_->work->city;accessJob_.reset();
    components_=std::move(data->components_);exits_=std::move(data->exits_);arrivals_=std::move(data->arrivals_);departures_=std::move(data->departures_);
    for(size_t i=0;i<buildings_.size();++i){auto& b=buildings_[i];const auto& from=data->buildings_[i];b.access=from.access;b.component=from.component;b.external=from.external;b.services=from.services;b.pollution=from.pollution;}
    topology_=w.topologyRevision();accessDirty_=false;return true;
}
bool CitySimulation::computeAccess(World& w,size_t& cursor) {
    if(!NetworkWorker::instance().isWorkerThread())throw std::logic_error("Coverage must run on network worker");
    std::vector<int> queue;
    if(cursor==0) {
    std::fill(components_.begin(),components_.end(),-1);exits_.clear();arrivals_.clear();departures_.clear();
    for(int t=0;t<Cells;++t)if(w.road(t%MapSize,t/MapSize)&&components_[t]<0){int id=int(exits_.size());exits_.push_back({});arrivals_.emplace_back();departures_.emplace_back();queue.clear();queue.push_back(t);components_[t]=id;
        for(size_t i=0;i<queue.size();++i){Cell c{queue[i]%MapSize,queue[i]/MapSize};if((c.x==0||c.z==0||c.x==MapSize-1||c.z==MapSize-1)&&(!w.highwayCount()||w.highway(c))){
                for(int d=0;d<8;++d){Cell inside{c.x+DX[d],c.z+DZ[d]};if(w.canTravel(c,inside))arrivals_[id].push_back(c);if(w.canTravel(inside,c))departures_[id].push_back(c);}
                if(!arrivals_[id].empty()&&!departures_[id].empty())exits_[id]=c;
            }
            for(int d=0;d<8;++d){Cell n{c.x+DX[d],c.z+DZ[d]};if((w.connections(c.x,c.z)&(1<<d))&&components_[tile(n)]<0){components_[tile(n)]=id;queue.push_back(tile(n));}}}}
    for(auto& b:buildings_){b.access={};b.component=-1;b.external=false;b.services.fill(0);
        for(auto frontage:facilityFootprint(b))for(int d=0;d<4;++d){Cell n{frontage.x+DX[d],frontage.z+DZ[d]};if(w.road(n.x,n.z)&&w.zoningFrontage(n)){int id=components_[tile(n)];if(b.component<0||exits_[id].x>=0){b.access=n;b.component=id;b.external=exits_[id].x>=0;if(b.external)break;}}}}
    cursor=1;return false;
    }
    // One facility per job lets routing and updated topology interleave with coverage.
    std::vector<int> depths(Cells,-1),touched;
    for(size_t facilityIndex=cursor-1;facilityIndex<buildings_.size();++facilityIndex){const auto& facility=buildings_[facilityIndex];cursor=facilityIndex+2;if(facility.kind<BuildingKind::Garbage||facility.kind>BuildingKind::Park||facility.component<0)continue;
        int index=int(facility.kind)-int(BuildingKind::Garbage),radius=definition(facility.kind).radius;
        for(int t:touched)depths[t]=-1;touched.clear();queue.clear();queue.push_back(tile(facility.access));depths[queue[0]]=0;touched.push_back(queue[0]);
        for(size_t i=0;i<queue.size();++i){int t=queue[i];if(depths[t]>=radius)continue;Cell c{t%MapSize,t/MapSize};for(int d=0;d<8;++d){Cell n{c.x+DX[d],c.z+DZ[d]};if((w.connections(c.x,c.z)&(1<<d))&&depths[tile(n)]<0){depths[tile(n)]=depths[t]+1;queue.push_back(tile(n));touched.push_back(tile(n));}}}
        for(auto& b:buildings_)if(b.component==facility.component&&depths[tile(b.access)]>=0)b.services[index]=1;
        return false;
    }
    for(auto& b:buildings_){b.pollution=0;for(const auto& f:buildings_)if(f.kind==BuildingKind::Industrial||f.kind==BuildingKind::Garbage||f.kind==BuildingKind::Power){int d=distance(b.cell,f.cell);if(d<12)b.pollution+=float(12-d)*0.3f;}b.pollution=std::min(100.f,b.pollution);}
    topology_=w.topologyRevision();accessDirty_=false;return true;
}
void CitySimulation::refresh(World& w){if(accessDirty_||topology_!=w.topologyRevision())rebuild(w);publish(w);}
bool CitySimulation::sendTrip(TrafficSimulation& traffic,uint64_t source,uint64_t destination,uint8_t kind,int cargo,uint64_t household) {
    for(const auto& t:trips_)if((household&&t.household==household)||(!household&&t.destination==destination&&t.kind==kind))return false;
    auto* a=mutableFind(source);auto* b=mutableFind(destination);if((a&&a->component<0)||(b&&b->component<0)||(!a&&!b))return false;
    if(kind==0&&household&&a&&b){
        for(auto j:railway.journeys())if(j.id==household)return true;
        const Building* first=nullptr;const Building* last=nullptr;int best=INT_MAX;
        for(auto& x:buildings_)if(x.kind==BuildingKind::PassengerStation&&x.component==a->component&&distance(x.cell,a->cell)<=64)
            for(auto& y:buildings_)if(y.kind==BuildingKind::PassengerStation&&y.component==b->component&&distance(y.cell,b->cell)<=64&&railway.local(x.id,y.id)){int score=distance(x.cell,a->cell)+distance(y.cell,b->cell);if(score<best){best=score;first=&x;last=&y;}}
        if(first&&best<distance(a->cell,b->cell)&&railway.requestPassenger(household,first->id,last->id,unsigned(distance(first->cell,a->cell)*4),unsigned(distance(last->cell,b->cell)*4)))return true;
    }
    int component=a?a->component:b->component;if(component<0)return false;
    auto nearest=[&](const std::vector<Cell>& candidates,Cell access){Cell result{};int best=INT_MAX;for(Cell c:candidates){int d=distance(c,access);if(d<best){best=d;result=c;}}return result;};
    Cell from=a?a->access:nearest(arrivals_[component],b->access),to=b?b->access:nearest(departures_[component],a->access);if(from.x<0||to.x<0)return false;
    CityTrip t{nextTrip_++,source,destination,household,kind,cargo,ticks_};
    if(!traffic.requestTrip({t.id,source?source:destination,from,to,kind}))return false;
    trips_.push_back(t);return true;
}
void CitySimulation::events(TrafficSimulation& traffic) {
    for(const auto& e:traffic.takeEvents()){
        auto it=std::find_if(trips_.begin(),trips_.end(),[&](const CityTrip& t){return t.id==e.id;});if(it==trips_.end())continue;
        auto t=*it;auto* a=mutableFind(t.source);auto* b=mutableFind(t.destination);
        if(e.completed){++stats_.completedTrips;tripSeconds_+=double(e.ticks)/30;
            if(t.kind==1){if(b){b->inventory+=t.cargo;if((!t.source||(a&&a->kind==BuildingKind::CargoTerminal))&&!sandbox)treasury-=t.cargo*0.5;++stats_.deliveries;}else if(a&&!t.destination){if(!sandbox)treasury+=t.cargo*0.5;++stats_.deliveries;}}
            if(t.kind==1&&a&&a->kind==BuildingKind::CargoTerminal&&b)railway.recordCargo(t.cargo);
            if(t.kind==7){if(b&&b->kind==BuildingKind::CargoTerminal)railExports_.push_back({t.source,t.destination,0,t.cargo});else if(a)a->inventory+=t.cargo;}
            if(b&&t.kind==2)b->waste=std::max(0.f,b->waste-80);
            if(b&&t.kind==3)b->health=std::min(100.f,b->health+60);
            if(b&&t.kind==4)b->fire=0;
            if(b&&t.kind==0){b->happiness=std::min(100.f,b->happiness+2);
                for(auto& h:households_)if(h.id==t.household){h.travelSeconds=float(ticks_-t.requested)/30;float expected=a?float(distance(a->cell,b->cell))*1.5f+15:30;h.commuteQuality=std::clamp(expected/std::max(expected,h.travelSeconds),0.25f,1.f);break;}}
        }else {++stats_.failedTrips;if((t.kind==1||t.kind==7)&&a)a->inventory+=t.cargo;if(t.household)for(auto& h:households_)if(h.id==t.household)h.commuteQuality=0.25f;}
        if(t.kind==1&&e.completed&&!b&&t.destination!=0&&a)a->inventory+=t.cargo;
        *it=trips_.back();trips_.pop_back();
    }
    stats_.averageTripSeconds=stats_.completedTrips?tripSeconds_/double(stats_.completedTrips):0;
}
void CitySimulation::cityStep(World& w,TrafficSimulation& traffic) {
    auto start=std::chrono::steady_clock::now(),phase=start;
    auto mark=[&](double& total){auto now=std::chrono::steady_clock::now();total+=std::chrono::duration<double,std::milli>(now-phase).count();phase=now;};
    bool ready=!(accessDirty_||topology_!=w.topologyRevision())||rebuild(w);
    mark(performance_.accessMs);
    if(!ready){++performance_.deferredSteps;return;}
    ++performance_.steps;
    const auto second=ticks_/30;
    regionalTraffic(w,traffic);
    struct Network {std::array<int,3> capacity{};int demand=0,population=0;std::array<int,6> serviceCapacity{};};
    std::vector<Network> nets(exits_.size());
    int workforce=0,jobs=0,pop=0;
    for(const auto& b:buildings_){pop+=b.residents;workforce+=b.residents/2;if(b.level&&zone(b.kind)&&!residential(b.kind))jobs+=definition(b.kind).capacity*b.level;
        if(b.component<0)continue;auto& n=nets[b.component];n.population+=b.residents;
        if(b.kind>=BuildingKind::Power&&b.kind<=BuildingKind::Sewage)n.capacity[int(b.kind)-int(BuildingKind::Power)]+=definition(b.kind).capacity;
        if(b.kind>=BuildingKind::Garbage&&b.kind<=BuildingKind::Park)n.serviceCapacity[int(b.kind)-int(BuildingKind::Garbage)]+=definition(b.kind).capacity;
        if(zone(b.kind))n.demand+=std::max(4,residential(b.kind)?b.residents:definition(b.kind).capacity*std::max(1,int(b.level)));
    }
    stats_.capacity.fill(0);stats_.consumption.fill(0);
    for(const auto& n:nets)for(int i=0;i<3;++i){stats_.capacity[i]+=n.capacity[i];stats_.consumption[i]+=n.demand;}
    mark(performance_.servicesMs);
    std::unordered_map<int,std::vector<Building*>> employers;
    for(auto& b:buildings_){b.workers=0;b.productivity=0;if(b.level&&(commercial(b.kind)||b.kind==BuildingKind::Industrial)&&b.component>=0)employers[b.component].push_back(&b);}
    std::unordered_map<int,size_t> cursor;
    std::unordered_map<uint64_t,float> commuteWait;
    for(const auto& t:trips_)if(t.household)commuteWait[t.household]=float(ticks_-t.requested)/30;
    for(auto j:railway.journeys())commuteWait[j.id]=float(ticks_-j.requested)/30;
    for(auto& h:households_){auto* home=mutableFind(h.home);h.workplace=0;if(!home)continue;h.members=home->residents;
        auto& list=employers[home->component];auto& index=cursor[home->component];
        while(index<list.size()&&list[index]->workers>=definition(list[index]->kind).capacity*list[index]->level)++index;
        if(index<list.size()){auto* job=list[index];int available=definition(job->kind).capacity*job->level-job->workers;int assigned=std::min(available,h.members/2);job->workers+=assigned;
            float expected=float(distance(home->cell,job->cell))*1.5f+15;
            float quality=std::min(h.commuteQuality,std::clamp(expected/std::max(expected,commuteWait[h.id]),0.25f,1.f));
            job->productivity+=assigned*quality;h.workplace=job->id;}}
    int employed=0;for(auto& b:buildings_){employed+=b.workers;b.productivity=b.workers?b.productivity/b.workers:0;}
    stats_.unemployment=workforce?1-float(employed)/workforce:0;
    stats_.demand[0]=std::clamp(0.6f+(jobs-workforce)/100.f-(taxes[0]-9)*0.08f,0.f,1.f);
    stats_.demand[1]=std::clamp(0.35f+(pop/4.f-jobs/2.f)/100.f-(taxes[1]-9)*0.08f,0.f,1.f);
    stats_.demand[2]=std::clamp(0.4f+(workforce-jobs)/100.f-(taxes[2]-9)*0.08f,0.f,1.f);
    mark(performance_.employmentMs);
    std::fill(roadPressure_.begin(),roadPressure_.end(),0.f);
    for(const auto& car:traffic.debugSnapshot())roadPressure_[tile(car.tile)]=std::min(1.f,roadPressure_[tile(car.tile)]+0.2f+0.6f*(1-car.speed/16));
    std::map<int,Building*> passengerAccess,freightAccess;
    for(auto& station:buildings_)if(station.component>=0&&railway.regional(station.id)&&std::min({station.utilities[0],station.utilities[1],station.utilities[2]})>=.95f){if(station.kind==BuildingKind::PassengerStation)passengerAccess.try_emplace(station.component,&station);if(station.kind==BuildingKind::CargoTerminal)freightAccess.try_emplace(station.component,&station);}
    int migrations=0;
    for(auto& b:buildings_){++b.age;b.utilities.fill(0);
        if(b.component>=0&&zone(b.kind))b.external=exits_[b.component].x>=0||(residential(b.kind)?passengerAccess.contains(b.component):freightAccess.contains(b.component));
        if(b.component>=0){auto& n=nets[b.component];for(int i=0;i<3;++i)b.utilities[i]=std::min(1.f,float(n.capacity[i])/std::max(1,n.demand));}
        float supply=*std::min_element(b.utilities.begin(),b.utilities.end());
        b.problem=b.component<0?"No adjacent local road":!b.external?"No outside road connection":supply<0.95f?"Insufficient utility capacity":"";
        if(!zone(b.kind))continue;
        if(!b.level&&b.component>=0&&b.external&&supply>=0.95f&&stats_.demand[demandCategory(b.kind)]>0.1f&&b.age>=5){b.level=1;b.distress=0;accessDirty_=true;++accessRevision_;if(residential(b.kind))households_.push_back({nextId_++,b.id,0,0});}
        if(!b.level){if(b.problem.empty())b.problem=b.age<5?"Preparing development":stats_.demand[demandCategory(b.kind)]<=0.1f?"Low demand for this zone":"Waiting for construction";continue;}
        b.noise=b.access.x>=0?roadPressure_[tile(b.access)]*50:0;
        auto service=[&](int i){return b.component>=0?b.services[i]*std::min(1.f,float(nets[b.component].serviceCapacity[i])/std::max(1,nets[b.component].population)):0.f;};
        b.waste=std::min(100.f,b.waste+0.025f*std::max(1,b.residents));
        b.health=std::clamp(b.health+(b.pollution<20?0.05f:-0.05f)-b.waste*0.001f+(service(1)>0?0.02f:0),0.f,100.f);
        b.crime=std::clamp(b.crime+(service(3)>0?-0.2f:0.015f),0.f,100.f);
        b.education=std::clamp(b.education+(service(4)>0?0.1f:-0.02f),0.f,100.f);
        if(pop>=500&&random()%100000==0)b.fire=1;
        if(b.fire>0)b.fire=std::min(100.f,b.fire+0.25f);
        b.landValue=std::clamp(45+service(5)*25+service(4)*10-b.pollution*0.4f-b.noise*0.2f,0.f,100.f);
        float target=75*supply+service(5)*10-b.pollution*0.15f-b.waste*0.12f-b.crime*0.15f-b.fire*0.5f-std::max(0,taxes[demandCategory(b.kind)]-9)*3;
        if(!b.external)target-=25;if(residential(b.kind))target-=stats_.unemployment*25;
        b.happiness=std::clamp(b.happiness+(target-b.happiness)*0.03f,0.f,100.f);
        if(residential(b.kind)){
            int cap=definition(b.kind).capacity*b.level;
            if(second%3==b.id%3&&b.residents<cap&&b.external&&supply>0.95f&&b.happiness>45&&stats_.demand[0]>0.1f&&migrations<32){bool roadOutside=exits_[b.component].x>=0;auto it=passengerAccess.find(b.component);if(roadOutside||(it!=passengerAccess.end()&&it->second->railPassengers>0)){++b.residents;++migrations;if(!roadOutside){--it->second->railPassengers;railway.recordPassenger();}}}
            if(b.happiness<25&&second%10==0&&b.residents>0)--b.residents;
        }else {
            if(b.workers==0&&pop>50)b.problem="Missing workers";
            if(b.kind==BuildingKind::Industrial&&b.workers&&supply>0.5f)b.inventory=std::min(500,b.inventory+std::max(1,int(b.workers*b.productivity/3)));
            if(commercial(b.kind)){if(b.inventory>0&&b.workers>0&&second%5==0)--b.inventory;else if(b.inventory==0){b.problem="Goods have not arrived";b.happiness=std::max(0.f,b.happiness-0.2f);}}
        }
        if(b.fire>0)b.problem="Fire: dispatch needs road access";else if(b.waste>60)b.problem="Garbage collection overdue";else if(b.health<40)b.problem="Healthcare needed";
        if(supply<0.5f||!b.external||b.happiness<20||b.fire>=90)++b.distress;else b.distress=std::max(0,b.distress-2);
        if(b.distress>120)b.problem="Deteriorating: restore access and services";
        if(b.distress>=300){b.level=0;b.residents=0;b.workers=0;b.inventory=0;b.age=0;b.distress=0;b.fire=0;b.waste=0;b.happiness=60;std::erase_if(households_,[&](const auto& h){return h.home==b.id;});for(auto& h:households_)if(h.workplace==b.id)h.workplace=0;b.problem="Abandoned; waiting for demand and services";}
        if(b.level==1&&b.age>120&&b.happiness>65&&b.landValue>=60&&supply>0.95f&&stats_.milestone>=2000){b.level=2;accessDirty_=true;++accessRevision_;}
    }
    updateRailServices(w);
    mark(performance_.developmentMs);
    // Persistent sampled commuting, freight and dispatched service work.
    for(auto& h:households_)if(h.workplace&&second%60==h.id%60)sendTrip(traffic,h.home,h.workplace,0,0,h.id);
    for(auto& b:buildings_)if(b.level&&b.component>=0){
        if(commercial(b.kind)&&b.inventory<20&&second%15==b.id%15){Building* source=nullptr;
            for(auto* f:employers[b.component])if(f->kind==BuildingKind::Industrial&&f->inventory>=20&&(!source||distance(f->cell,b.cell)<distance(source->cell,b.cell)))source=f;
            if(source){if(sendTrip(traffic,source->id,b.id,1,20))source->inventory-=20;}
            else if(freightAccess.contains(b.component)&&freightAccess[b.component]->inventory>=20){auto* terminal=freightAccess[b.component];if(sendTrip(traffic,terminal->id,b.id,1,20))terminal->inventory-=20;}
            else if(b.external&&sendTrip(traffic,0,b.id,1,20)){/* Imported goods are charged on successful arrival. */}}
        if(b.kind==BuildingKind::Industrial&&b.inventory>200&&second%30==b.id%30){
            bool shipped=false;if(freightAccess.contains(b.component)){auto* terminal=freightAccess[b.component];int waiting=0;for(auto shipment:railExports_)if(shipment.terminal==terminal->id)waiting+=shipment.amount;for(auto trip:trips_)if(trip.kind==7&&trip.destination==terminal->id)waiting+=trip.cargo;if(waiting<120)shipped=sendTrip(traffic,b.id,terminal->id,7,20);}
            if(!shipped)shipped=sendTrip(traffic,b.id,0,1,20);if(shipped)b.inventory-=20;
        }
        if(zone(b.kind))for(int k=0;k<3;++k){bool need=k==0?b.waste>40:k==1?b.health<50:b.fire>0;if(!need)continue;
            Building* best=nullptr;auto kind=BuildingKind(int(BuildingKind::Garbage)+k);
            for(auto& f:buildings_)if(f.kind==kind&&f.component==b.component&&std::min({f.utilities[0],f.utilities[1],f.utilities[2]})>0.5f&&(!best||distance(f.cell,b.cell)<distance(best->cell,b.cell)))best=&f;
            if(best){int active=0;for(auto& t:trips_)if(t.source==best->id)++active;if(active<4)sendTrip(traffic,best->id,b.id,uint8_t(2+k));}}
    }
    mark(performance_.dispatchMs);
    stats_.population=0;stats_.jobs=0;stats_.employed=0;stats_.happiness=0;
    double income=0,expenses=w.railUpkeep()+w.roadUpkeep()+(loanTaken?250:0);
    for(const auto& b:buildings_){stats_.population+=b.residents;stats_.happiness+=b.happiness*b.residents;stats_.employed+=b.workers;
        if(zone(b.kind)){int category=demandCategory(b.kind);if(residential(b.kind))income+=b.residents*taxes[0]*0.12;else if(b.level){stats_.jobs+=definition(b.kind).capacity*b.level;income+=b.workers*b.productivity*taxes[category]*0.4*(commercial(b.kind)&&b.inventory==0?0.2:1);}}
        else expenses+=definition(b.kind).upkeep;}
    if(stats_.population)stats_.happiness/=stats_.population;
    for(int m:{500,2000,5000,10000})if(stats_.population>=m)stats_.milestone=std::max(stats_.milestone,m);
    stats_.income=income;stats_.expenses=expenses;stats_.balance=income-expenses;
    if(second%60==0&&!sandbox){treasury+=stats_.balance;if(treasury<0)++insolvency_;else insolvency_=0;if(insolvency_>=3){bankrupt=true;paused=true;}}
    for(auto& h:households_)if(auto* b=mutableFind(h.home))h.members=b->residents;
    mark(performance_.financeMs);
    ++revision;publish(w);mark(performance_.publishMs);stats_.tickMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
void CitySimulation::tick(World& w,TrafficSimulation& traffic){
    auto phase=std::chrono::steady_clock::now();
    auto mark=[&](double& total){auto now=std::chrono::steady_clock::now();total+=std::chrono::duration<double,std::milli>(now-phase).count();phase=now;};
    if(accessDirty_||topology_!=w.topologyRevision())rebuild(w);
    mark(performance_.accessMs);++ticks_;++performance_.ticks;
    railway.tick(w,traffic);railEvents(w,traffic);mark(performance_.railMs);
    traffic.tick(w);mark(performance_.trafficMs);
    events(traffic);mark(performance_.eventsMs);
    if(ticks_%30==0)cityStep(w,traffic);
}
void CitySimulation::update(World& w,TrafficSimulation& traffic,double elapsed){
    if(!paused&&(accessDirty_||topology_!=w.topologyRevision()))rebuild(w);
    if(paused){accumulator_=0;refresh(w);return;}if(!std::isfinite(elapsed)||elapsed<0)return;
    accumulator_+=std::min(elapsed,0.25)*std::clamp(speed,1,3);int steps=0;
    while(accumulator_>=1.0/30&&steps++<24){tick(w,traffic);accumulator_-=1.0/30;if(paused)break;}
}
void CitySimulation::setOverlay(World& w,CityOverlay o){overlay_=o;w.clearTints();publish(w);}
void CitySimulation::publish(World& w){
    if(overlay_!=CityOverlay::None)for(int t=0;t<Cells;++t)if(w.road(t%MapSize,t/MapSize)){
        float v=1;int component=components_[t];
        if(overlay_==CityOverlay::Access)v=component>=0&&size_t(component)<exits_.size()&&exits_[component].x>=0?1.f:0.f;
        else if(overlay_==CityOverlay::Traffic)v=1-roadPressure_[t];
        else continue;
        w.setTileTint({t%MapSize,t/MapSize},v<0.4f?1:v<0.85f?2:3);
    }
    for(const auto& b:buildings_){uint8_t tint=0;
    float v=1;switch(overlay_){case CityOverlay::None:break;case CityOverlay::Access:v=b.external?1.f:0;break;case CityOverlay::Traffic:v=1-b.noise/50;break;
        case CityOverlay::Power:case CityOverlay::Water:case CityOverlay::Sewage:v=b.utilities[int(overlay_)-int(CityOverlay::Power)];break;
        case CityOverlay::Services:v=1-b.waste/100;break;case CityOverlay::Pollution:v=1-b.pollution/100;break;case CityOverlay::LandValue:v=b.landValue/100;break;
        case CityOverlay::Healthcare:case CityOverlay::Fire:case CityOverlay::Police:case CityOverlay::Education:case CityOverlay::Parks:v=b.services[int(overlay_)-int(CityOverlay::Healthcare)+1];break;}
    if(overlay_!=CityOverlay::None)tint=v<0.4f?1:v<0.85f?2:3;
    uint8_t variant=uint8_t(b.variant+(highDensity(b.kind)?4:0));for(auto p:facilityFootprint(b)){w.setTileTint(p,tint);w.setParcelVisual(p,{uint8_t(visualKind(b.kind)),b.level,variant,0});}}}
void CitySimulation::loadDefinitions(const std::filesystem::path& path){
    std::ifstream in(path);if(!in)throw std::runtime_error("Missing city definitions: "+path.string());auto candidate=definitions_;std::string line;size_t index=1;
    while(std::getline(in,line)){if(line.empty()||line[0]=='#')continue;if(index>=candidate.size())throw std::runtime_error("Too many building definitions");std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);auto& d=candidate[index++];
        if(!(row>>d.cost>>d.upkeep>>d.capacity>>d.unlock>>d.radius)||d.cost<0||d.cost>100000000||d.upkeep<0||d.upkeep>100000000||d.capacity>10000000||d.capacity<=0||d.radius<0||d.radius>512)throw std::runtime_error("Invalid building definition");}
    if(index!=candidate.size())throw std::runtime_error("Incomplete city definitions");definitions_=std::move(candidate);
}
}

namespace vc {
void CitySimulation::save(World& w,TrafficSimulation& traffic,const std::filesystem::path& path) {
    if(!w.vegetationEnabled())w.initializeVegetation();
    traffic.synchronize(w);events(traffic);refresh(w);
    using namespace binary;std::ostringstream out(std::ios::binary);w.writeRoads(out);
    write(out,treasury);for(auto t:taxes)write(out,t);write(out,uint8_t(paused));write(out,uint8_t(sandbox));write(out,uint8_t(loanTaken));write(out,uint8_t(bankrupt));write(out,speed);
    write(out,ticks_);write(out,nextId_);write(out,nextTrip_);write(out,random_);write(out,insolvency_);write(out,tripSeconds_);
    write(out,stats_.population);write(out,stats_.jobs);write(out,stats_.employed);write(out,stats_.milestone);
    write(out,stats_.income);write(out,stats_.expenses);write(out,stats_.balance);write(out,stats_.happiness);write(out,stats_.unemployment);
    for(auto v:stats_.demand)write(out,v);for(auto v:stats_.capacity)write(out,v);for(auto v:stats_.consumption)write(out,v);
    write(out,stats_.deliveries);write(out,stats_.failedTrips);write(out,stats_.completedTrips);
    for(const auto& d:definitions_){text(out,d.name);write(out,d.cost);write(out,d.upkeep);write(out,d.capacity);write(out,d.unlock);write(out,d.radius);}
    write(out,uint32_t(buildings_.size()));
    for(const auto& b:buildings_){write(out,b.id);write(out,b.cell.x);write(out,b.cell.z);write(out,uint8_t(b.kind));write(out,b.level);write(out,b.variant);write(out,b.productivity);
        for(int v:{b.residents,b.workers,b.inventory,b.distress,b.age})write(out,v);
        for(float v:{b.waste,b.health,b.fire,b.crime,b.education,b.happiness,b.landValue,b.pollution,b.noise})write(out,v);
        for(float v:b.utilities)write(out,v);for(float v:b.services)write(out,v);text(out,b.problem);}
    write(out,uint32_t(households_.size()));for(const auto& h:households_){write(out,h.id);write(out,h.home);write(out,h.workplace);write(out,h.members);write(out,h.commuteQuality);write(out,h.travelSeconds);}
    write(out,uint32_t(trips_.size()));for(const auto& t:trips_){write(out,t.id);write(out,t.source);write(out,t.destination);write(out,t.household);write(out,t.kind);write(out,t.cargo);write(out,t.requested);}
    traffic.saveState(out);w.writeVegetation(out);w.writeRails(out);railway.save(out);
    for(auto& b:buildings_)write(out,b.railPassengers);
    write(out,uint32_t(railExports_.size()));for(auto e:railExports_){write(out,e.source);write(out,e.terminal);write(out,e.train);write(out,e.amount);}
    std::string data=out.str();auto temporary=path;temporary+=L".tmp";
    try {std::ofstream f(temporary,std::ios::binary|std::ios::trunc);
        for(uint32_t v:{0x59544356u,7u,uint32_t(MapSize),uint32_t(MapSize),uint32_t(TileSize),checksum(data)})write(f,v);
        write(f,uint64_t(data.size()));f.write(data.data(),data.size());f.flush();if(!f)throw std::runtime_error("Could not write city save");f.close();
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Could not replace city save");
    }catch(...){std::error_code ec;std::filesystem::remove(temporary,ec);throw;}
}
void CitySimulation::load(World& w,TrafficSimulation& traffic,const std::filesystem::path& path) {
    using namespace binary;std::ifstream file(path,std::ios::binary);std::array<uint32_t,6> header;for(auto& v:header)v=read<uint32_t>(file);
    if(header[0]!=0x59544356u)throw std::runtime_error("Invalid VoxelCity save");
    auto candidateWorld=std::make_unique<World>();CitySimulation candidate;TrafficSimulation candidateTraffic({0,42,20000,64,true});
    if(header[1]<=2){candidateWorld->load(path);candidate.definitions_=definitions_;candidate.refresh(*candidateWorld);candidateWorld->initializeVegetation();}
    else {
        if((header[1]<3||header[1]>7)||header[2]!=MapSize||header[3]!=MapSize||header[4]!=TileSize)throw std::runtime_error("Unsupported city version");
        auto size=read<uint64_t>(file);if(size>512ull*1024*1024||std::filesystem::file_size(path)!=size+32)throw std::runtime_error("Invalid city payload size");
        std::string payload(size,'\0');file.read(payload.data(),size);if(!file||checksum(payload)!=header[5])throw std::runtime_error("City checksum mismatch");
        bool modernLayout=header[1]>=6;if(!modernLayout&&payload.size()>=size_t(Cells)*3){modernLayout=true;for(int i=0;i<Cells;++i){auto road=uint8_t(payload[i]),cls=uint8_t(payload[Cells*2+i]);if(cls>uint8_t(RoadClass::Median)||(road!=0)!=(cls!=0)){modernLayout=false;break;}}}
        std::istringstream in(payload,std::ios::binary);candidateWorld->readRoads(in,modernLayout);
        auto& c=candidate;c.treasury=read<double>(in);for(auto& t:c.taxes)t=read<int>(in);c.paused=read<uint8_t>(in)!=0;c.sandbox=read<uint8_t>(in)!=0;c.loanTaken=read<uint8_t>(in)!=0;c.bankrupt=read<uint8_t>(in)!=0;c.speed=read<int>(in);
        c.ticks_=read<uint64_t>(in);c.nextId_=read<uint64_t>(in);c.nextTrip_=read<uint64_t>(in);c.random_=read<uint32_t>(in);c.insolvency_=read<int>(in);c.tripSeconds_=read<double>(in);
        auto& s=c.stats_;s.population=read<int>(in);s.jobs=read<int>(in);s.employed=read<int>(in);s.milestone=read<int>(in);
        s.income=read<double>(in);s.expenses=read<double>(in);s.balance=read<double>(in);s.happiness=read<float>(in);s.unemployment=read<float>(in);
        for(auto& v:s.demand)v=read<float>(in);for(auto& v:s.capacity)v=read<int>(in);for(auto& v:s.consumption)v=read<int>(in);
        s.deliveries=read<uint64_t>(in);s.failedTrips=read<uint64_t>(in);s.completedTrips=read<uint64_t>(in);s.averageTripSeconds=s.completedTrips?c.tripSeconds_/double(s.completedTrips):0;
        auto readDefinition=[&](BuildingDefinition& d){d.name=text(in,256);d.cost=read<int>(in);d.upkeep=read<int>(in);d.capacity=read<int>(in);d.unlock=read<int>(in);d.radius=read<int>(in);if(d.cost<0||d.upkeep<0||d.capacity<0||d.capacity>10000000||d.unlock<0||d.radius<0||d.radius>512)throw std::runtime_error("Invalid saved definitions");};
        if(modernLayout){int definitions=header[1]>=7?int(BuildingKind::Count):int(BuildingKind::PassengerStation);for(int i=0;i<definitions;++i)readDefinition(c.definitions_[i]);}else {std::array<BuildingDefinition,13> legacy;for(auto& d:legacy)readDefinition(d);c.definitions_[0]=legacy[0];c.definitions_[size_t(BuildingKind::LowDensityResidential)]=legacy[1];c.definitions_[size_t(BuildingKind::LowDensityCommercial)]=legacy[2];c.definitions_[size_t(BuildingKind::Industrial)]=legacy[3];for(int old=4;old<13;++old)c.definitions_[old+2]=legacy[old];}
        auto count=[&](uint32_t max){auto n=read<uint32_t>(in);if(n>max)throw std::runtime_error("City array too large");return n;};
        c.buildings_.resize(count(Cells));
        for(auto& b:c.buildings_){b.id=read<uint64_t>(in);b.cell.x=read<int>(in);b.cell.z=read<int>(in);uint8_t rawKind=read<uint8_t>(in);if(!modernLayout)rawKind=rawKind<=1?rawKind:rawKind==2?uint8_t(BuildingKind::LowDensityCommercial):uint8_t(rawKind+2);b.kind=BuildingKind(rawKind);b.level=read<uint8_t>(in);b.variant=read<uint8_t>(in);b.productivity=read<float>(in);
            b.residents=read<int>(in);b.workers=read<int>(in);b.inventory=read<int>(in);b.distress=read<int>(in);b.age=read<int>(in);
            for(auto ptr:{&b.waste,&b.health,&b.fire,&b.crime,&b.education,&b.happiness,&b.landValue,&b.pollution,&b.noise})*ptr=read<float>(in);
            for(auto& v:b.utilities)v=read<float>(in);for(auto& v:b.services)v=read<float>(in);b.problem=text(in,512);
            if(!World::valid(b.cell.x,b.cell.z)||c.parcel_[tile(b.cell)]>=0||!b.id||b.id>=c.nextId_||c.ids_.contains(b.id))throw std::runtime_error("Invalid building identity / occupancy");
            size_t i=&b-c.buildings_.data();c.parcel_[tile(b.cell)]=int(i);c.ids_[b.id]=i;
        }
        c.households_.resize(count(Cells));for(auto& h:c.households_){h.id=read<uint64_t>(in);h.home=read<uint64_t>(in);h.workplace=read<uint64_t>(in);h.members=read<int>(in);h.commuteQuality=read<float>(in);h.travelSeconds=read<float>(in);}
        c.trips_.resize(count(1000000));for(auto& t:c.trips_){t.id=read<uint64_t>(in);t.source=read<uint64_t>(in);t.destination=read<uint64_t>(in);t.household=read<uint64_t>(in);t.kind=read<uint8_t>(in);t.cargo=read<int>(in);t.requested=read<uint64_t>(in);}
        c.validate(*candidateWorld);c.rebuild(*candidateWorld,true);c.publish(*candidateWorld);candidateTraffic.loadState(in,*candidateWorld);
        auto trafficIds=candidateTraffic.ownedTripIds();std::vector<uint64_t> cityIds;for(auto& t:c.trips_)cityIds.push_back(t.id);
        std::sort(trafficIds.begin(),trafficIds.end());std::sort(cityIds.begin(),cityIds.end());if(trafficIds!=cityIds)throw std::runtime_error("City / traffic ownership mismatch");
        if(header[1]>=5)candidateWorld->readVegetation(in);else candidateWorld->initializeVegetation();
        if(header[1]>=7){candidateWorld->readRails(in);c.railway.load(in,*candidateWorld);
            for(auto& b:c.buildings_){b.railPassengers=read<int>(in);if(b.railPassengers<0||b.railPassengers>120)throw std::runtime_error("Invalid station passengers");if(railFacility(b.kind))for(auto p:occupiedFootprint(b)){if(!World::valid(p.x,p.z)||(c.parcel_[tile(p)]>=0&&c.parcel_[tile(p)]!=int(c.ids_.at(b.id))))throw std::runtime_error("Overlapping station footprint");c.parcel_[tile(p)]=int(c.ids_.at(b.id));}}
            c.railExports_.resize(count(100000));for(auto& e:c.railExports_){e.source=read<uint64_t>(in);e.terminal=read<uint64_t>(in);e.train=read<uint64_t>(in);e.amount=read<int>(in);if(e.amount<=0||e.amount>120)throw std::runtime_error("Invalid rail cargo");}
            c.updateRailServices(*candidateWorld);c.publish(*candidateWorld);
        }
        if(in.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Unexpected data after city state");
    }
    // All parsing and validation precedes publication of the replacement city.
    w=std::move(*candidateWorld);traffic.swap(candidateTraffic);traffic.synchronize(w);*this=std::move(candidate);++revision;
}
void CitySimulation::validate(const World& w)const {
    if(!std::isfinite(treasury)||!std::isfinite(tripSeconds_)||speed<1||speed>3||!random_||!nextId_||!nextTrip_)throw std::runtime_error("Invalid city clock or treasury");
    for(int t:taxes)if(t<0||t>30)throw std::runtime_error("Invalid tax rate");
    std::unordered_set<uint64_t> allIds,homes,tripIds;
    for(const auto& b:buildings_){if(!World::valid(b.cell.x,b.cell.z)||w.roadOccupies(b.cell)||b.kind<=BuildingKind::None||b.kind>=BuildingKind::Count||b.level>2||b.variant>3||b.residents<0||b.workers<0||b.inventory<0||b.distress<0||b.age<0||b.residents>definition(b.kind).capacity*2||b.id>=nextId_||!allIds.insert(b.id).second)throw std::runtime_error("Invalid building state");
        for(float v:{b.productivity,b.waste,b.health,b.fire,b.crime,b.education,b.happiness,b.landValue,b.pollution,b.noise})if(!std::isfinite(v)||v<0||v>100)throw std::runtime_error("Invalid building condition");}
    for(const auto& h:households_){auto* b=find(h.home);if(!std::isfinite(h.commuteQuality)||h.commuteQuality<0.25f||h.commuteQuality>1||!std::isfinite(h.travelSeconds)||h.travelSeconds<0||!b||!residential(b->kind)||!b->level||h.members!=b->residents||h.id>=nextId_||!allIds.insert(h.id).second||!homes.insert(h.home).second)throw std::runtime_error("Invalid household");
        if(h.workplace){auto* job=find(h.workplace);if(!job||(!commercial(job->kind)&&job->kind!=BuildingKind::Industrial))throw std::runtime_error("Invalid workplace");}}
    for(const auto& b:buildings_)if(b.level&&residential(b.kind)&&!homes.contains(b.id))throw std::runtime_error("Home missing household");
    for(const auto& t:trips_)if(!t.id||t.id>=nextTrip_||!tripIds.insert(t.id).second||t.kind>7||t.cargo<0||t.cargo>500||t.requested>ticks_)throw std::runtime_error("Invalid owned trip");
}
void CitySimulation::scenario(World& w,TrafficSimulation& traffic,int population){
    auto defs=definitions_;*this=CitySimulation(42);definitions_=defs;w.generateScenario(0);TrafficSimulation empty({0,42,20000,64,true});traffic.swap(empty);
    w.initializeVegetation();w.generateRegionalHighway();w.generateRegionalRailway();
    w.stroke({HighwayJunctionX,216},{HighwayJunctionX,300},true);
    int rows=population>=10000?18:population>0?12:4;
    for(int r=0;r<rows;++r){int z=220+r*3;w.stroke({240,z},{population>=10000?335:270,z},true);}
    w.stroke({240,220},{240,220+(rows-1)*3},true);w.stroke({population>=10000?335:270,220},{population>=10000?335:270,220+(rows-1)*3},true);
    if(population>=10000)for(int x:{272,296,320})w.stroke({x,220},{x,271},true);
    auto add=[&](Cell cell,BuildingKind kind,int level,int residents){Building b;b.id=nextId_++;b.cell=cell;b.kind=kind;b.level=uint8_t(level);b.variant=uint8_t(random()%4);b.residents=residents;b.age=150;b.inventory=commercial(kind)?100:kind==BuildingKind::Industrial?200:0;
        ids_[b.id]=buildings_.size();parcel_[tile(cell)]=int(buildings_.size());buildings_.push_back(b);if(residential(kind)&&level)households_.push_back({nextId_++,b.id,0,residents});};
    int remaining=population,industrial=0,commercial=0;
    for(int r=0;r<rows;++r)for(int x=241;x<(population>=10000?335:270);++x){if(w.road(x,221+r*3))continue;Cell c{x,221+r*3};BuildingKind kind=x%5==0?BuildingKind::Industrial:x%7==0?BuildingKind::Commercial:BuildingKind::Residential;
        if(population>=10000&&kind==BuildingKind::Industrial&&industrial>=190)continue;
        if(population>=10000&&kind==BuildingKind::Commercial&&commercial>=140)continue;
        if(kind==BuildingKind::Industrial)++industrial;if(kind==BuildingKind::Commercial)++commercial;
        int residents=kind==BuildingKind::Residential?std::min(remaining,population>=10000?16:8):0;remaining-=residents;
        if(kind==BuildingKind::Residential&&population>0&&residents==0)continue;
        add(c,kind,population>0?(population>=10000?2:1):0,residents);}
    int groups=population>=10000?9:1;
    for(int g=0;g<groups;++g)for(int k=int(BuildingKind::Power);k<=int(BuildingKind::Park);++k){Cell c{241+(g%3)*32+(k-int(BuildingKind::Power)),219+(g/3)*18};if(w.road(c.x,c.z)||at(c))c={251+(g%3)*32+(k-int(BuildingKind::Power)),219+(g/3)*18};if(!w.road(c.x,c.z)&&!at(c))add(c,BuildingKind(k),1,0);}
    if(population>=10000)stats_.milestone=10000;else if(population>=500)stats_.milestone=500;
    rebuild(w,true);railway.seedRegional(w);regionalTraffic(w,traffic,true);cityStep(w,traffic);publish(w);
}
}

namespace vc {
void CitySimulation::prepareTrafficStress(World& w){
    for(int z=0;z<MapSize;z+=8)if(z!=RegionalRailRow)w.stroke({352,z},{511,z},true);
    for(int x=352;x<MapSize;x+=8)w.stroke({x,0},{x,511},true);
    w.stroke({335,220},{352,220},true);accessDirty_=true;++accessRevision_;
}
void CitySimulation::replenishTrafficStress(TrafficSimulation& traffic,size_t target){
    // Explicit diagnostic trips stress a road grid next to the representative city.
    size_t pending=traffic.outstandingTrips();
    for(size_t i=pending;i<target+256&&i<pending+128;++i){
        Cell a{352+int(random()%20)*8,int(random()%512)},b{352+int(random()%20)*8,int(random()%512)};
        if(a==b)continue;CityTrip t{nextTrip_++,0,0,0,5,0,ticks_};
        if(traffic.requestTrip({t.id,0,a,b,5}))trips_.push_back(t);
    }
}
}

namespace vc {
void CitySimulation::newCity(World& w,TrafficSimulation& traffic){
    auto defs=definitions_;auto seed=random_;*this=CitySimulation(seed);definitions_=std::move(defs);
    w.generateScenario(0);w.initializeVegetation();w.generateRegionalHighway();w.generateRegionalRailway();TrafficSimulation empty({0,seed,20000,64,true});traffic.swap(empty);
    rebuild(w,true);railway.seedRegional(w);regionalTraffic(w,traffic,true);publish(w);++revision;
}
size_t CitySimulation::regionalTrips()const {return std::count_if(trips_.begin(),trips_.end(),[](const CityTrip& t){return t.kind==6;});}
void CitySimulation::regionalTraffic(World& w,TrafficSimulation& traffic,bool seed){
    if(!w.highwayCount())return;
    auto request=[&](Cell from,Cell to){if(!w.highway(from)||!w.highway(to))return;CityTrip trip{nextTrip_++,0,0,0,6,0,ticks_};if(traffic.requestTrip({trip.id,0,from,to,6}))trips_.push_back(trip);};
    if(seed){
        // Existing regional traffic is distributed along the highway on a new map.
        for(int x=8;x<MapSize-8;x+=16){request({x,HighwayEastRow},{MapSize-1,HighwayEastRow});request({x,HighwayWestRow},{0,HighwayWestRow});}
    }else if(ticks_%180==0&&regionalTrips()<100){
        request({0,HighwayEastRow},{MapSize-1,HighwayEastRow});
        request({MapSize-1,HighwayWestRow},{0,HighwayWestRow});
    }
}
}
