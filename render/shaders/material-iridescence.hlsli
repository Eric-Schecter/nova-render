#ifndef NOVA_MATERIAL_IRIDESCENCE
#define NOVA_MATERIAL_IRIDESCENCE
float filmBaseReflectance(float ior)
{
    float amplitude = (ior - 1) / (ior + 1);
    return amplitude * amplitude;
}
float3 filmInterference(float3 front, float3 back, float3 phase)
{
    float3 crossTerm = 2 * front * back * cos(phase);
    float3 product = front * back;
    return saturate((front * front + back * back + crossTerm) /
                    max(1 + product * product + crossTerm, 1e-6));
}
float3 thinFilmReflectance(float cosine, float3 baseF0, float filmIor, float thickness)
{
    float ct1 = saturate(cosine);
    float sinSq = 1 - ct1 * ct1;
    float ct2 = sqrt(saturate(1 - sinSq / (filmIor * filmIor)));
    float3 baseAmplitude = sqrt(clamp(baseF0, 0, .9999));
    float3 substrateIor = (1 + baseAmplitude) / (1 - baseAmplitude);
    float3 ct3 = sqrt(saturate(1 - sinSq / (substrateIor * substrateIor)));

    // Signed Fresnel amplitudes retain the phase reversal at each interface.
    float amplitudeS = (ct1 - filmIor * ct2) / max(ct1 + filmIor * ct2, 1e-6);
    float amplitudeP = (filmIor * ct1 - ct2) / max(filmIor * ct1 + ct2, 1e-6);
    float3 amplitudeS2 = (filmIor * ct2 - substrateIor * ct3) / max(filmIor * ct2 + substrateIor * ct3, 1e-6);
    float3 amplitudeP2 = (substrateIor * ct2 - filmIor * ct3) / max(substrateIor * ct2 + filmIor * ct3, 1e-6);

    // Sample a small wavelength band per linear RGB channel. Averaging the
    // three samples avoids treating each channel as a monochromatic light.
    float phaseNumerator = 12.566370614 * filmIor * thickness * ct2;
    float3 wavelength = float3(610, 547, 462);
    float3 bandwidth = float3(36, 38, 29);
    float3 result = 0;
    [unroll] for (int i = -1; i <= 1; ++i)
    {
        float3 phase = phaseNumerator / (wavelength + bandwidth * i);
        result += filmInterference(amplitudeS, amplitudeS2, phase) +
                  filmInterference(amplitudeP, amplitudeP2, phase);
    }
    return result / 6;
}
float3 blendThinFilm(float3 ordinary, float cosine, float3 baseF0, float3 film)
{
    return film.x > 0 && film.z > 0
      ? lerp(ordinary, thinFilmReflectance(cosine, baseF0, film.y, film.z), film.x)
      : ordinary;
}
#endif
