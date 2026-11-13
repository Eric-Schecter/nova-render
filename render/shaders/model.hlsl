struct Vertex { float4 position; float4 normal; float4 uv; };
[[vk::binding(0, 0)]] cbuffer Scene : register(b0) {
    float4x4 viewProjection;
    float4 eyeExposure;
    float4 baseColorFactor;
    float4 materialFactor;
    float4 emissiveFactor; // rgb: emissive factor, w: PBR enabled
};
[[vk::binding(1, 0)]] StructuredBuffer<Vertex> vertices : register(t0);
[[vk::binding(2, 0)]] Texture2D<float4> baseTexture : register(t1);
[[vk::binding(3, 0)]] Texture2D<float4> normalTexture : register(t2);
[[vk::binding(4, 0)]] Texture2D<float4> materialTexture : register(t3);
[[vk::binding(5, 0)]] Texture2D<float4> occlusionTexture : register(t4);
[[vk::binding(6, 0)]] Texture2D<float4> emissiveTexture : register(t5);
[[vk::binding(7, 0)]] SamplerState textureSampler : register(s0);
struct VertexOut {
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
};
VertexOut VSMain(uint id : SV_VertexID) {
    Vertex v=vertices[id]; VertexOut o;
    o.position=mul(viewProjection,v.position);
    o.worldPosition=v.position.xyz; o.normal=v.normal.xyz; o.uv=v.uv.xy;
    return o;
}
float3 fresnel(float cosine,float3 f0) {return f0+(1-f0)*pow(1-saturate(cosine),5);}
float3 directLight(float3 n,float3 v,float3 l,float3 base,float metallic,float roughness,float3 radiance) {
    float3 h=normalize(v+l);float nv=max(dot(n,v),0.001),nl=saturate(dot(n,l)),nh=saturate(dot(n,h));
    float a=roughness*roughness,a2=a*a;float d=a2/(3.14159265*pow(nh*nh*(a2-1)+1,2)+0.0001);
    float k=pow(roughness+1,2)/8;float g=(nv/(nv*(1-k)+k))*(nl/(nl*(1-k)+k));
    float3 f=fresnel(dot(v,h),lerp(float3(0.04,0.04,0.04),base,metallic));
    return ((1-f)*(1-metallic)*base/3.14159265 + d*g*f/(4*nv*max(nl,0.001)))*radiance*nl;
}
// Analytic studio environment; no external HDR file is required by this example.
float3 studio(float3 r,float roughness) {
    float3 sky=lerp(float3(0.035,0.04,0.055),float3(0.45,0.52,0.65),saturate(r.y*0.5+0.5));
    float exponent=lerp(160,2,roughness);
    sky+=float3(4,3.7,3.3)*pow(saturate(dot(r,normalize(float3(-1,1,1)))),exponent);
    sky+=float3(1.4,1.8,2.5)*pow(saturate(dot(r,normalize(float3(1,0.4,-1)))),exponent);
    return sky;
}
float4 PSMain(VertexOut input) : SV_Target {
    float3 base=pow(max(baseTexture.Sample(textureSampler,input.uv).rgb,0),2.2)*baseColorFactor.rgb;
    if (emissiveFactor.w < 0.5) {
        // Base color only: no lighting, AO, emissive, exposure or tone mapping.
#ifdef NOVA_WEB_OUTPUT
        base=pow(max(base,0),1.0/2.2);
#endif
        return float4(base,1);
    }
    float3 n=normalize(input.normal);
    float3 dp1=ddx(input.worldPosition),dp2=ddy(input.worldPosition);
    float2 duv1=ddx(input.uv),duv2=ddy(input.uv);
    float3 p2perp=cross(dp2,n),p1perp=cross(n,dp1);
    float3 t=p2perp*duv1.x+p1perp*duv2.x, b=p2perp*duv1.y+p1perp*duv2.y;
    float scale=rsqrt(max(max(dot(t,t),dot(b,b)),0.00001));
    float3 sampled=normalTexture.Sample(textureSampler,input.uv).xyz*2-1;
    sampled.xy*=materialFactor.z;
    n=normalize(t*scale*sampled.x+b*scale*sampled.y+n*sampled.z);
    float3 v=normalize(eyeExposure.xyz-input.worldPosition);
    float4 mr=materialTexture.Sample(textureSampler,input.uv);
    float metallic=saturate(mr.b*materialFactor.x),roughness=clamp(mr.g*materialFactor.y,0.06,1);
    float ao=lerp(1,occlusionTexture.Sample(textureSampler,input.uv).r,materialFactor.w);
    float3 color=directLight(n,v,normalize(float3(-1,1.6,2)),base,metallic,roughness,float3(3.5,3.2,2.8));
    color+=directLight(n,v,normalize(float3(1,0.7,-1)),base,metallic,roughness,float3(1,1.6,2.7));
    float3 f=fresnel(dot(n,v),lerp(float3(0.04,0.04,0.04),base,metallic));
    color+=(base*(1-metallic)*studio(n,1)*0.4+studio(reflect(-v,n),roughness)*f)*ao;
    color+=pow(max(emissiveTexture.Sample(textureSampler,input.uv).rgb,0),2.2)*emissiveFactor.rgb;
    color=1-exp(-max(color,0)*eyeExposure.w);
#ifdef NOVA_WEB_OUTPUT
    color=pow(color,1.0/2.2);
#endif
    return float4(color,1);
}
