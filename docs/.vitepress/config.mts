import { defineConfig } from 'vitepress'
import shaderCompiler from '../../scripts/webgpu/plugin.mjs'

export default defineConfig({
  vite: { plugins: [shaderCompiler()], server: { port: 5173, strictPort: true } },
  lang: 'zh-CN',
  title: 'Nova Render',
  description: 'Nova Render 跨平台渲染库接口文档：Vulkan、D3D12、Metal 与统一 HLSL。',
  cleanUrls: true,
  lastUpdated: false,
  themeConfig: {
    sidebar: [
      {
        text: '使用指南',
        items: [
          { text: '快速开始', link: '/guide/quick-start' },
          { text: '编写示例', link: '/guide/integration' },
          { text: '作为依赖使用', link: '/guide/package' },
          { text: '积分器对比', link: '/guide/integrator-comparison' },
        ],
      },
      {
        text: '例子',
        items: [
          { text: '01 · 三角形', link: '/examples/triangle' },
          { text: '02 · 模型加载', link: '/examples/damaged-helmet' },
          { text: '03 · 光照', link: '/examples/lighting' },
          { text: '04 · 天空盒与环境光', link: '/examples/environment' },
          { text: '05 · 骨骼动画', link: '/examples/skeletal-animation' },
          { text: '06 · 积分器对比', link: '/examples/integrators' },
          { text: '07 · 多球场景', link: '/examples/weekend-spheres' },
          { text: '08 · 环境遮蔽 AO', link: '/examples/ambient-occlusion' },
          { text: '09 · 抗锯齿', link: '/examples/antialiasing' },
          { text: '10 · 阴影', link: '/examples/shadows' },
          { text: '11 · HDR 与后处理', link: '/examples/postprocessing' },
          { text: '12 · 屏幕空间反射 SSR', link: '/examples/ssr' },
          { text: '13 · 平面反射', link: '/examples/planar' },
          { text: '14 · 反射探针', link: '/examples/probe' },
        ],
      },
      {
        text: 'API 参考',
        items: [
          { text: '总览', link: '/api/' },
          { text: 'Window', link: '/api/window' },
          { text: 'Nova Render Web', link: '/api/web' },
          { text: '模型与相机', link: '/api/model' },
          { text: '骨骼动画', link: '/api/animation' },
          { text: '天空盒与环境光', link: '/api/environment' },
          { text: '渲染模式', link: '/api/render-mode' },
          { text: '环境遮蔽 AO', link: '/api/ambient-occlusion' },
          { text: '抗锯齿', link: '/api/antialiasing' },
          { text: '阴影', link: '/api/shadows' },
          { text: '后处理', link: '/api/postprocessing' },
          { text: '屏幕空间反射 SSR', link: '/api/ssr' },
          { text: '平面反射', link: '/api/planar' },
          { text: '反射探针', link: '/api/probe' },
          { text: '参数面板', link: '/api/ui' },
          { text: '类型与配置', link: '/api/types' },
          { text: '后端查询与选择', link: '/api/selection' },
          { text: 'Renderer', link: '/api/renderer' },
        ],
      },
    ],
    search: {
      provider: 'local',
      options: {
        locales: {
          root: {
            translations: {
              button: { buttonText: '搜索文档', buttonAriaLabel: '搜索文档' },
              modal: {
                noResultsText: '没有找到结果',
                resetButtonTitle: '清空',
                footer: { selectText: '选择', navigateText: '切换', closeText: '关闭' },
              },
            },
          },
        },
      },
    },
    aside: false,
    outline: false,
    docFooter: { prev: '上一页', next: '下一页' },
    darkModeSwitchLabel: '切换主题',
    sidebarMenuLabel: '目录',
    returnToTopLabel: '返回顶部',
    footer: {
      copyright: '<a href="/credits">资源声明</a>',
    },
  },
})
