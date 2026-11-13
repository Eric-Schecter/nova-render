import { defineConfig } from 'vitepress'

export default defineConfig({
  lang: 'zh-CN',
  title: 'Nova Render',
  description: 'C++17 最小渲染模块接口文档：Vulkan、D3D12、Metal 与统一 HLSL。',
  cleanUrls: true,
  lastUpdated: true,
  themeConfig: {
    nav: [
      { text: '指南', link: '/guide/quick-start' },
      { text: 'API', link: '/api/' },
      { text: '验证状态', link: '/guide/validation' },
    ],
    sidebar: [
      { text: '使用指南', items: [
        { text: '快速开始', link: '/guide/quick-start' },
        { text: '接入 Renderer', link: '/guide/integration' },
        { text: '后端与校验', link: '/guide/backends' },
        { text: 'HLSL 与离线构建', link: '/guide/shaders' },
        { text: '示例与验证状态', link: '/guide/validation' },
      ] },
      { text: 'API 参考', items: [
        { text: '总览', link: '/api/' },
        { text: '类型与配置', link: '/api/types' },
        { text: '后端查询与选择', link: '/api/selection' },
        { text: 'Renderer', link: '/api/renderer' },
      ] },
    ],
    search: { provider: 'local', options: { locales: { root: {
      translations: {
        button: { buttonText: '搜索文档', buttonAriaLabel: '搜索文档' },
        modal: { noResultsText: '没有找到结果', resetButtonTitle: '清空',
          footer: { selectText: '选择', navigateText: '切换', closeText: '关闭' } },
      },
    } } } },
    outline: { level: [2, 3], label: '本页内容' },
    docFooter: { prev: '上一页', next: '下一页' },
    lastUpdated: { text: '最后更新' },
    darkModeSwitchLabel: '切换主题',
    sidebarMenuLabel: '目录',
    returnToTopLabel: '返回顶部',
    footer: { message: '最小固定三角形渲染器 · 接口以当前源码为准' },
  },
})
