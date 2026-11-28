export async function applyLunariaToon(renderer, assetId, toonMix) {
  const materials = renderer.getMaterials(assetId)
  for (const [slot, material] of materials.entries()) {
    const lighting =
      material.name === 'Lunaria_Skin_Anime' ? 'ToonSkin'
      : material.name === 'Lunaria_Outfit_PBR' ? 'ToonSurface'
      : undefined
    if (!lighting) continue
    await renderer.setMaterial(slot, {
      ...material, lighting, toon: { toonMix, skinSSS: 0.85, rimStrength: 0.65 },
    }, assetId)
  }
}
