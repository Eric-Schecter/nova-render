// Static procedural room shared by the visible mesh and the HDR cubemap capture.
bool probeScene()
{
    return aa[8].y < -1.5;
}
float3 probeRoomColor(float3 p, uint wall)
{
    float3 color = wall == 2 ? float3(.65, .055, .035) : wall == 3 ? float3(.035, .18, .65) : float3(.5, .52, .55);
    float2 uv    = wall == 0 || wall == 4 ? p.xz : wall == 1 ? p.xy : p.zy;
    float2 grid  = abs(frac(uv) - .5);
    if (max(grid.x, grid.y) > .475)
        color *= .3;
    if (wall == 1 && p.y > .35 && p.y < 1.65 && abs(p.x) < 2.9)
    {
        uint stripe = uint(floor(p.x + 3));
        color = stripe % 3 == 0 ? float3(.9, .55, .035) : stripe % 3 == 1 ? float3(.05, .7, .2) : float3(.65, .05, .7);
    }
    return color;
}
void probeProp(uint id, out float3 center, out float3 size, out float3 color)
{
    center = float3(-2.6 + id * 2.6, -.15 + id * .22, -2.8);
    size   = float3(.7, 1.7 + id * .44, .7);
    color  = id == 0 ? float3(.95, .25, .025) : id == 1 ? float3(.08, .7, .23) : float3(.15, .3, .9);
}
float3 probeShade(float3 p, float3 n, float3 color)
{
    if (abs(p.z + 4) < .005 && abs(p.x) < .9 && p.y > 2.1 && p.y < 2.5)
        return float3(5, 4.4, 3.5);
    return color * (.2 + 1.25 * saturate(dot(n, normalize(float3(-.5, 1.2, 1.3)))));
}
float3 probeCapture(float3 direction)
{
    float3 origin    = aa[11].xyz;
    float  nearest   = 1000;
    float3 hitNormal = 0, hitColor = 0, hitPosition = 0;
    for (uint wall = 0; wall < 5; wall++)
    {
        uint  axis  = wall == 0 || wall == 4 ? 1 : wall == 1 ? 2 : 0;
        float plane = wall == 0 ? -1 : wall == 4 ? 3 : wall == 1 || wall == 2 ? -4 : 4;
        if (abs(direction[axis]) < .00001)
            continue;
        float  t = (plane - origin[axis]) / direction[axis];
        float3 p = origin + direction * t;
        if (
          t > .001 && t < nearest && p.x >= -4.001 && p.x <= 4.001 && p.y >= -1.001 && p.y <= 3.001 && p.z >= -4.001
          && p.z <= 4.001)
        {
            nearest         = t;
            hitNormal       = 0;
            hitNormal[axis] = wall == 0 || wall == 1 || wall == 2 ? 1 : -1;
            hitPosition     = p;
            hitColor        = probeRoomColor(p, wall);
        }
    }
    float3 safe = float3(
      direction.x >= 0 ? max(direction.x, .00001) : min(direction.x, -.00001),
      direction.y >= 0 ? max(direction.y, .00001) : min(direction.y, -.00001),
      direction.z >= 0 ? max(direction.z, .00001) : min(direction.z, -.00001));
    for (uint id = 0; id < 3; id++)
    {
        float3 center, size, color;
        probeProp(id, center, size, color);
        float3 a = (center - size * .5 - origin) / safe, b = (center + size * .5 - origin) / safe;
        float3 low = min(a, b), high = max(a, b);
        float  t = max(max(low.x, low.y), low.z), farT = min(min(high.x, high.y), high.z);
        if (t > .001 && farT >= t && t < nearest)
        {
            nearest      = t;
            hitPosition  = origin + direction * t;
            hitColor     = color;
            float3 delta = (hitPosition - center) / (size * .5), ab = abs(delta);
            hitNormal = ab.x >= ab.y && ab.x >= ab.z ? float3(delta.x > 0 ? 1 : -1, 0, 0)
                        : ab.y >= ab.z               ? float3(0, delta.y > 0 ? 1 : -1, 0)
                                                     : float3(0, 0, delta.z > 0 ? 1 : -1);
        }
    }
    return nearest < 999 ? probeShade(hitPosition, hitNormal, hitColor) : environment(direction, 0);
}
float3 probeFaceDirection(uint face, float2 uv)
{
    float2 p = uv * 2 - 1;
    return normalize(
      face == 0   ? float3(1, -p.y, -p.x)
      : face == 1 ? float3(-1, -p.y, p.x)
      : face == 2 ? float3(p.x, 1, p.y)
      : face == 3 ? float3(p.x, -1, -p.y)
      : face == 4 ? float3(p.x, -p.y, 1)
                  : float3(-p.x, -p.y, -1));
}
float3 probeSampleLevel(float3 d, uint level)
{
    float3 a = abs(d);
    uint   face;
    float2 uv;
    if (a.x >= a.y && a.x >= a.z)
    {
        face = d.x >= 0 ? 0 : 1;
        uv   = float2(d.x >= 0 ? -d.z : d.z, -d.y) / a.x;
    }
    else if (a.y >= a.z)
    {
        face = d.y >= 0 ? 2 : 3;
        uv   = float2(d.x, d.y >= 0 ? d.z : -d.z) / a.y;
    }
    else
    {
        face = d.z >= 0 ? 4 : 5;
        uv   = float2(d.z >= 0 ? d.x : -d.x, -d.y) / a.z;
    }
    float  size = float(128 >> level), offset = 512 - 4 * size;
    float2 pixel = float2((face % 3) * size, offset + (face / 3) * size) + clamp((uv * .5 + .5) * size, .5, size - .5);
    return hiz3.SampleLevel(LinearSampler, pixel / float2(384, 512), 0).rgb;
}
float3 probeSample(float3 direction, float roughness)
{
    float mip = roughness * 7;
    uint  low = uint(floor(mip));
    return lerp(probeSampleLevel(direction, low), probeSampleLevel(direction, min(low + 1, 7)), frac(mip));
}
float4 probeAtlas(float2 pixel)
{
    uint  level = 0;
    float size = 128, offset = 0;
    for (uint k = 1; k < 8; k++)
    {
        float s = float(128 >> k), o = 512 - 4 * s;
        if (pixel.y >= o)
        {
            level  = k;
            size   = s;
            offset = o;
        }
    }
    if (pixel.x >= size * 3 || pixel.y >= offset + size * 2)
        return 0;
    uint   face = uint(pixel.x / size) + uint((pixel.y - offset) / size) * 3;
    float2 uv   = frac(float2(pixel.x, pixel.y - offset) / size);
    float3 n    = probeFaceDirection(face, uv);
    if (level == 0)
        return float4(probeCapture(n), 1);
    float  rough = float(level) / 7, alpha = rough * rough;
    float3 up = abs(n.z) < .999 ? float3(0, 0, 1) : float3(1, 0, 0), t = normalize(cross(up, n)), b = cross(n, t);
    float3 sum    = 0;
    float  weight = 0;
    for (uint j = 0; j < 64; j++)
    {
        uint bits = j;
        bits      = (bits << 16) | (bits >> 16);
        bits      = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1);
        bits      = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2);
        bits      = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4);
        bits      = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8);
        float xi = float(bits) * 2.3283064365386963e-10, phi = 2 * PI * (float(j) + .5) / 64;
        float cosTheta = sqrt((1 - xi) / (1 + (alpha * alpha - 1) * xi)),
              sinTheta = sqrt(max(0, 1 - cosTheta * cosTheta));
        float3 h       = t * cos(phi) * sinTheta + b * sin(phi) * sinTheta + n * cosTheta;
        float3 l       = normalize(2 * dot(n, h) * h - n);
        float  w       = saturate(dot(n, l));
        if (w > 0)
        {
            sum += probeCapture(l) * w;
            weight += w;
        }
    }
    return float4(sum / max(weight, .0001), 1);
}
