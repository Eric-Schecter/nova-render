// Footprint-aware filtering for a procedural checker. Avoids undersampling before AA.
float checkerIntegral(float x)
{
    return 1 - abs(frac(x * .5) * 2 - 1);
}
float filteredChecker(float2 uv, float2 footprint)
{
    footprint      = max(footprint, .00001);
    float2 average = float2(
                       checkerIntegral(uv.x + footprint.x * .5) - checkerIntegral(uv.x - footprint.x * .5),
                       checkerIntegral(uv.y + footprint.y * .5) - checkerIntegral(uv.y - footprint.y * .5))
                     / footprint;
    return saturate(.5 - .5 * average.x * average.y);
}

float filteredChecker(float2 uv)
{
    return filteredChecker(uv, fwidth(uv));
}
