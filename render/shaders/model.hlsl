#ifdef NOVA_D3D12
cbuffer DrawOffsets : register(b1)
{
    uint2 drawOffsets;
};
#endif
struct Vertex
{
    float4 position;
    float4 normal;
    float4 uv;
    float4 uv1;
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
[[vk::binding(28, 0)]] Texture2D<float4> coatStrengthTexture : register(t28);
[[vk::binding(29, 0)]] Texture2D<float4> coatRoughnessTexture : register(t29);
[[vk::binding(30, 0)]] Texture2D<float4> coatNormalTexture : register(t30);
[[vk::binding(7, 0)]] SamplerState             textureSampler : register(s0);
[[vk::binding(8, 0)]] TextureCube<float4>      environmentTexture : register(t6);
[[vk::binding(9, 0)]] TextureCube<float4>      backgroundTexture : register(t7);
[[vk::binding(10, 0)]] Texture2D<float4>       pathTexture : register(t8);
// Column-major matrices and immutable local-space vertices, shared by every backend.
struct SkinInput
{
    float4 position;
    float4 normal;
    uint4  joints;
    float4 weights;
};
struct JointMatrix
{
    float4 c0, c1, c2, c3;
};
[[vk::binding(11, 0)]] StructuredBuffer<SkinInput>   skinInputs : register(t9);
[[vk::binding(12, 0)]] StructuredBuffer<JointMatrix> jointMatrices : register(t10);
float4x4                                             jointMatrix(uint index)
{
    JointMatrix m = jointMatrices[index];
    return transpose(float4x4(m.c0, m.c1, m.c2, m.c3));
}
Vertex skinVertex(uint id, uint paletteOffset = 0, bool animate = true)
{
    Vertex v = vertices[id];
    if (skyParams.w > .5 && animate)
    {
        SkinInput input = skinInputs[id];
        float4x4  m     = jointMatrix(paletteOffset + input.joints.x) * input.weights.x
                     + jointMatrix(paletteOffset + input.joints.y) * input.weights.y
                     + jointMatrix(paletteOffset + input.joints.z) * input.weights.z
                     + jointMatrix(paletteOffset + input.joints.w) * input.weights.w;
        v.position = float4(mul(m, input.position).xyz, v.position.w);
        float3 a   = float3(m[0][0], m[1][0], m[2][0]);
        float3 b   = float3(m[0][1], m[1][1], m[2][1]);
        float3 c   = float3(m[0][2], m[1][2], m[2][2]);
        float3 co0 = cross(b, c), co1 = cross(c, a), co2 = cross(a, b);
        float  det = dot(a, co0);
        float3 n   = co0 * input.normal.x + co1 * input.normal.y + co2 * input.normal.z;
        v.normal   = float4(abs(det) > 1e-8 ? normalize(n / det) : input.normal.xyz, 0);
    }
    return v;
}
// Validation readback uses the exact function called by the production vertex shader.
[[vk::binding(13, 0)]] RWStructuredBuffer<Vertex> skinOutput : register(u0);
[numthreads(64, 1, 1)] void                       SkinningCSMain(uint3 id : SV_DispatchThreadID) {
    uint count = uint(skyParams.z); // Validation dispatch count; unused by the vertex stage.
    if (id.x < count)
        skinOutput[id.x] = skinVertex(id.x);
}

struct InstanceData
{
    float4x4 world;
    float4x4 normalMatrix;
    float4   baseColor;
    float4   material;
    float4   emissive;
    float4   uv;
};
[[vk::binding(14, 0)]] StructuredBuffer<InstanceData> instances : register(t11);

float3 encodeDisplayRGB(float3 color);
float3 diagnosticLinear(float3 color);
float3 applyFog(float3 color, float3 position)
{
    if (irradiance[3].w < .5)
        return color;
    InstanceData globals = instances[uint(irradiance[3].w) - 1];
    float        mode    = globals.baseColor.w;
    if (mode < .5)
        return color;
    float  depth  = max(0, dot(position - eyeExposure.xyz, cameraForward.xyz));
    float  amount = mode < 1.5
                      ? saturate((depth - globals.material.x) / max(.0001, globals.material.y - globals.material.x))
                      : 1 - exp(-depth * depth * globals.material.z * globals.material.z);
    float3 fog    = globals.baseColor.rgb;
#if defined(NOVA_WEB_OUTPUT) && !defined(NOVA_RASTER_AA)
    // Decode the web presentation value before mixing, matching native sRGB render targets.
    float3 linearColor = float3(
      color.x <= .04045 ? color.x / 12.92 : pow((color.x + .055) / 1.055, 2.4),
      color.y <= .04045 ? color.y / 12.92 : pow((color.y + .055) / 1.055, 2.4),
      color.z <= .04045 ? color.z / 12.92 : pow((color.z + .055) / 1.055, 2.4));
    return encodeDisplayRGB(lerp(linearColor, fog, saturate(amount)));
#else
    if (irradiance[0].w > .5)
        fog = -log(max(1 - fog, float3(.00001, .00001, .00001)));
#endif
    return lerp(color, fog, saturate(amount));
}
bool shadowBox(float3 low, float3 high, float3 origin, float3 direction, float maxDistance)
{
    float nearT = 0, farT = maxDistance;
    [unroll] for (uint k = 0; k < 3; k++)
    {
        float pad = 1e-5 * max(1, max(abs(low[k]), abs(high[k])));
        if (abs(direction[k]) < 1e-15)
        {
            if (origin[k] < low[k] - pad || origin[k] > high[k] + pad)
                return false;
        }
        else
        {
            float a = (low[k] - pad - origin[k]) / direction[k], b = (high[k] + pad - origin[k]) / direction[k];
            nearT = max(nearT, min(a, b));
            farT  = min(farT, max(a, b));
            if (farT < nearT)
                return false;
        }
    }
    return true;
}
bool shadowTriangle(InstanceData tri, float3 origin, float3 direction, float maxDistance, uint materialBase)
{
    float3 a = tri.baseColor.xyz, e = tri.material.xyz - a, f = tri.emissive.xyz - a, p = cross(direction, f);
    float  determinant = dot(e, p);
    if (abs(determinant) < 1e-12)
        return false;
    float  inverse = 1 / determinant;
    float3 delta   = origin - a;
    float  u       = dot(delta, p) * inverse;
    if (u < 0 || u > 1)
        return false;
    float3 q        = cross(delta, e);
    float  v        = dot(direction, q) * inverse;
    float  distance = dot(f, q) * inverse;
    if (!(v >= 0 && u + v <= 1 && distance > 1e-5 && distance < maxDistance))
        return false;
    InstanceData material = instances[materialBase + uint(tri.emissive.w)];
    uint         flags = uint(material.emissive.w), mode = flags & 3;
    if (mode == 2 || ((flags & 4) == 0 && determinant <= 0))
        return false;
    if (mode == 1)
    {
        float2 uv =
          tri.uv.xy * (1 - u - v) + tri.uv.zw * u + float2(tri.normalMatrix[0][0], tri.normalMatrix[1][1]) * v;
        uv = frac(uv) * material.uv.xy + material.uv.zw;
        if (baseTexture.SampleLevel(textureSampler, uv, 0).a * material.baseColor.a < frac(material.emissive.w) * 4)
            return false;
    }
    return true;
}
bool sceneOccluded(uint header, float3 origin, float3 direction, float maxDistance, bool shadowOnly = true)
{
    InstanceData settings = instances[header];
    [loop] for (uint i = 0; i < uint(settings.emissive.x); i++)
    {
        InstanceData instance = instances[i];
        if (shadowOnly && instance.uv.z < .5)
            continue;
        // Explicit BVH transform components; the Web visibility kernel is optimized
        // to inline matrix storage access before SPIR-V/WGSL lowering.
        float3 translation = float3(instances[i].world._m03, instances[i].world._m13, instances[i].world._m23);
        float3 nx =
          float3(instances[i].normalMatrix._m00, instances[i].normalMatrix._m10, instances[i].normalMatrix._m20);
        float3 ny =
          float3(instances[i].normalMatrix._m01, instances[i].normalMatrix._m11, instances[i].normalMatrix._m21);
        float3 nz =
          float3(instances[i].normalMatrix._m02, instances[i].normalMatrix._m12, instances[i].normalMatrix._m22);
        float3 delta          = origin - translation;
        float3 localOrigin    = float3(dot(nx, delta), dot(ny, delta), dot(nz, delta));
        float3 localDirection = float3(dot(nx, direction), dot(ny, direction), dot(nz, direction));
        uint   root = header + 1 + uint(instance.uv.y), node = 0, count = uint(instance.uv.w);
        [loop] while (node < count)
        {
            InstanceData box = instances[root + node];
            if (!shadowBox(box.baseColor.xyz, box.material.xyz, localOrigin, localDirection, maxDistance))
            {
                node = uint(box.baseColor.w);
                continue;
            }
            uint triangles = uint(box.uv.w);
            if (triangles == 0)
            {
                node = uint(box.uv.x);
                continue;
            }
            [loop] for (uint t = 0; t < triangles; t++) if (shadowTriangle(
                                                              instances[root + uint(box.uv.z) + t],
                                                              localOrigin,
                                                              localDirection,
                                                              maxDistance,
                                                              uint(instance.uv.x))) return true;
            node = uint(box.baseColor.w);
        }
    }
    return false;
}
float sceneVisibility(float3 position, float3 normal, float3 light, float softness)
{
    if (irradiance[3].w < .5)
        return 1;
    uint         header   = uint(irradiance[3].w) - 1;
    InstanceData settings = instances[header];
    if ((irradiance[1].w > .5 ? irradiance[4].w < .5 : settings.emissive.y < .5) || settings.emissive.x < .5)
        return 1;
    light          = normalize(light);
    float3 axis    = abs(light.y) < .99 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tangent = normalize(cross(light, axis)), bitangent = cross(light, tangent);
    uint   count   = softness < .0001 ? 1 : uint(irradiance[1].w > .5 ? irradiance[2].w : settings.emissive.w);
    float  visible = 0;
    [loop] for (uint sampleIndex = 0; sampleIndex < count; sampleIndex++)
    {
        float  radius = count == 1 ? 0 : sqrt((float(sampleIndex) + .5) / count) * softness * .006;
        float  angle  = float(sampleIndex) * 2.39996323 + max(0, irradiance[6].w - 1) * 2.39996323;
        float3 ray    = normalize(light + radius * (cos(angle) * tangent + sin(angle) * bitangent));
        visible += sceneOccluded(header, position + normalize(normal) * settings.uv.x, ray, 1e30) ? 0 : 1;
    }
    return visible / count;
}
// Finite shadow segment: local BVH directions are deliberately not normalized,
// so their intersection parameter retains world-space distance under nonuniform scale.
float localLightVisibility(float3 position, float3 normal, float3 lightPosition, float softness)
{
    if (irradiance[3].w < .5)
        return 1;
    uint         header   = uint(irradiance[3].w) - 1;
    InstanceData settings = instances[header];
    if ((irradiance[1].w > .5 ? irradiance[4].w < .5 : settings.emissive.y < .5) || settings.emissive.x < .5)
        return 1;
    float3 origin   = position + normalize(normal) * settings.uv.x;
    float3 delta    = lightPosition - origin;
    float  distance = length(delta);
    if (distance < 1e-5)
        return 1;
    float3 light   = delta / distance;
    float3 axis    = abs(light.y) < .99 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tangent = normalize(cross(light, axis)), bitangent = cross(light, tangent);
    uint   count   = softness < .0001 ? 1 : uint(irradiance[1].w > .5 ? irradiance[2].w : settings.emissive.w);
    float  visible = 0;
    [loop] for (uint i = 0; i < count; ++i)
    {
        // Deterministic disk around the source, radius in world units.
        float  radius         = count == 1 ? 0 : sqrt((float(i) + .5) / count) * softness * .006;
        float  angle          = float(i) * 2.39996323 + max(0, irradiance[6].w - 1) * 2.39996323;
        float3 sampleDelta    = delta + radius * (cos(angle) * tangent + sin(angle) * bitangent);
        float  sampleDistance = length(sampleDelta);
        visible += sceneOccluded(header, origin, sampleDelta / sampleDistance, max(0, sampleDistance - 1e-5)) ? 0 : 1;
    }
    return visible / count;
}
struct VertexOut
{
    float4                position : SV_Position;
    float3                worldPosition : TEXCOORD0;
    float3                normal : TEXCOORD1;
    float2                uv : TEXCOORD2;
    nointerpolation float generatedNormal : TEXCOORD3;
    nointerpolation uint  instanceIndex : TEXCOORD4;
    nointerpolation float winding : TEXCOORD5;
    float2 uv1 : TEXCOORD6;
};
VertexOut VSMain(uint id : SV_VertexID, uint instanceId : SV_InstanceID)
{
#ifdef NOVA_D3D12
    id += drawOffsets.x;
    instanceId += drawOffsets.y;
#endif
    bool   scene         = lightDirection.w > .5 && lightDirection.w < 1.5;
    float  palette       = scene ? instances[instanceId].normalMatrix._m03 : 1;
    Vertex v             = skinVertex(id, uint(max(0, palette - 1)), palette > .5);
    uint   materialIndex = uint(max(1, v.position.w)) - 1;
    v.position.w         = 1;
    if (lightDirection.w > .5 && lightDirection.w < 1.5)
    {
        InstanceData instance = instances[instanceId];
        materialIndex += uint(instance.uv.x);
        v.position   = mul(instance.world, v.position);
        v.normal.xyz = mul((float3x3)instance.normalMatrix, v.normal.xyz);
    }
    VertexOut o;
    o.winding = 1;
    if (lightDirection.w > .5 && lightDirection.w < 1.5)
        o.winding = determinant((float3x3)instances[instanceId].world) < 0 ? -1 : 1;
    o.instanceIndex   = materialIndex;
    o.position        = mul(viewProjection, v.position);
    o.worldPosition   = v.position.xyz;
    o.normal          = v.normal.xyz;
    o.uv              = v.uv.xy;
    o.uv1             = v.uv1.xy;
    o.generatedNormal = v.uv.z;
    return o;
}
float4 materialSpecular(InstanceData material)
{
    return float4(material.world._m00, material.world._m10, material.world._m20, material.world._m30);
}
float2 materialClearcoat(InstanceData material)
{
    return float2(material.world._m01, material.world._m11);
}
float3 dielectricF0(float4 specular)
{
    return min(0.04 * specular.rgb, 1.0) * specular.a;
}
float3 fresnel(float cosine, float3 f0, float f90)
{
    return lerp(f0, f90, pow(1 - saturate(cosine), 5));
}
float3 diffuseFresnel(float cosine, float3 reflection, float4 specular)
{
    // Preserve the core metallic-roughness shading for default materials.
    if (all(specular == 1))
        return reflection;
    float3 dielectric = fresnel(cosine, dielectricF0(specular), specular.a);
    return max(max(dielectric.r, dielectric.g), dielectric.b).xxx;
}
float3 directLight(
  float3 n, float3 v, float3 l, float3 base, float metallic, float roughness, float4 specular, float3 radiance)
{
    float3 halfVector = v + l;
    float3 h          = halfVector * rsqrt(max(dot(halfVector, halfVector), 1e-8));
    float  nv = max(dot(n, v), 0.001), nl = saturate(dot(n, l)), nh = saturate(dot(n, h));
    float  a = roughness * roughness, a2 = a * a;
    float  d = a2 / (3.14159265 * pow(nh * nh * (a2 - 1) + 1, 2) + 0.0001);
    float  k = pow(roughness + 1, 2) / 8;
    float  g = (nv / (nv * (1 - k) + k)) * (nl / (nl * (1 - k) + k));
    float  vh = dot(v, h);
    float3 f = fresnel(vh, lerp(dielectricF0(specular), base, metallic), lerp(specular.a, 1, metallic));
    float3 diffuse = (1 - diffuseFresnel(vh, f, specular)) * (1 - metallic) * base / 3.14159265;
    return (diffuse + d * g * f / (4 * nv * max(nl, 0.001))) * radiance * nl;
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
float3 encodeDisplayRGB(float3 c)
{
    c = max(c, 0);
    return float3(
      c.x <= .0031308 ? 12.92 * c.x : 1.055 * pow(c.x, 1 / 2.4) - .055,
      c.y <= .0031308 ? 12.92 * c.y : 1.055 * pow(c.y, 1 / 2.4) - .055,
      c.z <= .0031308 ? 12.92 * c.z : 1.055 * pow(c.z, 1 / 2.4) - .055);
}
float3 displayRadiance(float3 color)
{
    if (irradiance[0].w > .5)
        return max(color, 0) * eyeExposure.w;
    color = 1 - exp(-max(color, 0) * eyeExposure.w);
#if defined(NOVA_WEB_OUTPUT) && !defined(NOVA_RASTER_AA)
    color = encodeDisplayRGB(color);
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
    output.position = float4(p, 1, 1);
    output.uv       = float2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    output.direction =
      cameraForward.xyz
      + (cameraUp.w < 0 ? float3(0, 0, 0) : p.x * cameraRight.w * cameraRight.xyz + p.y * cameraUp.w * cameraUp.xyz);
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
        float3 color     = pathTexture.SampleLevel(textureSampler, clamp(input.uv, halfTexel, 1 - halfTexel), 0).rgb;
        return float4(skyParams.z > 1.5 ? diagnosticLinear(color) : color, 1);
    }
    float3 direction = environmentDirection(normalize(input.direction));
    float3 color     = backgroundTexture.Sample(textureSampler, direction).rgb * skyParams.x;
    return float4(displayRadiance(color), 1);
}
struct SurfaceData
{
    float3 position, normal, geometricNormal, base, emissive;
    float  metallic, roughness, ao;
    float  opacity, alphaInfo;
    float4 specular;
    float2 clearcoat;
    float3 coatNormal;
    float4 visibility0, visibility1, visibility2, visibility3;
    uint   useVisibility;
};
struct CoatUVs
{
    float2 strength, roughness, normal;
    uint mask;
    float scale;
};
CoatUVs clearcoatUVs(InstanceData material, float2 uv0, float2 uv1)
{
    CoatUVs p;
    uint sets = uint(material.world._m02);
    p.strength = (sets & 1u) ? uv1 : uv0;
    p.roughness = (sets & 2u) ? uv1 : uv0;
    p.normal = (sets & 4u) ? uv1 : uv0;
    p.mask = uint(material.world._m31);
    p.scale = material.world._m12;
    return p;
}
float3 applyCoatNormal(float3 n, float3 map, float scale, float3 dp1, float3 dp2, float2 duv1, float2 duv2)
{
    float3 t = cross(dp2, n) * duv1.x + cross(n, dp1) * duv2.x;
    float3 b = cross(dp2, n) * duv1.y + cross(n, dp1) * duv2.y;
    float basis = max(dot(t, t), dot(b, b));
    float3 perturbed = (t * map.x + b * map.y) * (scale * rsqrt(max(basis, 1e-20))) + n * map.z;
    return (basis > 1e-20 && dot(perturbed, perturbed) > 1e-12) ? normalize(perturbed) : n;
}
SurfaceData sampleSurface(VertexOut input)
{
    float  alphaInfo  = .125;
    float4 specular   = 1;
    float2 clearcoat  = 0;
    InstanceData materialRecord = (InstanceData)0;
    float2 uv         = input.uv;
    float2 aoUV       = uv;
    float4 baseFactor = baseColorFactor, matFactor = materialFactor, emissionFactor = emissiveFactor;
    if (lightDirection.w > .5)
    {
        InstanceData instance = instances[input.instanceIndex];
        uv                    = frac(uv) * instance.uv.xy + instance.uv.zw;
        aoUV                  = instance.world._m21 > .5 ? frac(input.uv1) * instance.uv.xy + instance.uv.zw : uv;
        baseFactor            = instance.baseColor;
        matFactor             = instance.material;
        emissionFactor.rgb    = instance.emissive.rgb;
        alphaInfo             = instance.emissive.w;
        specular              = materialSpecular(instance);
        clearcoat             = materialClearcoat(instance);
        materialRecord        = instance;
    }
    float4 baseSample = baseTexture.Sample(textureSampler, uv);
    float3 base       = baseSample.rgb * baseFactor.rgb;
    float3 n          = normalize(input.normal);
    float3 dp1 = ddx(input.worldPosition), dp2 = ddy(input.worldPosition);
    if (input.generatedNormal > .5)
    {
        float3 face = cross(dp1, dp2);
        if (dot(face, face) > 1e-12)
        {
            face = normalize(face);
            n    = dot(face, n) < 0 ? -face : face;
        }
    }
    float3 coatNormal = n;
    CoatUVs coatUV = clearcoatUVs(materialRecord, input.uv, input.uv1);
    float2 coatDX = ddx(coatUV.normal), coatDY = ddy(coatUV.normal);
    float2 strengthDX = ddx(coatUV.strength), strengthDY = ddy(coatUV.strength);
    float2 roughDX = ddx(coatUV.roughness), roughDY = ddy(coatUV.roughness);
    if (clearcoat.x > 0)
    {
        float2 atlasNormalUV = frac(coatUV.normal) * materialRecord.uv.xy + materialRecord.uv.zw;
        if (coatUV.mask & 1u)
            clearcoat.x *= coatStrengthTexture.SampleGrad(textureSampler,
                frac(coatUV.strength) * materialRecord.uv.xy + materialRecord.uv.zw,
                strengthDX * materialRecord.uv.xy, strengthDY * materialRecord.uv.xy).r;
        if (coatUV.mask & 2u)
            clearcoat.y *= coatRoughnessTexture.SampleGrad(textureSampler,
                frac(coatUV.roughness) * materialRecord.uv.xy + materialRecord.uv.zw,
                roughDX * materialRecord.uv.xy, roughDY * materialRecord.uv.xy).g;
        if (coatUV.mask & 4u)
            coatNormal = applyCoatNormal(n, coatNormalTexture.SampleGrad(textureSampler, atlasNormalUV,
                coatDX * materialRecord.uv.xy, coatDY * materialRecord.uv.xy).xyz * 2 - 1,
                coatUV.scale, dp1, dp2, coatDX, coatDY);
    }
    float2 duv1 = ddx(input.uv), duv2 = ddy(input.uv);
    float3 p2perp = cross(dp2, n), p1perp = cross(n, dp1);
    float3 t = p2perp * duv1.x + p1perp * duv2.x, b = p2perp * duv1.y + p1perp * duv2.y;
    float  scale   = rsqrt(max(max(dot(t, t), dot(b, b)), 0.00001));
    float3 sampled = normalTexture.Sample(textureSampler, uv).xyz * 2 - 1;
    sampled.xy *= matFactor.z;
    n                    = normalize(t * scale * sampled.x + b * scale * sampled.y + n * sampled.z);
    float4      mr       = materialTexture.Sample(textureSampler, uv);
    float       metallic = saturate(mr.b * matFactor.x), roughness = clamp(mr.g * matFactor.y, 0.06, 1);
    float       ao = lerp(1, occlusionTexture.Sample(textureSampler, aoUV).r, matFactor.w);
    SurfaceData result;
    result.useVisibility = 0;
    result.specular = specular;
    result.clearcoat = clearcoat;
    result.coatNormal = coatNormal;
    result.visibility0 = result.visibility1 = result.visibility2 = result.visibility3 = 1;
    result.opacity                                                                    = baseSample.a * baseFactor.a;
    result.alphaInfo                                                                  = alphaInfo;
    result.position                                                                   = input.worldPosition;
    result.normal                                                                     = n;
    result.geometricNormal                                                            = normalize(input.normal);
    result.base                                                                       = base;
    result.metallic                                                                   = metallic;
    result.roughness                                                                  = roughness;
    result.ao                                                                         = ao;
    result.emissive = emissiveTexture.Sample(textureSampler, uv).rgb * emissionFactor.rgb;
    return result;
}
float hybridVisibility(SurfaceData s, uint index)
{
    return index < 4    ? s.visibility0[index]
           : index < 8  ? s.visibility1[index - 4]
           : index < 12 ? s.visibility2[index - 8]
                        : s.visibility3[min(index - 12, 3)];
}
float clearcoatTransmission(SurfaceData s, float3 v)
{
    return 1 - s.clearcoat.x * fresnel(dot(s.coatNormal, v), 0.04, 1).x;
}
float3 surfaceDirectLight(SurfaceData s, float3 v, float3 l, float3 radiance, float transmission)
{
    float3 color = directLight(s.normal, v, l, s.base, s.metallic, s.roughness, s.specular, radiance);
    if (s.clearcoat.x > 0)
        color = color * transmission + s.clearcoat.x * directLight(
          s.coatNormal, v, l, 0.04, 1, max(.06, s.clearcoat.y), 1, radiance);
    return color;
}
float3 clearcoatEnvironment(SurfaceData s, float3 v)
{
    if (s.clearcoat.x <= 0)
        return 0;
    float3 n = s.coatNormal;
    float3 r = reflect(-v, n);
    float roughness = max(.06, s.clearcoat.y);
    float nv = saturate(dot(n, v));
    if (cameraForward.w > .5)
    {
        float3 filtered = environmentTexture.SampleLevel(
          textureSampler, environmentDirection(r), roughness * environmentParams.w).rgb;
        float2 brdf = environmentBRDF(roughness, nv);
        return s.clearcoat.x * filtered * (0.04 * brdf.x + brdf.y) * environmentParams.z;
    }
    return s.clearcoat.x * studio(r, roughness) * fresnel(nv, 0.04, 1);
}
float3 surfaceRadiance(SurfaceData s, float3 raySpecular, float reflectionMix, float3 viewDirection)
{
    float3 base = s.base;
    if (emissiveFactor.w < .5)
        return base;
    float3 n = s.normal, v = viewDirection;
    float  metallic = s.metallic, roughness = s.roughness, ao = s.ao;
    float  coatTransmission = clearcoatTransmission(s, v);
    bool   collection = false;
    uint   header     = 0;
    if (irradiance[3].w > .5)
    {
        header     = uint(irradiance[3].w) - 1;
        collection = instances[header].uv.w > .5;
    }
    float3 color = 0;
    if (collection)
    {
        uint count = uint(instances[header].uv.y), offset = uint(instances[header].uv.z);
        [loop] for (uint i = 0; i < count; ++i)
        {
            InstanceData source      = instances[offset + i];
            uint         type        = uint(source.emissive.w);
            float3       l           = source.emissive.xyz;
            float        attenuation = 1;
            if (type != 0)
            {
                float3 delta           = source.material.xyz - s.position;
                float  distanceSquared = dot(delta, delta);
                float  distance        = sqrt(distanceSquared);
                l                      = distance > .000001 ? delta / distance : float3(0, 1, 0);
                attenuation            = 1 / max(distanceSquared, .01);
                if (source.material.w > 0)
                    attenuation *= pow(saturate(1 - pow(distance / source.material.w, 4)), 2);
                if (type == 2)
                {
                    float cone = dot(-l, source.emissive.xyz);
                    attenuation *= source.uv.x > source.uv.y + .000001 ? smoothstep(source.uv.y, source.uv.x, cone)
                                                                       : step(source.uv.y, cone);
                }
            }
            if (attenuation <= 0)
                continue;
            float visibility = 1;
            if (s.useVisibility)
                visibility = hybridVisibility(s, i);
            else if (source.uv.z > .5)
                visibility = type == 0
                               ? sceneVisibility(s.position, s.geometricNormal, l, source.uv.w)
                               : localLightVisibility(s.position, s.geometricNormal, source.material.xyz, source.uv.w);
            color += surfaceDirectLight(
              s, v, l, source.baseColor.rgb * source.baseColor.w * attenuation * visibility, coatTransmission);
        }
    }
    else
        color = surfaceDirectLight(
          s,
          v,
          lightDirection.xyz,
          lightColorIntensity.rgb * lightColorIntensity.w
            * (s.useVisibility?s.visibility0.x:sceneVisibility(
              s.position,
              s.geometricNormal,
              lightDirection.xyz,
              irradiance[3].w > .5 ? instances[uint(irradiance[3].w) - 1].emissive.z : 0)),
          coatTransmission);
    float3 f0 = lerp(dielectricF0(s.specular), base, metallic);
    float  f90 = lerp(s.specular.a, 1, metallic);
    float  nv = saturate(dot(n, v));
    float3 f = fresnel(nv, f0, f90);
    float3 diffuse = (1 - metallic) * base;
    float3 diffuseEnergy = 1 - diffuseFresnel(nv, f, s.specular);
    if (cameraForward.w > 0.5)
    {
        float3 reflected = environmentDirection(reflect(-v, n));
        float3 prefiltered =
          environmentTexture.SampleLevel(textureSampler, reflected, roughness * environmentParams.w).rgb;
        float2 brdf    = environmentBRDF(roughness, saturate(dot(n, v)));
        float3 ambient = diffuseEnergy * diffuse * diffuseEnvironment(n) * environmentParams.z
                         + lerp(prefiltered * (f0 * brdf.x + f90 * brdf.y) * environmentParams.z, raySpecular, reflectionMix);
        color += ambient * ao * coatTransmission;
    }
    else
    {
        if (!collection)
            color += surfaceDirectLight(s, v, normalize(float3(1, 0.7, -1)), float3(1, 1.6, 2.7), coatTransmission);
        color += (diffuse * (all(s.specular == 1) ? float3(1, 1, 1) : diffuseEnergy) * studio(n, 1) * 0.4
                  + lerp(studio(reflect(-v, n), roughness) * f, raySpecular, reflectionMix))
                 * ao * coatTransmission;
    }
    color += s.emissive * coatTransmission + clearcoatEnvironment(s, v) * ao;
    return color;
}
float4 shadeSurface(SurfaceData s)
{
    float3 base = s.base;
    if (emissiveFactor.w < .5)
    {
#if defined(NOVA_WEB_OUTPUT) && !defined(NOVA_RASTER_AA)
        base = encodeDisplayRGB(base);
#endif
        return float4(applyFog(base, s.position), 1);
    }
    return float4(
      applyFog(
        displayRadiance(
          surfaceRadiance(s, 0, 0, cameraUp.w < 0 ? -cameraForward.xyz : normalize(eyeExposure.xyz - s.position))),
        s.position),
      1);
}

void materialCoverage(inout SurfaceData s, bool face, float winding, bool transparent)
{
    bool front = winding < 0 ? !face : face;
    uint flags = (uint)s.alphaInfo, mode = flags & 3;
    if ((flags & 4) == 0 && !front)
        discard;
    if (mode == 1 && s.opacity < frac(s.alphaInfo) * 4)
        discard;
    if (transparent ? (mode != 2 || s.opacity <= 0) : mode == 2)
        discard;
    if (!front)
    {
        s.normal          = -s.normal;
        s.geometricNormal = -s.geometricNormal;
        s.coatNormal = -s.coatNormal;
    }
}
float4 PSMain(VertexOut input, bool face : SV_IsFrontFace)
  : SV_Target
{
    SurfaceData s = sampleSurface(input);
    materialCoverage(s, face, input.winding, false);
    return shadeSurface(s);
}
struct OITOutput
{
    float4 accumulation : SV_Target0;
    float4 revealage : SV_Target1;
};
OITOutput OITPSMain(VertexOut input, bool face : SV_IsFrontFace)
{
    SurfaceData s = sampleSurface(input);
    materialCoverage(s, face, input.winding, true);
    float     depth  = max(0, dot(s.position - eyeExposure.xyz, cameraForward.xyz));
    float     weight = clamp(1 / (1 + .01 * depth * depth), .01, 1);
    OITOutput o;
    o.accumulation = float4(shadeSurface(s).rgb * s.opacity * weight, s.opacity * weight);
    o.revealage    = s.opacity.xxxx;
    return o;
}

// Four 8-byte targets fit WebGPU's baseline 32-byte attachment budget.
// Both normals use octahedral encoding; roughness > 0 marks a covered pixel.
float2 octEncode(float3 n)
{
    n /= max(abs(n.x) + abs(n.y) + abs(n.z), 1e-8);
    float2 r = n.xy;
    if (n.z < 0)
        r = (1 - abs(r.yx)) * float2(r.x >= 0 ? 1 : -1, r.y >= 0 ? 1 : -1);
    return r;
}
float3 octDecode(float2 e)
{
    float3 n = float3(e, 1 - abs(e.x) - abs(e.y));
    float  t = saturate(-n.z);
    n.xy += float2(n.x >= 0 ? -t : t, n.y >= 0 ? -t : t);
    return normalize(n);
}
struct ModelGBuffer
{
    float4 baseRoughness : SV_Target0;
    float4 normals : SV_Target1;
    float4 positionAO : SV_Target2;
    float4 emissiveMetallic : SV_Target3;
    float4 clearcoat : SV_Target4;
};
ModelGBuffer GBufferPSMain(VertexOut input, bool face : SV_IsFrontFace)
{
    SurfaceData s = sampleSurface(input);
    materialCoverage(s, face, input.winding, false);
    ModelGBuffer o;
    o.baseRoughness = float4(s.base, s.roughness);
    o.normals       = float4(octEncode(s.normal), octEncode(s.geometricNormal));
    // Split logarithmic view depth avoids half-float world-position quantization.
    // Both halves survive RGBA16F storage, preserving near/far scale without an extra target.
    float depth        = log2(max(1e-20, dot(s.position - eyeExposure.xyz, cameraForward.xyz)));
    float high         = f16tof32(f32tof16(depth));
    // Material slots sit immediately before the globals record. The relative
    // index fits exactly in half precision even when the scene has many instances.
    float slotDistance = lightDirection.w > .5 && irradiance[3].w > .5
                         ? irradiance[3].w - 1 - float(input.instanceIndex) : 0;
    o.positionAO       = float4(high, depth - high, s.ao, slotDistance);
    o.emissiveMetallic = float4(s.emissive, s.metallic);
    o.clearcoat        = float4(octEncode(s.coatNormal), s.clearcoat);
    return o;
}
[[vk::binding(17, 1)]] Texture2D<float4> gBase : register(t17);
[[vk::binding(18, 1)]] Texture2D<float4> gNormals : register(t18);
[[vk::binding(19, 1)]] Texture2D<float4> gPosition : register(t19);
[[vk::binding(20, 1)]] Texture2D<float4> gEmission : register(t20);
[[vk::binding(31, 1)]] Texture2D<float4> gClearcoat : register(t31);
[[vk::binding(21, 1)]] Texture2D<float4> hybridShadow0 : register(t21);
[[vk::binding(22, 1)]] Texture2D<float4> hybridShadow1 : register(t22);
[[vk::binding(23, 1)]] Texture2D<float4> hybridShadow2 : register(t23);
[[vk::binding(24, 1)]] Texture2D<float4> hybridShadow3 : register(t24);
float3                                   surfacePosition(SkyOut input, float2 encodedDepth)
{
    float depth = exp2(encodedDepth.x + encodedDepth.y);
    if (cameraUp.w >= 0)
        return eyeExposure.xyz + input.direction * depth;
    float2 ndc = float2(input.uv.x * 2 - 1, 1 - input.uv.y * 2);
    return eyeExposure.xyz + cameraForward.xyz * depth + cameraRight.xyz * (ndc.x * cameraRight.w)
           + cameraUp.xyz * (ndc.y * abs(cameraUp.w));
}
bool localLightCastsAt(InstanceData light, float3 position)
{
    float3 delta = light.material.xyz - position;
    float distance = length(delta);
    if (light.material.w > 0 && distance >= light.material.w)
        return false;
    if (uint(light.emissive.w) != 2)
        return true;
    float3 direction = distance > .000001 ? delta / distance : float3(0, 1, 0);
    float cone = dot(-direction, light.emissive.xyz);
    // Smooth cones have zero weight at the boundary. A hard cone includes it.
    return light.uv.x > light.uv.y + .000001 ? cone > light.uv.y : cone >= light.uv.y;
}
// Sixteen independent light visibilities, four light slots per target.
struct HybridShadowTargets
{
    float4 slots0 : SV_Target0;
    float4 slots1 : SV_Target1;
    float4 slots2 : SV_Target2;
    float4 slots3 : SV_Target3;
};
HybridShadowTargets HybridShadowPSMain(SkyOut input)
{
    int3 pixel = int3(int2(input.position.xy), 0);
    HybridShadowTargets output;
    float        visibility[16];
    [unroll] for (uint j = 0; j < 16; j++) visibility[j] = 1;
    if (gBase.Load(pixel).w > 0 && irradiance[4].w > .5 && irradiance[3].w > .5)
    {
        float3 position = surfacePosition(input, gPosition.Load(pixel).xy), normal = octDecode(gNormals.Load(pixel).zw);
        uint   header        = uint(irradiance[3].w) - 1;
        InstanceData globals = instances[header];
        if (globals.uv.w > .5)
        {
            uint count = min(16, uint(globals.uv.y)), offset = uint(globals.uv.z);
            [loop] for (uint i = 0; i < count; i++)
            {
                InstanceData light = instances[offset + i];
                if (light.uv.z <= .5 || light.baseColor.w <= 0)
                    continue;
                if (uint(light.emissive.w) == 0)
                    visibility[i] = sceneVisibility(position, normal, light.emissive.xyz, light.uv.w);
                else
                {
                    // The selected-light diagnostic shows raw visibility outside the light influence too.
                    bool showRawVisibility = irradiance[5].w == float(i + 1);
                    if (showRawVisibility || localLightCastsAt(light, position))
                        visibility[i] = localLightVisibility(position, normal, light.material.xyz, light.uv.w);
                }
            }
        }
        else
            visibility[0] = sceneVisibility(position, normal, lightDirection.xyz, globals.emissive.z);
    }
    output.slots0 = float4(visibility[0], visibility[1], visibility[2], visibility[3]);
    output.slots1 = float4(visibility[4], visibility[5], visibility[6], visibility[7]);
    output.slots2 = float4(visibility[8], visibility[9], visibility[10], visibility[11]);
    output.slots3 = float4(visibility[12], visibility[13], visibility[14], visibility[15]);
    return output;
}
float3 diagnosticLinear(float3 c)
{
    c = saturate(c);
    return float3(
      c.x <= .04045 ? c.x / 12.92 : pow((c.x + .055) / 1.055, 2.4),
      c.y <= .04045 ? c.y / 12.92 : pow((c.y + .055) / 1.055, 2.4),
      c.z <= .04045 ? c.z / 12.92 : pow((c.z + .055) / 1.055, 2.4));
}
// Cosine-weighted finite hemisphere visibility. AO only attenuates indirect lighting.
float hybridAO(float3 position, float3 normal, float rotation)
{
    if (irradiance[3].w < .5)
        return 1;
    uint  header = uint(irradiance[3].w) - 1;
    float radius = instances[header].world._m00;
    if (radius <= 0)
        return 1;
    uint   samples   = uint(instances[header].world._m10);
    float  strength  = instances[header].world._m20;
    float3 tangent   = normalize(cross(abs(normal.y) < .99 ? float3(0, 1, 0) : float3(1, 0, 0), normal));
    float3 bitangent = cross(normal, tangent);
    float  visible   = 0;
    float3 origin    = position + normal * max(1e-4, instances[header].uv.x);
    [loop] for (uint i = 0; i < samples; ++i)
    {
        float  r = sqrt((i + .5) / samples), phi = i * 2.39996323 + rotation;
        float3 direction = tangent * (r * cos(phi)) + bitangent * (r * sin(phi)) + normal * sqrt(1 - r * r);
        visible += sceneOccluded(header, origin, direction, radius, false) ? 0 : 1;
    }
    return pow(saturate(visible / max(1, samples)), strength);
}
// Nearest-hit traversal uses world-distance parameters even for scaled instances.
// BLEND is excluded, MASK and sidedness use the same coverage as shadow rays.
bool hybridNearest(
  uint        header,
  float3      origin,
  float3      direction,
  inout float distance,
  out uint    hitInstance,
  out uint    hitTriangle,
  out float2  hitBary)
{
    bool found  = false;
    hitInstance = hitTriangle = 0;
    hitBary                   = 0;
    [loop] for (uint i = 0; i < uint(instances[header].emissive.x); ++i)
    {
        float3 translation = float3(instances[i].world._m03, instances[i].world._m13, instances[i].world._m23);
        float3 nx =
          float3(instances[i].normalMatrix._m00, instances[i].normalMatrix._m10, instances[i].normalMatrix._m20);
        float3 ny =
          float3(instances[i].normalMatrix._m01, instances[i].normalMatrix._m11, instances[i].normalMatrix._m21);
        float3 nz =
          float3(instances[i].normalMatrix._m02, instances[i].normalMatrix._m12, instances[i].normalMatrix._m22);
        float3 delta = origin - translation;
        float3 o     = float3(dot(nx, delta), dot(ny, delta), dot(nz, delta));
        float3 d     = float3(dot(nx, direction), dot(ny, direction), dot(nz, direction));
        uint   root = header + 1 + uint(instances[i].uv.y), node = 0, count = uint(instances[i].uv.w);
        [loop] while (node < count)
        {
            InstanceData box = instances[root + node];
            if (!shadowBox(box.baseColor.xyz, box.material.xyz, o, d, distance))
            {
                node = uint(box.baseColor.w);
                continue;
            }
            uint triangles = uint(box.uv.w);
            if (triangles == 0)
            {
                node = uint(box.uv.x);
                continue;
            }
            [loop] for (uint t = 0; t < triangles; ++t)
            {
                uint         index = root + uint(box.uv.z) + t;
                InstanceData tri   = instances[index];
                if (!shadowTriangle(tri, o, d, distance, uint(instances[i].uv.x)))
                    continue;
                float3 e = tri.material.xyz - tri.baseColor.xyz, f = tri.emissive.xyz - tri.baseColor.xyz;
                float3 p = cross(d, f), q = cross(o - tri.baseColor.xyz, e);
                float  inverse = 1 / dot(e, p);
                distance       = dot(f, q) * inverse;
                hitBary        = float2(dot(o - tri.baseColor.xyz, p), dot(d, q)) * inverse;
                hitInstance    = i;
                hitTriangle    = index;
                found          = true;
            }
            node = uint(box.baseColor.w);
        }
    }
    return found;
}
float hybridRotation(uint2 pixel)
{
    if (irradiance[6].w < .5)
        return 0;
    uint seed = pixel.x * 1973u + pixel.y * 9277u + uint(irradiance[6].w) * 26699u;
    seed      = (seed ^ (seed >> 16)) * 2246822519u;
    seed ^= seed >> 13;
    return (seed & 65535u) * (6.28318530718 / 65536.0);
}
float3 hybridHitRadiance(uint instance, uint hitTri, float2 bary, float3 position, float3 ray)
{
    InstanceData tri = instances[hitTri];
    InstanceData mat = instances[uint(instances[instance].uv.x) + uint(tri.emissive.w)];
    float2 uv0 = tri.uv.xy, uv1 = tri.uv.zw;
    float2 uv2 = float2(tri.normalMatrix[0][0], tri.normalMatrix[1][1]);
    float2 uv1a = float2(tri.world._m00, tri.world._m10);
    float2 uv1b = float2(tri.world._m20, tri.world._m30);
    float2 uv1c = float2(tri.world._m01, tri.world._m11);
    float2 tex0 = uv0 + (uv1 - uv0) * bary.x + (uv2 - uv0) * bary.y;
    float2 tex1 = uv1a + (uv1b - uv1a) * bary.x + (uv1c - uv1a) * bary.y;
    float2 uv   = frac(tex0) * mat.uv.xy + mat.uv.zw;
    float2 aoUV = mat.world._m21 > .5 ? frac(tex1) * mat.uv.xy + mat.uv.zw : uv;
    float3 localEdge1 = tri.material.xyz - tri.baseColor.xyz;
    float3 localEdge2 = tri.emissive.xyz - tri.baseColor.xyz;
    float3 worldNormal = normalize(mul(
      (float3x3)instances[instance].normalMatrix, cross(localEdge1, localEdge2)));
    if (dot(worldNormal, ray) > 0)
        worldNormal = -worldNormal;

    SurfaceData hit = (SurfaceData)0;
    hit.position = position;
    hit.specular = materialSpecular(mat);
    hit.clearcoat = materialClearcoat(mat);
    hit.normal = hit.geometricNormal = hit.coatNormal = worldNormal;
    if (hit.clearcoat.x > 0)
    {
        CoatUVs coatUV = clearcoatUVs(mat, tex0, tex1);
        if (coatUV.mask & 1u)
            hit.clearcoat.x *= coatStrengthTexture.SampleLevel(textureSampler,
                frac(coatUV.strength) * mat.uv.xy + mat.uv.zw, 0).r;
        if (coatUV.mask & 2u)
            hit.clearcoat.y *= coatRoughnessTexture.SampleLevel(textureSampler,
                frac(coatUV.roughness) * mat.uv.xy + mat.uv.zw, 0).g;
        if (coatUV.mask & 4u)
        {
            bool uvSet = uint(mat.world._m02) & 4u;
            float3 coatMap = coatNormalTexture.SampleLevel(textureSampler,
                frac(coatUV.normal) * mat.uv.xy + mat.uv.zw, 0).xyz * 2 - 1;
            hit.coatNormal = applyCoatNormal(worldNormal, coatMap, coatUV.scale,
                mul((float3x3)instances[instance].world, localEdge1),
                mul((float3x3)instances[instance].world, localEdge2),
                uvSet ? uv1b - uv1a : uv1 - uv0, uvSet ? uv1c - uv1a : uv2 - uv0);
        }
    }
    float2 deltaUV1 = uv1 - uv0, deltaUV2 = uv2 - uv0;
    float  uvDet = deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x;
    if (abs(uvDet) > 1e-8 && mat.material.z != 0)
    {
        float3 edge1 = mul((float3x3)instances[instance].world, localEdge1);
        float3 edge2 = mul((float3x3)instances[instance].world, localEdge2);
        // The determinant sign preserves the orientation of mirrored UV charts.
        float3 t = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * sign(uvDet);
        float3 b = (edge2 * deltaUV1.x - edge1 * deltaUV2.x) * sign(uvDet);
        float  scale = rsqrt(max(max(dot(t, t), dot(b, b)), 1e-12));
        float3 sampled = normalTexture.SampleLevel(textureSampler, uv, 0).xyz * 2 - 1;
        sampled.xy *= mat.material.z;
        float3 mapped = (t * sampled.x + b * sampled.y) * scale + worldNormal * sampled.z;
        if (dot(mapped, mapped) > 1e-12)
            hit.normal = normalize(mapped);
    }
    hit.base      = baseTexture.SampleLevel(textureSampler, uv, 0).rgb * mat.baseColor.rgb;
    float4 mr     = materialTexture.SampleLevel(textureSampler, uv, 0);
    hit.metallic  = saturate(mr.b * mat.material.x);
    hit.roughness = clamp(mr.g * mat.material.y, .04, 1);
    hit.ao        = lerp(1, occlusionTexture.SampleLevel(textureSampler, aoUV, 0).r, mat.material.w);
    hit.emissive = emissiveTexture.SampleLevel(textureSampler, uv, 0).rgb * mat.emissive.rgb;
    return surfaceRadiance(hit, 0, 0, -ray);
}

// Two buffers keep the previous update available while writing the next batch.
struct HybridProbeLobe
{
    float4 irradiance; // RGB and validity
    float4 distanceMoments; // Normalized mean and second moment
};
[[vk::binding(25, 2)]] cbuffer HybridProbeUpdate : register(b0, space2)
{
    float4 probeOrigin;
    float4 probeSpacingDistance; // XYZ spacing, W ray limit
    uint4 probeDimensionsFirst; // XYZ counts, W first probe in this batch
    uint2 probeBatch; // Count and update sequence
    float probeHistoryWeight;
    uint probeSceneHeader;
};
[[vk::binding(26, 2)]] StructuredBuffer<HybridProbeLobe> probePrevious : register(t0, space2);
[[vk::binding(27, 2)]] RWStructuredBuffer<HybridProbeLobe> probeUpdated : register(u0, space2);

[numthreads(32, 1, 1)]
void HybridProbeUpdateCS(uint3 thread : SV_DispatchThreadID)
{
    const uint lobeCount = 6, sampleCount = 16;
    uint localProbe = thread.x / lobeCount;
    if (localProbe >= probeBatch.x)
        return;

    uint3 dimensions = probeDimensionsFirst.xyz;
    uint probe = probeDimensionsFirst.w + localProbe;
    if (probe >= dimensions.x * dimensions.y * dimensions.z)
        return;

    uint3 cell = uint3(
      probe % dimensions.x,
      probe / dimensions.x % dimensions.y,
      probe / (dimensions.x * dimensions.y));
    float3 origin = probeOrigin.xyz + float3(cell) * probeSpacingDistance.xyz;
    uint lobe = thread.x % lobeCount;
    uint axis = lobe / 2;
    float3 n = float3(axis == 0, axis == 1, axis == 2) * ((lobe & 1) ? -1 : 1);
    float3 tangent = normalize(cross(abs(n.y) < .99 ? float3(0, 1, 0) : float3(1, 0, 0), n));
    float3 bitangent = cross(n, tangent);
    float rotation = frac(float(probeBatch.y) * .61803399 + float(probe) * .75487767) * 6.28318531;
    float3 incoming = 0;
    float2 moments = 0;
    [loop] for (uint j = 0; j < sampleCount; ++j)
    {
        float u = (j + .5) / sampleCount, phi = rotation + j * 2.39996323;
        float3 ray = sqrt(u) * (tangent * cos(phi) + bitangent * sin(phi)) + n * sqrt(1 - u);
        float distance = probeSpacingDistance.w;
        uint instance, hitTriangle;
        float2 bary;
        if (hybridNearest(probeSceneHeader, origin, ray, distance, instance, hitTriangle, bary))
            incoming += hybridHitRadiance(instance, hitTriangle, bary, origin + ray * distance, ray);
        float normalizedDistance = saturate(distance / probeSpacingDistance.w);
        moments += float2(normalizedDistance, normalizedDistance * normalizedDistance);
    }
    uint index = probe * lobeCount + lobe;
    HybridProbeLobe previous = probePrevious[index];
    float history = saturate(probeHistoryWeight) * previous.irradiance.w;
    HybridProbeLobe result;
    result.irradiance = float4(lerp(incoming / sampleCount, previous.irradiance.rgb, history), 1);
    result.distanceMoments = float4(lerp(moments / sampleCount, previous.distanceMoments.xy, history), 0, 0);
    probeUpdated[index] = result;
}

// Axis lobes use squared direction components so constant radiance stays constant.
float4 hybridProbeDirection(uint probe, float3 direction, bool distance)
{
    float3 weights = direction * direction;
    uint first = probe * 6;
    HybridProbeLobe x = probePrevious[first     + uint(direction.x < 0)];
    HybridProbeLobe y = probePrevious[first + 2 + uint(direction.y < 0)];
    HybridProbeLobe z = probePrevious[first + 4 + uint(direction.z < 0)];
    if (distance)
        return x.distanceMoments * weights.x + y.distanceMoments * weights.y + z.distanceMoments * weights.z;
    return x.irradiance * weights.x + y.irradiance * weights.y + z.irradiance * weights.z;
}

float hybridProbeVisibility(float normalizedDistance, float2 moments)
{
    float delta = max(normalizedDistance - moments.x, 0);
    float variance = max(moments.y - moments.x * moments.x, 1e-5);
    float bound = variance / (variance + delta * delta);
    // Squaring the upper bound suppresses broad variance tails behind walls.
    return bound * bound;
}

float3 hybridSampleProbes(float3 position, float3 normal)
{
    uint3 dimensions = probeDimensionsFirst.xyz;
    if (any(dimensions == 0))
        return 0;
    float3 rel = (position - probeOrigin.xyz) / probeSpacingDistance.xyz;
    if (any(rel < -.5) || any(rel > float3(dimensions) - .5))
        return 0;

    float3 grid = clamp(rel, 0, float3(dimensions - 1));
    uint3 first = uint3(floor(grid));
    float3 fraction = frac(grid);
    float3 total = 0;
    float weightTotal = 0;
    [loop] for (uint corner = 0; corner < 8; ++corner)
    {
        uint3 offset = uint3(corner & 1, (corner >> 1) & 1, corner >> 2);
        float3 axisWeight = lerp(1 - fraction, fraction, float3(offset));
        float weight = axisWeight.x * axisWeight.y * axisWeight.z;
        if (weight == 0)
            continue;

        uint3 cell = first + offset;
        uint probe = cell.x + dimensions.x * (cell.y + dimensions.y * cell.z);
        float4 irradiance = hybridProbeDirection(probe, normal, false);
        if (irradiance.w <= 0)
            continue;

        float3 center = probeOrigin.xyz + float3(cell) * probeSpacingDistance.xyz;
        float3 toSurface = position - center;
        float distance = length(toSurface);
        float3 direction = distance > 1e-5 ? toSurface / distance : normal;
        float2 moments = hybridProbeDirection(probe, direction, true).xy;
        float visibility = hybridProbeVisibility(distance / probeSpacingDistance.w, moments);
        float facing = saturate(.5 - .5 * dot(normal, direction));
        total += irradiance.rgb * (weight * visibility * facing);
        // Keep blocked probes in the denominator so normalization cannot restore leaked light.
        weightTotal += weight * irradiance.w;
    }
    return weightTotal > 0 ? total / weightTotal : 0;
}

#if defined(NOVA_PROBE_SAMPLE_VALIDATION)
[[vk::binding(3, 2)]] StructuredBuffer<float4> probeQueries : register(t1, space2);
[[vk::binding(4, 2)]] RWStructuredBuffer<float4> probeSamples : register(u1, space2);
[numthreads(32, 1, 1)]
void HybridProbeSampleCS(uint3 thread : SV_DispatchThreadID)
{
    if (thread.x < probeBatch.x)
        probeSamples[thread.x] = float4(hybridSampleProbes(
          probeQueries[thread.x * 2].xyz, probeQueries[thread.x * 2 + 1].xyz), 1);
}
#endif

float3 hybridDiffuseBounce(SurfaceData surface, uint header, float rotation)
{
    float3 n = surface.geometricNormal;
    float3 t = normalize(cross(abs(n.y) < .99 ? float3(0, 1, 0) : float3(1, 0, 0), n));
    float3 b = cross(n, t);
    float3 origin = surface.position + n * max(1e-4, instances[header].uv.x);
    float3 incoming = 0;
    const uint samples = 4;
    [loop] for (uint j = 0; j < samples; ++j)
    {
        float u = (j + .5) / samples, phi = j * 2.39996323 + rotation;
        float3 ray = sqrt(u) * (t * cos(phi) + b * sin(phi)) + n * sqrt(1 - u);
        float distance = instances[header].world._m31;
        uint instance, hitTriangle;
        float2 bary;
        // The existing environment lighting already accounts for rays that leave the scene.
        if (hybridNearest(header, origin, ray, distance, instance, hitTriangle, bary))
            incoming += hybridHitRadiance(instance, hitTriangle, bary, origin + ray * distance, ray);
    }
    return incoming * surface.base * (1 - surface.metallic) / samples;
}

float3 hybridReflection(SurfaceData primary, float rotation)
{
    uint   header      = uint(irradiance[3].w) - 1;
    float  maxDistance = instances[header].world._m01;
    uint   samples     = uint(instances[header].world._m11);
    float3 n = primary.normal, v = cameraUp.w < 0 ? -cameraForward.xyz : normalize(eyeExposure.xyz - primary.position);
    float3 tangent   = normalize(cross(abs(n.y) < .99 ? float3(0, 1, 0) : float3(1, 0, 0), n)),
           bitangent = cross(n, tangent);
    float  nv = max(.001, dot(n, v)), rough = max(.02, primary.roughness), a = rough * rough;
    float3 sum = 0;
    [loop] for (uint j = 0; j < samples; ++j)
    {
        float  u = (j + .5) / samples, phi = j * 2.39996323 + rotation;
        float  cosTheta = sqrt((1 - u) / (1 + (a * a - 1) * u)), sinTheta = sqrt(max(0, 1 - cosTheta * cosTheta));
        float3 h   = normalize(tangent * (sinTheta * cos(phi)) + bitangent * (sinTheta * sin(phi)) + n * cosTheta);
        float3 ray = reflect(-v, h);
        float  nl = dot(n, ray), nh = max(.001, dot(n, h)), vh = max(0, dot(v, h));
        if (nl <= 0)
            continue;
        float  distance = maxDistance;
        uint   instance, hitTri;
        float2 bary;
        float3 origin = primary.position + primary.geometricNormal * max(1e-4, instances[header].uv.x);
        float3 radiance;
        if (hybridNearest(header, origin, ray, distance, instance, hitTri, bary))
        {
            radiance = hybridHitRadiance(instance, hitTri, bary, origin + ray * distance, ray);
        }
        else
            radiance =
              cameraForward.w > .5
                ? environmentTexture.SampleLevel(textureSampler, environmentDirection(ray), 0).rgb * environmentParams.z
                : studio(ray, 0);
        float k = pow(rough + 1, 2) / 8;
        float g = (nv / (nv * (1 - k) + k)) * (nl / (nl * (1 - k) + k));
        float3 f0 = lerp(dielectricF0(primary.specular), primary.base, primary.metallic);
        float f90 = lerp(primary.specular.a, 1, primary.metallic);
        sum += radiance * fresnel(vh, f0, f90) * (g * vh / (nv * nh));
    }
    return sum / max(1, samples);
}

float4 DeferredPSMain(SkyOut input)
  : SV_Target
{
    float3 backgroundDirection = environmentDirection(normalize(input.direction));
    float3 backgroundDX = ddx(backgroundDirection), backgroundDY = ddy(backgroundDirection);
    int3   pixel = int3(int2(input.position.xy), 0);
    float4 base  = gBase.Load(pixel);
    uint   debug = uint(irradiance[8].w);
    if (base.w <= 0)
    {
        if (debug != 0 || (irradiance[1].w > .5 && irradiance[5].w > .5))
            return float4(0, 0, 0, 1);
        if (cameraForward.w > .5 && skyParams.y > .5)
            return float4(
              displayRadiance(
                backgroundTexture.SampleGrad(textureSampler, backgroundDirection, backgroundDX, backgroundDY).rgb
                * skyParams.x),
              1);
        discard;
    }
    float4      normals = gNormals.Load(pixel), pos = gPosition.Load(pixel), emission = gEmission.Load(pixel);
    SurfaceData s;
    s.useVisibility = irradiance[1].w > .5 ? 1 : 0;
    s.visibility0 = s.visibility1 = s.visibility2 = s.visibility3 = 1;
    if (s.useVisibility)
    {
        s.visibility0 = hybridShadow0.Load(pixel);
        s.visibility1 = hybridShadow1.Load(pixel);
        s.visibility2 = hybridShadow2.Load(pixel);
        s.visibility3 = hybridShadow3.Load(pixel);
    }
    if (s.useVisibility && irradiance[5].w > .5)
        return float4(diagnosticLinear(hybridVisibility(s, uint(irradiance[5].w) - 1).xxx), 1);
    s.base            = base.rgb;
    s.roughness       = base.w;
    s.normal          = octDecode(normals.xy);
    s.geometricNormal = octDecode(normals.zw);
    s.position        = surfacePosition(input, pos.xy);
    s.ao              = pos.z;
    s.emissive        = emission.rgb;
    s.metallic        = emission.w;
    s.specular        = 1;
    float4 coat       = gClearcoat.Load(pixel);
    s.coatNormal      = octDecode(coat.xy);
    s.clearcoat       = coat.zw;
    if (pos.w > .5 && irradiance[3].w > pos.w)
    {
        InstanceData material = instances[uint(irradiance[3].w - 1 - pos.w)];
        s.specular = materialSpecular(material);
    }
    if (s.useVisibility)
    {
        float ao = hybridAO(s.position, s.geometricNormal, hybridRotation(uint2(input.position.xy)));
        if (irradiance[3].w > .5 && abs(instances[uint(irradiance[3].w) - 1].world._m30 - 2) < .1)
            return float4(diagnosticLinear(ao.xxx), 1);
        s.ao *= ao;
    }
    if (s.useVisibility && irradiance[3].w > .5)
    {
        uint header = uint(irradiance[3].w) - 1;
        float reflectionMix = 0;
        float3 reflection = 0;
        float rotation = hybridRotation(uint2(input.position.xy));
        bool reflections = instances[header].world._m01 > 0;
        if (reflections)
        {
            reflection = hybridReflection(s, rotation);
            if (instances[header].world._m30 > 2.5)
                return float4(displayRadiance(reflection), 1);
            reflectionMix = instances[header].world._m21;
        }
        bool diffuseBounce = instances[header].world._m31 > 0 && emissiveFactor.w > .5;
#if defined(NOVA_HYBRID_PROBES)
        bool probes = probeDimensionsFirst.x > 0 && emissiveFactor.w > .5;
#else
        bool probes = false;
#endif
        if (debug == 0 && (reflections || diffuseBounce || probes))
        {
            float3 viewDirection = cameraUp.w < 0 ? -cameraForward.xyz : normalize(eyeExposure.xyz - s.position);
            float3 color = surfaceRadiance(s, reflection, reflectionMix, viewDirection);
#if defined(NOVA_HYBRID_PROBES)
            if (probes && s.metallic < 1)
                color += hybridSampleProbes(s.position, s.normal) * s.base * (1 - s.metallic)
                         * clearcoatTransmission(s, viewDirection);
#endif
            if (!probes && diffuseBounce && s.metallic < 1)
                color += hybridDiffuseBounce(s, header, rotation) * clearcoatTransmission(s, viewDirection);
            return float4(applyFog(displayRadiance(color), s.position), 1);
        }
    }
    if (debug == 0)
        return shadeSurface(s);
    float3 value = 0;
    float  range = max(.001, irradiance[7].w);
    if (debug == 1)
        value = s.base;
    if (debug == 2)
        value = s.normal * .5 + .5;
    if (debug == 3)
        value = s.geometricNormal * .5 + .5;
    if (debug == 4)
        value = (max(0, dot(s.position - eyeExposure.xyz, cameraForward.xyz)) / range).xxx;
    if (debug == 5)
        value = s.roughness.xxx;
    if (debug == 6)
        value = s.metallic.xxx;
    if (debug == 7)
        value = s.ao.xxx;
    if (debug == 8)
        value = s.emissive;
    if (debug == 9)
        value = s.position / range * .5 + .5;
    return float4(diagnosticLinear(value), 1);
}

// TAA uses the previous camera and skin palette, not a screen-space history blend.
[[vk::binding(15, 0)]] cbuffer PreviousScene : register(b15)
{
    float4x4 previousViewProjection;
    float4   previousSceneRest[20];
}
[[vk::binding(16, 0)]] StructuredBuffer<JointMatrix> previousJoints : register(t16);
struct MotionOut
{
    float4                position : SV_Position;
    float4                current : TEXCOORD0;
    float4                previous : TEXCOORD1;
    float2                uv : TEXCOORD2;
    nointerpolation uint  materialIndex : TEXCOORD3;
    nointerpolation float winding : TEXCOORD4;
    float                 previousDepth : TEXCOORD5;
    nointerpolation float identity : TEXCOORD6;
};
MotionOut MotionVSMain(uint id : SV_VertexID, uint instanceId : SV_InstanceID)
{
#ifdef NOVA_D3D12
    id += drawOffsets.x;
    instanceId += drawOffsets.y;
#endif
    bool   scene         = lightDirection.w > .5 && lightDirection.w < 1.5;
    float  palette       = scene ? instances[instanceId].normalMatrix._m03 : 1;
    uint   paletteOffset = uint(max(0, palette - 1));
    Vertex current = skinVertex(id, paletteOffset, palette > .5), old = vertices[id];
    if (skyParams.w > .5 && palette > .5)
    {
        SkinInput input    = skinInputs[id];
        float4    position = 0;
        [unroll] for (uint j = 0; j < 4; j++)
        {
            JointMatrix m      = previousJoints[paletteOffset + input.joints[j]];
            float4x4    matrix = transpose(float4x4(m.c0, m.c1, m.c2, m.c3));
            position += mul(matrix, input.position) * input.weights[j];
        }
        old.position = float4(position.xyz, 1);
    }
    uint  materialIndex = uint(max(1, current.position.w)) - 1;
    float winding       = 1;
    current.position.w = old.position.w = 1;
    if (lightDirection.w > .5 && lightDirection.w < 1.5)
    {
        materialIndex += uint(instances[instanceId].uv.x);
        winding          = determinant((float3x3)instances[instanceId].world) < 0 ? -1 : 1;
        current.position = mul(instances[instanceId].world, current.position);
        old.position     = mul(instances[instanceId].world, old.position);
    }
    MotionOut o;
    o.uv            = current.uv.xy;
    o.materialIndex = materialIndex;
    o.winding       = winding;
    o.current       = mul(viewProjection, current.position);
    o.previous      = mul(previousViewProjection, old.position);
    o.position      = o.current;
    o.previousDepth = dot(old.position.xyz - previousSceneRest[0].xyz, previousSceneRest[8].xyz);
    o.identity      = float(instanceId + 1);
    return o;
}
float4 MotionPSMain(MotionOut input, bool face : SV_IsFrontFace)
  : SV_Target
{
    float2 uv     = input.uv;
    float4 factor = baseColorFactor;
    float  info   = .125;
    if (lightDirection.w > .5)
    {
        InstanceData m = instances[input.materialIndex];
        uv             = frac(uv) * m.uv.xy + m.uv.zw;
        factor         = m.baseColor;
        info           = m.emissive.w;
    }
    float opacity = baseTexture.Sample(textureSampler, uv).a * factor.a;
    uint  flags = uint(info), mode = flags & 3;
    bool  front = input.winding < 0 ? !face : face;
    if (((flags & 4) == 0 && !front) || mode == 2 || (mode == 1 && opacity < frac(info) * 4))
        discard;
    float2 current = input.current.xy / input.current.w * float2(.5, -.5) + .5,
           old     = input.previous.xy / input.previous.w * float2(.5, -.5) + .5;
    if (irradiance[1].w > .5)
        return float4(old - current, log2(max(1e-20, input.previousDepth)), input.identity);
    return float4(old - current, input.current.w / 16, .125);
}
float4 SkyMotionPSMain(SkyOut input)
  : SV_Target
{
    float4 previous = mul(previousViewProjection, float4(input.direction, 0));
    float2 uv       = previous.xy / max(previous.w, .00001) * float2(.5, -.5) + .5;
    return float4(uv - input.uv, irradiance[1].w > .5 ? 0 : 1, irradiance[1].w > .5 ? 0 : 1);
}
