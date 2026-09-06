#include "Traffic.h"
#include "NetworkWorker.h"
#include "Binary.h"
#include <deque>
#include <sstream>
#include <unordered_set>
#include <immintrin.h>
#include <malloc.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <random>
#include <stdexcept>
#include <unordered_map>

namespace vc {
namespace {
constexpr int Cells=MapSize*MapSize;
constexpr float Dt=1.f/30, Gap=8, Half=3;
// A rotating rear corner needs more clearance than half the car's length.
constexpr float TailClearance=4.5f;
constexpr auto& DX=RoadDX;constexpr auto& DZ=RoadDZ;
int neighbor(int tile,int dir) {
    int x=tile%MapSize+DX[dir],z=tile/MapSize+DZ[dir];
    return World::valid(x,z)?z*MapSize+x:-1;
}
int direction(int from,int to) {
    int x=(to%MapSize>from%MapSize)-(to%MapSize<from%MapSize),z=(to/MapSize>from/MapSize)-(to/MapSize<from/MapSize);
    for(int d=0;d<8;++d)if(DX[d]==x&&DZ[d]==z)return d;return 0;
}
int manhattan(int a,int b) {return std::abs(a%MapSize-b%MapSize)+std::abs(a/MapSize-b/MapSize);}
struct FloatArray {
    float* data=nullptr;
    ~FloatArray(){_aligned_free(data);}
    void grow(size_t old,size_t size) {
        auto* next=static_cast<float*>(_aligned_malloc(size*sizeof(float),32));
        if(!next) throw std::bad_alloc();
        std::fill_n(next,size,0.f);if(data) std::copy_n(data,old,next);
        _aligned_free(data);data=next;
    }
    float& operator[](size_t i){return data[i];}
    float operator[](size_t i) const{return data[i];}
};
struct Point {float x,z;};
struct Path {
    std::array<Point,33> point{};
    std::array<float,33> distance{};
    float length=0;
    void sample(float s,Point& p,Point& heading) const {
        auto end=std::upper_bound(distance.begin(),distance.end(),std::clamp(s,0.f,length));
        int i=std::clamp(int(end-distance.begin())-1,0,31);
        float len=distance[i+1]-distance[i],t=std::clamp((s-distance[i])/len,0.f,1.f);
        heading={(point[i+1].x-point[i].x)/len,(point[i+1].z-point[i].z)/len};
        p={point[i].x+(point[i+1].x-point[i].x)*t,point[i].z+(point[i+1].z-point[i].z)*t};
    }
};
// Tile-local lane paths. Coordinates use screen-independent x/z right-hand lanes.
Path makePath(int in,int out) {
    float ni=in<4?1.f:0.70710678f,no=out<4?1.f:0.70710678f;
    Point hi{DX[in]*ni,DZ[in]*ni},ho{DX[out]*no,DZ[out]*no};
    Path p;Point a{8.f-DX[in]*8.f-hi.z*3.f,8.f-DZ[in]*8.f+hi.x*3.f};
    Point b{8.f+DX[out]*8.f-ho.z*3.f,8.f+DZ[out]*8.f+ho.x*3.f};
    for(int i=0;i<=32;++i) {
        float t=float(i)/32,u=1-t;
        if(in==out) p.point[i]={a.x+(b.x-a.x)*t,a.z+(b.z-a.z)*t};
        else {
            float handle=out==(oppositeDirection(in))?10.f:((DX[in]*DZ[out]-DZ[in]*DX[out])>0?5.f:11.f)*0.55228475f;
            Point c{a.x+hi.x*handle,a.z+hi.z*handle},d{b.x-ho.x*handle,b.z-ho.z*handle};
            p.point[i]={u*u*u*a.x+3*u*u*t*c.x+3*u*t*t*d.x+t*t*t*b.x,u*u*u*a.z+3*u*u*t*c.z+3*u*t*t*d.z+t*t*t*b.z};
        }
        if(i) {auto q=p.point[i-1];auto r=p.point[i];p.distance[i]=p.distance[i-1]+std::hypot(r.x-q.x,r.z-q.z);}
    }
    p.length=p.distance.back();return p;
}
Path makeRoundPath(int index,int in,int out){
    Path p=makePath(in,out);if(index==4||in>=4||out>=4)return p;
    Point center{24.f-(index%3)*16,24.f-(index/3)*16};
    auto internal=[&](int side){int x=index%3+DX[side],z=index/3+DZ[side];return x>=0&&x<3&&z>=0&&z<3&&!(x==1&&z==1);};
    auto crossing=[&](int side){Point q{};
        if(side==1||side==3){q.x=side==1?16.f:0.f;q.z=center.z+(index/3==0?-1.f:1.f)*std::sqrt(std::max(0.f,256-(q.x-center.x)*(q.x-center.x)));}
        else {q.z=side==2?16.f:0.f;q.x=center.x+(index%3==0?-1.f:1.f)*std::sqrt(std::max(0.f,256-(q.z-center.z)*(q.z-center.z)));}
        return q;
    };
    bool enter=internal(oppositeDirection(in)),leave=internal(out);
    Point a=enter?crossing(oppositeDirection(in)):p.point.front(),b=leave?crossing(out):p.point.back();
    Point hi=enter?Point{(a.z-center.z)/16,-(a.x-center.x)/16}:Point{float(DX[in]),float(DZ[in])};
    Point ho=leave?Point{(b.z-center.z)/16,-(b.x-center.x)/16}:Point{float(DX[out]),float(DZ[out])};
    float angle=std::atan2(a.z-center.z,a.x-center.x),end=std::atan2(b.z-center.z,b.x-center.x);
    while(end>=angle)end-=6.283185307f;
    float handle=std::min(8.f,std::hypot(b.x-a.x,b.z-a.z)*.55f);
    p.distance.fill(0);
    for(int k=0;k<=32;++k){float t=k/32.f,u=1-t;
        if(enter&&leave&&in!=oppositeDirection(out)){float theta=angle+(end-angle)*t;p.point[k]={center.x+16*std::cos(theta),center.z+16*std::sin(theta)};}
        else {Point c{a.x+hi.x*handle,a.z+hi.z*handle},d{b.x-ho.x*handle,b.z-ho.z*handle};p.point[k]={u*u*u*a.x+3*u*u*t*c.x+3*u*t*t*d.x+t*t*t*b.x,u*u*u*a.z+3*u*u*t*c.z+3*u*t*t*d.z+t*t*t*b.z};}
        if(k)p.distance[k]=p.distance[k-1]+std::hypot(p.point[k].x-p.point[k-1].x,p.point[k].z-p.point[k-1].z);
    }
    p.length=p.distance.back();return p;
}
struct Footprint {Point center,forward;};
bool overlaps(const Footprint& a,const Footprint& b) {
    const Point rightA{-a.forward.z,a.forward.x},rightB{-b.forward.z,b.forward.x};
    const Point delta{b.center.x-a.center.x,b.center.z-a.center.z};
    auto dot=[](Point u,Point v){return u.x*v.x+u.z*v.z;};
    for(Point axis:{a.forward,rightA,b.forward,rightB}) {
        // Inflate the 6 x 3 body to cover gaps between sampled poses and leave clearance.
        float extent=3.2f*(std::abs(dot(a.forward,axis))+std::abs(dot(b.forward,axis)))+
                     1.7f*(std::abs(dot(rightA,axis))+std::abs(dot(rightB,axis)));
        if(std::abs(dot(delta,axis))>=extent)return false;
    }
    return true;
}
std::array<uint64_t,64> movementConflicts(const std::array<Path,64>& paths) {
    std::array<std::array<Footprint,65>,64> poses;
    for(int movement=0;movement<64;++movement)for(int k=0;k<=64;++k)
        paths[movement].sample(paths[movement].length*float(k)/64,poses[movement][k].center,poses[movement][k].forward);
    std::array<uint64_t,64> conflicts{};
    for(int a=0;a<64;++a)for(int b=a;b<64;++b) {
        bool conflict=a/8==b/8 || a%8==b%8; // Shared approach or merging exit.
        for(int i=0;i<=64 && !conflict;++i)for(int j=0;j<=64 && !conflict;++j)conflict=overlaps(poses[a][i],poses[b][j]);
        if(conflict){conflicts[a]|=uint64_t(1ull<<b);conflicts[b]|=uint64_t(1ull<<a);}
    }
    return conflicts;
}
struct RoutePool {
    std::vector<int> data;
    std::array<std::vector<uint32_t>,20> free;
    uint32_t allocate(const std::vector<int>& route,unsigned& bucket) {
        bucket=0;while((size_t(1)<<bucket)<route.size()) ++bucket;
        uint32_t offset;
        if(free[bucket].empty()) {offset=uint32_t(data.size());data.resize(data.size()+(size_t(1)<<bucket));}
        else {offset=free[bucket].back();free[bucket].pop_back();}
        std::copy(route.begin(),route.end(),data.begin()+offset);return offset;
    }
    void release(uint32_t offset,unsigned bucket){free[bucket].push_back(offset);}
};
}
void integrateCars(size_t count,float dt,float* speed,const float* limit,float* distance,const float* speedCaps) {
    const auto zero=_mm256_setzero_ps(),step=_mm256_set1_ps(dt),accel=_mm256_set1_ps(6*dt);
    for(size_t i=0;i<count;i+=8) {
        const auto mask=_mm256_setr_epi32(i<count?-1:0,i+1<count?-1:0,i+2<count?-1:0,i+3<count?-1:0,i+4<count?-1:0,i+5<count?-1:0,i+6<count?-1:0,i+7<count?-1:0);
        auto maxSpeed=speedCaps?_mm256_load_ps(speedCaps+i):_mm256_set1_ps(16);
        auto gap=_mm256_max_ps(zero,_mm256_load_ps(limit+i));
        auto safe=_mm256_sqrt_ps(_mm256_mul_ps(_mm256_set1_ps(12),gap));
        auto v=_mm256_min_ps(maxSpeed,_mm256_min_ps(safe,_mm256_add_ps(_mm256_load_ps(speed+i),accel)));
        auto d=_mm256_min_ps(gap,_mm256_mul_ps(v,step));
        v=_mm256_min_ps(v,_mm256_div_ps(d,step));
        _mm256_maskstore_ps(speed+i,mask,v);_mm256_maskstore_ps(distance+i,mask,d);
    }
}
struct TrafficGraph {
    std::shared_ptr<const World> world;
    bool directed=false,purposeful=false;
    std::vector<uint8_t> mask=std::vector<uint8_t>(Cells);
    std::vector<int> component=std::vector<int>(Cells,-1),jump=std::vector<int>(Cells*8,-1);
    std::vector<int> eligible,componentTiles,componentOffsets;
    bool junction(int tile)const{return mask[tile]!=5&&mask[tile]!=10;}
    void build() {
        if(!NetworkWorker::instance().isWorkerThread())throw std::logic_error("Graph building must run on network worker");
        directed=purposeful||world->diagonalCount()!=0;
        for(int t=0;t<Cells;++t){mask[t]=world->connections(t%MapSize,t/MapSize);directed|=world->roadRule({t%MapSize,t/MapSize})!=0;}
        std::fill(component.begin(),component.end(),-1);componentTiles.clear();componentOffsets.clear();eligible.clear();
        for(int t=0;t<Cells;++t)if(mask[t] && component[t]<0) {
            int id=int(componentOffsets.size());componentOffsets.push_back(int(componentTiles.size()));
            size_t begin=componentTiles.size();componentTiles.push_back(t);component[t]=id;
            for(size_t i=begin;i<componentTiles.size();++i) {
                int a=componentTiles[i];for(int d=0;d<8;++d)if(mask[a]&(1<<d)) {int b=neighbor(a,d);if(component[b]<0){component[b]=id;componentTiles.push_back(b);}}
            }
        }
        componentOffsets.push_back(int(componentTiles.size()));eligible=componentTiles;
        // Directional dynamic programming compresses each straight corridor in O(map size).
        for(int d=0;d<8;++d)for(int k=0;k<Cells;++k) {
            int t=(d==1||d==2)?Cells-1-k:k;
            int b=(mask[t]&(1<<d))?neighbor(t,d):-1;
            jump[t*8+d]=b>=0 && !junction(b)?jump[b*8+d]:b;
        }
    }
};
struct RouteSearch {
    std::shared_ptr<const TrafficGraph> network;
    std::shared_ptr<const std::vector<int>> congestion;
    uint64_t expansions=0;
    struct Open {int state,g,f;bool operator<(const Open& b)const{return f!=b.f?f>b.f:g<b.g;}};
    struct Frontier:std::priority_queue<Open> {auto& data(){return c;} const auto& data()const{return c;} void clear(){c.clear();}size_t bytes()const{return c.capacity()*sizeof(Open);}} open;
    std::vector<int> costs,parent;
    std::vector<uint32_t> stamps;
    uint32_t searchStamp=0;
    struct Search {bool active=false;int start=0,target=0,car=-1,in=-1;};
    Search search;
    std::vector<int> result;
    void beginSearch(int start,int target,int car=-1,int in=-1) {
        if(costs.empty()){costs.resize(Cells*8);parent.resize(Cells*8);stamps.resize(Cells*8);}
        search={true,start,target,car,in};open.clear();
        if(++searchStamp==0){std::fill(stamps.begin(),stamps.end(),0);++searchStamp;}
        int state=start*8+(in<0?0:in);
        costs[state]=0;parent[state]=-1;stamps[state]=searchStamp;open.push({state,0,manhattan(start,target)});
    }
    bool advanceSearch(unsigned& budget) {
        if(!NetworkWorker::instance().isWorkerThread())throw std::logic_error("Pathfinding must run on network worker");
        while(budget && !open.empty()) {
            auto a=open.top();open.pop();--budget;++expansions;
            if(costs[a.state]!=a.g)continue;
            int tile=a.state/8,in=a.state%8;
            if(tile==search.target) {
                result.clear();for(int t=a.state;t>=0;t=parent[t])result.push_back(t/8);
                std::reverse(result.begin(),result.end());return true;
            }
            for(int d=0;d<8;++d) {
                bool unconstrainedStart=parent[a.state]<0 && search.in<0;
                if(!unconstrainedStart && d==oppositeDirection(in) && (network->mask[tile]&(network->mask[tile]-1)))continue;
                int b=network->directed?((network->mask[tile]&(1<<d))?neighbor(tile,d):-1):network->jump[tile*8+d];if(b<0)continue;
                if(network->directed && !network->world->canTravel({tile%MapSize,tile/MapSize},{b%MapSize,b/MapSize}))continue;
                int target=search.target;
                if(!network->directed&&manhattan(tile,target)+manhattan(target,b)==manhattan(tile,b))b=target;
                int g=a.g+manhattan(tile,b)*(network->purposeful&&network->world->highwayCount()&&!network->world->highway({b%MapSize,b/MapSize})?2:1)+(network->purposeful?std::min(30,(*congestion)[b]*3):0),state=b*8+d;
                if(stamps[state]!=searchStamp || g<costs[state]) {stamps[state]=searchStamp;costs[state]=g;parent[state]=a.state;open.push({state,g,g+manhattan(b,target)});}
            }
        }
        if(open.empty()){result.clear();return true;}return false;
    }
};
struct RouteInput {int start=0,target=0,car=-1,in=-1;uint64_t carSerial=0;TripRequest trip;};
struct RouteAnswer {RouteInput input;std::vector<int> tiles;};
struct RouteSlice {std::vector<RouteAnswer> answers;bool done=false;uint64_t expansions=0;double ms=0;};
struct RouteBatch {
    RouteSearch engine;
    std::deque<RouteInput> inputs;
    RouteSlice advance(unsigned budget) {
        auto start=std::chrono::steady_clock::now();auto before=engine.expansions;RouteSlice slice;
        while(!inputs.empty()&&budget) {
            auto& input=inputs.front();
            if(!engine.search.active)engine.beginSearch(input.start,input.target,input.car,input.in);
            if(!engine.advanceSearch(budget))break;
            slice.answers.push_back({input,std::move(engine.result)});engine.search.active=false;inputs.pop_front();
        }
        slice.done=inputs.empty();slice.expansions=engine.expansions-before;slice.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();return slice;
    }
};
struct TrafficSimulation::Impl {
    TrafficConfig config; TrafficStats stats;
    const World* roadWorld=nullptr;
    std::deque<TripRequest> requests;
    std::vector<TripEvent> events;
    TripRequest searching{};
    std::unordered_set<uint64_t> owned;
    std::mt19937 random;
    uint64_t revision=~uint64_t(0),replacement=~uint64_t(0),serial=0;
    double accumulator=0;
    bool interpolate=false;
    size_t capacity=0;
    enum Field {X,Z,PX,PZ,HX,HZ,Progress,Speed,Limit,Distance,BaseX,BaseZ,SampleDistance,FieldCount};
    std::array<FloatArray,FieldCount> hot;
    FloatArray speedCaps; // Derived each tick; not part of the save wire format.
    struct Car {
        uint32_t id=0,route=0;unsigned bucket=0,count=0,next=1;
        int tile=0,in=0,out=0,destination=0,held=-1;
        float arrival=8;
        uint64_t serial=0,waiting=0,trip=0,tripOwner=0,started=0;
        bool repair=false,custom=false;
    };
    std::vector<Car> cars;
    std::vector<int> slots,freeIds,nextCar;
    std::shared_ptr<const TrafficGraph> network=std::make_shared<TrafficGraph>();
    struct GraphJob {NetworkLifetime lifetime;uint64_t topology,replacement;std::future<std::shared_ptr<TrafficGraph>> result;};
    std::unique_ptr<GraphJob> graphJob;
    struct RouteJob {
        NetworkLifetime lifetime;
        std::shared_ptr<RouteBatch> batch;
        std::deque<RouteInput> pending;
        std::future<RouteSlice> ready;
        std::chrono::steady_clock::time_point submitted=std::chrono::steady_clock::now();
    };
    std::unique_ptr<RouteJob> routeJob;
    std::shared_ptr<RouteBatch> idleBatch;
    std::shared_ptr<RouteSearch> blockingSearch;
    std::shared_ptr<std::vector<int>> blockingCongestion;
    // Movements sharing an entry always conflict, so four owner slots per tile suffice.
    std::vector<int> heads=std::vector<int>(Cells*8,-1),owner=std::vector<int>(Cells*8,-1),winner=std::vector<int>(Cells,-1);
    std::vector<uint64_t> reserved=std::vector<uint64_t>(Cells);
    std::vector<uint64_t> exclusive=std::vector<uint64_t>(Cells);
    std::vector<uint8_t> unreserved=std::vector<uint8_t>(Cells);
    std::vector<uint8_t> admitted;
    std::vector<uint8_t> requestedMovement;
    std::vector<int> nextCandidate,proposals,legacyTiles;
    std::vector<int> tileCount=std::vector<int>(Cells),usedTiles;
    std::vector<int> usedHeads,usedWinners;
    std::array<Path,64> paths;
    std::array<std::array<Path,64>,9> roundPaths;
    std::array<uint64_t,64> conflicts;
    std::unordered_map<uint32_t,Path> detours;
    mutable std::array<uint8_t,9*64*64> adjacentConflicts{};
    std::array<std::array<Footprint,65>,64> lanePoses;
    RoutePool pool;
    struct Open {int state,g,f;bool operator<(const Open& b)const{return f!=b.f?f>b.f:g<b.g;}};
    struct Frontier:std::priority_queue<Open> {auto& data(){return c;} const auto& data()const{return c;} void clear(){c.clear();}size_t bytes()const{return c.capacity()*sizeof(Open);}} open;
    std::vector<int> costs,parent;
    std::vector<uint32_t> stamps;
    uint32_t searchStamp=0;
    struct Search {bool active=false;int start=0,target=0,car=-1,in=-1;};
    Search search;
    std::vector<int> result;
    std::vector<CarInstance> render;
    explicit Impl(TrafficConfig c):config(c),random(c.seed) {
        for(int a=0;a<8;++a)for(int b=0;b<8;++b)paths[a*8+b]=makePath(a,b);
        for(int m=0;m<64;++m)for(int k=0;k<=64;++k)paths[m].sample(paths[m].length*k/64.f,lanePoses[m][k].center,lanePoses[m][k].forward);
        for(int i=0;i<9;++i)for(int a=0;a<8;++a)for(int b=0;b<8;++b)roundPaths[i][a*8+b]=makeRoundPath(i,a,b);
        static const auto sharedConflicts=movementConflicts(paths);conflicts=sharedConflicts;
        reserve(65536);
    }
    void reserve(size_t requested) {
        if(requested<=capacity)return;
        size_t n=std::max(size_t(65536),capacity*2);while(n<requested)n*=2;
        for(auto& a:hot)a.grow(cars.size(),n);speedCaps.grow(cars.size(),n);
        cars.reserve(n);nextCar.resize(n);admitted.resize(n);requestedMovement.resize(n);nextCandidate.resize(n);proposals.reserve(16);render.reserve(n);capacity=n;
    }
    bool junction(int tile)const{return network->mask[tile]!=5 && network->mask[tile]!=10;}
    static int movement(const Car& c){return c.in*8+c.out;}
    static int reservation(const Car& c){return c.tile*64+movement(c);}
    void claim(int key,int id,bool custom=false){owner[key/8]=id;reserved[key/64]|=uint64_t(1ull<<(key%64));if(custom)exclusive[key/64]|=uint64_t(1ull<<(key%64));}
    void release(int key,int id){if(key>=0 && owner[key/8]==id){owner[key/8]=-1;auto keep=uint64_t(~(1ull<<(key%64)));reserved[key/64]&=keep;exclusive[key/64]&=keep;}}
    const Path& lanePath(int tile,int in,int out)const{auto r=roadWorld->roadRule({tile%MapSize,tile/MapSize});return r>=64&&r<=72?roundPaths[r-64][in*8+out]:paths[in*8+out];}
    const Path& path(const Car& c)const{return c.custom?detours.at(c.id):lanePath(c.tile,c.in,c.out);}
    int routeNext(const Car& c)const{return c.next<c.count?pool.data[c.route+c.next]:c.destination;}
    int chooseOut(int tile,int in)const {
        if(network->mask[tile]&(1<<in))return in;
        for(int d=0;d<8;++d)if((network->mask[tile]&(1<<d)) && d!=oppositeDirection(in))return d;
        return oppositeDirection(in);
    }
    void erase(size_t i,bool completed=false) {
        auto c=cars[i];
        if(c.trip)events.push_back({c.trip,c.tripOwner,completed,stats.ticks-c.started});
        release(reservation(c),int(c.id));release(c.held,int(c.id));
        pool.release(c.route,c.bucket);slots[c.id]=-1;freeIds.push_back(int(c.id));
        if(c.custom)detours.erase(c.id);
        if(search.active && search.car==int(c.id))search.active=false;
        if(i+1<cars.size()) {cars[i]=cars.back();slots[cars[i].id]=int(i);for(auto& a:hot)a[i]=a[cars.size()-1];}
        cars.pop_back();if(completed)++stats.completed;else ++stats.removed;
    }
    void holdForTopology(World& world) {
        for(size_t i=0;i<cars.size();) {
            auto& c=cars[i];if(!world.road(c.tile%MapSize,c.tile/MapSize)||!world.road(c.destination%MapSize,c.destination/MapSize)){erase(i);continue;}
            c.repair=true;hot[Speed][i]=0;hot[PX][i]=hot[X][i];hot[PZ][i]=hot[Z][i];
            if(c.held>=0&&!world.road((c.held/64)%MapSize,(c.held/64)/MapSize)){release(c.held,int(c.id));c.held=-1;}
            ++i;
        }
    }
    bool rebuild(World& world,bool wait=false) {
        roadWorld=&world;
        if(graphJob&&(graphJob->topology!=world.topologyRevision()||graphJob->replacement!=world.replacementRevision()))graphJob.reset();
        if(!graphJob) {
            auto job=std::make_unique<GraphJob>();job->topology=world.topologyRevision();job->replacement=world.replacementRevision();
            auto graph=std::make_shared<TrafficGraph>();graph->world=std::make_shared<World>(world);graph->purposeful=config.purposeful;
            job->result=NetworkWorker::instance().submit([graph]{graph->build();return graph;},job->lifetime.cancelled,wait);
            if(!job->result.valid())return false;graphJob=std::move(job);world.takeTrafficChanges();
            if(routeJob)for(auto& input:routeJob->pending)if(input.trip.id)requests.push_back(input.trip);
            routeJob.reset();blockingSearch.reset();search.active=false;
        }
        if(!networkReady(graphJob->result,wait)){holdForTopology(world);return false;}
        auto next=graphJob->result.get();graphJob.reset();
        bool removed=false;for(int t=0;t<Cells;++t)removed|=(network->mask[t]&~next->mask[t])!=0;
        network=std::move(next);
        bool reset=replacement!=world.replacementRevision();
        replacement=world.replacementRevision();revision=world.topologyRevision();
        if(reset) {while(!cars.empty())erase(cars.size()-1);std::fill(owner.begin(),owner.end(),-1);std::fill(reserved.begin(),reserved.end(),uint64_t(0));accumulator=0;return true;}
        // Only edits can leave a newly controlled tile with unreserved occupants.
        // Track those few tiles rather than probing owner arrays for every car every tick.
        for(const auto& c:cars)if(junction(c.tile) && owner[reservation(c)/8]!=int(c.id) && !unreserved[c.tile]){unreserved[c.tile]=1;legacyTiles.push_back(c.tile);}
        if(!removed && !network->directed)return true;
        // Prefix sums validate compressed route segments without walking every road tile
        // once per car. A topology edit can split components, so connectivity is global.
        std::vector<int> horizontal(Cells),vertical(Cells);
        for(int t=0;t<Cells;++t){int missing=network->mask[t]?0:1;horizontal[t]=missing+(t%MapSize?horizontal[t-1]:0);vertical[t]=missing+(t>=MapSize?vertical[t-MapSize]:0);}
        for(size_t i=0;i<cars.size();) {
            auto& c=cars[i];
            if(!network->mask[c.tile] || network->component[c.tile]!=network->component[c.destination] || !network->mask[c.destination]) {erase(i);continue;}
            bool valid=!network->directed && (network->mask[c.tile]&(1<<c.out))!=0;
            int a=c.tile;
            for(unsigned j=c.next;valid && j<c.count;++j) {
                int b=pool.data[c.route+j];
                if(a==b)continue;
                if(a%MapSize!=b%MapSize && a/MapSize!=b/MapSize)valid=false;
                if(valid){int low=std::min(a,b),high=std::max(a,b);const auto& sums=a/MapSize==b/MapSize?horizontal:vertical;valid=sums[high]-sums[low]==0;a=b;}
            }
            if(!valid){c.repair=true;hot[Speed][i]=0;}
            if(c.held>=0 && !network->mask[c.held/64]){release(c.held,int(c.id));c.held=-1;}
            ++i;
        }
        // A newly created junction may contain several old straight-lane cars.
        // They retain their paths and drain; new entrants wait until all have left.
        return true;
    }
    void occupancy() {
        for(int h:usedHeads)heads[h]=-1;usedHeads.clear();
        for(int t:usedTiles)tileCount[t]=0;usedTiles.clear();
        for(size_t i=0;i<cars.size();++i) {
            int tile=cars[i].tile;if(tileCount[tile]++==0)usedTiles.push_back(tile);
            int h=cars[i].tile*8+cars[i].in;if(heads[h]<0)usedHeads.push_back(h);
            nextCar[i]=heads[h];heads[h]=int(i);
        }
        for(size_t k=0;k<legacyTiles.size();){int tile=legacyTiles[k];bool legacy=false;
            for(int d=0;d<8;++d)for(int i=heads[tile*8+d];i>=0;i=nextCar[i])if(owner[reservation(cars[i])/8]!=int(cars[i].id))legacy=true;
            if(legacy)++k;else {unreserved[tile]=0;legacyTiles[k]=legacyTiles.back();legacyTiles.pop_back();}
        }
    }
    float firstPosition(int tile,int in,int exclude=-1)const {
        float value=1e9f;
        for(int i=heads[tile*8+in];i>=0;i=nextCar[i])if(i!=exclude)value=std::min(value,hot[Progress][i]);
        return value;
    }
    bool occupiedJunction(int tile,int except=-1)const {
        uint64_t bits=reserved[tile];while(bits){int m=std::countr_zero(bits);bits&=bits-1;if(owner[tile*8+m/8]!=except)return true;}
        int own=except>=0 && slots[except]>=0 && cars[slots[except]].tile==tile?1:0;
        return tileCount[tile]>own;
    }
    bool adjacentConflict(int tile,int movement,int other,int otherMovement)const{
        if(!roadWorld->diagonalCount()||(movement/8<4&&movement%8<4&&otherMovement/8<4&&otherMovement%8<4))return false;
        int dx=other%MapSize-tile%MapSize,dz=other/MapSize-tile/MapSize;
        if(std::abs(dx)>1||std::abs(dz)>1||(dx==0&&dz==0))return false;
        int key=((dz+1)*3+dx+1)*4096+movement*64+otherMovement;
        auto& cached=adjacentConflicts[key];if(cached)return cached==2;
        bool conflict=false;
        for(auto a:lanePoses[movement]){for(auto b:lanePoses[otherMovement]){b.center.x+=dx*16;b.center.z+=dz*16;if(overlaps(a,b)){conflict=true;break;}}if(conflict)break;}
        cached=conflict?2:1;return conflict;
    }
    bool occupiedMovement(int tile,int in,int out,int except=-1)const {
        const auto conflict=roadWorld->roundaboutOrigin({tile%MapSize,tile/MapSize}).x>=0?~uint64_t(0):conflicts[in*8+out];uint64_t bits=(reserved[tile]&conflict)|exclusive[tile];
        if(bits && except<0)return true;
        while(bits){int m=std::countr_zero(bits);bits&=bits-1;if(owner[tile*8+m/8]!=except)return true;}
        if(roadWorld->diagonalCount())for(int d=0;d<8;++d){int other=neighbor(tile,d);if(other<0)continue;
            uint64_t occupied=reserved[other];while(occupied){int m=std::countr_zero(occupied);occupied&=occupied-1;
                if(owner[other*8+m/8]!=except&&adjacentConflict(tile,in*8+out,other,m))return true;
            }
        }
        if(!unreserved[tile])return false;
        // Edits can turn an occupied straight tile into a junction before its cars
        // have reservations. Honor their actual movements until they drain.
        for(int d=0;d<8;++d)for(int i=heads[tile*8+d];i>=0;i=nextCar[i]){
            const auto& c=cars[i];if(int(c.id)!=except && ((conflict&(1ull<<movement(c))) || c.custom))return true;
        }
        return false;
    }
    void beginSearch(int start,int target,int car=-1,int in=-1) {
        search={true,start,target,car,in};
        if(!blockingSearch)blockingSearch=std::make_shared<RouteSearch>();blockingSearch->network=network;blockingSearch->congestion=blockingCongestion;
        // Even search initialization belongs to the worker.
        auto engine=blockingSearch;
        auto job=NetworkWorker::instance().submit([engine,start,target,car,in]{engine->beginSearch(start,target,car,in);return true;});job.get();
    }
    bool advanceSearch(unsigned& budget) {
        auto engine=blockingSearch;
        auto job=NetworkWorker::instance().submit([engine,budget]() mutable {auto before=engine->expansions;bool done=engine->advanceSearch(budget);return std::tuple{done,budget,engine->expansions-before};});
        auto [done,left,expansions]=job.get();budget=left;stats.routeExpansions+=expansions;
        if(done)result=engine->result;return done;
    }
    void position(size_t i,bool initialize=false) {
        const auto& p=path(cars[i]);float distance=std::clamp(hot[Progress][i],0.f,p.length);
        if(cars[i].in==cars[i].out && cars[i].in<4 && !cars[i].custom && roadWorld->roundaboutOrigin({cars[i].tile%MapSize,cars[i].tile/MapSize}).x<0) {
            int d=cars[i].in;
            hot[BaseX][i]=float(cars[i].tile%MapSize*TileSize)+p.point[0].x;
            hot[BaseZ][i]=float(cars[i].tile/MapSize*TileSize)+p.point[0].z;
            hot[SampleDistance][i]=distance;hot[HX][i]=float(DX[d]);hot[HZ][i]=float(DZ[d]);
            if(initialize){hot[X][i]=hot[PX][i]=hot[BaseX][i]+DX[d]*distance;hot[Z][i]=hot[PZ][i]=hot[BaseZ][i]+DZ[d]*distance;}
            return;
        }
        auto end=std::upper_bound(p.distance.begin(),p.distance.end(),distance);
        int k=std::clamp(int(end-p.distance.begin())-1,0,31);
        float len=p.distance[k+1]-p.distance[k];
        Point h{(p.point[k+1].x-p.point[k].x)/len,(p.point[k+1].z-p.point[k].z)/len};
        hot[BaseX][i]=float(cars[i].tile%MapSize*TileSize)+p.point[k].x;
        hot[BaseZ][i]=float(cars[i].tile/MapSize*TileSize)+p.point[k].z;
        hot[SampleDistance][i]=distance-p.distance[k];
        hot[HX][i]=h.x;hot[HZ][i]=h.z;
        if(initialize){hot[X][i]=hot[PX][i]=hot[BaseX][i]+h.x*hot[SampleDistance][i];hot[Z][i]=hot[PZ][i]=hot[BaseZ][i]+h.z*hot[SampleDistance][i];}
    }
    void finishSearch() {
        search.active=false;
        if(search.car>=0) {
            int i=slots[search.car];if(i<0)return;
            if(result.empty()){erase(size_t(i));return;}
            auto& c=cars[i];
            int nextOut=result.size()>1?direction(c.tile,result[1]):chooseOut(c.tile,c.in);
            if(nextOut!=c.out) {
                // A road edit can invalidate the current exit. Reserve the tile and
                // connect the actual car position to its new exit without teleporting.
                if(occupiedJunction(c.tile,int(c.id)))return;
                Point a,h;path(c).sample(hot[Progress][i],a,h);
                Point b=lanePath(c.tile,c.in,nextOut).point.back();
                float handle=std::min(4.f,std::hypot(b.x-a.x,b.z-a.z)*.4f);
                Point controlA{std::clamp(a.x+h.x*handle,3.f,13.f),std::clamp(a.z+h.z*handle,3.f,13.f)};
                Point controlB{b.x-DX[nextOut]*handle,b.z-DZ[nextOut]*handle};Path detour;
                for(int k=0;k<=32;++k){float t=float(k)/32,u=1-t;
                    detour.point[k]={u*u*u*a.x+3*u*u*t*controlA.x+3*u*t*t*controlB.x+t*t*t*b.x,u*u*u*a.z+3*u*u*t*controlA.z+3*u*t*t*controlB.z+t*t*t*b.z};
                    if(k)detour.distance[k]=detour.distance[k-1]+std::hypot(detour.point[k].x-detour.point[k-1].x,detour.point[k].z-detour.point[k-1].z);
                }
                detour.length=detour.distance.back();detours[c.id]=detour;c.custom=true;hot[Progress][i]=0;
                release(reservation(c),int(c.id));c.out=nextOut;claim(reservation(c),int(c.id),true);
            }
            pool.release(c.route,c.bucket);c.route=pool.allocate(result,c.bucket);c.count=unsigned(result.size());c.next=1;c.repair=false;
            return;
        }
        if(result.size()<2) {
            if(searching.id){events.push_back({searching.id,searching.owner,!result.empty(),0});searching={};}
            return;
        }
        if(!config.purposeful && cars.size()>=config.target)return;
        int tile=search.start,out=direction(tile,result[1]),in=out;
        if(!(network->mask[tile]&(1<<(oppositeDirection(in))))) {
            for(int d=0;d<8;++d)if(network->mask[tile]&(1<<d)){in=oppositeDirection(d);if(in!=oppositeDirection(out))break;}
        }
        const auto& p=lanePath(tile,in,out);
        float progress=4.f+float(random()%10000)/10000.f*std::max(0.f,p.length-8.f);
        if(junction(tile) && occupiedMovement(tile,in,out))return;
        for(int i=heads[tile*8+in];i>=0;i=nextCar[i])if(std::abs(hot[Progress][i]-progress)<Gap)return;
        // Also check neighboring lane tails so random insertion never cuts off a car.
        int prev=neighbor(tile,oppositeDirection(in));
        if(prev>=0)for(int d=0;d<8;++d)for(int i=heads[prev*8+d];i>=0;i=nextCar[i])
            if(cars[i].out==in && path(cars[i]).length-hot[Progress][i]+progress<Gap)return;
        int next=neighbor(tile,out);
        if(next>=0 && p.length-progress+firstPosition(next,out)<Gap)return;
        reserve(cars.size()+1);Car c;c.tile=tile;c.in=in;c.out=out;c.destination=search.target;c.serial=++serial;
        c.trip=searching.id;c.tripOwner=searching.owner;c.started=stats.ticks;searching={};
        c.arrival=4.f+float(random()%8000)/1000.f;
        if(freeIds.empty()){c.id=uint32_t(slots.size());slots.push_back(-1);}else {c.id=uint32_t(freeIds.back());freeIds.pop_back();}
        c.route=pool.allocate(result,c.bucket);c.count=unsigned(result.size());
        size_t i=cars.size();slots[c.id]=int(i);cars.push_back(c);
        if(tileCount[tile]++==0)usedTiles.push_back(tile);
        for(auto& a:hot)a[i]=0;hot[Progress][i]=progress;position(i,true);
        if(junction(tile))claim(reservation(c),int(c.id));
        int h=tile*8+in;if(heads[h]<0)usedHeads.push_back(h);nextCar[i]=heads[h];heads[h]=int(i);
    }
    void routeWork() {
        if(searching.id){requests.push_front(searching);searching={};}
        search.active=false;blockingSearch.reset();
        if(routeJob&&networkReady(routeJob->ready)) {
            auto slice=routeJob->ready.get();stats.routingMs=slice.ms;stats.routeExpansions+=slice.expansions;
            stats.routeLatencyMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-routeJob->submitted).count();
            for(auto& answer:slice.answers) {
                routeJob->pending.pop_front();const auto& input=answer.input;
                if(input.car>=0) {
                    int i=size_t(input.car)<slots.size()?slots[input.car]:-1;
                    if(i<0||cars[i].serial!=input.carSerial||!cars[i].repair||cars[i].tile!=input.start){++stats.staleRoutes;continue;}
                }
                search={true,input.start,input.target,input.car,input.in};searching=input.trip;result=std::move(answer.tiles);finishSearch();
                if(searching.id){requests.push_back(searching);searching={};}
                if(input.car>=0)occupancy();
            }
            if(slice.done){idleBatch=routeJob->batch;routeJob.reset();}
        }
        if(!routeJob) {
            auto job=std::make_unique<RouteJob>();
            const size_t limit=std::clamp(size_t(config.spawnBudget),size_t(1),size_t(256));
            for(const auto& c:cars)if(c.repair&&job->pending.size()<limit)job->pending.push_back({c.tile,c.destination,int(c.id),c.in,c.serial,{}});
            while(job->pending.size()<limit) {
                if(config.purposeful) {
                    if(requests.empty())break;auto request=requests.front();requests.pop_front();
                    int a=request.from.z*MapSize+request.from.x,b=request.to.z*MapSize+request.to.x;
                    if(!network->mask[a]||!network->mask[b]||network->component[a]!=network->component[b]){events.push_back({request.id,request.owner,false,0});continue;}
                    job->pending.push_back({a,b,-1,-1,0,request});
                } else {
                    if(cars.size()+job->pending.size()>=config.target||network->eligible.empty())break;
                    int a=network->eligible[random()%network->eligible.size()],id=network->component[a];
                    int begin=network->componentOffsets[id],n=network->componentOffsets[id+1]-begin;
                    int b=network->componentTiles[begin+random()%n];job->pending.push_back({a,b,-1,-1,0,{}});
                }
            }
            if(job->pending.empty())return;
            // Allocate scratch on the worker; a batch reuses it for all its requests.
            auto graph=network;auto congestion=std::make_shared<std::vector<int>>(tileCount);auto inputs=job->pending;
            job->batch=idleBatch?std::move(idleBatch):std::make_shared<RouteBatch>();job->batch->engine.network=graph;job->batch->engine.congestion=congestion;job->batch->inputs=std::move(inputs);
            routeJob=std::move(job);
        }
        if(!routeJob->ready.valid()) {
            auto batch=routeJob->batch;unsigned budget=config.routeBudget;
            routeJob->ready=NetworkWorker::instance().submit([batch,budget]{return batch->advance(budget);},routeJob->lifetime.cancelled);
        }
    }
    void blockingRouteWork() {
        if(!blockingCongestion)blockingCongestion=std::make_shared<std::vector<int>>(tileCount);else *blockingCongestion=tileCount;
        auto start=std::chrono::steady_clock::now();unsigned budget=config.routeBudget,processed=0;
        size_t repairCursor=0;
        while(budget && processed<config.spawnBudget) {
            if(!search.active) {
                while(repairCursor<cars.size() && !cars[repairCursor].repair)++repairCursor;
                if(repairCursor<cars.size()) {auto& c=cars[repairCursor++];beginSearch(c.tile,c.destination,int(c.id),c.in);}
                else {
                    if(config.purposeful) {
                        if(!searching.id) {if(this->requests.empty())break;searching=this->requests.front();this->requests.pop_front();}
                        int a=searching.from.z*MapSize+searching.from.x,b=searching.to.z*MapSize+searching.to.x;
                        if(!network->mask[a] || !network->mask[b] || network->component[a]!=network->component[b]) {events.push_back({searching.id,searching.owner,false,0});searching={};++processed;continue;}
                        beginSearch(a,b);
                    } else {
                    if(cars.size()>=config.target || network->eligible.empty())break;
                    int a=network->eligible[random()%network->eligible.size()],id=network->component[a];
                    int begin=network->componentOffsets[id],n=network->componentOffsets[id+1]-begin;
                    int b=network->componentTiles[begin+random()%n];if(a==b){++processed;continue;}
                    beginSearch(a,b);
                    }
                }
            }
            if(!advanceSearch(budget))break;
            finishSearch();++processed;
            if(search.car<0&&searching.id&&!search.active){this->requests.push_back(searching);searching={};}
            // A failed repair can swap-remove a car and invalidate occupancy indices.
            if(search.car>=0)occupancy();
        }
        stats.routingMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
    void step(World& world) {
        roadWorld=&world;
        auto start=std::chrono::steady_clock::now();
        if(revision!=world.topologyRevision()&&!rebuild(world))return;
        ++stats.ticks;occupancy();if(NetworkWorker::blocking)blockingRouteWork();else routeWork();
        for(int t:usedWinners)winner[t]=-1;usedWinners.clear();
        std::fill_n(admitted.data(),cars.size(),uint8_t(0));
        // Release all cleared tails before making any admission decisions, so dense
        // slot order cannot change which movements appear occupied this tick.
        for(size_t i=0;i<cars.size();++i){auto& c=cars[i];if(c.held>=0 && hot[Progress][i]>=TailClearance){release(c.held,int(c.id));c.held=-1;}}
        for(size_t i=0;i<cars.size();++i) {
            auto& c=cars[i];hot[PX][i]=hot[X][i];hot[PZ][i]=hot[Z][i];
            float length=path(c).length,limit=1e6f;
            if(c.tile==c.destination)limit=std::max(0.f,std::min(c.arrival,length-Half)-hot[Progress][i]);
            bool front=true;
            for(int j=heads[c.tile*8+c.in];j>=0;j=nextCar[j])if(hot[Progress][j]>hot[Progress][i]){front=false;limit=std::min(limit,hot[Progress][j]-hot[Progress][i]-Gap);}
            int next=neighbor(c.tile,c.out);
            if(c.repair || next<0){hot[Limit][i]=0;continue;}
            limit=std::min(limit,length-hot[Progress][i]+firstPosition(next,c.out)-Gap);
            hot[Limit][i]=std::max(0.f,limit);
            if(c.tile==c.destination || !front || !junction(next) || length-hot[Progress][i]>20)continue;
            if(!c.waiting)c.waiting=stats.ticks;
            // Never enter a junction unless its exit has room for the entire car.
            Car future=c;future.tile=next;future.in=c.out;
            if(future.next<future.count && next==pool.data[future.route+future.next])++future.next;
            future.out=next==c.destination?chooseOut(next,future.in):direction(next,routeNext(future));
            if(world.roadRule({next%MapSize,next/MapSize})<64 && (world.roadRule({next%MapSize,next/MapSize})&8) && ((stats.ticks/180)%2 != unsigned(future.in%2)))continue;
            if(occupiedMovement(next,future.in,future.out,int(c.id)))continue;
            int exit=neighbor(next,future.out);
            if(next!=c.destination && (exit<0 || firstPosition(exit,future.out)<Gap))continue;
            requestedMovement[i]=uint8_t(movement(future));
            if(winner[next]<0)usedWinners.push_back(next);
            nextCandidate[i]=winner[next];winner[next]=int(i);
        }
        std::unordered_map<int,int> roundaboutAdmissions;
        std::unordered_map<int,uint64_t> diagonalGrants;
        for(int tile:usedWinners){
            proposals.clear();for(int i=winner[tile];i>=0;i=nextCandidate[i])proposals.push_back(i);
            std::sort(proposals.begin(),proposals.end(),[&](int a,int b){const auto& ca=cars[a];const auto& cb=cars[b];return ca.waiting!=cb.waiting?ca.waiting<cb.waiting:ca.serial<cb.serial;});
            uint64_t granted=0;
            for(int i:proposals){int m=requestedMovement[i];if(!((world.roundaboutOrigin({tile%MapSize,tile/MapSize}).x>=0?~uint64_t(0):conflicts[m])&granted)){
                bool adjacentBusy=false;if(world.diagonalCount())for(int d=0;d<8&&!adjacentBusy;++d){int other=neighbor(tile,d);auto found=diagonalGrants.find(other);if(found==diagonalGrants.end())continue;uint64_t bits=found->second;while(bits){int movement=std::countr_zero(bits);bits&=bits-1;if(adjacentConflict(tile,m,other,movement)){adjacentBusy=true;break;}}}
                if(adjacentBusy)continue;
                Cell origin=world.roundaboutOrigin({tile%MapSize,tile/MapSize});
                if(origin.x>=0&&world.roundaboutOrigin({cars[i].tile%MapSize,cars[i].tile/MapSize})!=origin){
                    int occupied=0;for(int z=0;z<3;++z)for(int x=0;x<3;++x)occupied+=tileCount[(origin.z+z)*MapSize+origin.x+x];
                    int& entering=roundaboutAdmissions[origin.z*MapSize+origin.x];
                    // Preserve space for circulation, including entries admitted this tick.
                    if(occupied+entering>=3)continue;
                    ++entering;
                }
                granted|=uint64_t(1ull<<m);admitted[i]=1;if(world.diagonalCount())diagonalGrants[tile]|=1ull<<m;
            }}
        }
        for(size_t i=0;i<cars.size();++i) {
            auto& c=cars[i];int next=neighbor(c.tile,c.out);
            if(c.tile!=c.destination && next>=0 && junction(next) && !admitted[i])hot[Limit][i]=std::min(hot[Limit][i],std::max(0.f,path(c).length-hot[Progress][i]-Half));
        }
        for(size_t i=0;i<cars.size();++i){const auto& c=cars[i];int next=neighbor(c.tile,c.out);
            speedCaps[i]=world.highway({c.tile%MapSize,c.tile/MapSize})&&!junction(c.tile)&&next>=0&&!junction(next)?32.f:16.f;}
        integrateCars(cars.size(),Dt,hot[Speed].data,hot[Limit].data,hot[Distance].data,speedCaps.data);
        // Lifecycle and path transitions are sparse scalar work; position storage stays SoA.
        for(size_t i=0;i<cars.size();++i) {
            auto& c=cars[i];hot[Progress][i]+=hot[Distance][i];
            if(c.tile!=c.destination && !c.repair && hot[Progress][i]>=path(c).length-0.0001f) {
                int old=c.tile,next=neighbor(old,c.out);
                if(next>=0) {
                    hot[Progress][i]=std::max(0.f,hot[Progress][i]-path(c).length);
                    if(owner[reservation(c)/8]==int(c.id))c.held=reservation(c);
                    if(c.custom){detours.erase(c.id);c.custom=false;}
                    c.tile=next;c.in=c.out;c.waiting=0;
                    if(c.next<c.count && c.tile==pool.data[c.route+c.next])++c.next;
                    c.out=c.tile==c.destination?chooseOut(c.tile,c.in):direction(c.tile,routeNext(c));
                    if(junction(next))claim(reservation(c),int(c.id));
                }
            }
            position(i);
        }
        // SIMD position commit (eight contiguous positions per instruction).
        for(size_t i=0;i<cars.size();i+=8){
            auto d=_mm256_load_ps(hot[SampleDistance].data+i);
            _mm256_store_ps(hot[X].data+i,_mm256_add_ps(_mm256_load_ps(hot[BaseX].data+i),_mm256_mul_ps(_mm256_load_ps(hot[HX].data+i),d)));
            _mm256_store_ps(hot[Z].data+i,_mm256_add_ps(_mm256_load_ps(hot[BaseZ].data+i),_mm256_mul_ps(_mm256_load_ps(hot[HZ].data+i),d)));
        }
        for(size_t i=0;i<cars.size();) {
            const auto& c=cars[i];
            if(!c.repair && c.tile==c.destination && hot[Progress][i]>=std::min(c.arrival,path(c).length-Half)-0.001f)erase(i,true);else ++i;
        }
        stats.active=cars.size();stats.pending=config.purposeful?requests.size()+(searching.id?1:0)+(routeJob?routeJob->pending.size():0):(config.target>cars.size()?config.target-cars.size():0);
        for(const auto& c:cars)if(c.repair)++stats.pending;
        stats.capacity=capacity;
        stats.memoryBytes=capacity*((FieldCount+1)*sizeof(float)+sizeof(Car)+sizeof(CarInstance)+sizeof(int))+pool.data.capacity()*sizeof(int)+network->mask.capacity();
        for(const auto* v:std::initializer_list<const std::vector<int>*>{&slots,&freeIds,&network->component,&network->jump,&network->eligible,&network->componentTiles,&network->componentOffsets,&heads,&owner,&winner,&usedHeads,&usedWinners,&costs,&parent,&result,&tileCount,&usedTiles,&nextCandidate,&proposals,&legacyTiles})stats.memoryBytes+=v->capacity()*sizeof(int);
        stats.memoryBytes+=stamps.capacity()*sizeof(uint32_t)+detours.size()*sizeof(Path)+open.bytes();
        stats.memoryBytes+=(reserved.capacity()+exclusive.capacity())*sizeof(uint64_t)+admitted.capacity()+unreserved.capacity()+requestedMovement.capacity();
        for(const auto& v:pool.free)stats.memoryBytes+=v.capacity()*sizeof(uint32_t);
        if(routeJob||idleBatch||blockingSearch)stats.memoryBytes+=size_t(Cells)*8*(sizeof(int)*2+sizeof(uint32_t));
        stats.tickMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
};
TrafficSimulation::TrafficSimulation(TrafficConfig config):impl_(std::make_unique<Impl>(config)){}
TrafficSimulation::~TrafficSimulation()=default;
void TrafficSimulation::setTarget(size_t n){impl_->config.target=n;}
size_t TrafficSimulation::target()const{return impl_->config.target;}
const TrafficStats& TrafficSimulation::stats()const{return impl_->stats;}
void TrafficSimulation::synchronize(World& w){auto& s=*impl_;s.roadWorld=&w;if(s.revision!=w.topologyRevision())s.rebuild(w);s.stats.active=s.cars.size();}
void TrafficSimulation::tick(World& w){impl_->interpolate=false;impl_->step(w);}
void TrafficSimulation::update(World& w,double seconds) {
    auto& s=*impl_;s.roadWorld=&w;
    s.interpolate=true;
    if(s.revision!=w.topologyRevision()){if(!s.rebuild(w))return;s.stats.active=s.cars.size();}
    if(paused){s.accumulator=0;return;}
    if(!std::isfinite(seconds)||seconds<0)return;
    s.accumulator+=std::min(seconds,4.0/30);
    unsigned steps=0;while(s.accumulator>=1.0/30 && steps++<4){s.step(w);s.accumulator-=1.0/30;}
    s.accumulator=std::min(s.accumulator,1.0/30);
}
std::span<const CarInstance> TrafficSimulation::instances() {
    auto& s=*impl_;s.render.resize(s.cars.size());float alpha=paused || !s.interpolate?1.f:float(s.accumulator*30);
    for(size_t i=0;i<s.cars.size();++i)s.render[i]={s.hot[Impl::PX][i]+(s.hot[Impl::X][i]-s.hot[Impl::PX][i])*alpha,s.hot[Impl::PZ][i]+(s.hot[Impl::Z][i]-s.hot[Impl::PZ][i])*alpha,s.hot[Impl::HX][i],s.hot[Impl::HZ][i],uint32_t(s.cars[i].serial%6),s.cars[i].serial};
    return s.render;
}
void TrafficSimulation::validate(const World& w)const {
    impl_->roadWorld=&w;const auto& s=*impl_;
    std::vector<std::pair<int,size_t>> ordered;
    for(size_t i=0;i<s.cars.size();++i) {
        const auto& c=s.cars[i];
        if(s.slots[c.id]!=int(i) || !w.road(c.tile%MapSize,c.tile/MapSize) || !std::isfinite(s.hot[Impl::X][i]) || s.hot[Impl::Speed][i]<0 || s.hot[Impl::Speed][i]>(w.highway({c.tile%MapSize,c.tile/MapSize})?32.001f:16.001f) || c.next>c.count)
            throw std::runtime_error("Traffic state invariant failed");
        ordered.push_back({c.tile*8+c.in,i});
    }
    std::sort(ordered.begin(),ordered.end(),[&](auto a,auto b){return a.first!=b.first?a.first<b.first:s.hot[Impl::Progress][a.second]<s.hot[Impl::Progress][b.second];});
    for(size_t k=1;k<ordered.size();++k)if(ordered[k].first==ordered[k-1].first && s.hot[Impl::Progress][ordered[k].second]-s.hot[Impl::Progress][ordered[k-1].second]<Gap-.01f)
        throw std::runtime_error("Cars violated lane following gap");
    std::vector<float> first(Cells*8,1e9f);
    for(const auto& entry:ordered)first[entry.first]=std::min(first[entry.first],s.hot[Impl::Progress][entry.second]);
    for(size_t i=0;i<s.cars.size();++i){const auto& c=s.cars[i];int next=neighbor(c.tile,c.out);
        if(next>=0 && s.path(c).length-s.hot[Impl::Progress][i]+first[next*8+c.out]<Gap-.01f)
            throw std::runtime_error("Cars violated gap across tile boundary");}
    for(int tile=0;tile<Cells;++tile){uint64_t bits=s.reserved[tile];
        while(bits){int m=std::countr_zero(bits);bits&=bits-1;int key=tile*64+m,id=s.owner[key/8];
            if(id<0 || size_t(id)>=s.slots.size() || s.slots[id]<0)throw std::runtime_error("Orphaned junction reservation");
            const auto& c=s.cars[s.slots[id]];
            if(key!=Impl::reservation(c) && key!=c.held)throw std::runtime_error("Stale junction reservation");
            if(s.conflicts[m]&bits)throw std::runtime_error("Conflicting junction reservations");
        }
    }
    for(const auto& a:s.hot)if(reinterpret_cast<uintptr_t>(a.data)%32)throw std::runtime_error("Unaligned traffic storage");
}
std::vector<TrafficCarState> TrafficSimulation::debugSnapshot()const {
    const auto& s=*impl_;std::vector<TrafficCarState> result;result.reserve(s.cars.size());
    for(size_t i=0;i<s.cars.size();++i){const auto& c=s.cars[i];result.push_back({c.serial,{c.tile%MapSize,c.tile/MapSize},{c.destination%MapSize,c.destination/MapSize},s.hot[Impl::X][i],s.hot[Impl::Z][i],s.hot[Impl::Speed][i],c.repair});}
    return result;
}
bool TrafficSimulation::requestTrip(TripRequest request) {
    auto& s=*impl_;if(!s.config.purposeful || !request.id || !World::valid(request.from.x,request.from.z)||!World::valid(request.to.x,request.to.z))return false;
    if(!s.owned.insert(request.id).second)return false;s.requests.push_back(request);return true;
}
std::vector<TripEvent> TrafficSimulation::takeEvents(){auto result=std::move(impl_->events);impl_->events.clear();for(const auto& e:result)impl_->owned.erase(e.id);return result;}
size_t TrafficSimulation::outstandingTrips()const{return impl_->cars.size()+impl_->requests.size()+(impl_->searching.id?1:0)+(impl_->routeJob?impl_->routeJob->pending.size():0);}
std::vector<uint64_t> TrafficSimulation::ownedTripIds()const {
    std::vector<uint64_t> ids;const auto& s=*impl_;for(const auto& c:s.cars)if(c.trip)ids.push_back(c.trip);for(const auto& r:s.requests)ids.push_back(r.id);if(s.searching.id)ids.push_back(s.searching.id);if(s.routeJob)for(auto& input:s.routeJob->pending)if(input.trip.id)ids.push_back(input.trip.id);for(const auto& e:s.events)ids.push_back(e.id);return ids;
}
void TrafficSimulation::swap(TrafficSimulation& other) noexcept {impl_.swap(other.impl_);std::swap(paused,other.paused);}
void TrafficSimulation::saveState(std::ostream& out) const {
    using namespace binary;const auto& s=*impl_;
    write(out,uint32_t(3));write(out,s.config.seed);write(out,s.stats.ticks);write(out,s.stats.completed);write(out,s.stats.removed);write(out,s.serial);
    std::ostringstream rng;rng<<s.random;text(out,rng.str());
    auto request=[&](const TripRequest& r){write(out,r.id);write(out,r.owner);write(out,r.from.x);write(out,r.from.z);write(out,r.to.x);write(out,r.to.z);write(out,r.kind);};
    request(s.searching);auto pending=s.requests;if(s.routeJob)for(auto& input:s.routeJob->pending)if(input.trip.id)pending.push_back(input.trip);
    write(out,uint32_t(pending.size()));for(const auto& r:pending)request(r);
    write(out,uint32_t(s.events.size()));for(const auto& e:s.events){write(out,e.id);write(out,e.owner);write(out,uint8_t(e.completed));write(out,e.ticks);}
    vector(out,s.slots);vector(out,s.freeIds);vector(out,s.pool.data);for(const auto& f:s.pool.free)vector(out,f);
    write(out,uint32_t(s.cars.size()));
    for(size_t i=0;i<s.cars.size();++i){const auto& c=s.cars[i];
        write(out,c.id);write(out,c.route);write(out,c.bucket);write(out,c.count);write(out,c.next);
        write(out,c.tile);write(out,c.in);write(out,c.out);write(out,c.destination);write(out,c.held);write(out,c.arrival);
        write(out,c.serial);write(out,c.waiting);write(out,c.trip);write(out,c.tripOwner);write(out,c.started);write(out,uint8_t(c.repair));write(out,uint8_t(c.custom));
        for(const auto& a:s.hot)write(out,a[i]);
        if(c.custom){const auto& p=s.detours.at(c.id);for(auto v:p.point){write(out,v.x);write(out,v.z);}for(auto v:p.distance)write(out,v);write(out,p.length);}
    }
    vector(out,s.owner);vector(out,s.reserved);vector(out,s.exclusive);vector(out,s.unreserved);vector(out,s.legacyTiles);
    write(out,uint8_t(0));write(out,uint32_t(0));
}
void TrafficSimulation::loadState(std::istream& in,World& world) {
    using namespace binary;
    auto version=read<uint32_t>(in);if(version!=1&&version!=2&&version!=3)throw std::runtime_error("Unsupported traffic state");
    auto seed=read<uint32_t>(in);auto next=std::make_unique<Impl>(TrafficConfig{0,seed,20000,64,true});auto& s=*next;s.rebuild(world,true);
    s.stats.ticks=read<uint64_t>(in);s.stats.completed=read<uint64_t>(in);s.stats.removed=read<uint64_t>(in);s.serial=read<uint64_t>(in);
    std::istringstream rng(text(in));if(!(rng>>s.random))throw std::runtime_error("Invalid traffic random state");
    auto request=[&](){TripRequest r;r.id=read<uint64_t>(in);r.owner=read<uint64_t>(in);r.from.x=read<int>(in);r.from.z=read<int>(in);r.to.x=read<int>(in);r.to.z=read<int>(in);r.kind=read<uint8_t>(in);
        if(r.id && (!World::valid(r.from.x,r.from.z)||!World::valid(r.to.x,r.to.z)))throw std::runtime_error("Invalid trip endpoint");return r;};
    auto count=[&](uint32_t max){auto n=read<uint32_t>(in);if(n>max)throw std::runtime_error("Traffic state too large");return n;};
    s.searching=request();for(auto n=count(1000000);n--;)s.requests.push_back(request());
    for(auto n=count(1000000);n--;){TripEvent e;e.id=read<uint64_t>(in);e.owner=read<uint64_t>(in);e.completed=read<uint8_t>(in)!=0;e.ticks=read<uint64_t>(in);s.events.push_back(e);}
    vector(in,s.slots,1000000);vector(in,s.freeIds,1000000);vector(in,s.pool.data,64000000);for(auto& f:s.pool.free)vector(in,f,1000000);
    auto n=count(1000000);s.reserve(n);s.cars.resize(n);
    for(size_t i=0;i<n;++i){auto& c=s.cars[i];c.id=read<uint32_t>(in);c.route=read<uint32_t>(in);c.bucket=read<unsigned>(in);c.count=read<unsigned>(in);c.next=read<unsigned>(in);
        c.tile=read<int>(in);c.in=read<int>(in);c.out=read<int>(in);c.destination=read<int>(in);c.held=read<int>(in);c.arrival=read<float>(in);
        if(version==1&&c.held>=0){int movement=c.held%16;c.held=(c.held/16)*64+(movement/4)*8+movement%4;}
        c.serial=read<uint64_t>(in);c.waiting=read<uint64_t>(in);c.trip=read<uint64_t>(in);c.tripOwner=read<uint64_t>(in);c.started=read<uint64_t>(in);c.repair=read<uint8_t>(in)!=0;c.custom=read<uint8_t>(in)!=0;
        if(c.id>=s.slots.size() || c.bucket>=20 || uint64_t(c.route)+(uint64_t(1)<<c.bucket)>s.pool.data.size() || c.count>(1u<<c.bucket) || c.next>c.count || c.tile<0 || c.tile>=Cells || c.destination<0 || c.destination>=Cells || c.in<0 || c.in>7 || c.out<0 || c.out>7 || c.held < -1 || c.held>=Cells*64 || !std::isfinite(c.arrival))throw std::runtime_error("Invalid saved vehicle");
        for(auto& a:s.hot){a[i]=read<float>(in);if(!std::isfinite(a[i]))throw std::runtime_error("Invalid vehicle position");}
        if(c.custom){auto& p=s.detours[c.id];for(auto& v:p.point){v.x=read<float>(in);v.z=read<float>(in);}for(auto& v:p.distance)v=read<float>(in);p.length=read<float>(in);if(!std::isfinite(p.length)||p.length<=0)throw std::runtime_error("Invalid detour");}
    }
    if(version==1){
        std::vector<int> owners;std::vector<uint16_t> reserved,exclusive;
        vector(in,owners,Cells*4);vector(in,reserved,Cells);vector(in,exclusive,Cells);
        if(owners.size()!=Cells*4||reserved.size()!=Cells||exclusive.size()!=Cells)throw std::runtime_error("Invalid legacy reservations");
        for(int t=0;t<Cells;++t){for(int d=0;d<4;++d)s.owner[t*8+d]=owners[t*4+d];for(int m=0;m<16;++m){int movement=(m/4)*8+m%4;if(reserved[t]&(1<<m))s.reserved[t]|=1ull<<movement;if(exclusive[t]&(1<<m))s.exclusive[t]|=1ull<<movement;}}
    }else{vector(in,s.owner,Cells*8);vector(in,s.reserved,Cells);vector(in,s.exclusive,Cells);}vector(in,s.unreserved,Cells);vector(in,s.legacyTiles,Cells);
    if(s.owner.size()!=Cells*8||s.reserved.size()!=Cells||s.exclusive.size()!=Cells||s.unreserved.size()!=Cells)throw std::runtime_error("Invalid reservation arrays");
    s.search.active=read<uint8_t>(in)!=0;s.searchStamp=read<uint32_t>(in);
    if(s.search.active){s.search.start=read<int>(in);s.search.target=read<int>(in);s.search.car=read<int>(in);s.search.in=read<int>(in);
        if(s.search.start<0||s.search.start>=Cells||s.search.target<0||s.search.target>=Cells||s.search.car < -1||s.search.car>=int(s.slots.size())||s.search.in < -1||s.search.in>7)throw std::runtime_error("Invalid route search");
        if(version==1){
            std::vector<int> costs,parent;std::vector<uint32_t> stamps;vector(in,costs,Cells*4);vector(in,parent,Cells*4);vector(in,stamps,Cells*4);
            if(costs.size()!=Cells*4||parent.size()!=Cells*4||stamps.size()!=Cells*4)throw std::runtime_error("Invalid legacy search");
            for(int i=0;i<Cells*4;++i){int to=(i/4)*8+i%4;s.costs[to]=costs[i];s.parent[to]=parent[i]<0?-1:(parent[i]/4)*8+parent[i]%4;s.stamps[to]=stamps[i];}
        }else{vector(in,s.costs,Cells*8);vector(in,s.parent,Cells*8);vector(in,s.stamps,Cells*8);}
        if(s.costs.size()!=Cells*8||s.parent.size()!=Cells*8||s.stamps.size()!=Cells*8)throw std::runtime_error("Invalid search arrays");
        for(auto k=count(Cells*64);k--;){Impl::Open x;x.state=read<int>(in);if(version==1)x.state=(x.state/4)*8+x.state%4;x.g=read<int>(in);x.f=read<int>(in);if(x.state<0||x.state>=Cells*8)throw std::runtime_error("Invalid search node");s.open.data().push_back(x);}}
    for(auto t:s.pool.data)if(t<0||t>=Cells)throw std::runtime_error("Invalid route tile");
    for(auto t:s.legacyTiles)if(t<0||t>=Cells)throw std::runtime_error("Invalid legacy tile");
    std::vector<uint8_t> usedIds(s.slots.size());std::unordered_set<uint64_t> serials;
    std::vector<std::pair<uint64_t,uint64_t>> blocks;
    for(size_t i=0;i<s.cars.size();++i){const auto& c=s.cars[i];
        if(usedIds[c.id]++||s.slots[c.id]!=int(i)||!c.serial||c.serial>s.serial||c.started>s.stats.ticks||!serials.insert(c.serial).second||!c.count)throw std::runtime_error("Invalid saved car identity");
        blocks.push_back({c.route,uint64_t(c.route)+(uint64_t(1)<<c.bucket)});
        if(c.custom){const auto& p=s.detours.at(c.id);float last=-1;for(size_t k=0;k<p.distance.size();++k){float d=p.distance[k];if(!std::isfinite(d)||d<0||(k&&d<=last)||!std::isfinite(p.point[k].x)||!std::isfinite(p.point[k].z))throw std::runtime_error("Invalid saved detour geometry");last=d;}}
        if(version==1&&world.roundaboutOrigin({c.tile%MapSize,c.tile/MapSize}).x>=0&&!c.custom){s.hot[Impl::Progress][i]*=s.path(c).length/makePath(c.in,c.out).length;s.position(i,true);}
        if(s.hot[Impl::Progress][i]<0||s.hot[Impl::Progress][i]>s.path(c).length+0.01f)throw std::runtime_error("Invalid saved lane progress");
    }
    for(auto id:s.freeIds)if(id<0||size_t(id)>=s.slots.size()||usedIds[id]++||s.slots[id]!=-1)throw std::runtime_error("Invalid free vehicle identity");
    if(std::any_of(usedIds.begin(),usedIds.end(),[](auto used){return used!=1;}))throw std::runtime_error("Unaccounted vehicle identity");
    for(size_t b=0;b<s.pool.free.size();++b)for(auto offset:s.pool.free[b]){uint64_t end=uint64_t(offset)+(uint64_t(1)<<b);if(end>s.pool.data.size())throw std::runtime_error("Invalid free route block");blocks.push_back({offset,end});}
    std::sort(blocks.begin(),blocks.end());for(size_t i=1;i<blocks.size();++i)if(blocks[i].first<blocks[i-1].second)throw std::runtime_error("Overlapping route blocks");
    if(s.search.active)for(size_t i=0;i<s.stamps.size();++i)if(s.stamps[i]==s.searchStamp){int parent=s.parent[i];
        if(s.costs[i]<0||s.costs[i]>100000000||parent < -1||parent>=Cells*8)throw std::runtime_error("Invalid search costs");
        if(parent>=0&&(s.stamps[parent]!=s.searchStamp||s.costs[parent]>=s.costs[i]))throw std::runtime_error("Invalid route parent chain");}
    s.search.active=false; // Search scratch is private to the worker; restart logical requests.
    s.occupancy();s.stats.active=s.cars.size();s.stats.pending=s.requests.size()+(s.searching.id?1:0);
    TrafficSimulation candidate({0,seed,20000,64,true});candidate.impl_=std::move(next);candidate.validate(world);auto tripIds=candidate.ownedTripIds();std::sort(tripIds.begin(),tripIds.end());if(std::adjacent_find(tripIds.begin(),tripIds.end())!=tripIds.end())throw std::runtime_error("Duplicate owned trip");candidate.impl_->owned.insert(tripIds.begin(),tripIds.end());swap(candidate);
}

}
