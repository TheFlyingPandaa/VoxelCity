#include "City.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace vc {
std::vector<Cell> CitySimulation::facilityFootprint(const Building& b,bool platform){
    if(!railFacility(b.kind))return platform?std::vector<Cell>{}:std::vector<Cell>{b.cell};
    int r=b.variant%4;Cell along{RoadDX[(r+1)%4],RoadDZ[(r+1)%4]},side{RoadDX[(r+2)%4],RoadDZ[(r+2)%4]};
    std::vector<Cell> cells;for(int i=0;i<4;++i)cells.push_back({b.cell.x+along.x*i+(platform?side.x:0),b.cell.z+along.z*i+(platform?side.z:0)});return cells;
}
std::vector<Cell> CitySimulation::occupiedFootprint(const Building& b){auto all=facilityFootprint(b);auto platform=facilityFootprint(b,true);all.insert(all.end(),platform.begin(),platform.end());return all;}
bool CitySimulation::buildRail(World& w,Cell a,Cell b,bool erase,std::string& message){
    if(!World::valid(a.x,a.z)||!World::valid(b.x,b.z)|| (a.x!=b.x&&a.z!=b.z)){message="Drag railway along one grid axis; join strokes to turn.";return false;}
    std::vector<Cell> cells;int dx=(b.x>a.x)-(b.x<a.x),dz=(b.z>a.z)-(b.z<a.z);for(Cell c=a;;c={c.x+dx,c.z+dz}){cells.push_back(c);if(c==b)break;}
    if(erase&&w.rail(a).bridge>=0){int owner=w.rail(a).bridge;cells.clear();for(int i=0;i<MapSize*MapSize;++i)if(w.rails()[i].bridge==owner)cells.push_back({i%MapSize,i/MapSize});}
    int changed=0;for(auto c:cells){auto r=w.rail(c);if(erase){if(r.flags&2){message="Regional railway is maintained by neighboring cities.";return false;}if(at(c)){message="Remove the station before its platform track.";return false;}changed+=r.flags!=0;continue;}
        auto* facility=at(c);bool platform=false;if(facility&&railFacility(facility->kind)){auto track=facilityFootprint(*facility,true);platform=std::find(track.begin(),track.end(),c)!=track.end();int direction=(facility->variant+1)%4;uint8_t mask=uint8_t((1<<direction)|(1<<oppositeDirection(direction)));if((dx&&!(mask&East))||(dz&&!(mask&North)))platform=false;}
        if((facility&&!platform)||w.roundaboutOrigin(c).x>=0){message="Railway footprint is occupied.";return false;}
        if(w.roadOccupies(c)&&r.height<1){auto m=w.connections(c.x,c.z);bool cross=(dx&&m==(North|South))||(dz&&m==(East|West));if(w.roadClass(c)!=RoadClass::Street||!cross||r.links&&r.links!=uint8_t(dx?(East|West):(North|South))){message="Use a perpendicular street crossing or a highway overpass.";return false;}}
        if(r.bridge>=0&&((dx&&!(r.links&(East|West)))||(dz&&!(r.links&(North|South))))){message="Connect railway at the ends of the overpass.";return false;}changed+=!r.flags;
    }
    double cost=changed*(erase?5:40);if(!sandbox&&treasury<cost){message="Insufficient funds for railway construction.";return false;}
    if(erase){for(auto c:cells){auto r=w.rail(c);for(int d=0;d<4;++d)if(r.links&(1<<d)){Cell n{c.x+RoadDX[d],c.z+RoadDZ[d]};auto v=w.rail(n);v.links&=~(1<<oppositeDirection(d));w.setRail(n,v);}w.setRail(c,{});}}
    else {for(auto c:cells){auto r=w.rail(c);r.flags|=1;w.setRail(c,r);}for(size_t i=1;i<cells.size();++i){auto c=cells[i-1],n=cells[i];int d=dx? (dx>0?1:3):(dz>0?2:0);auto r=w.rail(c),v=w.rail(n);r.links|=1<<d;v.links|=1<<oppositeDirection(d);w.setRail(c,r);w.setRail(n,v);}}
    if(!sandbox)treasury-=cost;++revision;message=erase?"Railway removed.":"Double-track railway built. Extend from an existing track to connect it.";return true;
}
bool CitySimulation::placeRailFacility(World& w,Cell c,BuildingKind kind,int rotation,std::string& message){
    if(!railFacility(kind))return false;Building b;b.cell=c;b.kind=kind;b.variant=uint8_t(rotation%4);b.level=1;
    auto strip=facilityFootprint(b),track=facilityFootprint(b,true);int dir=(rotation+1)%4;uint8_t mask=(1<<dir)|(1<<oppositeDirection(dir));
    for(auto p:strip)if(!World::valid(p.x,p.z)||w.roadOccupies(p)||w.hasRail(p)||at(p)){message="The complete station building footprint must be clear.";return false;}
    for(auto p:track)if(!World::valid(p.x,p.z)||w.roadOccupies(p)||at(p)||w.rail(p).bridge>=0||(w.rail(p).links&~mask)){message="Platform needs four straight, clear ground-level railway tiles.";return false;}
    double cost=definition(kind).cost;for(auto p:track)if(!w.hasRail(p))cost+=40;
    if(!sandbox&&treasury<cost){message="Insufficient funds for facility and platform track.";return false;}
    for(int i=0;i<4;++i){auto r=w.rail(track[i]);r.flags|=1;if(i)r.links|=1<<oppositeDirection(dir);if(i<3)r.links|=1<<dir;w.setRail(track[i],r);}
    b.id=nextId_++;b.age=0;size_t index=buildings_.size();ids_[b.id]=index;buildings_.push_back(b);for(auto p:occupiedFootprint(b))parcel_[p.z*MapSize+p.x]=int(index);
    if(!sandbox)treasury-=cost;++revision;accessDirty_=true;++accessRevision_;publish(w);message="Rail facility built. Connect its platform ends, local road frontage and utilities.";return true;
}
bool CitySimulation::buildRailOverpass(World& w,Cell c,std::string& message){
    // Choose the perpendicular axis from the carriageway geometry, including the regional median.
    bool horizontalRoad=false;int low=0,high=0;bool found=false;
    for(int axis=0;axis<2&&!found;++axis){int first=100,last=-100;for(int o=-3;o<=3;++o){Cell p=axis?Cell{c.x+o,c.z}:Cell{c.x,c.z+o};if(w.highway(p)){first=std::min(first,o);last=std::max(last,o);}}
        if(first<last){found=true;horizontalRoad=axis==0;low=first-4;high=last+4;}}
    if(!found){message="Place overpass at the center of a straight divided highway.";return false;}
    std::vector<Cell> cells;int owner=c.z*MapSize+c.x;uint8_t mask=horizontalRoad?North|South:East|West;
    for(int o=low;o<=high;++o){Cell p=horizontalRoad?Cell{c.x,c.z+o}:Cell{c.x+o,c.z};if(!World::valid(p.x,p.z)||at(p)||w.rail(p).bridge>=0||(w.hasRail(p)&&(w.rail(p).flags&2||w.rail(p).links&~mask))||w.roundaboutOrigin(p).x>=0){message="Overpass ramps and deck need a clear corridor.";return false;}
        if(w.road(p.x,p.z)){auto links=w.connections(p.x,p.z);if(!w.highway(p)||links&uint8_t(horizontalRoad?(North|South):(East|West))||o<low+4||o>high-4){message="Overpass must cross straight highway lanes with clear ramps.";return false;}}cells.push_back(p);}
    if(!sandbox&&treasury<3000){message="Insufficient funds for highway overpass ($3,000).";return false;}
    for(size_t i=0;i<cells.size();++i){auto r=w.rail(cells[i]);r.flags=1;r.bridge=owner;r.height=std::min({float(i)*2,float(cells.size()-1-i)*2,8.f});int d=horizontalRoad?2:1;if(i)r.links|=1<<oppositeDirection(d);if(i+1<cells.size())r.links|=1<<d;w.setRail(cells[i],r);}
    if(!sandbox)treasury-=3000;++revision;message="Rail overpass built. Connect tracks to both ramp ends.";return true;
}
void CitySimulation::updateRailServices(World& w){
    std::vector<RailStation> stations;
    for(auto& b:buildings_)if(railFacility(b.kind)){auto track=facilityFootprint(b,true);bool connected=true;for(auto c:track)connected&=w.hasRail(c);
        bool utility=std::min({b.utilities[0],b.utilities[1],b.utilities[2]})>=.95f;bool active=connected&&b.component>=0&&utility;
        stations.push_back({b.id,track[1],uint8_t(int(b.kind)-int(BuildingKind::PassengerStation)+1),active,track.front(),track.back()});
        b.problem=!connected?"Platform railway missing":b.component<0?"No adjacent local road":!utility?"Insufficient utility capacity":!railway.regional(b.id)&&b.kind!=BuildingKind::TrainDepot?"No regional rail connection; local service needs a depot":"";
    }railway.setStations(std::move(stations));
}
int CitySimulation::waitingRailCargo(uint64_t station)const{int amount=0;for(auto shipment:railExports_)if(shipment.terminal==station&&!shipment.train)amount+=shipment.amount;for(auto trip:trips_)if(trip.kind==7&&trip.destination==station)amount+=trip.cargo;return amount;}
void CitySimulation::railEvents(World&,TrafficSimulation& traffic){
    for(auto e:railway.takeEvents()){
        auto* b=mutableFind(e.station);
        if(e.journey){for(auto& h:households_)if(h.id==e.journey){h.commuteQuality=e.completed?1.f:.25f;h.travelSeconds=e.seconds;if(!e.completed&&h.workplace)sendTrip(traffic,h.home,h.workplace,0,0,h.id);if(e.completed){if(auto* home=mutableFind(h.home))home->happiness=std::min(100.f,home->happiness+2);}}continue;}
        if(e.kind==1&&b){b->railPassengers=std::min(120,b->railPassengers+120);}
        if(e.kind==2&&b){b->inventory=std::min(120,b->inventory+120);for(auto& shipment:railExports_)if(shipment.terminal==b->id&&!shipment.train)shipment.train=e.train;}
        if(e.kind==4||!e.completed){for(auto it=railExports_.begin();it!=railExports_.end();){if(it->train==e.train){if(e.completed){if(!sandbox)treasury+=it->amount*.5;railway.recordCargo(it->amount);++stats_.deliveries;}else if(auto* source=mutableFind(it->source))source->inventory+=it->amount;it=railExports_.erase(it);}else ++it;}}
    }
    for(auto it=railExports_.begin();it!=railExports_.end();)if(!it->train&&!find(it->terminal)){if(auto* source=mutableFind(it->source))source->inventory+=it->amount;it=railExports_.erase(it);}else ++it;
}
void CitySimulation::railwayScenario(World& w,TrafficSimulation& traffic){
    newCity(w,traffic);sandbox=true;std::string message;auto require=[&](bool ok){if(!ok)throw std::runtime_error("Railway scenario: "+message);};
    require(buildRailOverpass(w,{236,206},message));require(buildRail(w,{236,192},{236,200},false,message));require(buildRail(w,{236,212},{236,222},false,message));require(buildRail(w,{236,222},{280,222},false,message));
    require(buildRoad(w,{248,216},{248,220},false,message));require(buildRoad(w,{230,220},{282,220},false,message));
    require(placeRailFacility(w,{240,221},BuildingKind::PassengerStation,0,message));require(placeRailFacility(w,{260,221},BuildingKind::PassengerStation,0,message));require(placeRailFacility(w,{270,221},BuildingKind::CargoTerminal,0,message));require(placeRailFacility(w,{250,221},BuildingKind::TrainDepot,0,message));
    require(place(w,{240,219},BuildingKind::Power,message));require(place(w,{241,219},BuildingKind::Water,message));require(place(w,{242,219},BuildingKind::Sewage,message));
    for(int x=250;x<270;++x)require(place(w,{x,219},x%4==0?BuildingKind::Industrial:x%5==0?BuildingKind::Commercial:BuildingKind::Residential,message));
    rebuild(w,true);publish(w);
}

}
