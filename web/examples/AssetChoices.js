// Example resources. The renderer itself has no built-in asset paths.
export function modelChoices(modelURL) {
  const directory = modelURL.slice(0, modelURL.lastIndexOf('/') + 1)
  const choices = [
    { name: 'Damaged Helmet', url: directory + 'DamagedHelmet.glb' },
    { name: 'Avocado', url: directory + 'Avocado.glb' },
    { name: 'BoomBox', url: directory + 'BoomBox.glb' },
    { name: 'Cornell Box', url: directory + 'CornellBox.glb' },
    { name: 'Cornell Indirect', url: directory + 'CornellIndirect.glb' },
    { name: 'Cornell Glossy', url: directory + 'CornellGlossy.glb' },
    { name: 'Cornell Caustics', url: directory + 'CornellCaustics.glb' },
  ]
  if (!choices.some((choice) => choice.url === modelURL))
    choices.unshift({ name: 'Custom', url: modelURL })
  return choices
}
export function environmentChoices(environmentURL) {
  const directory = environmentURL.slice(0, environmentURL.lastIndexOf('/') + 1)
  return [
    { name: 'Venice Sunset', url: environmentURL },
    { name: 'Kloppenheim 06', url: directory + 'kloppenheim_06.bin' },
    { name: 'Studio Small 09', url: directory + 'studio_small_09.bin' },
  ]
}
