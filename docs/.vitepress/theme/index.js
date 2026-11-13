import DefaultTheme from 'vitepress/theme'
import ShaderPlayground from './ShaderPlayground.vue'
export default { extends: DefaultTheme, enhanceApp({app}) {app.component('ShaderPlayground',ShaderPlayground)} }
