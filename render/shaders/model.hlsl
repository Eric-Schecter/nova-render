struct Vertex
{
    float4 position;
    float4 normal;
    float4 uv;
};
[[vk::binding(0, 0)]] cbuffer Scene : register(b0)
{
    float4x4 viewProjection;
    float4   eyeExposure;
    float4   baseColorFactor;
    float4   materialFactor;
    float4   emissiveFactor;      // rgb: emissive factor, w: PBR enabled
    float4   lightDirection;      // xyz: normalized surface-to-light direction
    float4   lightColorIntensity; // rgb: linear color, w: intensity
    float4   cameraRight;
    float4   cameraUp;
    float4   cameraForward;     // w: environment loaded
    float4   environmentParams; // sin/cos rotation, IBL intensity, highest mip
    float4   skyParams;         // background intensity and visibility
    float4   irradiance[9];
};
[[vk::binding(1, 0)]] StructuredBuffer<Vertex> vertices : register(t0);
[[vk::binding(2, 0)]] Texture2D<float4>        baseTexture : register(t1);
[[vk::binding(3, 0)]] Texture2D<float4>        normalTexture : register(t2);
[[vk::binding(4, 0)]] Texture2D<float4>        materialTexture : register(t3);
[[vk::binding(5, 0)]] Texture2D<float4>        occlusionTexture : register(t4);
[[vk::binding(6, 0)]] Texture2D<float4>        emissiveTexture : register(t5);
[[vk::binding(7, 0)]] SamplerState             textureSampler : register(s0);
[[vk::binding(8, 0)]] TextureCube<float4>      environmentTexture : register(t6);
[[vk::binding(9, 0)]] TextureCube<float4>      backgroundTexture : register(t7);
[[vk::binding(10, 0)]] Texture2D<float4>       pathTexture : register(t8);
struct VertexOut
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
};
VertexOut VSMain(uint id : SV_VertexID)
{
    Vertex    v = vertices[id];
    VertexOut o;
    o.position      = mul(viewProjection, v.position);
    o.worldPosition = v.position.xyz;
    o.normal        = v.normal.xyz;
    o.uv            = v.uv.xy;
    return o;
}
float3 fresnel(float cosine, float3 f0)
{
    return f0 + (1 - f0) * pow(1 - saturate(cosine), 5);
}
float3 directLight(float3 n, float3 v, float3 l, float3 base, float metallic, float roughness, float3 radiance)
{
    float3 halfVector = v + l;
    float3 h          = halfVector * rsqrt(max(dot(halfVector, halfVector), 1e-8));
    float  nv = max(dot(n, v), 0.001), nl = saturate(dot(n, l)), nh = saturate(dot(n, h));
    float  a = roughness * roughness, a2 = a * a;
    float  d = a2 / (3.14159265 * pow(nh * nh * (a2 - 1) + 1, 2) + 0.0001);
    float  k = pow(roughness + 1, 2) / 8;
    float  g = (nv / (nv * (1 - k) + k)) * (nl / (nl * (1 - k) + k));
    float3 f = fresnel(dot(v, h), lerp(float3(0.04, 0.04, 0.04), base, metallic));
    return ((1 - f) * (1 - metallic) * base / 3.14159265 + d * g * f / (4 * nv * max(nl, 0.001))) * radiance * nl;
}
// Analytic studio environment; no external HDR file is required by this example.
float3 studio(float3 r, float roughness)
{
    float3 sky      = lerp(float3(0.035, 0.04, 0.055), float3(0.45, 0.52, 0.65), saturate(r.y * 0.5 + 0.5));
    float  exponent = lerp(160, 2, roughness);
    sky += float3(4, 3.7, 3.3) * pow(saturate(dot(r, normalize(float3(-1, 1, 1)))), exponent);
    sky += float3(1.4, 1.8, 2.5) * pow(saturate(dot(r, normalize(float3(1, 0.4, -1)))), exponent);
    return sky;
}
float3 environmentDirection(float3 direction)
{
    float sine = environmentParams.x, cosine = environmentParams.y;
    return float3(cosine * direction.x + sine * direction.z, direction.y, -sine * direction.x + cosine * direction.z);
}
float3 diffuseEnvironment(float3 n)
{
    n              = environmentDirection(n);
    float basis[9] = {
      0.282095,
      0.488603 * n.y,
      0.488603 * n.z,
      0.488603 * n.x,
      1.092548 * n.x * n.y,
      1.092548 * n.y * n.z,
      0.315392 * (3 * n.z * n.z - 1),
      1.092548 * n.x * n.z,
      0.546274 * (n.x * n.x - n.y * n.y)};
    float3 result = 0;
    [unroll] for (int i = 0; i < 9; ++i) result += irradiance[i].rgb * basis[i];
    return max(result, 0);
}
// Analytical approximation to the split-sum BRDF integral (GGX prefilter is baked offline).
float2 environmentBRDF(float roughness, float nv)
{
    float4 r    = roughness * float4(-1, -0.0275, -0.572, 0.022) + float4(1, 0.0425, 1.04, -0.04);
    float  a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}
float3 displayRadiance(float3 color)
{
    color = 1 - exp(-max(color, 0) * eyeExposure.w);
#ifdef NOVA_WEB_OUTPUT
    color = pow(color, 1.0 / 2.2);
#endif
    return color;
}
struct SkyOut
{
    float4 position : SV_Position;
    float3 direction : TEXCOORD0;
    float2 uv : TEXCOORD1;
};
SkyOut SkyVSMain(uint id : SV_VertexID)
{
    float2 positions[3] = {float2(-1, -1), float2(-1, 3), float2(3, -1)};
    float2 p            = positions[id];
    SkyOut output;
    output.position  = float4(p, 1, 1);
    output.uv        = float2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    output.direction = cameraForward.xyz + p.x * cameraRight.w * cameraRight.xyz + p.y * cameraUp.w * cameraUp.xyz;
    return output;
}
float4 SkyPSMain(SkyOut input)
  : SV_Target
{
    if (skyParams.z > 0.5)
    {
        uint width, height;
        pathTexture.GetDimensions(width, height);
        float2 halfTexel = 0.5 / float2(width, height);
        return float4(pathTexture.SampleLevel(textureSampler, clamp(input.uv, halfTexel, 1 - halfTexel), 0).rgb, 1);
    }
    float3 direction = environmentDirection(normalize(input.direction));
    float3 color     = backgroundTexture.Sample(textureSampler, direction).rgb * skyParams.x;
    return float4(displayRadiance(color), 1);
}
float4 PSMain(VertexOut input)
  : SV_Target
{
    float3 base = pow(max(baseTexture.Sample(textureSampler, input.uv).rgb, 0), 2.2) * baseColorFactor.rgb;
    if (emissiveFactor.w < 0.5)
    {
        // Base color only: no lighting, AO, emissive, exposure or tone mapping.
#ifdef NOVA_WEB_OUTPUT
        base = pow(max(base, 0), 1.0 / 2.2);
#endif
        return float4(base, 1);
    }
    float3 n   = normalize(input.normal);
    float3 dp1 = ddx(input.worldPosition), dp2 = ddy(input.worldPosition);
    float2 duv1 = ddx(input.uv), duv2 = ddy(input.uv);
    float3 p2perp = cross(dp2, n), p1perp = cross(n, dp1);
    float3 t = p2perp * duv1.x + p1perp * duv2.x, b = p2perp * duv1.y + p1perp * duv2.y;
    float  scale   = rsqrt(max(max(dot(t, t), dot(b, b)), 0.00001));
    float3 sampled = normalTexture.Sample(textureSampler, input.uv).xyz * 2 - 1;
    sampled.xy *= materialFactor.z;
    n               = normalize(t * scale * sampled.x + b * scale * sampled.y + n * sampled.z);
    float3 v        = normalize(eyeExposure.xyz - input.worldPosition);
    float4 mr       = materialTexture.Sample(textureSampler, input.uv);
    float  metallic = saturate(mr.b * materialFactor.x), roughness = clamp(mr.g * materialFactor.y, 0.06, 1);
    float  ao = lerp(1, occlusionTexture.Sample(textureSampler, input.uv).r, materialFactor.w);
    float3 color =
      directLight(n, v, lightDirection.xyz, base, metallic, roughness, lightColorIntensity.rgb * lightColorIntensity.w);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), base, metallic);
    float3 f  = fresnel(saturate(dot(n, v)), f0);
    if (cameraForward.w > 0.5)
    {
        float3 reflected = environmentDirection(reflect(-v, n));
        float3 prefiltered =
          environmentTexture.SampleLevel(textureSampler, reflected, roughness * environmentParams.w).rgb;
        float2 brdf    = environmentBRDF(roughness, saturate(dot(n, v)));
        float3 ambient = (1 - f) * (1 - metallic) * base * diffuseEnvironment(n) + prefiltered * (f0 * brdf.x + brdf.y);
        color += ambient * environmentParams.z * ao;
    }
    else
    {
        color += directLight(n, v, normalize(float3(1, 0.7, -1)), base, metallic, roughness, float3(1, 1.6, 2.7));
        color += (base * (1 - metallic) * studio(n, 1) * 0.4 + studio(reflect(-v, n), roughness) * f) * ao;
    }
    color += pow(max(emissiveTexture.Sample(textureSampler, input.uv).rgb, 0), 2.2) * emissiveFactor.rgb;
    return float4(displayRadiance(color), 1);
}
