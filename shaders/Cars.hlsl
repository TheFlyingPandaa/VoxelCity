cbuffer Scene : register(b0) {
    row_major float4x4 viewProjection;
    float4 chunkOffset, eye, hover, sunlight, options;
};
RaytracingAccelerationStructure scene : register(t0);
struct Input {float3 position:POSITION;float3 normal:NORMAL;uint material:MATERIAL;float4 instance:INSTANCE;uint color:COLOR;float height:HEIGHT;};
struct Pixel {float4 position:SV_POSITION;float3 world:WORLD;float3 normal:NORMAL;nointerpolation uint material:MATERIAL;nointerpolation uint color:COLOR;};
Pixel vsMain(Input v) {
    Pixel p;float2 forward=normalize(v.instance.zw),right=float2(forward.y,-forward.x);
    float2 xz=v.instance.xy+right*v.position.x+forward*v.position.z;
    p.world=float3(xz.x,v.position.y+v.height,xz.y);p.position=mul(float4(p.world,1),viewProjection);
    float2 n=right*v.normal.x+forward*v.normal.z;p.normal=float3(n.x,v.normal.y,n.y);
    p.material=v.material;p.color=v.color;return p;
}
float4 psMain(Pixel p):SV_TARGET {
    float3 palette[6]={float3(.85,.18,.12),float3(.12,.45,.82),float3(.93,.73,.18),float3(.82,.85,.81),float3(.2,.65,.4),float3(.48,.24,.68)};
    float3 color=palette[p.color%6];
    if(p.material==1)color=float3(.12,.22,.28);
    if(p.material==2)color=float3(.04,.05,.06);
    if(p.material==3)color=float3(.95,.95,.7);
    if(p.material==4)color=float3(.65,.67,.65);
    float3 light=normalize(sunlight.xyz);float ndl=saturate(dot(p.normal,light)),visible=1;
    if(options.x>.5 && ndl>0) {
        RayDesc ray;ray.Origin=p.world+p.normal*.015;ray.Direction=light;ray.TMin=.001;ray.TMax=options.w*2;
        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE> query;
        query.TraceRayInline(scene,RAY_FLAG_NONE,0xff,ray);while(query.Proceed()){}
        visible=query.CommittedStatus()==COMMITTED_NOTHING?1:0;
    }
    color*=.42+.78*ndl*visible;
    color=lerp(color,float3(.26,.34,.39),saturate((length(eye.xyz-p.world)-1200)/6500)*.5);
    return float4(pow(saturate(color),1/2.2),1);
}
