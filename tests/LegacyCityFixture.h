#pragma once
#include "City.h"
#include "Binary.h"
#include <sstream>
// Produce the pre-rail (v6) payload for compatibility tests. These fixtures have no export shipments.
inline std::string preRailPayload(const std::string& bytes,const vc::World& world,const vc::CitySimulation& city){
    std::string payload=bytes.substr(32);std::istringstream in(payload,std::ios::binary);
    in.seekg(vc::MapSize*vc::MapSize*3+176);size_t first=0;
    for(int i=0;i<int(vc::BuildingKind::Count);++i){if(i==int(vc::BuildingKind::PassengerStation))first=size_t(in.tellg());vc::binary::text(in,256);for(int k=0;k<5;++k)vc::binary::read<int>(in);}
    size_t last=size_t(in.tellg());std::ostringstream rail(std::ios::binary);world.writeRails(rail);city.railway.save(rail);
    payload.resize(payload.size()-rail.str().size()-city.buildings().size()*4-4);
    payload.erase(first,last-first);return payload;
}
