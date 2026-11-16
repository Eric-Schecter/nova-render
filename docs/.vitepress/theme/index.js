import ScenePlayground from './ScenePlayground.vue'
import './custom.css'
import DefaultTheme from 'vitepress/theme'
import ModelPlayground from './ModelPlayground.vue'
import ShaderPlayground from './ShaderPlayground.vue'
import ReflectionProbePlayground from './ReflectionProbePlayground.vue'
import PlanarReflectionPlayground from './PlanarReflectionPlayground.vue'
import ScreenSpaceReflectionPlayground from './ScreenSpaceReflectionPlayground.vue'
import PostProcessingPlayground from './PostProcessingPlayground.vue'
import ShadowPlayground from './ShadowPlayground.vue'
import AntiAliasingPlayground from './AntiAliasingPlayground.vue'
export default {
  extends: DefaultTheme,
  enhanceApp({ app }) {
    app.component('ScenePlayground', ScenePlayground)
    app.component('ShaderPlayground', ShaderPlayground)
    app.component('AntiAliasingPlayground', AntiAliasingPlayground)
    app.component('ReflectionProbePlayground', ReflectionProbePlayground)
    app.component('PlanarReflectionPlayground', PlanarReflectionPlayground)
    app.component('ScreenSpaceReflectionPlayground', ScreenSpaceReflectionPlayground)
    app.component('PostProcessingPlayground', PostProcessingPlayground)
    app.component('ShadowPlayground', ShadowPlayground)
    app.component('ModelPlayground', ModelPlayground)
  },
}
