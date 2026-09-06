#define vsMain worldVsMain
#include "World.hlsl"
#undef vsMain
Pixel vsMain(Input v,float2 offset:INSTANCEOFFSET) {
    v.position+=float3(offset.x,0,offset.y);
    return worldVsMain(v);
}
