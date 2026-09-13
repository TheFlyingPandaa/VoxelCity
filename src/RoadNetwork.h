#pragma once
#include "World.h"

namespace vc {
// Compatibility scale: retain current geometry and vehicle speeds.
inline constexpr float WorldUnitsPerMeter=1.f;
using NetworkId=uint64_t;
struct RoadNode { NetworkId id; Cell cell; float x,y,z; };
struct RoadSegment {
    NetworkId id,from,to;
    float lengthMeters;
    RoadClass fromClass,toClass;
};
struct RoadLane { NetworkId id,segment,from,to; unsigned index; float speedMetersPerSecond; };

// Immutable after construction. IDs describe legacy locations, not lifetimes of
// demolished/rebuilt assets. Never persist these IDs as owned journey references.
class RoadNetwork {
public:
    explicit RoadNetwork(const World&);
    const std::vector<RoadNode>& nodes()const{return nodes_;}
    const std::vector<RoadSegment>& segments()const{return segments_;}
    const std::vector<RoadLane>& lanes()const{return lanes_;}
    bool canTravel(Cell from,Cell to)const;
    size_t memoryBytes()const;
    static NetworkId nodeId(Cell c){return World::valid(c.x,c.z)?NetworkId(c.z*MapSize+c.x)+1:0;}
private:
    std::vector<RoadNode> nodes_;
    std::vector<RoadSegment> segments_;
    std::vector<RoadLane> lanes_;
    std::vector<uint8_t> outgoing_=std::vector<uint8_t>(MapSize*MapSize);
};
}
