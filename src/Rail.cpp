#include "Rail.h"
#include "Binary.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <set>
#include <stdexcept>
namespace vc {
namespace {
int index(Cell c){return c.z*MapSize+c.x;}
std::vector<int> search(const World& w,Cell start,Cell* exit=nullptr){
    std::vector<int> parent(MapSize*MapSize,-1);if(!w.hasRail(start))return parent;
    std::vector<int> q{index(start)};parent[q[0]]=q[0];for(size_t i=0;i<q.size();++i){int t=q[i];Cell c{t%MapSize,t/MapSize};if(exit&&exit->x<0&&(c.x==0||c.z==0||c.x==MapSize-1||c.z==MapSize-1))*exit=c;for(int d=0;d<4;++d)if(w.rail(c).links&(1<<d)){Cell n{c.x+RoadDX[d],c.z+RoadDZ[d]};if(!World::valid(n.x,n.z)||!w.hasRail(n))continue;int j=index(n);if(parent[j]<0){parent[j]=t;q.push_back(j);}}}return parent;
}
std::vector<Cell> path(const std::vector<int>& p,Cell to){std::vector<Cell> out;int t=index(to);if(t<0||t>=int(p.size())||p[t]<0)return out;for(int steps=0;steps<MapSize*MapSize;++steps){out.push_back({t%MapSize,t/MapSize});if(p[t]==t)break;t=p[t];}std::reverse(out.begin(),out.end());return out;}
std::vector<Cell> platformRoute(std::vector<Cell> route,const RailStation* source,const RailStation* destination){
    if(route.size()<2)return route;
    auto endFor=[](const RailStation& s,Cell direction){return (s.last.x-s.first.x)*direction.x+(s.last.z-s.first.z)*direction.z>0?s.last:s.first;};
    if(source&&source->first.x>=0){Cell d{route[1].x-route[0].x,route[1].z-route[0].z};Cell end=endFor(*source,d);auto it=std::find(route.begin(),route.end(),end);if(it!=route.end())route.erase(route.begin(),it);}
    if(destination&&destination->first.x>=0&&route.size()>=2){Cell last=route.back(),before=route[route.size()-2],d{last.x-before.x,last.z-before.z},end=endFor(*destination,d);for(int i=0;i<3&&last!=end;++i){last={last.x+d.x,last.z+d.z};route.push_back(last);}}
    return route;
}
Cell trainCell(const RailTrain& t,int i){
    if(i<0){auto a=t.route[0],b=t.route[1];return {a.x+(b.x-a.x)*i,a.z+(b.z-a.z)*i};}
    if(i>=int(t.route.size())){auto b=t.route.back(),a=t.route[t.route.size()-2];int extra=i-int(t.route.size())+1;return {b.x+(b.x-a.x)*extra,b.z+(b.z-a.z)*extra};}return t.route[i];
}
float routeLength(const RailTrain& t){return float(t.route.size()-1)*TileSize;}
}
void RailSimulation::setStations(std::vector<RailStation> s){for(auto station:s)if(!station.id||!World::valid(station.cell.x,station.cell.z)||station.kind<1||station.kind>3)throw std::invalid_argument("Invalid rail station");if(s!=stations_){stations_=std::move(s);dirty_=true;}}
bool RailSimulation::regional(uint64_t s)const{for(auto& r:network_.services)if(!r.from&&r.to==s)return true;return false;}
bool RailSimulation::local(uint64_t a,uint64_t b)const{return a!=b&&network_.next.contains({a,b});}
bool RailSimulation::requestPassenger(uint64_t id,uint64_t a,uint64_t b,unsigned accessSeconds,unsigned egressSeconds){if(!local(a,b)||journeys_.size()>=10000)return false;for(auto j:journeys_)if(j.id==id)return true;journeys_.push_back({id,a,b,0,ticks_,ticks_+accessSeconds*30,uint32_t(egressSeconds*30),false});return true;}
void RailSimulation::seedRegional(const World& w){
    if(!w.hasRail({0,RegionalRailRow})||!trains_.empty())return;
    for(int direction=0;direction<2;++direction)for(int offset:{32,192,352}){RailTrain t;t.id=nextId_++;t.kind=direction?2:1;for(int x=0;x<MapSize;++x)t.route.push_back({direction?MapSize-1-x:x,RegionalRailRow});t.distance=float(offset*16);trains_.push_back(std::move(t));}
}
struct RailSimulation::Build {
    struct Edge{uint64_t a,b;std::vector<Cell> route;};
    std::shared_ptr<const World> world;std::vector<RailStation> stations;Network result;
    std::vector<Edge> edges;std::set<uint64_t> depotReach;size_t cursor=0;bool initialized=false;
    bool step(){
        if(!NetworkWorker::instance().isWorkerThread())throw std::logic_error("Rail routes must run on the network worker");
        const auto& w=*world;
        if(!initialized){initialized=true;if(w.hasRail({0,RegionalRailRow})&&w.hasRail({MapSize-1,RegionalRailRow})){auto p=path(search(w,{0,RegionalRailRow}),{MapSize-1,RegionalRailRow});if(!p.empty()){result.services.push_back({0,0,1,p});std::reverse(p.begin(),p.end());result.services.push_back({0,0,2,p});}}return false;}
        if(cursor<stations.size()){
            auto s=stations[cursor++];if(!s.active)return false;Cell boundary;auto parents=search(w,s.cell,&boundary);
            if(s.kind==3){for(auto& b:stations)if(b.active&&b.kind==1&&parents[index(b.cell)]>=0)depotReach.insert(b.id);return false;}
            if(boundary.x>=0){auto best=path(parents,boundary);std::reverse(best.begin(),best.end());result.services.push_back({0,s.id,s.kind,platformRoute(std::move(best),nullptr,&s)});}
            if(s.kind==1)for(auto& b:stations)if(b.active&&b.kind==1&&b.id>s.id){auto p=path(parents,b.cell);if(!p.empty())edges.push_back({s.id,b.id,std::move(p)});}return false;
        }
        std::sort(edges.begin(),edges.end(),[](auto& a,auto& b){return a.route.size()!=b.route.size()?a.route.size()<b.route.size():std::pair{a.a,a.b}<std::pair{b.a,b.b};});
        std::map<uint64_t,uint64_t> parent;for(auto s:stations)parent[s.id]=s.id;auto root=[&](uint64_t a){while(parent[a]!=a)a=parent[a];return a;};std::map<uint64_t,std::vector<uint64_t>> adjacent;
        for(auto& e:edges)if(depotReach.contains(e.a)&&root(e.a)!=root(e.b)){parent[root(e.a)]=root(e.b);auto a=std::find_if(stations.begin(),stations.end(),[&](auto s){return s.id==e.a;}),b=std::find_if(stations.begin(),stations.end(),[&](auto s){return s.id==e.b;});result.services.push_back({e.a,e.b,3,platformRoute(e.route,&*a,&*b)});std::reverse(e.route.begin(),e.route.end());result.services.push_back({e.b,e.a,3,platformRoute(e.route,&*b,&*a)});adjacent[e.a].push_back(e.b);adjacent[e.b].push_back(e.a);}
        for(auto& [start,neighbors]:adjacent){std::vector<uint64_t> q{start};std::map<uint64_t,uint64_t> first;first[start]=start;for(size_t i=0;i<q.size();++i)for(auto n:adjacent[q[i]])if(!first.contains(n)){first[n]=q[i]==start?n:first[q[i]];result.next[{start,n}]=first[n];q.push_back(n);}}return true;
    }
};
void RailSimulation::refresh(const World& w){
    if(dirty_||topology_!=w.railRevision())network_={};
    if(job_&&(job_->revision!=w.railRevision()||job_->stations!=stations_))job_.reset();
    if((dirty_||topology_!=w.railRevision())&&!job_){auto j=std::make_shared<Job>();j->revision=w.railRevision();j->stations=stations_;j->work=std::make_shared<Build>();j->work->world=std::make_shared<World>(w);j->work->stations=stations_;job_=j;}
    while(job_){
        if(!job_->future.valid()){auto work=job_->work;job_->future=NetworkWorker::instance().submit([work]{return work->step();},job_->life.cancelled);}
        if(!networkReady(job_->future))return;
        if(job_->future.get()){network_=std::move(job_->work->result);topology_=job_->revision;dirty_=false;job_.reset();return;}
        if(!NetworkWorker::blocking)return;
    }
}
void RailSimulation::tick(World& w,TrafficSimulation& traffic){
    ++ticks_;bool changed=dirty_||topology_!=w.railRevision();refresh(w);
    if(!dirty_&&topology_==w.railRevision())for(auto& j:journeys_)if(!j.train&&!j.arrived&&!local(j.from,j.to)){events_.push_back({0,0,j.id,0,false});j.id=0;}
    // Existing routes survive service refreshes only while every physical connection and destination remains valid.
    for(auto it=trains_.begin();it!=trains_.end();){bool valid=true;if(changed)for(size_t i=0;i<it->route.size();++i){auto c=it->route[i];if(!w.hasRail(c)){valid=false;break;}if(i){auto a=it->route[i-1];int d=0;for(;d<4;++d)if(Cell{a.x+RoadDX[d],a.z+RoadDZ[d]}==c)break;if(d==4||!(w.rail(a).links&(1<<d))){valid=false;break;}}}
        if(it->to)valid&=std::any_of(stations_.begin(),stations_.end(),[&](auto s){return s.id==it->to&&s.active;});
        if(!valid){events_.push_back({it->id,0,0,0,false});for(auto& j:journeys_)if(j.train==it->id){events_.push_back({0,0,j.id,0,false});j.id=0;}it=trains_.erase(it);}else ++it;
    }
    std::erase_if(journeys_,[](auto j){return !j.id;});
    if(!dirty_&&topology_==w.railRevision())for(auto& s:network_.services){auto key=std::pair{s.from,s.to?s.to:uint64_t(s.kind)};if(ticks_-dispatched_[key]<1800)continue;bool exists=std::any_of(trains_.begin(),trains_.end(),[&](auto& t){return t.from==s.from&&t.to==s.to&&t.kind==s.kind;});if(exists||trains_.size()>=256)continue;bool occupiedStart=false;for(auto& train:trains_){int head=std::min(int(train.distance/16),int(train.route.size())-1);for(int k=std::max(0,head-4);k<=std::min(head+4,int(train.route.size())-1);++k)if(train.route[k]==s.route.front())occupiedStart=true;}if(occupiedStart)continue;RailTrain t;t.id=nextId_++;t.from=s.from;t.to=s.to;t.kind=s.kind;t.route=s.route;if(t.route.size()<2)continue;trains_.push_back(std::move(t));dispatched_[key]=ticks_;}
    // Directional track blocks allow opposing trains on the two independent running tracks. Junctions are exclusive.
    std::map<std::pair<int,int>,uint64_t> occupied;std::vector<Cell> gates;
    auto block=[&](const RailTrain& t,int i){auto c=trainCell(t,i);int degree=0;for(int d=0;d<4;++d)degree+=(w.rail(c).links>>d)&1;int lane=0;if(degree<=2){auto a=trainCell(t,i-1),b=trainCell(t,i+1);lane=(b.x>a.x||b.z>a.z)?1:2;}return std::pair{index(c),lane};};
    for(auto& t:trains_){int head=int(t.distance/16);for(int i=head-4;i<=std::min(head+1,int(t.route.size())-1);++i){auto c=trainCell(t,i);if(World::valid(c.x,c.z))occupied[block(t,i)]=t.id;}}
    bool approachingCrossing=false;for(auto& t:trains_){int head=int(t.distance/16);for(int i=head;i<=std::min(head+5,int(t.route.size())-1);++i){auto c=t.route[i];approachingCrossing|=w.road(c.x,c.z)&&w.rail(c).height<1;}}
    std::set<int> carTiles;if(approachingCrossing)for(auto c:traffic.debugSnapshot())if(w.hasRail(c.tile))carTiles.insert(index(c.tile));
    for(auto& t:trains_){int head=int(t.distance/16);bool blocked=false;
        for(int i=head-4;i<=std::min(head+5,int(t.route.size())-1);++i){Cell c=trainCell(t,i);if(w.road(c.x,c.z)&&w.rail(c).height<1){gates.push_back(c);if(i>head&&carTiles.contains(index(c)))blocked=true;}
            if(i>head){auto k=block(t,i);auto f=occupied.find(k);if(f!=occupied.end()&&f->second!=t.id)blocked=true;}
        }
        if(t.dwell){--t.dwell;continue;}
        if(blocked)continue;
        for(int i=head;i<=std::min(head+5,int(t.route.size())-1);++i)occupied[block(t,i)]=t.id;
        if(t.kind==3){for(auto& j:journeys_)if(!j.train&&!j.arrived&&ticks_>=j.ready&&j.from==t.from&&network_.next.contains({j.from,j.to})&&network_.next.at({j.from,j.to})==t.to&&t.distance<1){size_t load=std::count_if(journeys_.begin(),journeys_.end(),[&](auto a){return a.train==t.id;});if(load<120)j.train=t.id;}}
        t.distance+=24.f/30;
        if(t.distance>=routeLength(t)){
            if(t.to&&!t.stopped){t.distance=routeLength(t);t.dwell=150;t.stopped=true;events_.push_back({t.id,t.to,0,t.kind,true});
                for(auto& j:journeys_)if(j.train==t.id){j.train=0;j.from=t.to;if(j.to==t.to){j.arrived=true;j.ready=ticks_+j.egress;}}
            }else if(t.to&&t.kind!=3&&t.stopped){RailTrain returning=t;std::reverse(returning.route.begin(),returning.route.end());auto station=std::find_if(stations_.begin(),stations_.end(),[&](auto s){return s.id==t.to;});if(station!=stations_.end())returning.route=platformRoute(std::move(returning.route),&*station,nullptr);
                if(returning.route.size()<2){events_.push_back({t.id,0,0,4,true});++stats_.completed;t.id=0;continue;}
                bool clear=true;for(int i=-4;i<=std::min(5,int(returning.route.size())-1);++i){auto c=trainCell(returning,i);if(!World::valid(c.x,c.z))continue;auto found=occupied.find(block(returning,i));if(found!=occupied.end()&&found->second!=t.id)clear=false;}
                if(!clear){t.distance=routeLength(t);continue;}for(int i=-4;i<=std::min(5,int(returning.route.size())-1);++i){auto c=trainCell(returning,i);if(World::valid(c.x,c.z))occupied[block(returning,i)]=t.id;}
                t.route=std::move(returning.route);t.distance=0;t.from=t.to;t.to=0;}
            else {events_.push_back({t.id,0,0,4,true});++stats_.completed;t.id=0;}
        }
    }
    for(auto& j:journeys_)if(j.arrived&&ticks_>=j.ready){events_.push_back({0,j.to,j.id,3,true,float(ticks_-j.requested)/30});j.id=0;++stats_.passengers;}
    std::erase_if(trains_,[](auto& t){return !t.id;});std::erase_if(journeys_,[](auto j){return !j.id;});w.closeCrossings(gates);
}
std::span<const CarInstance> RailSimulation::instances(const World& w){
    instances_.clear();for(auto& t:trains_)for(int car=0;car<4;++car){float distance=t.distance-car*12.f;int i=int(std::floor((distance+8)/16));float f=(distance+8)/16-i;
        int at=std::clamp(i,0,int(t.route.size())-1);Cell c=t.route[at],prev=at?t.route[at-1]:Cell{c.x-(t.route[1].x-c.x),c.z-(t.route[1].z-c.z)},next=at+1<int(t.route.size())?t.route[at+1]:Cell{c.x+c.x-prev.x,c.z+c.z-prev.z};
        float ix=float(c.x-prev.x),iz=float(c.z-prev.z),ox=float(next.x-c.x),oz=float(next.z-c.z),x=0,z=0,dx=ox,dz=oz;
        if(i!=at)f=(distance+8)/16-at;
        if(ix!=ox||iz!=oz){float angle=f*1.57079632679f,cs=std::cos(angle),sn=std::sin(angle);x=c.x*16+8-ix*8+ox*8-ox*8*cs+ix*8*sn;z=c.z*16+8-iz*8+oz*8-oz*8*cs+iz*8*sn;dx=ox*sn+ix*cs;dz=oz*sn+iz*cs;}
        else {x=c.x*16+8+dx*(f*16-8);z=c.z*16+8+dz*(f*16-8);}
        x+=dz*4;z-=dx*4;float h=w.rail(c).height,before=w.hasRail(prev)?w.rail(prev).height:h,after=w.hasRail(next)?w.rail(next).height:h;
        CarInstance instance{x,z,dx,dz,uint32_t(t.kind==2?2:1),0x4000000000000000ull+t.id*4+car,0};instance.y=f<.5f?h+(before-h)*(.5f-f):h+(after-h)*(f-.5f);instance.vehicle=car==0?1:t.kind==2?3:2;instances_.push_back(instance);
    }return instances_;
}
void RailSimulation::save(std::ostream& out)const{using namespace binary;write(out,ticks_);write(out,nextId_);write(out,stats_.passengers);write(out,stats_.cargo);write(out,stats_.completed);
    write(out,uint32_t(trains_.size()));for(auto& t:trains_){write(out,t.id);write(out,t.from);write(out,t.to);write(out,t.kind);write(out,t.distance);write(out,t.dwell);write(out,uint8_t(t.stopped));write(out,uint32_t(t.route.size()));for(auto c:t.route){write(out,c.x);write(out,c.z);}}
    write(out,uint32_t(journeys_.size()));for(auto j:journeys_){write(out,j.id);write(out,j.from);write(out,j.to);write(out,j.train);write(out,j.requested);write(out,j.ready);write(out,j.egress);write(out,uint8_t(j.arrived));}
    write(out,uint32_t(dispatched_.size()));for(auto [k,v]:dispatched_){write(out,k.first);write(out,k.second);write(out,v);}
}
void RailSimulation::load(std::istream& in,World& w){using namespace binary;ticks_=read<uint64_t>(in);nextId_=read<uint64_t>(in);stats_.passengers=read<uint64_t>(in);stats_.cargo=read<uint64_t>(in);stats_.completed=read<uint64_t>(in);auto count=[&](uint32_t max){auto n=read<uint32_t>(in);if(n>max)throw std::runtime_error("Rail state exceeds limits");return n;};std::set<uint64_t> ids;
    trains_.resize(count(256));for(auto& t:trains_){t.id=read<uint64_t>(in);t.from=read<uint64_t>(in);t.to=read<uint64_t>(in);t.kind=read<uint8_t>(in);t.distance=read<float>(in);t.dwell=read<int>(in);t.stopped=read<uint8_t>(in)!=0;t.route.resize(count(MapSize*MapSize));for(auto& c:t.route){c.x=read<int>(in);c.z=read<int>(in);if(!World::valid(c.x,c.z)||!w.hasRail(c))throw std::runtime_error("Invalid train route");}if(!t.id||t.id>=nextId_||!ids.insert(t.id).second||t.kind<1||t.kind>3||t.route.size()<2||!std::isfinite(t.distance)||t.distance<0||t.distance>routeLength(t)+1||t.dwell<0||t.dwell>150)throw std::runtime_error("Invalid train state");
        for(size_t i=1;i<t.route.size();++i){auto a=t.route[i-1],b=t.route[i];int d=0;for(;d<4;++d)if(Cell{a.x+RoadDX[d],a.z+RoadDZ[d]}==b)break;if(d==4||!(w.rail(a).links&(1<<d)))throw std::runtime_error("Disconnected train route");}
    }
    journeys_.resize(count(10000));std::set<uint64_t> journeyIds;for(auto& j:journeys_){j.id=read<uint64_t>(in);j.from=read<uint64_t>(in);j.to=read<uint64_t>(in);j.train=read<uint64_t>(in);j.requested=read<uint64_t>(in);j.ready=read<uint64_t>(in);j.egress=read<uint32_t>(in);j.arrived=read<uint8_t>(in)!=0;if(j.requested>ticks_||j.egress>100000||!j.id||!journeyIds.insert(j.id).second||(j.train&&!ids.contains(j.train)))throw std::runtime_error("Invalid train passenger");}
    auto n=count(100000);for(uint32_t i=0;i<n;++i){auto a=read<uint64_t>(in),b=read<uint64_t>(in),v=read<uint64_t>(in);if(v>ticks_)throw std::runtime_error("Invalid railway dispatch clock");dispatched_[{a,b}]=v;}dirty_=true;
    std::vector<Cell> crossings;for(auto& t:trains_){int head=int(t.distance/16);for(int i=std::max(0,head-4);i<=std::min(head+5,int(t.route.size())-1);++i)crossings.push_back(t.route[i]);}w.closeCrossings(crossings);
}
}
