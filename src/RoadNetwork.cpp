#include "RoadNetwork.h"
#include <algorithm>
#include <cmath>

namespace vc {
RoadNetwork::RoadNetwork(const World& world){
    for(int z=0;z<MapSize;++z)for(int x=0;x<MapSize;++x){
        Cell a{x,z};if(!world.road(x,z)||world.roadClass(a)==RoadClass::Median)continue;
        auto aid=nodeId(a);
        nodes_.push_back({aid,a,float(x*TileSize+TileSize/2),0,float(z*TileSize+TileSize/2)});
        auto mask=world.connections(x,z);
        for(int d=0;d<8;++d)if(mask&(1<<d)){
            Cell b{x+RoadDX[d],z+RoadDZ[d]};auto bid=nodeId(b);
            if(world.canTravel(a,b))outgoing_[z*MapSize+x]|=uint8_t(1<<d);
            if(aid>=bid)continue;
            // Coordinate-derived IDs survive rebuilds, rule edits and unrelated construction.
            auto sid=(aid-1)*8+unsigned(d)+1;
            segments_.push_back({sid,aid,bid,TileSize*std::hypot(float(RoadDX[d]),float(RoadDZ[d]))/WorldUnitsPerMeter,world.roadClass(a),world.roadClass(b)});
            unsigned count=std::min(world.lanesPerDirection(a),world.lanesPerDirection(b));
            float speed=std::min(world.speedLimit(a),world.speedLimit(b))/WorldUnitsPerMeter;
            for(unsigned reverse=0;reverse<2;++reverse){
                Cell from=reverse?b:a,to=reverse?a:b;
                if(!world.canTravel(from,to))continue;
                for(unsigned lane=0;lane<count;++lane)
                    lanes_.push_back({sid*8+reverse*4+lane,sid,nodeId(from),nodeId(to),lane,speed});
            }
        }
    }
}
bool RoadNetwork::canTravel(Cell a,Cell b)const{
    if(!nodeId(a)||!nodeId(b))return false;
    for(int d=0;d<8;++d)if(b==Cell{a.x+RoadDX[d],a.z+RoadDZ[d]})return (outgoing_[a.z*MapSize+a.x]&(1<<d))!=0;
    return false;
}
size_t RoadNetwork::memoryBytes()const{
    return nodes_.capacity()*sizeof(RoadNode)+segments_.capacity()*sizeof(RoadSegment)+lanes_.capacity()*sizeof(RoadLane)+outgoing_.capacity();
}
}
