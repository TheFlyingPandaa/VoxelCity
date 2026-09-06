cbuffer Scene : register(b0) {
    row_major float4x4 inverseVP, currentVP, previousVP;
    float4 eye, oldEye, sunlight, screen, settings, hover;
};
struct Primitive {float3 lo;uint data;float3 hi;uint material;};
struct Instance {uint primitive,words,id,color;float4 pose,previous;};
RaytracingAccelerationStructure scene : register(t0);
StructuredBuffer<Primitive> primitives : register(t1);
StructuredBuffer<uint> voxels : register(t2);
StructuredBuffer<Instance> instances : register(t3);
StructuredBuffer<uint> tileStyles : register(t4);
Texture2D<float4> positionTex : register(t5);
Texture2D<float4> normalTex : register(t6);
Texture2D<float4> albedoTex : register(t7);
Texture2D<float4> motionTex : register(t8);
Texture2D<float4> oldPositionTex : register(t9);
Texture2D<float4> oldNormalTex : register(t10);
Texture2D<float4> oldLightTex : register(t11);
Texture2D<float4> lightTex : register(t12);
Texture2D<float4> resolvedTex : register(t13);
SamplerState linearClamp : register(s0);
struct Pixel {float4 position:SV_POSITION;float2 uv:TEXCOORD;};
Pixel vsMain(uint id:SV_VertexID){Pixel p;p.uv=float2((id<<1)&2,id&2);p.position=float4(p.uv*float2(2,-2)+float2(-1,1),0,1);return p;}
uint hash(uint x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
float random(inout uint seed){seed=hash(seed);return (seed&0xffffffu)/16777216.;}
float2 jitter(){uint f=uint(screen.z)%8;float2 j[8]={float2(.5,.3333),float2(.25,.6667),float2(.75,.1111),float2(.125,.4444),float2(.625,.7778),float2(.375,.2222),float2(.875,.5556),float2(.0625,.8889)};return j[f]-.5;}
float3 cameraRay(float2 uv){float4 v=mul(float4(uv*float2(2,-2)+float2(-1,1),.5,1),inverseVP);return normalize(v.xyz/v.w);}
float3 transformNormal(float3 n,float4 pose){return float3(n.x*pose.w+n.z*pose.z,n.y,-n.x*pose.z+n.z*pose.w);}
bool brickHit(Primitive b,uint base,float3 o,float3 d,float minT,float maxT,float lod,out float t,out float3 n,out uint material){
    float enter=-1e30,leave=maxT;uint axis=0;
    [unroll]for(uint a=0;a<3;++a){
        if(abs(d[a])<1e-10){if(o[a]<b.lo[a]||o[a]>=b.hi[a]){t=0;n=0;material=0;return false;}}
        else {float t0=(b.lo[a]-o[a])/d[a],t1=(b.hi[a]-o[a])/d[a];float nearT=min(t0,t1);if(nearT>enter){enter=nearT;axis=a;}leave=min(leave,max(t0,t1));}
    }
    t=max(enter,minT);n=0;n[axis]=d[axis]>0?-1:1;material=b.material;
    if(t>leave||leave<minT)return false;
    if(material!=0)return true;
    if(lod>0&&t>lod){material=b.data>>24;return material!=0;}
    int3 cell=clamp(int3(floor((o+d*(t+1e-5)-b.lo)*4)),0,7);
    int3 step=int3(d.x>=0?1:-1,d.y>=0?1:-1,d.z>=0?1:-1);
    float3 delta,next;
    [unroll]for(uint a=0;a<3;++a){delta[a]=abs(d[a])>1e-10?abs(.25/d[a]):1e30;next[a]=abs(d[a])>1e-10?(b.lo[a]+(cell[a]+(step[a]>0?1:0))*.25-o[a])/d[a]:1e30;}
    [loop]for(uint i=0;i<25&&t<=leave;++i){
        uint index=uint(cell.x+8*(cell.y+8*cell.z));material=(voxels[base+(b.data&0xffffffu)+index/4]>>((index%4)*8))&255u;
        if(material!=0)return true;
        axis=next.x<=next.y?(next.x<=next.z?0:2):(next.y<=next.z?1:2);
        t=next[axis];next[axis]+=delta[axis];cell[axis]+=step[axis];n=0;n[axis]=-step[axis];
        if(cell[axis]<0||cell[axis]>=8)break;
    }return false;
}
struct Hit {float t;float3 normal;uint material;uint instance;};
Hit trace(float3 origin,float3 direction,float maxT,bool shadow,float lod){
    Hit h;h.t=maxT;h.normal=0;h.material=0;h.instance=0;
    RayDesc ray;ray.Origin=origin;ray.Direction=direction;ray.TMin=.003;ray.TMax=maxT;
    RayQuery<RAY_FLAG_SKIP_TRIANGLES> q;q.TraceRayInline(scene,RAY_FLAG_NONE,255,ray);
    [loop]while(q.Proceed()){
        Instance object=instances[q.CandidateInstanceID()];Primitive brick=primitives[object.primitive+q.CandidatePrimitiveIndex()];
        float t;float3 n;uint material;
        if(brickHit(brick,object.words,q.CandidateObjectRayOrigin(),q.CandidateObjectRayDirection(),ray.TMin,h.t,lod,t,n,material)){
            q.CommitProceduralPrimitiveHit(t);h.t=t;h.normal=transformNormal(n,object.pose);h.material=material;h.instance=q.CandidateInstanceID();if(shadow){q.Abort();break;}
        }
    }return h;
}
float3 sky(float3 d){return lerp(float3(.62,.74,.88),float3(.20,.40,.68),saturate(d.y))*.65;}
struct Material {float3 color;float roughness;float metal;float emission;};
Material materialAt(uint id,uint variant,float3 p){
    Material m;m.color=float3(.55,.55,.55);m.roughness=.85;m.metal=0;m.emission=0;
    switch(id){
        case 1:m.color=float3(.105,.155,.075);break;
        case 2:m.color=float3(.075,.085,.092);m.roughness=.9;break;
        case 3:m.color=float3(.72,.68,.49);break;
        case 4:m.color=float3(.46,.46,.41);break;
        case 5:m.color=float3(.42,.20,.12);break;
        case 6:m.color=float3(.69,.62,.46);break;
        case 7:m.color=float3(.19,.13,.105);m.roughness=.75;break;
        case 8:m.color=float3(.055,.10,.115);m.roughness=.12;break;
        case 9:m.color=float3(.40,.43,.44);m.roughness=.38;m.metal=.8;break;
        case 10:m.color=float3(.25,.13,.055);break;
        case 11:m.color=float3(.11,.22,.065);break;
        case 12:m.color=float3(.018,.021,.024);break;
        case 13:{float3 palette[6]={float3(.45,.075,.035),float3(.035,.12,.27),float3(.65,.43,.09),float3(.65,.64,.60),float3(.055,.20,.12),float3(.19,.07,.11)};m.color=palette[variant%6];m.roughness=.25;m.metal=.25;break;}
        case 14:m.color=float3(.9,.79,.48);m.roughness=.25;m.emission=.6;break;
        case 15:m.color=float3(.42,.46,.44);break;
        case 16:m.color=float3(.39,.40,.37);break;
        case 17:m.color=float3(.57,.50,.36);break;
        case 18:m.color=float3(.74,.72,.64);m.roughness=.6;break;
    }
    uint3 cell=uint3(int3(floor(p*4)));float noise=(hash(cell.x^hash(cell.y)^hash(cell.z))&255)/255.;
    if(id!=8&&id!=13&&id!=14)m.color*=.94+.12*noise;
    if(id==5||id==6||id==15||id==17)m.color*=.85+.3*((hash(variant)&255)/255.);
    return m;
}
float3 hemisphere(float3 n,inout uint seed){float a=random(seed)*6.2831853,r=sqrt(random(seed));float3 tangent=normalize(cross(abs(n.y)<.95?float3(0,1,0):float3(1,0,0),n));return normalize(tangent*(cos(a)*r)+cross(n,tangent)*(sin(a)*r)+n*sqrt(1-r*r));}
float3 sunDirection(inout uint seed){float3 sun=normalize(sunlight.xyz);float3 d=hemisphere(sun,seed);return normalize(sun+d*.018);}
float3 brdf(Material m,float3 n,float3 v,float3 l){
    float nl=saturate(dot(n,l)),nv=max(.001,dot(n,v));float3 h=normalize(l+v);float nh=saturate(dot(n,h)),vh=saturate(dot(v,h));
    float a=max(.025,m.roughness*m.roughness),a2=a*a,den=nh*nh*(a2-1)+1;float D=a2/(3.14159265*den*den);
    float k=(m.roughness+1)*(m.roughness+1)/8;float G=nv/(nv*(1-k)+k)*nl/max(.001,nl*(1-k)+k);
    float3 F=lerp(.04.xxx,m.color,m.metal)+(1-lerp(.04.xxx,m.color,m.metal))*pow(1-vh,5);
    return (m.color*(1-m.metal)/3.14159265+F*(D*G/max(.001,4*nv*nl)))*nl;
}
float3 directAt(Material m,float3 p,float3 n,float3 v,inout uint seed){
    float3 l=sunDirection(seed);float visibility=1;
    if(settings.y>.5&&dot(n,l)>0)visibility=trace(p+n*.012,l,16384,true,0).material==0?1:0;
    return brdf(m,n,v,l)*float3(3.8,3.35,2.65)*visibility;
}
// The diffuse bounce excludes specular caustics: these need a separate sampling
// strategy and otherwise produce isolated fireflies around glass and metal.
float3 diffuseAt(Material m,float3 p,float3 n,inout uint seed){
    float3 l=sunDirection(seed);float visibility=1;
    if(settings.y>.5&&dot(n,l)>0)visibility=trace(p+n*.012,l,16384,true,0).material==0?1:0;
    return m.color*(1-m.metal)/3.14159265*saturate(dot(n,l))*float3(3.8,3.35,2.65)*visibility;
}
struct Surface {float4 position:SV_TARGET0;float4 normal:SV_TARGET1;float4 albedo:SV_TARGET2;float4 motion:SV_TARGET3;};
Surface surfaceMain(Pixel pixel){
    Surface s=(Surface)0;float2 uv=(pixel.position.xy+jitter())/screen.xy;float3 ray=cameraRay(uv);
    Hit h=trace(eye.xyz,ray,32768,false,screen.y*(settings.x==0?.8:settings.x==1?1.2:1.8));
    if(!h.material){s.position=float4(0,0,0,0);s.albedo=float4(sky(ray),1);return s;}
    float3 p=eye.xyz+ray*h.t;Instance inst=instances[h.instance];Material m=materialAt(h.material,inst.color,p);
    s.position=float4(p,float(inst.id));s.normal=float4(h.normal,h.material);s.albedo=float4(m.color,m.roughness);
    float2 local=float2(dot(p.xz-inst.pose.xy,float2(inst.pose.w,-inst.pose.z)),dot(p.xz-inst.pose.xy,inst.pose.zw));
    float3 oldP=float3(inst.previous.x+local.x*inst.previous.w+local.y*inst.previous.z,p.y,inst.previous.y-local.x*inst.previous.z+local.y*inst.previous.w);
    float4 oldClip=mul(float4(oldP,1),previousVP);float2 oldUV=oldClip.xy/max(.001,oldClip.w)*float2(.5,-.5)+.5;
    s.motion=float4(oldUV-pixel.uv,inst.id>=0x800000u?1:0,0);return s;
}
float4 lightMain(Pixel pixel):SV_TARGET {
    int2 full=min(int2(pixel.position.xy)*2,int2(screen.xy)-1);float4 p=positionTex.Load(int3(full,0));
    if(p.w==0)return float4(albedoTex.Load(int3(full,0)).rgb,1);
    float4 nm=normalTex.Load(int3(full,0)),albedo=albedoTex.Load(int3(full,0));float3 n=nm.xyz,v=normalize(eye.xyz-p.xyz);uint material=uint(nm.w);
    Material m=materialAt(material,0,p.xyz);m.color=albedo.rgb;m.roughness=albedo.w;
    uint seed=hash(uint(full.x)+uint(full.y)*uint(screen.x)+uint(screen.z)*134775813u);
    float3 color=directAt(m,p.xyz,n,v,seed)+m.color*m.emission;
    uint samples=settings.x>=2?2:1;float3 indirect=0;
    [loop]for(uint i=0;i<samples;++i){
        float3 d=hemisphere(n,seed);Hit bounce=trace(p.xyz+n*.012,d,settings.x==0?24:settings.x==1?64:100,false,0);
        if(!bounce.material)indirect+=sky(d);
        else if(settings.x>0){float3 bp=p.xyz+n*.012+d*bounce.t;Instance obj=instances[bounce.instance];Material bm=materialAt(bounce.material,obj.color,bp);
            indirect+=diffuseAt(bm,bp,bounce.normal,seed)+bm.color*bm.emission;}
    }
    color+=m.color*(1-m.metal)*indirect/samples;
    if(m.roughness<.65){
        float3 reflected=reflect(-v,n);float3 rd=normalize(lerp(reflected,hemisphere(n,seed),m.roughness*m.roughness));
        Hit r=trace(p.xyz+n*.012,rd,settings.x==0?64:256,false,0);float3 radiance=sky(rd);
        if(r.material){float3 rp=p.xyz+n*.012+rd*r.t;Material rm=materialAt(r.material,instances[r.instance].color,rp);radiance=directAt(rm,rp,r.normal,-rd,seed)+rm.color*sky(r.normal)*.55+rm.color*rm.emission;}
        float3 f0=lerp(.04.xxx,m.color,m.metal);float3 F=f0+(1-f0)*pow(1-saturate(dot(n,v)),5);color+=radiance*F;
    }
    return float4(min(color,20),1);
}
float3 spatialLight(int2 pixel,float4 p,float3 n){
    float material=normalTex.Load(int3(pixel,0)).w;
    int2 base=pixel/2;float3 sum=0;float weight=0;
    [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){
        int2 q=clamp(base+int2(x,y),0,(int2(screen.xy)-1)/2);int2 fp=q*2;float4 np=positionTex.Load(int3(fp,0));float3 nn=normalTex.Load(int3(fp,0)).xyz;
        float w=exp(-.7*(x*x+y*y))*pow(saturate(dot(n,nn)),16);
        w*=(np.w==p.w&&normalTex.Load(int3(fp,0)).w==material)?1:0;w*=exp(-abs(dot(np.xyz-p.xyz,n))*8);
        sum+=lightTex.Load(int3(q,0)).rgb*w;weight+=w;
    }
    if(weight<.001){
        // Thin geometry can be absent from half-resolution samples. Shade its
        // own surface instead of borrowing a background pixel across an edge.
        float4 nm=normalTex.Load(int3(pixel,0)),a=albedoTex.Load(int3(pixel,0));Material m=materialAt(uint(nm.w),0,p.xyz);m.color=a.rgb;m.roughness=a.w;
        uint seed=hash(uint(pixel.x+pixel.y*int(screen.x)));return directAt(m,p.xyz,n,normalize(eye.xyz-p.xyz),seed)+m.color*sky(n)*.5+m.color*m.emission;
    }
    return sum/weight;
}
float4 resolveMain(Pixel pixel):SV_TARGET {
    int2 xy=int2(pixel.position.xy);float4 p=positionTex.Load(int3(xy,0));
    if(p.w==0)return float4(albedoTex.Load(int3(xy,0)).rgb,1);
    float4 nm=normalTex.Load(int3(xy,0));float3 n=nm.xyz;float3 color=spatialLight(xy,p,n);float4 motion=motionTex.Load(int3(xy,0));float2 uv=pixel.uv+motion.xy;
    float count=1;
    if(screen.w<.5&&all(uv>0)&&all(uv<1)){
        float tolerance=max(.15,length(eye.xyz-p.xyz)/screen.y*2);
        // Roof treads and risers alternate under subpixel camera jitter. They
        // belong to the same roof: rejecting their different face normals each
        // frame turns the small self-shadows into persistent flicker.
        bool roof=nm.w==7&&motion.z<.5;
        float2 historyPixel=uv*screen.xy-.5;int2 base=int2(floor(historyPixel));float2 fraction=frac(historyPixel);
        float4 history=0;float weight=0;
        [unroll]for(int y=0;y<2;++y)[unroll]for(int x=0;x<2;++x){
            int2 oldXY=clamp(base+int2(x,y),0,int2(screen.xy)-1);
            float4 oldP=oldPositionTex.Load(int3(oldXY,0)),oldN=oldNormalTex.Load(int3(oldXY,0));
            // Validate every bilinear tap, so a roof never accumulates a
            // neighboring chimney, wall or background through interpolation.
            if(oldP.w==p.w&&oldN.w==nm.w&&(roof||dot(n,oldN.xyz)>.95)&&(motion.z>.5||length(oldP.xyz-p.xyz)<tolerance)){
                float w=(x?fraction.x:1-fraction.x)*(y?fraction.y:1-fraction.y);
                history+=oldLightTex.Load(int3(oldXY,0))*w;weight+=w;
            }
        }
        if(weight>.001){
            history/=weight;count=min(history.w+1,motion.z>.5?4:roof?32:16);
            // Clamp temporal radiance to prevent bright reflection trails.
            // Matte roof history must retain both lit treads and shaded risers;
            // clamping it to the current face would reintroduce the flicker.
            float3 clipped=roof?history.rgb:clamp(history.rgb,color*.35,color*2.5+.04);color=lerp(clipped,color,1/count);
        }
    }
    return float4(color,count);
}
float3 tone(float3 x){return saturate((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14));}
float4 postMain(Pixel pixel):SV_TARGET {
    int2 xy=int2(pixel.position.xy);float4 p=positionTex.Load(int3(xy,0));float3 color=resolvedTex.Load(int3(xy,0)).rgb;
    float3 bloom=0;[unroll]for(int i=0;i<4;++i){float2 offsets[4]={float2(-4,0),float2(4,0),float2(0,-4),float2(0,4)};bloom+=max(resolvedTex.SampleLevel(linearClamp,pixel.uv+offsets[i]/screen.xy,0).rgb-1,0);}
    color+=bloom*.015;
    if(p.w!=0){
        float distance=length(eye.xyz-p.xyz);float fog=1-exp(-distance*.000055);color=lerp(color,sky(cameraRay(pixel.uv)),fog);
        uint2 tile=min(uint2(max(p.xz,0)/16),511u);uint style=tileStyles[tile.y*512+tile.x],tint=style&3u,rule=style>>2;
        if(tint)color=lerp(color,tint==1?float3(.8,.07,.035):tint==2?float3(.9,.48,.05):float3(.055,.7,.13),.65);
        if(p.y<.1&&(rule&16u))color*=.88;
        if(p.y<.1&&rule<64u&&(rule&7u)>0){float2 headings[4]={float2(0,-1),float2(1,0),float2(0,1),float2(-1,0)};float2 heading=headings[(rule&7u)-1],local=fmod(p.xz,16)-8;float along=dot(local,heading),side=dot(local,float2(-heading.y,heading.x));if((abs(side)<.5&&abs(along)<3)||(along>1&&along<4&&abs(side)<(4-along)*.8))color=float3(.8,.75,.45);}
        if(p.y<.1&&rule<64u&&(rule&8u)&&length(fmod(p.xz,16)-float2(4,4))<1.2)color=sunlight.w<.5?float3(.1,.85,.15):float3(.85,.05,.02);
    if(rule>=64u&&rule<=72u){
        uint index=rule-64u;float2 offset=float2(int(index%3)-1,int(index/3)-1)*16;
        float2 radial=normalize(offset),heading=float2(radial.y,-radial.x);
        float2 local=fmod(p.xz,16)-8+offset-radial*16;
        float along=dot(local,heading),side=dot(local,float2(-heading.y,heading.x));
        if((abs(side)<.4&&abs(along)<2)||(along>.5&&along<2.5&&abs(side)<(2.5-along)*.65))color=float3(.92,.90,.70);
    }
        if(hover.w>.5&&normalTex.Load(int3(xy,0)).y>.5){float2 grid=p.xz/8;float2 edge=abs(frac(grid-.5)-.5)/max(fwidth(grid),.0001);float gridLine=1-saturate(min(edge.x,edge.y));color*=1-gridLine*.24*saturate(1-distance/1100);}
        if(hover.x>=0&&all(floor(p.xz/16)>=hover.xy)&&all(floor(p.xz/16)<hover.xy+floor(hover.z/2)+1))color=lerp(color,fmod(hover.z,2)>.5?float3(.9,.13,.05):float3(.1,.65,.75),.4);
    }
    return float4(pow(tone(color*1.15),1/2.2),1);
}
// Headless GPU/CPU traversal conformance, compiled as a separate compute entry.
StructuredBuffer<float4> testRays : register(t14);
RWStructuredBuffer<float4> testHits : register(u0);
cbuffer TestCount : register(b1) {uint testCount;};
[numthreads(64,1,1)]
void testMain(uint3 thread:SV_DispatchThreadID){
    uint i=thread.x;if(i>=testCount)return;
    float4 a=testRays[i*2],b=testRays[i*2+1];float t;float3 n;uint material;
    bool hit=brickHit(primitives[uint(a.w)],0,a.xyz,b.xyz,.003,1000,0,t,n,material);
    testHits[i*2]=float4(hit?t:-1,n);testHits[i*2+1]=float4(hit?material:0,0,0,0);
}
