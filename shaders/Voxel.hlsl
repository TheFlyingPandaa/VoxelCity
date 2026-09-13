cbuffer Scene : register(b0) {
    row_major float4x4 inverseVP, currentVP, previousVP;
    float4 eye, oldEye, sunlight, screen, settings, hover;
};
struct Primitive {float3 lo;uint data;float3 hi;uint material;};
struct Instance {uint primitive,words,id,color;float4 pose,previous,roof,roofProfile,height;};
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
float fieldHash(int2 p){return (hash(uint(p.x)^hash(uint(p.y)))&65535u)/65535.;}
float fieldNoise(float2 p){
    int2 cell=int2(floor(p));float2 f=frac(p);f=f*f*(3-2*f);
    return lerp(lerp(fieldHash(cell),fieldHash(cell+int2(1,0)),f.x),
                lerp(fieldHash(cell+int2(0,1)),fieldHash(cell+1),f.x),f.y);
}
float random(inout uint seed){seed=hash(seed);return (seed&0xffffffu)/16777216.;}
float3 cameraRay(float2 uv){float4 v=mul(float4(uv*float2(2,-2)+float2(-1,1),.5,1),inverseVP);return normalize(v.xyz/v.w);}
float3 transformNormal(float3 n,float4 pose){return float3(n.x*pose.w+n.z*pose.z,n.y,-n.x*pose.z+n.z*pose.w);}
bool brickHitLimited(Primitive b,uint base,float3 o,float3 d,float minT,float maxT,float lod,uint ignored,float ignoreUntil,out float t,out float3 n,out uint material){
    float enter=-1e30,leave=maxT;uint axis=0;
    [unroll]for(uint a=0;a<3;++a){
        if(abs(d[a])<1e-10){if(o[a]<b.lo[a]||o[a]>=b.hi[a]){t=0;n=0;material=0;return false;}}
        else {float t0=(b.lo[a]-o[a])/d[a],t1=(b.hi[a]-o[a])/d[a];float nearT=min(t0,t1);if(nearT>enter){enter=nearT;axis=a;}leave=min(leave,max(t0,t1));}
    }
    t=max(enter,minT);n=0;n[axis]=d[axis]>0?-1:1;material=b.material;
    if(t>leave||leave<minT)return false;
    if(material!=0){if(material==ignored)t=max(t,ignoreUntil);return t<=leave;}
    if(lod>0&&t>lod){material=b.data>>24;return material!=0&&material!=ignored;}
    float3 cellSize=(b.hi-b.lo)/8;
    int3 cell=clamp(int3(floor((o+d*(t+1e-5)-b.lo)/cellSize)),0,7);
    int3 step=int3(d.x>=0?1:-1,d.y>=0?1:-1,d.z>=0?1:-1);
    float3 delta,next;
    [unroll]for(uint a=0;a<3;++a){delta[a]=abs(d[a])>1e-10?abs(cellSize[a]/d[a]):1e30;next[a]=abs(d[a])>1e-10?(b.lo[a]+(cell[a]+(step[a]>0?1:0))*cellSize[a]-o[a])/d[a]:1e30;}
    [loop]for(uint i=0;i<25&&t<=leave;++i){
        uint index=uint(cell.x+8*(cell.y+8*cell.z));material=(voxels[base+(b.data&0xffffffu)+index/4]>>((index%4)*8))&255u;
        if(material!=0){
            if(material!=ignored||t>=ignoreUntil)return true;
            if(ignoreUntil<=leave&&ignoreUntil<min(next.x,min(next.y,next.z))){t=ignoreUntil;return true;}
        }
        axis=next.x<=next.y?(next.x<=next.z?0:2):(next.y<=next.z?1:2);
        t=next[axis];next[axis]+=delta[axis];cell[axis]+=step[axis];n=0;n[axis]=-step[axis];
        if(cell[axis]<0||cell[axis]>=8)break;
    }return false;
}
bool brickHit(Primitive b,uint base,float3 o,float3 d,float minT,float maxT,float lod,uint ignored,out float t,out float3 n,out uint material){
    return brickHitLimited(b,base,o,d,minT,maxT,lod,ignored,1e30,t,n,material);
}
struct Hit {float t;float3 normal;uint material;uint instance;};
Hit traceMasked(float3 origin,float3 direction,float maxT,bool shadow,float lod,uint ignored){
    Hit h;h.t=maxT;h.normal=0;h.material=0;h.instance=0;
    RayDesc ray;ray.Origin=origin;ray.Direction=direction;ray.TMin=.003;ray.TMax=maxT;
    RayQuery<RAY_FLAG_SKIP_TRIANGLES> q;q.TraceRayInline(scene,RAY_FLAG_NONE,255,ray);
    [loop]while(q.Proceed()){
        Instance object=instances[q.CandidateInstanceID()];Primitive brick=primitives[object.primitive+q.CandidatePrimitiveIndex()];
        float t;float3 n;uint material;
        if(brickHit(brick,object.words,q.CandidateObjectRayOrigin(),q.CandidateObjectRayDirection(),ray.TMin,h.t,lod,ignored,t,n,material)){
            q.CommitProceduralPrimitiveHit(t);h.t=t;h.normal=transformNormal(n,object.pose);h.material=material;h.instance=q.CandidateInstanceID();if(shadow){q.Abort();break;}
        }
    }return h;
}
Hit trace(float3 origin,float3 direction,float maxT,bool shadow,float lod){return traceMasked(origin,direction,maxT,shadow,lod,0);}
float3 sky(float3 d){return lerp(float3(.62,.74,.88),float3(.20,.40,.68),saturate(d.y))*.65;}
float3 atmosphereSky(float3 d){
    float horizon=exp(-abs(d.y)*7);
    float sunward=pow(saturate(dot(d,normalize(sunlight.xyz))),12);
    return lerp(sky(d),float3(.43,.46,.47),horizon)+float3(.14,.105,.065)*sunward;
}
float aerialOpacity(float3 p){
    // Integrate exponential height density along the view segment. Using the
    // lower endpoint avoids overflow when looking down from the overview camera.
    float low=max(0,min(eye.y,p.y)),height=abs(max(eye.y,0)-max(p.y,0))/180;
    float averageDensity=exp(-low/180)*(height<.001?1-height*.5:(1-exp(-height))/height);
    return 1-exp(-length(p-eye.xyz)*.0004*averageDensity);
}
struct Material {float3 color;float roughness;float metal;float emission;float transmission;};
Material materialAt(uint id,uint variant,float3 p,float footprint){
    Material m;m.color=float3(.55,.55,.55);m.roughness=.85;m.metal=0;m.emission=0;m.transmission=0;
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
        case 11:m.color=lerp(float3(.12,.23,.045),float3(.055,.145,.055),float(variant%4)/3);m.transmission=.45;break;
        case 12:m.color=float3(.018,.021,.024);break;
        case 13:{float3 palette[6]={float3(.45,.075,.035),float3(.035,.12,.27),float3(.65,.43,.09),float3(.65,.64,.60),float3(.055,.20,.12),float3(.19,.07,.11)};m.color=palette[variant%6];m.roughness=.25;m.metal=.25;break;}
        case 14:m.color=float3(.9,.79,.48);m.roughness=.25;m.emission=.6;break;
        case 15:m.color=float3(.42,.46,.44);break;
        case 16:m.color=float3(.39,.40,.37);break;
        case 17:m.color=float3(.57,.50,.36);break;
        case 18:m.color=float3(.74,.72,.64);m.roughness=.75;break;
        case 19:m.color=float3(.55,.012,.005);m.roughness=.25;m.emission=.35;break;
        case 20:m.color=float3(.025,.07,.05);m.roughness=.18;break;
        case 21:m.color=float3(.45,.035,.025);m.roughness=.5;m.metal=.25;break;
        case 22:m.color=float3(.16,.105,.055);m.roughness=.95;break;
        case 23:m.color=float3(.40,.43,.44);m.roughness=.38;m.metal=.8;break;
        case 24:m.color=float3(.19,.235,.23);m.roughness=.72;m.metal=.35;break;
        case 25:m.color=float3(.018,.021,.024);m.roughness=.95;break;
        case 26:m.color=float3(.21,.205,.185);m.roughness=.95;break;
    }
    uint3 cell=uint3(int3(floor(p*4)));float noise=(hash(cell.x^hash(cell.y)^hash(cell.z))&255)/255.;
    if(id!=2&&id!=4&&id!=5&&id!=6&&id!=7&&id!=11&&id!=15&&id!=16&&id!=18&&id!=8&&id!=9&&id!=23&&id!=24&&id!=25&&id!=26&&id!=10&&id!=22&&id!=13&&id!=14&&id!=19&&id!=20)m.color*=.88+.24*noise;
    if(id==5||id==6||id==15||id==17)m.color*=.85+.3*((hash(variant)&255)/255.);
    // World-anchored, voxel-sized wear stays fixed while lighting samples change.
    float3 voxel=floor(p*4)*.25;
    if(id==1){
        // Continuous, rotated ground coordinates avoid quarter-unit patch edges
        // and reduce alignment between the meadow pattern and the street grid.
        float2 ground=float2(dot(p.xz,float2(.8,.6)),dot(p.xz,float2(-.6,.8)));
        float patch=fieldNoise(ground*.075),mottle=fieldNoise(ground*.55);
        float fine=1-smoothstep(.04,.3,footprint);
        float tussock=lerp(.5,fieldNoise(ground*1.7),fine);
        float grain=lerp(.5,noise,fine);
        m.color=lerp(float3(.058,.088,.029),float3(.12,.125,.045),smoothstep(.12,.9,patch));
        m.color*=.78+.2*mottle+.14*tussock+.14*grain;
        // Sparse dry soil between clumps, rather than a uniformly mottled lawn.
        float bare=smoothstep(.58,.88,mottle)*smoothstep(.42,.7,patch);
        m.color=lerp(m.color,float3(.115,.082,.046)*(.85+.2*grain),bare*.65);
    }else if(id==2){
        float patch=fieldNoise(voxel.xz*.18);
        float grain=fieldHash(int2(floor(p.xz*16)));
        float grainVisibility=1-smoothstep(.025,.16,footprint);
        m.color*=(.78+.34*patch)*lerp(1,.94+.12*grain,grainVisibility);
        // Scattered resurfacing patches are anchored in world space and clipped
        // by the existing asphalt material, so road markings remain readable.
        float2 patchSize=float2(12,9),patchCell=floor(voxel.xz/patchSize);
        float selected=fieldHash(int2(patchCell));
        float2 local=voxel.xz-patchCell*patchSize;
        float2 center=float2(4,3)+float2(fieldHash(int2(patchCell)+37),fieldHash(int2(patchCell)+91))*float2(4,3);
        float2 extent=float2(1.5,1)+float2(fieldHash(int2(patchCell)+13),selected)*float2(2,1.5);
        float repair=step(.9,selected)*step(max(abs(local.x-center.x)-extent.x,abs(local.y-center.y)-extent.y),0);
        m.color=lerp(m.color,float3(.037,.042,.045)*(.9+.2*noise),repair*.6);
        m.roughness=lerp(.94,.82,repair);
        float fracture=abs(fieldNoise(voxel.xz*.65)-.5);
        float crack=(1-smoothstep(.008,.027,fracture))*smoothstep(.65,.8,fieldNoise(voxel.xz*.11+23));
        m.color*=1-crack*.28;
        // Sparse flush iron covers: world-anchored, with filtered rim and ribs.
        int2 roadCell=int2(floor(p.xz/16));
        float selectedCover=step(.82,fieldHash(roadCell+int2(83,19)));
        float2 coverLocal=p.xz-float2(roadCell)*16-float2(5,5);
        float radius=length(coverLocal),aa=max(.008,footprint*.5);
        float cover=selectedCover*(1-smoothstep(.65-aa,.65+aa,radius));
        float fine=1-smoothstep(.04,.22,footprint);
        float rim=1-smoothstep(.025-aa,.025+aa,abs(radius-.57));
        float rib=1-smoothstep(.018-aa,.018+aa,abs(frac((coverLocal.x+coverLocal.y)*5)-.5)*.2);
        float3 iron=lerp(float3(.035,.039,.038),float3(.065,.067,.059),max(rim,rib*.55)*fine);
        m.color=lerp(m.color,iron,cover);
        m.roughness=lerp(m.roughness,.68,cover);m.metal=cover*.55;
    }else if(id==3){
        // Worn paint retains continuous, high-contrast lane guidance.
        float wear=smoothstep(.72,.92,noise)*fieldNoise(voxel.xz*1.1);
        m.color*=1-wear*.38;
    }else if(id==4){
        // Smaller paving slabs, with joints filtered to their area average.
        float2 edge=min(frac(p.xz),1-frac(p.xz));
        float aa=max(.002,footprint*.5);
        float joint=1-smoothstep(.014-aa,.014+aa,min(edge.x,edge.y));
        float resolved=1-smoothstep(.08,.4,footprint);
        joint=lerp(.055216,joint,resolved);
        m.color*=lerp(.78+fieldNoise(p.xz*.4)*.3,.56,joint);
        float slab=fieldHash(int2(floor(p.xz)));
        m.color*=lerp(.98,.91+.14*slab,resolved);
        float grain=fieldHash(int2(floor(p.xz*16)));
        m.color*=lerp(1,.96+.08*grain,1-smoothstep(.025,.18,footprint));
        // Occasional replacement slabs and settled cracks break up pristine
        // paving. All selection and detail remain fixed in world coordinates.
        float repair=step(.9,slab)*resolved;
        m.color=lerp(m.color,m.color*float3(.87,.9,.92),repair);
        float2 local=frac(p.xz);
        float bend=fieldHash(int2(floor(p.xz))+int2(37,91));
        if(bend<.5)local=local.yx;
        float crackY=.2+local.x*.55+(abs(local.x-.45)-.25)*(bend-.5)*.6;
        float fracture=1-smoothstep(.006-aa,.006+aa,abs(local.y-crackY));
        float cracked=step(.76,slab)*(1-step(.9,slab));
        m.color*=1-fracture*cracked*.32*(1-smoothstep(.04,.22,footprint));
    }else if(id==5||id==6||id==15||id==17){
        float3 finishPosition=(id==6||id==15)?p:voxel;
        float damp=(1-smoothstep(.5,3.5,p.y))*(.12+.24*fieldNoise(finishPosition.xz*.7));
        float wear=fieldNoise(float2(finishPosition.x+finishPosition.z,finishPosition.y)*.65);
        m.color*=1-damp;
        m.color*=.86+.24*wear;
        if(id==5){
            int course=int(floor(p.y*8));
            float2 bond=float2((p.x+p.z)*4+(course%2)*.5,p.y*8);
            float brick=fieldHash(int2(floor(bond)));
            float resolved=1-smoothstep(.04,.175,footprint);
            m.color*=lerp(.97,.82+.3*brick,resolved);
            // Staggered mortar beds are filtered by the projected pixel footprint.
            // Fine joints converge to their area average instead of aliasing at range.
            float2 edge=min(frac(bond),1-frac(bond))*float2(.25,.125);
            float aa=max(.002,footprint*.5);
            float mortar=1-smoothstep(.005-aa,.005+aa,min(edge.x,edge.y));
            mortar=lerp(.1168,mortar,resolved);
            m.color=lerp(m.color,float3(.205,.185,.155)*(1-damp),mortar*.65);
        }else if(id==6||id==15){
            // Fine lime-plaster aggregate and broad trowel variation avoid
            // imposing the geometry's quarter-unit grid on smooth wall faces.
            float2 wall=float2(p.x+p.z,p.y);
            float grain=fieldHash(int2(floor(wall*24)));
            m.color*=lerp(1,.96+.08*grain,1-smoothstep(.02,.15,footprint));
            float trowel=fieldNoise(wall*float2(2.4,3.8));
            m.color*=lerp(1,.97+.06*trowel,1-smoothstep(.08,.35,footprint));
        }
    }else if(id==7){
        float palette=(hash(variant)&255u)/255.;
        m.color=lerp(float3(.07,.085,.09),float3(.24,.095,.045),palette);
        // Small roof units replace independent noisy voxel faces. The projected
        // tile pattern and joints converge to their area average at distance.
        float2 tile=p.xz*2;
        tile.x+=(int(floor(tile.y))&1)*.5;
        float variation=fieldHash(int2(floor(tile)));
        float resolved=1-smoothstep(.06,.3,footprint);
        m.color*=lerp(1,.88+.24*variation,resolved);
        float2 edge=min(frac(tile),1-frac(tile))*.5;
        float aa=max(.002,footprint*.5);
        float joint=1-smoothstep(.008-aa,.008+aa,min(edge.x,edge.y));
        joint=lerp(.063,joint,resolved);
        m.color*=1-joint*.22;
        // Broad weathering crosses tile boundaries; fine ceramic grain fades
        // before it becomes subpixel so a resting roof stays visually stable.
        float weather=fieldNoise(p.xz*.32);
        float colonies=fieldNoise(p.xz*1.7);
        float lichen=smoothstep(.52,.78,weather)*smoothstep(.38,.72,colonies);
        m.color*=.88+.18*weather;
        m.color=lerp(m.color,float3(.14,.155,.085),lichen*.32);
        float grain=fieldHash(int2(floor(p.xz*24)));
        m.color*=lerp(1,.97+.06*grain,1-smoothstep(.02,.15,footprint));
        m.roughness=lerp(.75,.9,lichen);
    }else if(id==24){
        // Sheet seams run down each sawtooth slope. Filter fine ribs at distance.
        float panel=fieldHash(int2(floor(p.x/4),floor(p.z/1.5)));
        float seamDistance=min(frac(p.z/1.5),1-frac(p.z/1.5))*1.5;
        float aa=max(.003,footprint*.5);
        float seam=1-smoothstep(.025-aa,.025+aa,seamDistance);
        seam=lerp(.0333,seam,1-smoothstep(.08,.4,footprint));
        float oxide=smoothstep(.65,.9,panel)*(.08+.55*seam);
        m.color*= (.86+.28*panel)*(1-seam*.28);
        m.color=lerp(m.color,float3(.22,.09,.035),oxide);
        m.roughness=lerp(.72,.88,oxide);m.metal*=1-oxide;
    }else if(id==25){
        // Rolled roofing has continuous weathering and narrow lap seams.
        // Filter the seams and aggregate grain before they become subpixel.
        float weather=fieldNoise(p.xz*.35);
        float grain=fieldHash(int2(floor(p.xz*24)));
        m.color*=(.9+.2*weather)*lerp(1,.97+.06*grain,1-smoothstep(.02,.15,footprint));
        float edge=min(frac(p.x/1.5),1-frac(p.x/1.5))*1.5;
        float aa=max(.002,footprint*.5);
        float seam=1-smoothstep(.018-aa,.018+aa,edge);
        seam=lerp(.024,seam,1-smoothstep(.06,.3,footprint));
        m.color*=1-seam*.2;
    }else if(id==26){
        // Mineral mottling stays continuous across the stone's voxel facets.
        float weather=fieldNoise(p.xz*1.7+p.y*.3);
        float grain=fieldHash(int2(floor((p.xz+p.y*.25)*20)));
        m.color*=(.85+.3*weather)*lerp(1,.94+.12*grain,1-smoothstep(.025,.18,footprint));
        m.color*=.82+.18*smoothstep(.05,.4,p.y);
    }else if(id==16){
        // Cast concrete panels replace the coarse independent voxel checker.
        float2 panel=float2(p.x+p.z,p.y)/float2(3,2.5);
        float variation=fieldHash(int2(floor(panel)));
        float grain=fieldHash(int2(floor(float2(p.x+p.z,p.y)*16)));
        m.color*=(.93+.14*variation)*lerp(1,.96+.08*grain,1-smoothstep(.025,.18,footprint));
        float2 edge=min(frac(panel),1-frac(panel))*float2(3,2.5);
        float aa=max(.003,footprint*.5);
        float joint=1-smoothstep(.015-aa,.015+aa,min(edge.x,edge.y));
        joint=lerp(.022,joint,1-smoothstep(.08,.4,footprint));
        m.color*=1-joint*.3;
        float damp=(1-smoothstep(.25,2,p.y))*(.06+.1*fieldNoise(voxel.xz*.7));
        m.color*=1-damp;m.roughness=.92;
    }else if(id==9){
        float stain=fieldNoise(float2(p.x+p.z,p.y*.25)*1.1);
        float damp=1-smoothstep(.25,2,p.y);
        float oxide=smoothstep(.7,.9,stain)*(.2+.4*damp);
        m.color=lerp(m.color*(.85+.15*stain),float3(.19,.075,.025),oxide);
        float grain=fieldHash(int2(floor(float2(p.x+p.z,p.y)*16)));
        m.color*=lerp(1,.95+.1*grain,1-smoothstep(.025,.18,footprint));
        m.roughness=lerp(.38,.7,oxide);m.metal=lerp(.8,.1,oxide);
    }else if(id==18){
        // Matte paint has fine surface grain rather than independently colored
        // quarter-unit blocks. Broad staining remains anchored to the surface.
        float grain=fieldHash(int2(floor(float2(p.x+p.z,p.y)*24)));
        float fine=1-smoothstep(.02,.15,footprint);
        float stain=fieldNoise(float2(p.x+p.z,p.y*.4)*.6);
        m.color*=lerp(1,.975+.05*grain,fine)*(.96+.04*stain);
        m.color*=1-(1-smoothstep(.25,1.5,p.y))*.08*stain;
    }else if(id==10){
        float across=p.x+p.z;
        float grain=fieldNoise(float2(across*18,p.y*.8));
        float fine=1-smoothstep(.025,.2,footprint);
        float board=fieldHash(int2(floor(across/.75),0));
        m.color*=(.83+.26*board)*lerp(.9,.72+.36*grain,fine);
        float edge=min(frac(across/.75),1-frac(across/.75))*.75;
        float aa=max(.002,footprint*.5);
        float seam=1-smoothstep(.008-aa,.008+aa,edge);
        seam=lerp(.0213,seam,1-smoothstep(.08,.4,footprint));
        m.color*=1-seam*.35;
    }else if(id==22){
        float ridges=fieldNoise(float2((p.x+p.z)*9,p.y*.65));
        float fine=1-smoothstep(.04,.3,footprint);
        m.color*=lerp(.92,.64+.56*ridges,fine);
        m.color*=.85+.3*fieldNoise(voxel.xz*.6);
        m.color*=.8+.2*smoothstep(.25,2,p.y);
    }else if(id==11){
        // Coherent leaf-cluster color instead of high-contrast voxel checkers.
        // Fine mottling fades out as a voxel becomes smaller than a pixel.
        float cluster=fieldNoise((p.xz+float2(p.y*.27,-p.y*.19))*1.1);
        m.color*=(.82+.28*cluster)*lerp(1,.94+.12*noise,1-smoothstep(.06,.3,footprint));
    }
    return m;
}
Material materialAt(uint id,uint variant,float3 p){return materialAt(id,variant,p,0);}
// Primary color and roughness already include footprint-filtered material detail.
// Recover the remaining response from that same sample instead of evaluating
// the procedural surfaces again with a zero footprint during lighting/resolve.
Material surfaceMaterial(uint id,float4 albedo){
    Material m;m.color=albedo.rgb;m.roughness=albedo.w;m.metal=0;m.emission=0;m.transmission=0;
    if(id==2)m.metal=.55*saturate((.9-m.roughness)/.22);
    else if(id==9)m.metal=lerp(.8,.1,saturate((m.roughness-.38)/.32));
    else if(id==13||id==21)m.metal=.25;
    else if(id==23)m.metal=.8;
    else if(id==24)m.metal=.35*(1-saturate((m.roughness-.72)/.16));
    if(id==11)m.transmission=.45;
    if(id==14)m.emission=.6;
    else if(id==19)m.emission=.35;
    return m;
}
float3 backgroundSky(float3 d){
    float3 clear=lerp(sky(d),float3(.43,.46,.47),exp(-abs(d.y)*14))
        +float3(.14,.105,.065)*pow(saturate(dot(d,normalize(sunlight.xyz))),12);
    // A fixed high cloud layer: direction-only coordinates keep the sky still
    // at rest. Fade into horizon haze before the projected pattern compresses.
    float visibility=smoothstep(.003,.09,d.y);
    if(visibility<=0)return clear;
    float2 p=d.xz/(max(d.y,0)+.22)*2.4+float2(17,31);
    float body=fieldNoise(p)*.6+fieldNoise(p*2.07+11)*.28+fieldNoise(p*4.13-7)*.12;
    float cover=smoothstep(.4,.62,body)*visibility;
    float sunward=pow(saturate(dot(d,normalize(sunlight.xyz))),6);
    float3 cloud=lerp(float3(.48,.51,.55),float3(.78,.75,.69),sunward);
    return lerp(clear,cloud,cover*.75);
}
float3 hemisphereSample(float3 n,float2 sample){float a=sample.x*6.2831853,r=sqrt(sample.y);float3 tangent=normalize(cross(abs(n.y)<.95?float3(0,1,0):float3(1,0,0),n));return normalize(tangent*(cos(a)*r)+cross(n,tangent)*(sin(a)*r)+n*sqrt(1-r*r));}
float3 hemisphere(float3 n,inout uint seed){float2 sample=float2(random(seed),random(seed));return hemisphereSample(n,sample);}
float3 sunDisk(float2 sample){
    float3 sun=normalize(sunlight.xyz);
    float3 tangent=normalize(cross(abs(sun.y)<.95?float3(0,1,0):float3(1,0,0),sun));
    float angle=sample.x*6.2831853,radius=sqrt(sample.y)*.035;
    return normalize(sun+radius*(tangent*cos(angle)+cross(sun,tangent)*sin(angle)));
}
float3 sunDirection(inout uint seed){float2 sample=float2(random(seed),random(seed));return sunDisk(sample);}
float3 brdf(Material m,float3 n,float3 v,float3 l){
    float nl=saturate(dot(n,l)),nv=max(.001,dot(n,v));float3 h=normalize(l+v);float nh=saturate(dot(n,h)),vh=saturate(dot(v,h));
    float a=max(.025,m.roughness*m.roughness),a2=a*a,den=nh*nh*(a2-1)+1;float D=a2/(3.14159265*den*den);
    float k=(m.roughness+1)*(m.roughness+1)/8;float G=nv/(nv*(1-k)+k)*nl/max(.001,nl*(1-k)+k);
    float3 F=lerp(.04.xxx,m.color,m.metal)+(1-lerp(.04.xxx,m.color,m.metal))*pow(1-vh,5);
    return (m.color*(1-m.metal)/3.14159265+F*(D*G/max(.001,4*nv*nl)))*nl;
}
// Visit the acceleration structure once for the two foliage shadow intervals.
bool foliageOccluded(float3 origin,float3 direction){
    RayDesc ray;ray.Origin=origin;ray.Direction=direction;ray.TMin=.003;ray.TMax=16384.75;
    RayQuery<RAY_FLAG_SKIP_TRIANGLES> q;q.TraceRayInline(scene,RAY_FLAG_NONE,255,ray);
    [loop]while(q.Proceed()){
        Instance object=instances[q.CandidateInstanceID()];Primitive brick=primitives[object.primitive+q.CandidatePrimitiveIndex()];
        float3 o=q.CandidateObjectRayOrigin(),d=q.CandidateObjectRayDirection();float t;float3 n;uint material;
        if(brickHitLimited(brick,object.words,o,d,.003,16384.75,0,11,.75,t,n,material)){
            q.Abort();return true;
        }
    }
    return false;
}
// Thin foliage admits some backlight; thicker foliage and opaque walls block it.
float3 foliageBacklight(Material m,float3 p,float3 n,float3 l){
    float back=saturate(-dot(n,l));
    if(m.transmission==0||back==0)return 0;
    float visibility=1;
    if(settings.y>.5){
        visibility=foliageOccluded(p+l*.006,l)?0:1;
    }
    return m.color*m.transmission*back*visibility/3.14159265*float3(4.2,3.55,2.65);
}
float3 directWithSun(Material m,float3 p,float3 n,float3 v,float3 l){
    float visibility=1;
    if(settings.y>.5&&dot(n,l)>0)visibility=trace(p+n*.012,l,16384,true,0).material==0?1:0;
    return brdf(m,n,v,l)*float3(4.2,3.55,2.65)*visibility+foliageBacklight(m,p,n,l);
}
float3 directAt(Material m,float3 p,float3 n,float3 v,inout uint seed){return directWithSun(m,p,n,v,sunDirection(seed));}
// The diffuse bounce excludes specular caustics: these need a separate sampling
// strategy and otherwise produce isolated fireflies around glass and metal.
float3 diffuseAt(Material m,float3 p,float3 n,inout uint seed){
    float3 l=sunDirection(seed);float visibility=1;
    if(settings.y>.5&&dot(n,l)>0)visibility=trace(p+n*.012,l,16384,true,0).material==0?1:0;
    return m.color*(1-m.metal)/3.14159265*saturate(dot(n,l))*float3(4.2,3.55,2.65)*visibility+foliageBacklight(m,p,n,l);
}
struct Surface {float4 position:SV_TARGET0;float4 normal:SV_TARGET1;float4 albedo:SV_TARGET2;float4 motion:SV_TARGET3;};
Surface surfaceMain(Pixel pixel){
    // SV_POSITION already addresses pixel centers; fixed rays keep static edges stable.
    Surface s=(Surface)0;float2 uv=pixel.position.xy/screen.xy;float3 ray=cameraRay(uv);
    Hit h=trace(eye.xyz,ray,32768,false,screen.y*(settings.x==0?.8:settings.x==1?1.2:1.8));
    if(!h.material){s.position=float4(0,0,0,0);s.albedo=float4(backgroundSky(ray),1);return s;}
    float3 p=eye.xyz+ray*h.t;Instance inst=instances[h.instance];
    float footprint=h.t*.828427/screen.y/max(.15,abs(dot(ray,h.normal)));
    Material m=materialAt(h.material,inst.color,p,footprint);
    s.position=float4(p,float(inst.id));s.normal=float4(h.normal,h.material);s.albedo=float4(m.color,m.roughness);
    float2 local=float2(dot(p.xz-inst.pose.xy,float2(inst.pose.w,-inst.pose.z)),dot(p.xz-inst.pose.xy,inst.pose.zw));
    float roofLift=0;
    if(h.material==7&&dot(inst.roof.xy,inst.roof.xy)>.5&&h.normal.y>=0&&p.y>=inst.roof.w){
        float2 axis=float2(inst.pose.w*inst.roof.x+inst.pose.z*inst.roof.y,-inst.pose.z*inst.roof.x+inst.pose.w*inst.roof.y);
        if(h.normal.y>.5||abs(dot(h.normal.xz,axis))>.5){
            float across=dot(local,inst.roof.xy)-inst.roof.z;
            float2 slope=axis*sign(across)*inst.roofProfile.y;
            s.normal.xyz=normalize(float3(slope.x,1,slope.y));
            // Upper envelope of the authored staircase. Keep G-buffer hit
            // positions exact; only primary lighting rays use this small lift.
            roofLift=clamp(inst.roof.w+(inst.roofProfile.x-abs(across))*inst.roofProfile.y+inst.roofProfile.z-p.y,0,inst.roofProfile.w);
        }
    }
    if(h.material==24&&dot(inst.roof.xy,inst.roof.xy)>.5&&h.normal.y>=0){
        float2 axis=float2(inst.pose.w*inst.roof.x+inst.pose.z*inst.roof.y,-inst.pose.z*inst.roof.x+inst.pose.w*inst.roof.y);
        // The rising slope faces toward negative local X. Preserve the exposed
        // high end and gable edges instead of treating them as part of the slope.
        if(h.normal.y>.5||dot(h.normal.xz,axis)<-.5){
            s.normal.xyz=normalize(float3(-axis.x*inst.roofProfile.y,1,-axis.y*inst.roofProfile.y));
            float run=frac((dot(local,inst.roof.xy)-inst.roof.z)/inst.roofProfile.x)*inst.roofProfile.x;
            roofLift=clamp(inst.roof.w+run*inst.roofProfile.y+inst.roofProfile.z-p.y,0,inst.roofProfile.w);
        }
    }
    // The thin stair-step panes describe a 45-degree windscreen. Use its overall
    // slope for reflection/Fresnel shading instead of alternating tread normals.
    if(h.material==8&&inst.id>=0x800000u&&abs(local.x)<1.126&&abs(local.y)>1.124&&p.y>=1.75){
        float side=local.y<0?-1:1;
        s.normal.xyz=normalize(float3(inst.pose.z*side,1,inst.pose.w*side));
    }
    float3 oldP=float3(inst.previous.x+local.x*inst.previous.w+local.y*inst.previous.z,p.y-inst.height.x+inst.height.y,inst.previous.y-local.x*inst.previous.z+local.y*inst.previous.w);
    float4 oldClip=mul(float4(oldP,1),previousVP);float2 oldUV=oldClip.xy/max(.001,oldClip.w)*float2(.5,-.5)+.5;
    s.motion=float4(oldUV-pixel.uv,inst.id>=0x800000u?1:0,roofLift);return s;
}
float3 windowLight(float3 p,float3 n,float3 v,int2 full,inout uint seed){
    float3 d=-v;
    Hit inside=traceMasked(p+d*.006,d,256,false,0,8);
    float3 transmitted=backgroundSky(d);
    if(inside.material){
        float3 q=p+d*(.006+inside.t);Material m=materialAt(inside.material,instances[inside.instance].color,q);
        float3 l=sunDirection(seed);
        // Back-facing room surfaces receive no direct sun contribution.
        float visible=1;
        if(settings.y>.5&&dot(inside.normal,l)>0)
            visible=traceMasked(q+inside.normal*.012,l,16384,true,0,8).material==0?1:0;
        transmitted=brdf(m,inside.normal,v,l)*float3(4.2,3.55,2.65)*visible;
        // Sample room illumination through the panes. Enclosed corners receive
        // bounced surface light instead of the same ambient fill as open windows.
        uint samples=settings.x>=2?4:settings.x>=1?2:1;float3 indirect=0;
        uint roomSeed=hash((uint(full.x)+uint(full.y)*uint(screen.x))^0xa511e9b3u);
        float2 rotation=float2(random(roomSeed),random(roomSeed));
        [loop]for(uint i=0;i<samples;++i){
            // Continue the room sequence across frames without repeating a grid.
            float2 sample=frac(rotation+(float((uint(screen.z)%1024u)*samples+i)+.5)*float2(.754877666,.569840296));
            random(seed);random(seed);
            float3 bounceDirection=hemisphereSample(inside.normal,sample);
            float3 origin=q+inside.normal*.012;
            Hit bounce=traceMasked(origin,bounceDirection,settings.x==0?24:64,false,0,8);
            if(!bounce.material)indirect+=sky(bounceDirection);
            else if(settings.x>0){
                float3 bp=origin+bounceDirection*bounce.t;
                Material bm=materialAt(bounce.material,instances[bounce.instance].color,bp);
                float3 sun=sunDirection(seed);
                float lit=1;
                if(settings.y>.5&&dot(bounce.normal,sun)>0)
                    lit=traceMasked(bp+bounce.normal*.012,sun,16384,true,0,8).material==0?1:0;
                indirect+=bm.color*(1-bm.metal)/3.14159265*saturate(dot(bounce.normal,sun))*float3(4.2,3.55,2.65)*lit+bm.color*bm.emission;
            }
        }
        transmitted+=m.color*(1-m.metal)*indirect/samples+m.color*m.emission;
    }
    float3 rd=reflect(d,n);Hit reflected=trace(p+n*.012,rd,256,false,0);
    float3 radiance=backgroundSky(rd);
    if(reflected.material){
        float3 q=p+n*.012+rd*reflected.t;Material m=materialAt(reflected.material,instances[reflected.instance].color,q);
        radiance=directAt(m,q,reflected.normal,-rd,seed)+m.color*sky(reflected.normal)*.4+m.color*m.emission;
    }
    float fresnel=.04+.96*pow(1-saturate(dot(n,v)),5);
    return lerp(transmitted*float3(.88,.95,.92),radiance,fresnel);
}
float4 shadeSurface(int2 full){
    float4 p=positionTex.Load(int3(full,0));
    if(p.w==0)return float4(albedoTex.Load(int3(full,0)).rgb,1);
    p.y+=motionTex.Load(int3(full,0)).w;
    float4 nm=normalTex.Load(int3(full,0)),albedo=albedoTex.Load(int3(full,0));float3 n=nm.xyz,v=normalize(eye.xyz-p.xyz);uint material=uint(nm.w);
    Material m=surfaceMaterial(material,albedo);
    // Calm, world-anchored ripples vary reflections without moving primary edges.
    if(material==20&&n.y>.5)n=normalize(n+float3(.018*sin(p.x*2+p.z*.3),0,.018*sin(p.z*2.5-p.x*.2)));
    uint seed=hash(uint(full.x)+uint(full.y)*uint(screen.x)+uint(screen.z)*134775813u);
    if(material==8&&p.w>=2048&&(abs(n.y)<.5||p.w>=0x800000u))return float4(min(windowLight(p.xyz,n,v,full,seed),20),1);
    // A pixel-scrambled low-discrepancy sequence covers the sun disk over time.
    uint sunSeed=hash(uint(full.x)+uint(full.y)*uint(screen.x));
    float2 offset=float2(random(sunSeed),random(sunSeed));
    float2 sunSample=frac(offset+(float(uint(screen.z)%1024u)+.5)*float2(.754877666,.569840296));
    // Preserve the independent bounce stream when replacing the direct sample.
    random(seed);random(seed);
    float3 color=directWithSun(m,p.xyz,n,v,sunDisk(sunSample))+m.color*m.emission;
    uint samples=settings.x>=2?2:1;float3 indirect=0;
    uint bounceSeed=hash((uint(full.x)+uint(full.y)*uint(screen.x))^0x68bc21ebu);
    float2 bounceOffset=float2(random(bounceSeed),random(bounceSeed));
    [loop]for(uint i=0;i<samples;++i){
        float2 sample=frac(bounceOffset+(float((uint(screen.z)%1024u)*samples+i)+.5)*float2(.754877666,.569840296));
        random(seed);random(seed);
        float3 d=hemisphereSample(n,sample);Hit bounce=trace(p.xyz+n*.012,d,settings.x==0?24:settings.x==1?64:100,false,0);
        if(!bounce.material)indirect+=sky(d);
        else if(settings.x>0){float3 bp=p.xyz+n*.012+d*bounce.t;Instance obj=instances[bounce.instance];Material bm=materialAt(bounce.material,obj.color,bp);
            indirect+=diffuseAt(bm,bp,bounce.normal,seed)+bm.color*bm.emission;}
    }
    color+=m.color*(1-m.metal)*indirect/samples;
    if(m.roughness<.65){
        float3 reflected=reflect(-v,n);float3 rd=normalize(lerp(reflected,hemisphere(n,seed),m.roughness*m.roughness));
        Hit r=trace(p.xyz+n*.012,rd,settings.x==0?64:256,false,0);
        // Resolve the same environment seen by primary rays. The sampled
        // reflection direction already spreads with material roughness.
        float3 radiance=backgroundSky(rd);
        if(r.material){float3 rp=p.xyz+n*.012+rd*r.t;Material rm=materialAt(r.material,instances[r.instance].color,rp);radiance=directAt(rm,rp,r.normal,-rd,seed)+rm.color*sky(r.normal)*.55+rm.color*rm.emission;}
        float3 f0=lerp(.04.xxx,m.color,m.metal);float3 F=f0+(1-f0)*pow(1-saturate(dot(n,v)),5);color+=radiance*F;
    }
    return float4(min(color,20),1);
}
float4 lightMain(Pixel pixel):SV_TARGET {
    int2 full=min(int2(pixel.position.xy)*2,int2(screen.xy)-1);
    float4 color=shadeSurface(full);
    float roofMaterial=normalTex.Load(int3(full,0)).w;
    if(settings.x>=2&&(roofMaterial==7||roofMaterial==24)){
        float4 p=positionTex.Load(int3(full,0));float3 albedo=albedoTex.Load(int3(full,0)).rgb;
        p.y+=motionTex.Load(int3(full,0)).w;
        float3 faceNormal=normalTex.Load(int3(full,0)).xyz;
        bool flatFootprint=true;
        [unroll]for(int fy=0;fy<2;++fy)[unroll]for(int fx=0;fx<2;++fx){
            int2 fp=min(full+int2(fx,fy),int2(screen.xy)-1);
            float4 neighbor=positionTex.Load(int3(fp,0)),face=normalTex.Load(int3(fp,0));
            neighbor.y+=motionTex.Load(int3(fp,0)).w;
            flatFootprint=flatFootprint&&neighbor.w==p.w&&face.w==roofMaterial&&dot(face.xyz,faceNormal)>.999&&abs(dot(neighbor.xyz-p.xyz,faceNormal))<.01;
        }
        // A single planar face needs no extra geometric footprint integration.
        // Preserve all four samples where a tread, riser or boundary intervenes.
        if(flatFootprint)return color;
        float count=1;
        // Integrate the fixed 2x2 primary footprint before downsampling. Sampling
        // only its upper-left tread aliases roof steps into broad lighting bands.
        [unroll]for(int y=0;y<2;++y)[unroll]for(int x=0;x<2;++x){
            if(x==0&&y==0)continue;
            int2 q=full+int2(x,y);if(any(q>=int2(screen.xy)))continue;
            if(positionTex.Load(int3(q,0)).w!=p.w||normalTex.Load(int3(q,0)).w!=roofMaterial)continue;
            color.rgb+=shadeSurface(q).rgb*albedo/max(albedoTex.Load(int3(q,0)).rgb,.015);count++;
        }
        color.rgb/=count;
    }
    return color;
}
float3 spatialLight(int2 pixel,float4 p,float3 n){
    float material=normalTex.Load(int3(pixel,0)).w;
    float4 targetAlbedo=albedoTex.Load(int3(pixel,0));
    // A stepped matte roof represents one slope. Reconstruct its pixel footprint
    // across adjacent treads/risers rather than isolating each tiny voxel face.
    bool roof=material==7||material==24;
    bool broadMatte=material==6||material==16||material==17;
    float footprint=max(.25,length(eye.xyz-p.xyz)/screen.y);
    int2 base=pixel/2;float3 sum=0;float weight=0;
    [unroll]for(int y=-2;y<=2;++y)[unroll]for(int x=-2;x<=2;++x){
        if(!broadMatte&&(abs(x)>1||abs(y)>1))continue;
        int2 q=clamp(base+int2(x,y),0,(int2(screen.xy)-1)/2);int2 fp=q*2;
        float4 np=positionTex.Load(int3(fp,0));
        if(np.w!=p.w)continue;
        float4 neighbor=normalTex.Load(int3(fp,0));
        if(neighbor.w!=material)continue;
        float normalAgreement=dot(n,neighbor.xyz);
        if(roof?normalAgreement<0:normalAgreement<=0)continue;
        // Rejected neighbors have zero weight; avoid fetching their lighting
        // and material color or evaluating their distance falloff.
        // The unrolled sample offsets use fixed Gaussian weights.
        // Precompute both kernels instead of exponentiating a selected constant.
        static const float broadKernel[9]={1,0.70468809,0.496585304,0.349937749,0.246596964,0.173773943,0.122456428,0.0862935865,0.0608100626};
        static const float narrowKernel[9]={1,0.496585304,0.246596964,0.122456428,0.0608100626,0.0301973834,0.0149955768,0.00744658307,0.00369786372};
        float w=broadMatte?broadKernel[x*x+y*y]:narrowKernel[x*x+y*y];
        float normalWeight=saturate(normalAgreement);
        normalWeight*=normalWeight;normalWeight*=normalWeight;
        normalWeight*=normalWeight;normalWeight*=normalWeight;
        w*=roof?1:normalWeight;
        w*=roof?exp(-dot(np.xyz-p.xyz,np.xyz-p.xyz)/(8*footprint*footprint)):exp(-abs(dot(np.xyz-p.xyz,n))*8);
        float3 radiance=lightTex.Load(int3(q,0)).rgb;
        // Filter illumination on matte surfaces, retaining the full-resolution
        // material color instead of smearing brickwork and terrain into flat paint.
        if(targetAlbedo.w>=.65)radiance/=max(albedoTex.Load(int3(fp,0)).rgb,.015);
        sum+=radiance*w;weight+=w;
    }
    if(weight<.001){
        // Thin geometry can be absent from half-resolution samples. Shade its
        // own surface instead of borrowing a background pixel across an edge.
        float4 nm=normalTex.Load(int3(pixel,0)),a=albedoTex.Load(int3(pixel,0));Material m=surfaceMaterial(uint(nm.w),a);
        p.y+=motionTex.Load(int3(pixel,0)).w;
        uint seed=hash(uint(pixel.x+pixel.y*int(screen.x)));
        if(nm.w==8&&p.w>=2048&&(abs(n.y)<.5||p.w>=0x800000u))return windowLight(p.xyz,n,normalize(eye.xyz-p.xyz),pixel,seed);
        return directAt(m,p.xyz,n,normalize(eye.xyz-p.xyz),seed)+m.color*sky(n)*.5+m.color*m.emission;
    }
    // The target color is constant across all taps, so apply it only once.
    float3 filtered=sum/weight;
    return targetAlbedo.w>=.65?filtered*targetAlbedo.rgb:filtered;
}
float4 resolveMain(Pixel pixel):SV_TARGET {
    int2 xy=int2(pixel.position.xy);float4 p=positionTex.Load(int3(xy,0));
    if(p.w==0)return float4(albedoTex.Load(int3(xy,0)).rgb,1);
    float4 nm=normalTex.Load(int3(xy,0));float3 n=nm.xyz;float3 color=spatialLight(xy,p,n);float4 motion=motionTex.Load(int3(xy,0));float2 uv=pixel.uv+motion.xy;
    // Static views reuse the exact pixel and converge longer without reprojection drift.
    // Dynamic instances keep short history so traffic remains responsive.
    bool stationarySurface=settings.z>.5&&motion.z<.5;
    if(stationarySurface)uv=(float2(xy)+.5)/screen.xy;
    float count=1;
    if(screen.w<.5&&all(uv>0)&&all(uv<1)){
        float tolerance=max(.15,length(eye.xyz-p.xyz)/screen.y*2);
        // Preserve history across adjacent roof treads and risers during camera
        // movement; they belong to the same roof despite different face normals.
        bool roof=(nm.w==7||nm.w==24)&&motion.z<.5;
        float2 historyPixel=uv*screen.xy-.5;int2 base=int2(floor(historyPixel));float2 fraction=frac(historyPixel);
        if(stationarySurface){base=xy;fraction=0;}
        float4 history=0;float weight=0;
        [unroll]for(int y=0;y<2;++y)[unroll]for(int x=0;x<2;++x){
            float w=(x?fraction.x:1-fraction.x)*(y?fraction.y:1-fraction.y);
            // Exact stationary reprojection has only one contributing tap.
            // Avoid validating or fetching neighbors whose weight is zero.
            if(w<=0)continue;
            int2 oldXY=clamp(base+int2(x,y),0,int2(screen.xy)-1);
            float4 oldP=oldPositionTex.Load(int3(oldXY,0)),oldN=oldNormalTex.Load(int3(oldXY,0));
            // Validate every bilinear tap, so a roof never accumulates a
            // neighboring chimney, wall or background through interpolation.
            if(oldP.w==p.w&&oldN.w==nm.w&&(roof||dot(n,oldN.xyz)>.95)&&(motion.z>.5||length(oldP.xyz-p.xyz)<tolerance)){
                history+=oldLightTex.Load(int3(oldXY,0))*w;weight+=w;
            }
        }
        if(weight>.001){
            // Pane-space motion cannot reconstruct parallax behind glass exactly.
            // Moving views use short history; stationary panes can denoise room GI.
            bool movingGlass=nm.w==8&&length(motion.xy*screen.xy)>.05;
            bool stationaryMatte=albedoTex.Load(int3(xy,0)).w>=.65&&motion.z<.5&&length(motion.xy*screen.xy)<.05;
            history/=weight;count=min(history.w+1,stationarySurface?256:(motion.z>.5||movingGlass)?4:(roof||nm.w==8||stationaryMatte)?32:16);
            // Clamp temporal radiance to prevent bright reflection trails.
            // Matte roof history must retain both lit treads and shaded risers;
            // clamping it to the current face would reintroduce the flicker.
            // Validated stationary matte surfaces retain their lighting average;
            // clamping to a dark stochastic sample makes shaded surfaces pulse.
            float3 clipped=(stationarySurface||roof||stationaryMatte||(nm.w==8&&!movingGlass))?history.rgb:clamp(history.rgb,color*.35,color*2.5+.04);color=lerp(clipped,color,1/count);
        }
    }
    return float4(color,count);
}
float3 tone(float3 x){return saturate((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14));}
// Smooth high-contrast raster edges after temporal lighting has converged.
// No camera jitter or extra history is introduced by this spatial filter.
float edgeLuma(float3 color){return dot(tone(color),float3(.299,.587,.114));}
float3 edgeLight(float2 uv,float3 center){
    float2 texel=1/screen.xy;
    float nw=edgeLuma(resolvedTex.SampleLevel(linearClamp,uv+float2(-1,-1)*texel,0).rgb);
    float ne=edgeLuma(resolvedTex.SampleLevel(linearClamp,uv+float2(1,-1)*texel,0).rgb);
    float sw=edgeLuma(resolvedTex.SampleLevel(linearClamp,uv+float2(-1,1)*texel,0).rgb);
    float se=edgeLuma(resolvedTex.SampleLevel(linearClamp,uv+float2(1,1)*texel,0).rgb);
    float mid=edgeLuma(center),low=min(mid,min(min(nw,ne),min(sw,se))),high=max(mid,max(max(nw,ne),max(sw,se)));
    if(high-low<max(.03125,high*.125))return center;
    float2 direction=float2(-(nw+ne-sw-se),nw+sw-ne-se);
    float reduction=max((nw+ne+sw+se)*.03125,.0078125);
    direction=clamp(direction/(min(abs(direction.x),abs(direction.y))+reduction),-8,8)*texel;
    float3 narrow=.5*(resolvedTex.SampleLevel(linearClamp,uv-direction/6,0).rgb+resolvedTex.SampleLevel(linearClamp,uv+direction/6,0).rgb);
    float3 wide=narrow*.5+.25*(resolvedTex.SampleLevel(linearClamp,uv-direction*.5,0).rgb+resolvedTex.SampleLevel(linearClamp,uv+direction*.5,0).rgb);
    float brightness=edgeLuma(wide);
    return brightness<low||brightness>high?narrow:wide;
}
float4 postMain(Pixel pixel):SV_TARGET {
    int2 xy=int2(pixel.position.xy);float4 p=positionTex.Load(int3(xy,0));float3 color=resolvedTex.Load(int3(xy,0)).rgb;
    color=edgeLight(pixel.uv,color);
    float3 bloom=0;[unroll]for(int i=0;i<4;++i){float2 offsets[4]={float2(-4,0),float2(4,0),float2(0,-4),float2(0,4)};bloom+=max(resolvedTex.SampleLevel(linearClamp,pixel.uv+offsets[i]/screen.xy,0).rgb-1,0);}
    color+=bloom*.015;
    if(p.w!=0){
        float distance=length(eye.xyz-p.xyz);color=lerp(color,atmosphereSky(cameraRay(pixel.uv)),aerialOpacity(p.xyz));
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
    bool hit=b.w<0?brickHitLimited(primitives[uint(a.w)],0,a.xyz,b.xyz,.003,1000,0,11,.75,t,n,material):brickHit(primitives[uint(a.w)],0,a.xyz,b.xyz,.003,1000,0,uint(b.w),t,n,material);
    testHits[i*2]=float4(hit?t:-1,n);testHits[i*2+1]=float4(hit?material:0,0,0,0);
}
