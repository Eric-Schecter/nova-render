# CarConcept showroom

The showroom uses a neutral studio HDRI with two rectangular softboxes, a low front-side fill, layered Carmine paint and baked tire contact shadows. Drag to orbit or scroll to zoom. Run the native example from the repository root with `./build/nova_showroom --api metal` or `--api vulkan`.

Hybrid lighting includes one-bounce diffuse global illumination. While orbiting or zooming, the showroom uses two samples for the top softbox shadow. It restores four after the camera settles.

<ClientOnly><ModelPlayground showroom /></ClientOnly>
