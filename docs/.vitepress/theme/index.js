import DefaultTheme from 'vitepress/theme'
import ModelPlayground from './ModelPlayground.vue'
import ShaderPlayground from './ShaderPlayground.vue'
export default {
  extends: DefaultTheme,
  enhanceApp({ app }) {
    app.component('ShaderPlayground', ShaderPlayground)
    app.component('ModelPlayground', ModelPlayground)
  },
}
