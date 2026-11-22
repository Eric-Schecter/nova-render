#ifndef NOVA_MATERIAL_ANISOTROPY
#define NOVA_MATERIAL_ANISOTROPY
float3 anisotropicBasis(float3 n)
{
    return normalize(cross(abs(n.y) < .999 ? float3(0, 1, 0) : float3(1, 0, 0), n));
}
float3 anisotropicTangent(float3 n, float3 tangent)
{
    float3 projected = tangent - n * dot(tangent, n);
    return dot(projected, projected) > 1e-10 ? normalize(projected) : anisotropicBasis(n);
}
float2 anisotropicRoughness(float roughness, float strength)
{
    float alpha = max(.0004, roughness * roughness);
    return float2(lerp(alpha, 1, strength * strength), alpha);
}
float3 anisotropicCoordinates(float3 direction, float3 n, float3 t)
{
    return float3(dot(direction, t), dot(direction, cross(n, t)), dot(direction, n));
}
float anisotropicD(float3 h, float3 n, float3 t, float2 alpha)
{
    float3 projected = anisotropicCoordinates(h, n, t);
    projected.xy /= alpha;
    float q = dot(projected, projected);
    return 1 / max(3.14159265 * alpha.x * alpha.y * q * q, 1e-12);
}
float anisotropicVisibility(float3 v, float3 l, float3 n, float3 t, float2 alpha)
{
    float3 vv = anisotropicCoordinates(v, n, t), ll = anisotropicCoordinates(l, n, t);
    float d = ll.z * length(float3(vv.xy * alpha, vv.z)) +
              vv.z * length(float3(ll.xy * alpha, ll.z));
    return .5 / max(d, 1e-6);
}
float3 anisotropicHalfVector(float2 randomSample, float3 n, float3 t, float2 alpha)
{
    float phi = 6.283185307 * randomSample.y;
    float radius = sqrt(randomSample.x / max(1 - randomSample.x, 1e-7));
    return normalize(n + radius * (t * (cos(phi) * alpha.x) + cross(n, t) * (sin(phi) * alpha.y)));
}
float3 anisotropicEnvironmentDirection(float3 n, float3 v, float3 t, float roughness, float strength)
{
    if (strength <= 0)
        return reflect(-v, n);
    float3 b = cross(n, t);
    float3 normal = anisotropicTangent(b, v);
    if (dot(normal, n) < 0)
        normal = -normal;
    float bend = 1 - pow(1 - strength * (1 - roughness), 4);
    normal = normalize(lerp(n, normal, bend));
    return normalize(lerp(reflect(-v, normal), normal, roughness * roughness));
}
#endif
