# Integrator comparison

Diffuse Cornell scenes help inspect color bleeding, shadows and energy. Nova adds **Cornell Indirect**, with a baffle under the top light, **Cornell Glossy**, with a small light and rough metal sphere, and **Cornell Caustics**, with a closed floating glass sphere and a floor receiver. These are procedural test scenes, not measured Cornell replicas. See [Jensen's glass-caustic examples](https://www-graphics.stanford.edu/~henrik/images/cbox.html).

## Try it

Open the [comparison example](/examples/integrators) for equal camera/lighting and fixed-time/sample stopping. It defaults to glass caustics. Alternatively select a Cornell model in the [environment example](/examples/environment), then Ray tracing → Integrator. Use yaw 0, pitch 0, distance 3.6, disabling directional/environment/sky lighting and denoising. Recorded base/indirect/glossy screenshots used exposure 3/12/16, held constant per comparison row.

Photon mapping defaults to 8192 photon paths/iteration and radius 0.16. Camera spp share a map per iteration, so equal spp is not equal work. Finite radii bias/smooth illumination and shrink progressively.

![Rendered Cornell variants](/benchmarks/cornell-integrators.png)

## Convergence measurements

Recorded Chrome WebGPU run: 48×48, five bounces, four camera samples/iteration, no display denoising. A 32768 spp BDPT reference uses two nonoverlapping sample blocks to estimate reference noise; 16384 spp PT cross-checks it. The reference still has finite-sample error.

Top plots show indoor linear-luminance error normalized by squared reference mean. Bottom plots compare the visible floor under equal time; primary rays classify the region before measurement and exclude silhouettes. Warmed timings include submission, completion and readback; repeat on target hardware.

![Equal-spp and equal-time convergence](/benchmarks/integrator-convergence.png)

Photon maps may look smoother at low samples without winning per second. Occluded lights increase indirect PT noise; glossy caustic receivers better expose photon reuse, while metal reflection noise may favor BDPT. In this roughly one-second glossy test, normalized floor MSE was PT `0.0258`, BDPT `0.00716`, photon mapping `0.000849`; base scenes favored BDPT. These values apply only to this machine/settings.

Reproduce locally with `node scripts/webgpu/benchmark-integrators.mjs` after starting the dev server. [Raw data](/benchmarks/integrator-benchmark.json) includes actual spp, photons, time and error. See [integrator APIs](/api/render-mode).

## Glass caustics

This snapshot uses 64 spp for PT/BDPT/photons and 64 mutations/pixel for MLT, eight bounces, exposure 3, 8192 photons/iteration and radius 0.1. MLT uses 262144 bootstrap samples, large-step probability 0.3, mutation scale 0.01. Timing includes first-iteration MLT initialization; equal sample counts have different costs.

![Four integrators rendering glass caustics](/benchmarks/glass-comparison.png)

Glass is a rough dielectric without absorption, dispersion or nested media. Photon mapping has finite-radius bias. Stronger claims require higher-sample references, region errors and measured time.

## MLT validation

PSSMLT uses BDPT as its indirect estimator, with direct light separate. Analytic tests cover acceptance/rejection, brightness conservation, batch invariance and convergence; three independent glass seeds check normalization and native/web energy.

Recorded Chrome WebGPU: 32×32, eight bounces, 8192 spp BDPT reference. Region MSE fell from `1.23e-5` at 32 mpp to `9.51e-7` at 2048 mpp (about 92%); mean luminance differed by about 0.18%. Default bootstrap normalization was `0.004118 ± 0.000183` standard error, about 4.4%, so seeds and bootstrap size matter.

![MLT convergence and independent bootstrap seeds](/benchmarks/mlt-validation.png)

Convergence with more mutations does not prove MLT is faster. Full BDPT evaluation, sample-vector state and global splats add overhead and correlated noise. More mpp reduces chain noise but not fixed bootstrap normalization error; report initialization, time and error together.

Reproduce: `ctest --test-dir build -R mlt -V` and `node scripts/webgpu/check-mlt.mjs`. [MLT data](/benchmarks/mlt-validation.json) includes normalization, acceptance and errors; local logs are under `build/validation/mlt-*`.
