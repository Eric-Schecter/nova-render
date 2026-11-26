# Post-processing

Renderer, Window, AntiAliasingView and ShadowView share the raster post-processing chain. It is off by default; example 11's `PostProcessingView` enables it for a procedural HDR room. HDR means floating-point internal color; output remains sRGB SDR and does not require an HDR monitor.

```cpp
render::PostProcessingSettings post;
post.enabled = true;
post.toneMapping = render::ToneMapping::ACES;
post.bloomIntensity = 0.08f;
window.SetPostProcessing(post);
window.DrawFrame();
```

```js
renderer.setPostProcessing({ enabled: true, toneMapping: 'aces', bloomIntensity: 0.08 })
await renderer.drawFrame()
```

Native getters return complete settings; web setters accept partial objects. Invalid settings throw and preserve state. This chain does not alter AO diagnostics, ray-tracing output or PresentImage.

## Settings

- `enabled`: default false for ordinary renderers, true in the HDR example.
- `toneMapping`: Linear / Reinhard / ACES / AgX / ACES Filmic. The default is the Narkowicz ACES fit. ACES Filmic (`aces-filmic` on web) uses the three.js RRT/ODT fit and applies display contrast and saturation after the curve. AgX uses a real-time approximation of the SDR default look, without Blender's OpenColorIO looks or display transforms.
- `exposureEV`: −8–8, default 0; each EV doubles input. With auto exposure this becomes compensation.
- `autoExposure`: default false; geometric mean luminance targets 0.18 gray with exponential adaptation using actual frame intervals.
- `minEV` / `maxEV`: −10–10, defaults −6 / 6; min must not exceed max.
- `adaptationRate`: 0.1–10, default 1.5, higher is faster.
- `bloom`: default true, inactive if the chain is disabled.
- `bloomThreshold`: 0–20 linear luminance, default 1.
- `bloomKnee`: 0–1, default 0.5.
- `bloomIntensity`: 0–2, default 0.08; zero skips bloom passes.
- `saturation`: 0–2, default 1; zero is grayscale.
- `contrast`: 0.1–2, default 1; adjusted around 0.18 before tone mapping.

RGBA16F pipeline: MSAA/SSAA resolve → HDR TAA if selected → auto exposure and five-level bloom → grading/tone mapping → FXAA/SMAA if selected → one sRGB encoding. Bloom extracts highlights and downsamples, then upsamples with tent filtering. Luminance reduction handles NPOT and tiny sizes.

Enabled model rendering skips its old material tone mapper to avoid double processing. Camera exposure remains a scene multiplier; `exposureEV` is post exposure. Disabling the chain restores the previous rendering path.

## Product showcase preset

Use the optional preset for a fixed-exposure product view with ACES Filmic, soft bloom, and 16 raster shadow samples. Set the light, environment, and exposure to suit the scene. Hybrid uses its own shadow settings.

```cpp
#include <render/ProductShowcase.hpp>

auto settings = render::MakeProductShowcaseSettings();
window.SetPostProcessing(settings.postProcessing);
window.SetShadows(settings.shadows);
window.SetAntiAliasing(settings.antiAliasing);
```

```js
import { makeProductShowcaseSettings } from '@nova/render-web'

const { postProcessing, shadows, antiAliasing } = makeProductShowcaseSettings()
renderer.setPostProcessing(postProcessing)
renderer.setShadows(shadows)
renderer.setAntiAliasing(antiAliasing)
```

On interaction state changes, apply only `ProductShowcaseAntiAliasing(interacting)` in C++ or `productShowcaseAntiAliasing(interacting)` on web. Both use FXAA during interaction and 2×2 SSAA at rest. The application supplies the interaction state and can select FXAA when the device cannot afford the extra samples.

## HDR laboratory

```cpp
#include <render/PostProcessingView.hpp>
render::PostProcessingView view;
view.SetSceneIntensity(4.0f);
window.PresentImage(view.DrawFrame(800, 485));
```

```js
const view = await Nova.createPostProcessingView({ canvas })
view.setSceneIntensity(4)
await view.drawFrame(800, 485)
```

This view shares camera, time, six AA modes and history methods. `SceneIntensity` is 0.05–20, default 1; time moves scene objects. Auto-exposure edits settle for about eight adaptation time constants. Hidden previews pause. Native results include Image readback cost in metrics. The [SSR view](./ssr) supplies additional normal/roughness/depth data for its own scene.
