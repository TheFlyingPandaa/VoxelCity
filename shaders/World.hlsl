cbuffer Scene : register(b0) {
    row_major float4x4 viewProjection;
    float4 chunkOffset;
    float4 eye;
    float4 hover; // tile x/z, erase, grid
    float4 sunlight;
    float4 options; // shadows, road tile size, grid size, world size
};
RaytracingAccelerationStructure scene : register(t0);
StructuredBuffer<uint> tileStyles : register(t1);
struct Input { float3 position:POSITION; float3 normal:NORMAL; uint material:MATERIAL; };
struct Pixel { float4 position:SV_POSITION; float3 world:WORLD; float3 normal:NORMAL; nointerpolation uint material:MATERIAL; };
Pixel vsMain(Input v) {
    Pixel o;
    o.world=v.position+float3(chunkOffset.x,0,chunkOffset.y);
    o.position=mul(float4(o.world,1),viewProjection);
    o.normal=v.normal; o.material=v.material;
    return o;
}
float4 psMain(Pixel p):SV_TARGET {
    float3 colors[18]={float3(0.23,0.36,0.24),float3(0.105,0.13,0.155),float3(0.91,0.84,0.57),float3(0.56,0.61,0.59),float3(0.40,0.43,0.42),float3(0.25,0.65,0.38),float3(0.20,0.48,0.78),float3(0.78,0.56,0.18),float3(0.45,0.70,0.72),float3(0.65,0.42,0.65),float3(0.25,0.28,0.32),float3(0.8,0.88,0.72),float3(0.85,0.16,0.13),float3(0.2,0.8,0.35),float3(0.95,0.65,0.15),float3(0.20,0.22,0.24),float3(.25,.13,.055),float3(.11,.22,.065)};
    float3 color=colors[min(p.material,17u)];
    uint2 tile=min(uint2(p.world.xz/options.y),511u);
    uint style=tileStyles[tile.y*512+tile.x],tint=style&3u,rule=style>>2;
    if(tint)color=lerp(color,tint==1?float3(0.85,0.12,0.10):tint==2?float3(0.95,0.6,0.12):float3(0.12,0.8,0.3),0.65);
    if(p.world.y<0.1 && (rule&16u))color*=0.82;
    if(p.world.y<0.1 && rule<64u&&(rule&7u)>0){
        float2 headings[4]={float2(0,-1),float2(1,0),float2(0,1),float2(-1,0)};
        float2 heading=headings[(rule&7u)-1],local=fmod(p.world.xz,options.y)-8;
        float along=dot(local,heading),side=dot(local,float2(-heading.y,heading.x));
        if((abs(side)<0.5&&abs(along)<3)||(along>1&&along<4&&abs(side)<(4-along)*0.8))color=float3(0.92,0.90,0.70);
    }
    if(p.world.y<0.1 && rule<64u&&(rule&8u) && length(fmod(p.world.xz,options.y)-float2(4,4))<1.2)color=sunlight.w<0.5?float3(0.2,0.9,0.3):float3(0.9,0.2,0.1);
    if(rule>=64u&&rule<=72u){
        uint index=rule-64u;float2 offset=float2(int(index%3)-1,int(index/3)-1)*16;
        float2 radial=normalize(offset),heading=float2(radial.y,-radial.x);
        float2 local=fmod(p.world.xz,16)-8+offset-radial*16;
        float along=dot(local,heading),side=dot(local,float2(-heading.y,heading.x));
        if((abs(side)<.4&&abs(along)<2)||(along>.5&&along<2.5&&abs(side)<(2.5-along)*.65))color=float3(.92,.90,.70);
    }
    float dist=length(eye.xyz-p.world);
    float2 voxel=floor(p.world.xz);
    float checker=fmod(voxel.x+voxel.y,2);
    color*=1+(checker-0.5)*0.035*saturate(1-dist/900);
    float3 light=normalize(sunlight.xyz);
    float ndl=saturate(dot(p.normal,light));
    float visible=1;
    if(options.x>0.5 && ndl>0) {
        RayDesc ray;
        ray.Origin=p.world+p.normal*0.015;
        ray.Direction=light;
        ray.TMin=0.001; ray.TMax=options.w*2;
        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE> query;
        query.TraceRayInline(scene,RAY_FLAG_NONE,0xff,ray);
        while(query.Proceed()) {}
        visible=query.CommittedStatus()==COMMITTED_NOTHING?1:0;
    }
    color*=0.42+0.78*ndl*visible;
    if(hover.w>0.5 && p.normal.y>0.5) {
        float2 grid=p.world.xz/options.z;
        float2 edge=abs(frac(grid-0.5)-0.5)/max(fwidth(grid),0.0001);
        float gridLine=1-saturate(min(edge.x,edge.y));
        color=lerp(color,color*0.60,gridLine*0.6*saturate(1-dist/1100));
    }
    if(all(floor(p.world.xz/options.y)>=hover.xy) && all(floor(p.world.xz/options.y)<hover.xy+floor(hover.z/2)+1) && hover.x>=0) {
        float3 tint=fmod(hover.z,2)>0.5?float3(1,0.25,0.19):float3(0.3,0.85,0.92);
        color=lerp(color,tint,0.40);
    }
    color=lerp(color,float3(0.26,0.34,0.39),saturate((dist-1200)/6500)*0.5);
    return float4(pow(saturate(color),1/2.2),1);
}
