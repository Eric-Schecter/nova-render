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
Vertex skinVertex(uint id)
{
    Vertex v = vertices[id];
    if (skyParams.w > .5)
    {
        SkinInput input = skinInputs[id];
        float4x4  m     = jointMatrix(input.joints.x) * input.weights.x + jointMatrix(input.joints.y) * input.weights.y
                     + jointMatrix(input.joints.z) * input.weights.z + jointMatrix(input.joints.w) * input.weights.w;
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
bool shadowBox(float3 low, float3 high, float3 origin, float3 direction)
{
    float nearT = 0, farT = 1e30;
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
bool shadowTriangle(InstanceData tri, float3 origin, float3 direction)
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
    float3 q = cross(delta, e);
    float  v = dot(direction, q) * inverse;
    return v >= 0 && u + v <= 1 && dot(f, q) * inverse > 1e-5;
}
bool sceneOccluded(uint header, float3 origin, float3 direction)
{
    InstanceData settings = instances[header];
    [loop] for (uint i = 0; i < uint(settings.emissive.x); i++)
    {
        InstanceData instance = instances[i];
        if (instance.uv.z < .5)
            continue;
        float3x3 inverse        = transpose((float3x3)instance.normalMatrix);
        float3   localOrigin    = mul(inverse, origin - mul(instance.world, float4(0, 0, 0, 1)).xyz);
        float3   localDirection = mul(inverse, direction);
        uint     root = header + 1 + uint(instance.uv.y), node = 0, count = uint(instance.uv.w);
        [loop] while (node < count)
        {
            InstanceData box = instances[root + node];
            if (!shadowBox(box.baseColor.xyz, box.material.xyz, localOrigin, localDirection))
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
                                                              localDirection)) return true;
            node = uint(box.baseColor.w);
        }
    }
    return false;
}
float sceneVisibility(float3 position, float3 normal)
{
    if (irradiance[3].w < .5)
        return 1;
    uint         header   = uint(irradiance[3].w) - 1;
    InstanceData settings = instances[header];
    if (settings.emissive.y < .5 || settings.emissive.x < .5)
        return 1;
    float3 light = normalize(lightDirection.xyz), axis = abs(light.y) < .99 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tangent = normalize(cross(light, axis)), bitangent = cross(light, tangent);
    uint   count   = settings.emissive.z < .0001 ? 1 : uint(settings.emissive.w);
    float  visible = 0;
    [loop] for (uint sampleIndex = 0; sampleIndex < count; sampleIndex++)
    {
        float  radius = count == 1 ? 0 : sqrt((float(sampleIndex) + .5) / count) * settings.emissive.z * .006;
        float  angle  = float(sampleIndex) * 2.39996323;
        float3 ray    = normalize(light + radius * (cos(angle) * tangent + sin(angle) * bitangent));
        visible += sceneOccluded(header, position + normalize(normal) * settings.uv.x, ray) ? 0 : 1;
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
};
VertexOut VSMain(uint id : SV_VertexID, uint instanceId : SV_InstanceID)
{
#ifdef NOVA_D3D12
    id += drawOffsets.x;
    instanceId += drawOffsets.y;
#endif
    Vertex    v = skinVertex(id);
    uint      materialIndex = uint(max(1, v.position.w)) - 1;
    v.position.w            = 1;
    if (lightDirection.w > .5 && lightDirection.w < 1.5)
    {
        InstanceData instance = instances[instanceId];
        materialIndex += uint(instance.uv.x);
        v.position            = mul(instance.world, v.position);
        v.normal.xyz          = mul((float3x3)instance.normalMatrix, v.normal.xyz);
    }
    VertexOut o;
    o.instanceIndex   = materialIndex;
    o.position        = mul(viewProjection, v.position);
    o.worldPosition   = v.position.xyz;
    o.normal          = v.normal.xyz;
    o.uv              = v.uv.xy;
    o.generatedNormal = v.uv.z;
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
    output.position  = float4(p, 1, 1);
    output.uv        = float2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
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
        return float4(pathTexture.SampleLevel(textureSampler, clamp(input.uv, halfTexel, 1 - halfTexel), 0).rgb, 1);
    }
    float3 direction = environmentDirection(normalize(input.direction));
    float3 color     = backgroundTexture.Sample(textureSampler, direction).rgb * skyParams.x;
    return float4(displayRadiance(color), 1);
}
float4 PSMain(VertexOut input)
  : SV_Target
{
    float2 uv         = input.uv;
    float4 baseFactor = baseColorFactor, matFactor = materialFactor, emissionFactor = emissiveFactor;
    if (lightDirection.w > .5)
    {
        InstanceData instance = instances[input.instanceIndex];
        uv                    = frac(uv) * instance.uv.xy + instance.uv.zw;
        baseFactor            = instance.baseColor;
        matFactor             = instance.material;
        emissionFactor.rgb    = instance.emissive.rgb;
    }
    float3 base = baseTexture.Sample(textureSampler, uv).rgb * baseFactor.rgb;
    if (emissiveFactor.w < 0.5)
    {
        // Base color only: no lighting, AO, emissive, exposure or tone mapping.
#if defined(NOVA_WEB_OUTPUT) && !defined(NOVA_RASTER_AA)
        base = encodeDisplayRGB(base);
#endif
        return float4(applyFog(base, input.worldPosition), 1);
    }
    float3 n   = normalize(input.normal);
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
    float2 duv1 = ddx(input.uv), duv2 = ddy(input.uv);
    float3 p2perp = cross(dp2, n), p1perp = cross(n, dp1);
    float3 t = p2perp * duv1.x + p1perp * duv2.x, b = p2perp * duv1.y + p1perp * duv2.y;
    float  scale   = rsqrt(max(max(dot(t, t), dot(b, b)), 0.00001));
    float3 sampled = normalTexture.Sample(textureSampler, uv).xyz * 2 - 1;
    sampled.xy *= matFactor.z;
    n               = normalize(t * scale * sampled.x + b * scale * sampled.y + n * sampled.z);
    float3 v        = cameraUp.w < 0 ? -cameraForward.xyz : normalize(eyeExposure.xyz - input.worldPosition);
    float4 mr       = materialTexture.Sample(textureSampler, uv);
    float  metallic = saturate(mr.b * matFactor.x), roughness = clamp(mr.g * matFactor.y, 0.06, 1);
    float  ao = lerp(1, occlusionTexture.Sample(textureSampler, uv).r, matFactor.w);
    float3 color = directLight(
      n,
      v,
      lightDirection.xyz,
      base,
      metallic,
      roughness,
      lightColorIntensity.rgb * lightColorIntensity.w * sceneVisibility(input.worldPosition, input.normal));
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
    color += emissiveTexture.Sample(textureSampler, uv).rgb * emissionFactor.rgb;
    return float4(applyFog(displayRadiance(color), input.worldPosition), 1);
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
    float4 position : SV_Position;
    float4 current : TEXCOORD0;
    float4 previous : TEXCOORD1;
};
MotionOut MotionVSMain(uint id : SV_VertexID, uint instanceId : SV_InstanceID)
{
#ifdef NOVA_D3D12
    id += drawOffsets.x;
    instanceId += drawOffsets.y;
#endif
    Vertex current = skinVertex(id), old = vertices[id];
    if (skyParams.w > .5)
    {
        SkinInput input    = skinInputs[id];
        float4    position = 0;
        [unroll] for (uint j = 0; j < 4; j++)
        {
            JointMatrix m      = previousJoints[input.joints[j]];
            float4x4    matrix = transpose(float4x4(m.c0, m.c1, m.c2, m.c3));
            position += mul(matrix, input.position) * input.weights[j];
        }
        old.position = float4(position.xyz, 1);
    }
    current.position.w = old.position.w = 1;
    if (lightDirection.w > .5 && lightDirection.w < 1.5)
    {
        current.position = mul(instances[instanceId].world, current.position);
        old.position     = mul(instances[instanceId].world, old.position);
    }
    MotionOut o;
    o.current  = mul(viewProjection, current.position);
    o.previous = mul(previousViewProjection, old.position);
    o.position = o.current;
    return o;
}
float4 MotionPSMain(MotionOut input)
  : SV_Target
{
    float2 current = input.current.xy / input.current.w * float2(.5, -.5) + .5,
           old     = input.previous.xy / input.previous.w * float2(.5, -.5) + .5;
    return float4(old - current, input.current.w / 16, .125);
}
float4 SkyMotionPSMain(SkyOut input)
  : SV_Target
{
    float4 previous = mul(previousViewProjection, float4(input.direction, 0));
    float2 uv       = previous.xy / max(previous.w, .00001) * float2(.5, -.5) + .5;
    return float4(uv - input.uv, 1, 1);
}
