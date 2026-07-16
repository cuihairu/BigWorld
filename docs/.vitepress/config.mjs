export default {
  lang: "zh-CN",
  title: "BigWorld 引擎架构研究",
  description: "面向 MMO 游戏服务器架构学习、源码证据链与现代化取舍分析的 BigWorld 文档站",
  cleanUrls: true,
  lastUpdated: true,
  ignoreDeadLinks: [
    /^\/home\/cui\/workspaces\/BigWorld\//
  ],
  markdown: {
    container: {
      tipLabel: "提示",
      warningLabel: "注意",
      dangerLabel: "风险",
      infoLabel: "信息",
      detailsLabel: "详细信息"
    }
  },
  themeConfig: {
    logo: "/logo.png",
    siteTitle: "BigWorld 架构研究",
    search: {
      provider: "local"
    },
    nav: [
      { text: "总览", link: "/" },
      { text: "架构研究", link: "/architecture/research-method" },
      { text: "Python 3.12", link: "/migration/python-3-12" },
      { text: "原始 PDF", link: "/references/original-docs" }
    ],
    sidebar: {
      "/architecture/": [
        {
          text: "研究基线",
          items: [
            { text: "研究方法与证据分级", link: "/architecture/research-method" },
            { text: "进程拓扑与职责切分", link: "/architecture/process-topology" },
            { text: "主循环、Tick 与事件分发", link: "/architecture/event-loop" },
            { text: "网络 I/O 模型选择", link: "/architecture/network-io-model" }
          ]
        },
        {
          text: "后续专题",
          collapsed: false,
          items: [
            { text: "完整专题大纲", link: "/architecture/outline" }
          ]
        }
      ],
      "/analysis/": [
        {
          text: "旧版草稿",
          items: [
            { text: "总体画像", link: "/analysis/overview" },
            { text: "目录与模块", link: "/analysis/modules" },
            { text: "构建与运行体系", link: "/analysis/build-and-runtime" },
            { text: "技术债与风险", link: "/analysis/risks" }
          ]
        }
      ],
      "/migration/": [
        {
          text: "现代化路线",
          items: [
            { text: "Python 3.12 路线图", link: "/migration/python-3-12" }
          ]
        }
      ],
      "/references/": [
        {
          text: "参考资料",
          items: [
            { text: "原始 PDF 文档", link: "/references/original-docs" }
          ]
        }
      ]
    },
    socialLinks: [
      { icon: "github", link: "https://sourceforge.net/p/bigworld/code/HEAD/tree/" }
    ],
    footer: {
      message: "按源码证据链、历史取舍和现代对比组织，而不是按目录机械罗列。",
      copyright: "BigWorld Open-Source Edition Architecture Notes"
    }
  }
}
