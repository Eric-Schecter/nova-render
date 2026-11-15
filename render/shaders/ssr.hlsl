// Shared HDR surface, planar mirror views, Hi-Z SSR and environment fallback.
cbuffer Parameters : register(b0)
{
    float4 aa[16];
}
Texture2D<float4> sceneColor : register(t1);
Texture2D<float4> surface : register(t2);
Texture2D<float4> sceneDepth : register(t3);
Texture2D<float4> environmentMap : register(t4);
Texture2D<float4> hiz1 : register(t5);
Texture2D<float4> hiz2 : register(t6);
Texture2D<float4> hiz3 : register(t7);
Texture2D<float4> hiz4 : register(t8);
Texture2D<float4> hiz5 : register(t11);
Texture2D<float4> hiz6 : register(t12);
Texture2D<float4> hiz7 : register(t13);
Texture2D<float4> hiz8 : register(t14);
Texture2D<float4> hiz9 : register(t15);
Texture2D<float4> hiz10 : register(t16);
Texture2D<float4> hiz11 : register(t17);
Texture2D<float4> hiz12 : register(t18);
SamplerState      LinearSampler : register(s9);
SamplerState      PointSampler : register(s10);
#ifndef AA_PASS
#define AA_PASS 17
#endif
static const float PI = 3.141592653589793;
float3             gammaRGB(float3 c)
{
    return pow(saturate(c), 1 / 2.2);
}
float4 rgbL(float3 c)
{
    return float4(c, dot(gammaRGB(c), float3(.299, .587, .114)));
}
void basis(float yaw, float pitch, out float3 right, out float3 up, out float3 forward)
{
    float sy = sin(yaw), cy = cos(yaw), sp = sin(pitch), cp = cos(pitch);
    right   = float3(cy, 0, -sy);
    up      = float3(-sy * sp, cp, -cy * sp);
    forward = float3(sy * cp, sp, cy * cp);
}
float3 eye()
{
    float3 r, u, f;
    basis(aa[2].x, aa[4].z, r, u, f);
    return f * aa[4].x;
}
float3 viewPosition(float3 p)
{
    float3 r, u, f;
    basis(aa[2].x, aa[4].z, r, u, f);
    return float3(dot(p, r), dot(p, u), aa[4].x - dot(p, f));
}
float3 viewDirection(float3 d)
{
    float3 r, u, f;
    basis(aa[2].x, aa[4].z, r, u, f);
    return float3(dot(d, r), dot(d, u), -dot(d, f));
}
float4 projectWorld(float3 p, float yaw, float pitch, float distance)
{
    float3 r, u, f;
    basis(yaw, pitch, r, u, f);
    float3 v = float3(dot(p, r), dot(p, u), distance - dot(p, f));
    return float4(v.x * 1.85 / (aa[0].x / aa[0].y), v.y * 1.85, v.z * .99 - .1, v.z);
}
float2 projectView(float3 p)
{
    return float2(p.x * 1.85 / (aa[0].x / aa[0].y), -p.y * 1.85) / max(p.z, .00001) * .5 + .5;
}
float3 cameraRay(float2 uv)
{
    float3 r, u, f;
    basis(aa[2].x, aa[4].z, r, u, f);
    float2 n = uv * 2 - 1;
    return normalize(r * n.x * (aa[0].x / aa[0].y) / 1.85 - u * n.y / 1.85 - f);
}
float3 worldPosition(float2 uv, float z)
{
    float3 r, u, f;
    basis(aa[2].x, aa[4].z, r, u, f);
    float2 n = uv * 2 - 1;
    return eye() + r * n.x * z * (aa[0].x / aa[0].y) / 1.85 - u * n.y * z / 1.85 - f * z;
}
float3 atlasLevel(float3 direction, uint level)
{
    float2 uv = float2(atan2(direction.z, direction.x) / (2 * PI) + .5, acos(clamp(direction.y, -1, 1)) / PI);
    float  w = max(1.0, 256.0 / exp2(float(level))), h = max(1.0, 128.0 / exp2(float(level))), y = 256 - w;
    float2 p = float2(clamp(uv.x * w, .5, w - .5), y + clamp(uv.y * h, .5, h - .5));
    return environmentMap.SampleLevel(LinearSampler, p / 256, 0).rgb;
}
float3 environment(float3 direction, float roughness)
{
    float level = saturate(roughness) * 8;
    uint  low   = uint(floor(level));
    return lerp(atlasLevel(normalize(direction), low), atlasLevel(normalize(direction), min(low + 1, 8)), frac(level))
           * aa[10].w;
}
bool planarScene()
{
    return aa[8].y < 0;
}
float3 mirrorPoint(float3 p, uint plane)
{
    if (plane == 1)
        p.y = -2 - p.y;
    if (plane == 2)
        p.z = -6.4 - p.z;
    return p;
}
#if AA_PASS == 17
struct Geometry
{
    float3 p;
    float3 normal;
    float3 color;
    float  roughness;
};
Geometry geometry(uint id, float time)
{
    Geometry g;
    g.roughness       = .2;
    const uint tri[6] = {0, 1, 3, 0, 3, 2};
    if (id < 6)
    {
        uint q      = tri[id];
        g.p         = float3((q & 1) ? 4 : -4, -1, (q & 2) ? 6 : -4);
        g.normal    = float3(0, 1, 0);
        g.color     = float3(.055, .065, .08);
        g.roughness = .035;
        return g;
    }
    id -= 6;
    if (id < 19 * 36)
    {
        uint   object = id / 36, face = (id % 36) / 6, q = tri[id % 6];
        float2 uv = float2(q & 1, (q >> 1) & 1) * 2 - 1;
        float3 center, size;
        if (object < 4)
        {
            center  = float3(-2.4 + object * 1.6, -.4 + object * .18, -2.2);
            size    = float3(.55, 1.2 + object * .36, .55);
            g.color = object == 0   ? float3(.85, .16, .06)
                      : object == 1 ? float3(.06, .55, .85)
                      : object == 2 ? float3(.75, .55, .08)
                                    : float3(.16, .7, .28);
        }
        else if (object < 16)
        {
            center  = float3(-3.3 + (object - 4) * .6, -.85, 2.4);
            size    = float3(.06, .3, .06);
            g.color = float3(.7, .7, .72);
            if (planarScene() && object < 8)
            {
                uint frame = object - 4;
                center     = frame < 2 ? float3(frame == 0 ? -3.86 : 3.86, 1.1, -3.16)
                                       : float3(0, frame == 2 ? -1.06 : 3.26, -3.16);
                size       = frame < 2 ? float3(.12, 4.44, .12) : float3(7.84, .12, .12);
                g.color    = float3(.12, .15, .19);
            }
        }
        else
        {
            float k     = object - 16;
            center      = float3(-2 + k * 2, -.6 + sin(time) * .12, -.15);
            size        = float3(.65, .8, .65);
            g.color     = k == 0 ? float3(.9, .28, .04) : k == 1 ? float3(.08, .7, .27) : float3(.14, .3, .9);
            g.roughness = .15 + k * .2;
            // Bright magenta witness behind the initial camera: appears in the mirror, not the primary view.
            if (planarScene() && object == 18)
            {
                center  = float3(2, .25, 9);
                size    = float3(1, 1.6, 1);
                g.color = float3(.9, .03, .55);
            }
        }
        float sign = (face & 1) ? 1 : -1;
        uint  axis = face / 2;
        g.normal   = axis == 0 ? float3(sign, 0, 0) : axis == 1 ? float3(0, sign, 0) : float3(0, 0, sign);
        float3 p   = axis == 0 ? float3(sign, uv) : axis == 1 ? float3(uv.x, sign, uv.y) : float3(uv, sign);
        g.p        = center + p * size * .5;
        return g;
    }
    id -= 19 * 36;
    if (planarScene() && id >= 6912)
    {
        uint q      = tri[id - 6912];
        g.p         = float3((q & 1) ? 3.8 : -3.8, (q & 2) ? 3.2 : -1, -3.2);
        g.normal    = float3(0, 0, 1);
        g.color     = float3(.01, .015, .02);
        g.roughness = aa[9].y;
        return g;
    }
    uint  object = id / 2304, k = id % 2304, cell = k / 6, q = tri[k % 6];
    float u = (float(cell % 24) + float(q & 1)) / 24 * 2 * PI, v = (float(cell / 24) + float((q >> 1) & 1)) / 16 * PI;
    g.normal    = float3(sin(v) * cos(u), cos(v), sin(v) * sin(u));
    g.p         = float3(-1.7 + object * 1.7, -.44, 1) + g.normal * .55;
    g.color     = object == 0 ? float3(.8, .14, .05) : object == 1 ? float3(.9, .62, .15) : float3(.08, .35, .85);
    g.roughness = .08 + object * .22;
    return g;
}
struct Varying
{
    float4               position : SV_Position;
    float3               world : TEXCOORD0;
    float3               normal : TEXCOORD1;
    float3               color : TEXCOORD2;
    float                roughness : TEXCOORD3;
    float4               current : TEXCOORD4;
    float4               previous : TEXCOORD5;
    nointerpolation uint sky : TEXCOORD6;
};
Varying VSMain(uint id : SV_VertexID)
{
    Varying o;
    uint    skyStart = planarScene() ? 7608 : 7602;
    o.sky            = id >= skyStart ? 1 : 0;
    if (o.sky)
    {
        float2 uv   = float2(((id - skyStart) << 1) & 2, (id - skyStart) & 2);
        o.position  = float4(uv * float2(2, -2) + float2(-1, 1), .999999, 1);
        o.world     = 0;
        o.normal    = 0;
        o.color     = 0;
        o.roughness = 1;
        o.current   = 0;
        o.previous  = 0;
        return o;
    }
    Geometry g  = geometry(id, aa[0].z);
    o.world     = g.p;
    o.normal    = g.normal;
    o.color     = g.color;
    o.roughness = g.roughness;
    o.current   = projectWorld(mirrorPoint(g.p, uint(aa[5].w)), aa[2].x, aa[4].z, aa[4].x);
    o.previous  = projectWorld(mirrorPoint(geometry(id, aa[2].w).p, uint(aa[5].w)), aa[2].y, aa[4].w, aa[4].y);
    o.position  = o.current;
    o.position.xy += aa[1].xy * 2 / aa[0].xy * float2(1, -1) * o.position.w;
    return o;
}
struct SurfaceOutput
{
    float4 color : SV_Target0;
    float4 motion : SV_Target1;
    float4 normalRoughness : SV_Target2;
    float4 depth : SV_Target3;
};
SurfaceOutput PSMain(Varying i)
{
    SurfaceOutput o;
    if (i.sky)
    {
        float2 uv = (i.position.xy / (aa[0].xy * (aa[0].w == 4 ? 2 : 1))) - aa[1].xy / aa[0].xy;
        float3 d  = cameraRay(uv), r, u, f;
        if (aa[5].w == 1)
            d.y = -d.y;
        if (aa[5].w == 2)
            d.z = -d.z;
        basis(aa[2].y, aa[4].w, r, u, f);
        float3 motionDirection = cameraRay(uv);
        float3 old             = float3(dot(motionDirection, r), dot(motionDirection, u), -dot(motionDirection, f));
        float2 prev            = projectView(old);
        o.color                = rgbL(environment(d, 0) * aa[2].z);
        o.motion               = float4(prev - uv, 1, 1);
        o.normalRoughness      = float4(0, 0, 0, 1);
        o.depth                = float4(1000, 1000, 0, 1);
        return o;
    }
    if (planarScene() && aa[5].w > .5)
    {
        // Clip the original scene to the camera-facing half-space; omit receivers from reflection views.
        if ((i.world.y < -.999 && i.normal.y > .99) || (i.world.z < -3.199 && i.normal.z > .99))
            discard;
        if (aa[5].w == 1 && (i.world.y + 1) * (eye().y + 1) < -.001)
            discard;
        if (aa[5].w == 2 && (i.world.z + 3.2) * (eye().z + 3.2) < -.001)
            discard;
    }
    float3 n = normalize(i.normal), v = normalize(mirrorPoint(eye(), uint(aa[5].w)) - i.world),
           l = normalize(float3(-.5, 1.2, 1.3)), h = normalize(l + v);
    float rough = i.roughness;
    if (n.y > .99 && i.world.y < -.98)
        rough = planarScene() ? aa[9].x : lerp(.035, .6, saturate((i.world.x + 4) / 8));
    float  power = lerp(160, 6, rough);
    float3 c     = i.color * (.08 + 1.4 * saturate(dot(n, l))) + pow(saturate(dot(n, h)), power) * .4
               + environment(n, .9) * i.color * .12;
    o.color           = rgbL(c * aa[2].z);
    float2 uv         = i.current.xy / i.current.w * float2(.5, -.5) + .5,
           old        = i.previous.xy / i.previous.w * float2(.5, -.5) + .5;
    o.motion          = float4(old - uv, i.current.w / 32, .125);
    o.normalRoughness = float4(n, rough);
    o.depth           = float4(i.current.w, i.current.w, 0, 1);
    return o;
}
#else
struct Varying
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
Varying VSMain(uint id : SV_VertexID)
{
    Varying o;
    o.uv       = float2((id << 1) & 2, id & 2);
    o.position = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float2 rangeAt(int2 pixel, uint level)
{
    int2 p = pixel >> level;
    if (level == 0)
        return sceneDepth.Load(int3(p, 0)).rr;
    if (level == 1)
        return hiz1.Load(int3(p, 0)).rg;
    if (level == 2)
        return hiz2.Load(int3(p, 0)).rg;
    if (level == 3)
        return hiz3.Load(int3(p, 0)).rg;
    if (level == 4)
        return hiz4.Load(int3(p, 0)).rg;
    if (level == 5)
        return hiz5.Load(int3(p, 0)).rg;
    if (level == 6)
        return hiz6.Load(int3(p, 0)).rg;
    if (level == 7)
        return hiz7.Load(int3(p, 0)).rg;
    if (level == 8)
        return hiz8.Load(int3(p, 0)).rg;
    if (level == 9)
        return hiz9.Load(int3(p, 0)).rg;
    if (level == 10)
        return hiz10.Load(int3(p, 0)).rg;
    if (level == 11)
        return hiz11.Load(int3(p, 0)).rg;
    return hiz12.Load(int3(p, 0)).rg;
}
float rayDepth(float s, float z0, float z1)
{
    return rcp(lerp(rcp(z0), rcp(z1), s));
}
float3 filteredHit(float2 uv, float rough, float travelled, float thickness, float2 size)
{
    float  radius = rough * rough * min(24.0, travelled * 4);
    float3 sum    = 0;
    float  count = 0, depth = sceneDepth.SampleLevel(PointSampler, uv, 0).r;
    [unroll] for (int y = -1; y <= 1; y++)[unroll] for (int x = -1; x <= 1; x++)
    {
        float2 q = uv + float2(x, y) * radius / size;
        float  z = sceneDepth.SampleLevel(PointSampler, q, 0).r;
        if (all(q >= 0) && all(q <= 1) && abs(z - depth) < max(thickness * 2, rough * .5))
        {
            float weight = (x == 0 ? 2 : 1) * (y == 0 ? 2 : 1);
            sum += sceneColor.SampleLevel(LinearSampler, q, 0).rgb * weight;
            count += weight;
        }
    }
    return sum / max(count, 1);
}
float4 PSMain(Varying i)
  : SV_Target
{
#if AA_PASS == 18
    uint w, h;
    sceneColor.GetDimensions(w, h);
    int2  base = int2(i.position.xy) * 2;
    float lo = 1000, hi = 0;
    [unroll] for (int y = 0; y < 2; y++)[unroll] for (int x = 0; x < 2; x++)
    {
        int2 p = base + int2(x, y);
        if (p.x < int(w) && p.y < int(h))
        {
            float2 d = sceneColor.Load(int3(p, 0)).rg;
            lo       = min(lo, d.x);
            hi       = max(hi, aa[5].x > .5 ? d.x : d.y);
        }
    }
    return float4(lo, hi, 0, 1);
#elif AA_PASS == 19
    uint width, height;
    sceneDepth.GetDimensions(width, height);
    float2 size = float2(width, height);
    float  z    = sceneDepth.SampleLevel(PointSampler, i.uv, 0).r;
    float4 nr   = surface.SampleLevel(PointSampler, i.uv, 0);
    if (z > 64 || z < .05 || nr.w >= aa[9].y)
        return 0;
    float2 unjittered = i.uv - aa[1].xy / aa[0].xy;
    float3 p = worldPosition(unjittered, z), n = normalize(nr.xyz),
           direction = normalize(reflect(normalize(p - eye()), n));
    float3 origin = p + n * max(.015, aa[8].w * .2), start = viewPosition(origin), ray = viewDirection(direction);
    float  distance = aa[8].z;
    if (ray.z < -.00001)
        distance = min(distance, (.06 - start.z) / ray.z);
    if (distance < .05)
        return 0;
    float3 end = start + ray * distance;
    float2 uv0 = projectView(start) + aa[1].xy / aa[0].xy, delta = projectView(end) + aa[1].xy / aa[0].xy - uv0;
    float  maxS = 1;
    if (delta.x > 0)
        maxS = min(maxS, (1 - uv0.x) / delta.x);
    if (delta.x < 0)
        maxS = min(maxS, -uv0.x / delta.x);
    if (delta.y > 0)
        maxS = min(maxS, (1 - uv0.y) / delta.y);
    if (delta.y < 0)
        maxS = min(maxS, -uv0.y / delta.y);
    float pixels = max(abs(delta.x * width), abs(delta.y * height));
    if (pixels < 1 || maxS <= 0)
        return 0;
    float epsilon = .03 / pixels, s = 0;
    uint  level = aa[9].w > .5 ? min(uint(aa[6].y), 3) : 0, maxLevel = uint(aa[6].y);
    [loop] for (uint iteration = 0; iteration < uint(aa[8].y); iteration++)
    {
        if (s >= maxS)
            break;
        float2 uv = uv0 + delta * s;
        if (any(uv < 0) || any(uv >= 1))
            break;
        float nextS;
        if (aa[9].w > .5)
        {
            float  cellSize = exp2(float(level));
            float2 at       = (uv + sign(delta) * .002 / size) * size;
            float2 cell = floor(at / cellSize), boundary = (cell + step(0, delta)) * cellSize / size;
            float2 crossS = float2(
              abs(delta.x) > .0000001 ? (boundary.x - uv0.x) / delta.x : 1e9,
              abs(delta.y) > .0000001 ? (boundary.y - uv0.y) / delta.y : 1e9);
            nextS = min(maxS, max(s + epsilon, min(crossS.x, crossS.y)));
        }
        else
            nextS = min(maxS, s + aa[9].x / pixels);
        float  za = rayDepth(s, start.z, end.z), zb = rayDepth(nextS, start.z, end.z);
        int2   pixel   = clamp(int2(uv * size), int2(0, 0), int2(width - 1, height - 1));
        float2 bounds  = rangeAt(pixel, level);
        bool   overlap = max(za, zb) >= bounds.x - aa[8].w && min(za, zb) <= bounds.y + aa[8].w;
        if (overlap && level > 0)
        {
            level--;
            continue;
        }
        if (overlap && bounds.x < 64 && za <= bounds.x + aa[8].w)
        {
            float low = s, high = nextS;
            [unroll] for (uint refine = 0; refine < 6; refine++)
            {
                float mid   = (low + high) * .5;
                float depth = sceneDepth.SampleLevel(PointSampler, uv0 + delta * mid, 0).r;
                if (rayDepth(mid, start.z, end.z) > depth)
                    high = mid;
                else
                    low = mid;
            }
            float  hitS = (low + high) * .5;
            float2 hit  = uv0 + delta * hitS;
            float  hitZ = rayDepth(hitS, start.z, end.z), surfaceZ = sceneDepth.SampleLevel(PointSampler, hit, 0).r;
            float3 hitNormal = surface.SampleLevel(PointSampler, hit, 0).xyz;
            float  travel =
              abs(end.z - start.z) > .00001 ? (hitZ - start.z) / (end.z - start.z) * distance : hitS * distance;
            if (travel > max(.1, aa[8].w * 1.5) && abs(hitZ - surfaceZ) <= aa[8].w && dot(hitNormal, direction) < -.02)
            {
                float border     = min(min(hit.x, 1 - hit.x), min(hit.y, 1 - hit.y));
                float confidence = saturate(border / aa[9].z) * saturate((aa[9].y - nr.w) * 8)
                                   * (1 - smoothstep(distance * .8, distance, travel));
                return float4(filteredHit(hit, nr.w, travel, aa[8].w, size), confidence);
            }
        }
        s = nextS + epsilon;
        if (aa[9].w > .5)
            level = min(level + 1, maxLevel);
    }
    return 0;
#else
    float  z  = sceneDepth.SampleLevel(PointSampler, i.uv, 0).r;
    float4 nr = surface.SampleLevel(PointSampler, i.uv, 0);
    if (planarScene() && aa[5].y < 0)
    {
        float4 motion   = hiz3.SampleLevel(PointSampler, i.uv, 0);
        float3 position = worldPosition(i.uv - aa[1].xy / aa[0].xy, z);
        if (z < 64 && abs(nr.y) > .99 && abs(position.y + 1) < .02 && aa[8].z > .5 && aa[9].z > .01)
            motion.xy = hiz4.SampleLevel(LinearSampler, i.uv, 0).xy;
        if (z < 64 && abs(nr.z) > .99 && abs(position.z + 3.2) < .02 && aa[8].w > .5 && aa[9].w > .01)
            motion.xy = hiz5.SampleLevel(LinearSampler, i.uv, 0).xy;
        return motion;
    }
    uint debug = uint(aa[10].y);
    if (debug == 3)
        return rgbL(z > 64 ? 0 : nr.xyz * .5 + .5);
    if (debug == 4)
        return rgbL(z > 64 ? 0 : nr.www);
    if (debug == 5)
        return rgbL(z > 64 ? 0 : (z / 32).xxx);
    if (debug == 6)
    {
        float2 depth = aa[5].z < .5 ? float2(z, z) : hiz1.SampleLevel(PointSampler, i.uv, 0).rg;
        return rgbL(z > 64 && depth.x > 64 ? 0 : (depth.x / 32).xxx);
    }
    float3 base = sceneColor.SampleLevel(PointSampler, i.uv, 0).rgb;
    if (z > 64)
        return debug == 2 ? 0 : rgbL(base);
    float2 uv = i.uv - aa[1].xy / aa[0].xy;
    float3 p = worldPosition(uv, z), n = normalize(nr.xyz), v = normalize(eye() - p), direction = reflect(-v, n);
    float3 fallback = environment(direction, nr.w) * aa[2].z;
    if (planarScene())
    {
        bool   floor      = abs(n.y) > .99 && abs(p.y + 1) < .02;
        bool   mirror     = abs(n.z) > .99 && abs(p.z + 3.2) < .02;
        bool   enabled    = floor ? aa[8].z > .5 : mirror && aa[8].w > .5;
        float  intensity  = floor ? aa[9].z : aa[9].w;
        float3 reflection = fallback;
        if (enabled)
        {
            uint w, h;
            sceneColor.GetDimensions(w, h);
            float2 radius = nr.w * nr.w * 24 / float2(w, h);
            float3 sum    = 0;
            float  weight = 0;
            for (int y = -2; y <= 2; y++)
                for (int x = -2; x <= 2; x++)
                {
                    float  k = exp(-.5 * (x * x + y * y));
                    float2 q = i.uv + float2(x, y) * radius;
                    sum +=
                      (floor ? hiz1.SampleLevel(LinearSampler, q, 0).rgb : hiz2.SampleLevel(LinearSampler, q, 0).rgb)
                      * k;
                    weight += k;
                }
            reflection = sum / weight;
        }
        if (!floor && !mirror)
            return rgbL(base + fallback * .06);
        float f0      = mirror ? .92 : .65;
        float fresnel = f0 + (1 - f0) * pow(1 - saturate(abs(dot(n, v))), 5);
        return rgbL(base + reflection * fresnel * intensity * (1 - nr.w * .35));
    }
    float4 traced = aa[8].x > .5 ? hiz1.SampleLevel(PointSampler, i.uv, 0) : 0;
    if (debug == 2)
        return float4(traced.aaa, 1);
    float3 reflection = lerp(fallback, traced.rgb, traced.a);
    float  f0         = n.y > .99 && p.y < -.98 ? .45 : .12;
    float3 fresnel    = f0 + (1 - f0) * pow(1 - saturate(dot(n, v)), 5);
    float3 result     = debug == 1 ? reflection : base + reflection * fresnel * aa[10].x * (1 - nr.w * .65);
    return aa[0].w == 5 ? float4(result, traced.a) : rgbL(result);
#endif
}
#endif
