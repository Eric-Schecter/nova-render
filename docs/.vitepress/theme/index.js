import './custom.css'
import DefaultTheme from 'vitepress/theme'
import ModelPlayground from './ModelPlayground.vue'
import ShaderPlayground from './ShaderPlayground.vue'
import AntiAliasingPlayground from './AntiAliasingPlayground.vue'
export default {
  extends: DefaultTheme,
  enhanceApp({ app }) {
    app.component('ShaderPlayground', ShaderPlayground)
    app.component('AntiAliasingPlayground', AntiAliasingPlayground)
    app.component('ModelPlayground', ModelPlayground)
  },
}
