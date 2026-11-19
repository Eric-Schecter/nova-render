import { defineConfig } from 'vitepress'
import shaderCompiler from '../../scripts/webgpu/plugin.mjs'

const base = process.env.NOVA_DOCS_BASE || '/'

export default defineConfig({
  base,
  vite: {
    plugins: [shaderCompiler()],
    server: { port: Number(process.env.NOVA_DOCS_PORT || 5173), strictPort: true },
  },
  lang: 'en-US',
  title: 'Nova Render',
  description:
    'Cross-platform rendering APIs: Vulkan, DirectX 12, Metal and WebGPU with shared HLSL.',
  cleanUrls: false,
  lastUpdated: false,
  themeConfig: {
    sidebar: [
      {
        text: 'Guide',
        items: [
          { text: 'Quick start', link: '/guide/quick-start' },
          { text: 'Writing examples', link: '/guide/integration' },
          { text: 'Package integration', link: '/guide/package' },
          { text: 'Integrator comparison', link: '/guide/integrator-comparison' },
        ],
      },
      {
        text: 'Examples',
        items: [
          { text: '01 · Triangle', link: '/examples/triangle' },
          { text: '02 · Model loading', link: '/examples/damaged-helmet' },
          { text: '03 · Lighting', link: '/examples/lighting' },
          { text: '04 · Skybox and environment', link: '/examples/environment' },
          { text: '05 · Skeletal animation', link: '/examples/skeletal-animation' },
          { text: '06 · Integrator comparison', link: '/examples/integrators' },
          { text: '07 · Weekend spheres', link: '/examples/weekend-spheres' },
          { text: '08 · Ambient occlusion', link: '/examples/ambient-occlusion' },
          { text: '09 · Anti-aliasing', link: '/examples/antialiasing' },
          { text: '10 · Shadows', link: '/examples/shadows' },
          { text: '11 · HDR and post-processing', link: '/examples/postprocessing' },
          { text: '12 · Screen-space reflections', link: '/examples/ssr' },
          { text: '13 · Planar reflections', link: '/examples/planar' },
          { text: '14 · Reflection probes', link: '/examples/probe' },
          { text: '15 · Scene instances', link: '/examples/scene' },
          { text: '16 · Raster pipelines', link: '/examples/pipelines' },
          { text: '17 · Deferred models', link: '/examples/deferred' },
          { text: '18 · Alpha materials', link: '/examples/material-alpha' },
          { text: '19 · Hybrid rendering', link: '/examples/hybrid' },
          { text: '20 · CarConcept showroom', link: '/examples/car-showroom' },
        ],
      },
      {
        text: 'API reference',
        items: [
          { text: 'Overview', link: '/api/' },
          { text: 'Window', link: '/api/window' },
          { text: 'Nova Render Web', link: '/api/web' },
          { text: 'Models and cameras', link: '/api/model' },
          { text: 'Skeletal animation', link: '/api/animation' },
          { text: 'Skybox and environment', link: '/api/environment' },
          { text: 'Render modes', link: '/api/render-mode' },
          { text: 'Ambient occlusion', link: '/api/ambient-occlusion' },
          { text: 'Anti-aliasing', link: '/api/antialiasing' },
          { text: 'Shadows', link: '/api/shadows' },
          { text: 'Post-processing', link: '/api/postprocessing' },
          { text: 'Screen-space reflections', link: '/api/ssr' },
          { text: 'Planar reflections', link: '/api/planar' },
          { text: 'Reflection probes', link: '/api/probe' },
          { text: 'Raster pipeline laboratory', link: '/api/pipelines' },
          { text: 'Model raster pipelines', link: '/api/raster' },
          { text: 'Hybrid rendering', link: '/api/hybrid' },
          { text: 'Parameter panels', link: '/api/ui' },
          { text: 'Types and configuration', link: '/api/types' },
          { text: 'Backend selection', link: '/api/selection' },
          { text: 'Renderer', link: '/api/renderer' },
          { text: 'Scene instances', link: '/api/scene' },
          { text: 'Scene picking', link: '/api/picking' },
          { text: 'Atmosphere and integration', link: '/api/atmosphere' },
          { text: 'Materials', link: '/api/materials' },
          { text: 'Camera and viewport', link: '/api/camera' },
        ],
      },
    ],
    search: {
      provider: 'local',
      options: {
        locales: {
          root: {
            translations: {
              button: {
                buttonText: 'Search documentation',
                buttonAriaLabel: 'Search documentation',
              },
              modal: {
                noResultsText: 'No results found',
                resetButtonTitle: 'Clear',
                footer: { selectText: 'Select', navigateText: 'Navigate', closeText: 'Close' },
              },
            },
          },
        },
      },
    },
    aside: false,
    outline: false,
    docFooter: { prev: 'Previous', next: 'Next' },
    darkModeSwitchLabel: 'Toggle theme',
    sidebarMenuLabel: 'Contents',
    returnToTopLabel: 'Return to top',
    footer: {
      copyright: `<a href="${base}credits.html">Credits</a>`,
    },
  },
})
