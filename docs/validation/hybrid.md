# Hybrid implementation validation

## GPU presentation foundation

Native AA/deferred devices expose their owned output texture to the model presenter. Metal, Vulkan and DX12 adapters can sample that texture directly; no automatic image readback/upload is required on this path. An explicit private capture operation remains available for verification. Queue completion is still synchronized; this stage does not claim asynchronous frames in flight or shared hardware ray-query acceleration structures.

`nova_gpu_presentation_test` verifies Forward and Deferred on Apple M4 Metal and Vulkan/MoltenVK: three consecutive GPU frames perform zero readbacks, then an explicit capture matches the legacy image path (MAE < 0.05/255). The deferred and alpha material regression tests also pass (six targeted native tests in total). DX12 runtime validation remains deferred.

This is an internal foundation; the Hybrid mode and individual effect capabilities are added in subsequent implementation stages. Existing public Raster/Ray behavior remains unchanged at this stage.
