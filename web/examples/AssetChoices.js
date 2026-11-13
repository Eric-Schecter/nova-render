// Example resources. The renderer itself has no built-in asset paths.
export function modelChoices(modelURL) {
  const directory = modelURL.slice(0, modelURL.lastIndexOf('/') + 1)
  return [
    { name: 'Damaged Helmet', url: modelURL },
    { name: 'Avocado', url: directory + 'Avocado.glb' },
    { name: 'BoomBox', url: directory + 'BoomBox.glb' },
  ]
}
export function environmentChoices(environmentURL) {
  const directory = environmentURL.slice(0, environmentURL.lastIndexOf('/') + 1)
  return [
    { name: 'Venice Sunset', url: environmentURL },
    { name: 'Kloppenheim 06', url: directory + 'kloppenheim_06.nenv' },
    { name: 'Studio Small 09', url: directory + 'studio_small_09.nenv' },
  ]
}
