export default {
  lang: "zh-CN",
  title: "BigWorld 引擎架构研究",
  description: "面向 MMO 游戏服务器架构学习、源码证据链与现代化取舍分析的 BigWorld 文档站",
  base: "/BigWorld/",
  cleanUrls: false,
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
      { text: "专题大纲", link: "/architecture/outline" }
    ],
    sidebar: {
      "/architecture/": [
        {
          text: "基础架构",
          items: [
            { text: "研究方法与证据分级", link: "/architecture/research-method" },
            { text: "进程拓扑与职责切分", link: "/architecture/process-topology" },
            { text: "主循环、Tick 与事件分发", link: "/architecture/event-loop" },
            { text: "网络 I/O 模型选择", link: "/architecture/network-io-model" }
          ]
        },
        {
          text: "通信与协议",
          collapsed: false,
          items: [
            { text: "Mercury 可靠 UDP", link: "/architecture/mercury-reliable-udp" },
            { text: "通信抽象与 RPC", link: "/architecture/communication-rpc" },
            { text: "序列化与 EntityDef", link: "/architecture/serialization-entitydef" },
            { text: "网络背压与故障注入", link: "/architecture/network-backpressure-fault-injection" }
          ]
        },
        {
          text: "游戏状态模型",
          collapsed: false,
          items: [
            { text: "实体模型 Base/Cell/Client", link: "/architecture/entity-model" },
            { text: "EntityDef 契约与协议生成", link: "/architecture/entitydef-contract-generation" },
            { text: "AOI、Witness 与 Ghost", link: "/architecture/aoi-witness-ghost" },
            { text: "Cell 分区与负载均衡", link: "/architecture/cell-partition-load-balance" },
            { text: "实体迁移与 Offload", link: "/architecture/entity-migration-offload" },
            { text: "实体生命周期状态机", link: "/architecture/entity-lifecycle-state-machine" },
            { text: "持久化与 DB 线程模型", link: "/architecture/persistence-db-model" }
          ]
        },
        {
          text: "运行时工程",
          collapsed: false,
          items: [
            { text: "登录、会话与 Proxy 接管", link: "/architecture/login-session-proxy-flow" },
            { text: "线程架构与后台任务", link: "/architecture/threading-background-tasks" },
            { text: "脚本热更新与迁移", link: "/architecture/hot-reload-script-migration" },
            { text: "测试体系与可控时间", link: "/architecture/testing-time-control" },
            { text: "动态扩展与容灾", link: "/architecture/scaling-fault-tolerance" },
            { text: "machined 控制面与进程发现", link: "/architecture/machined-control-plane" }
          ]
        },
        {
          text: "工程质量与现代化",
          collapsed: false,
          items: [
            { text: "安全、限流与加密", link: "/architecture/security-rate-limit" },
            { text: "Watcher、Profiler 与日志", link: "/architecture/observability-watcher-profiler-logs" },
            { text: "内存、对象生命周期与资源管理", link: "/architecture/memory-lifecycle" },
            { text: "构建、平台与依赖治理", link: "/architecture/build-platform-dependencies" },
            { text: "现代 MMO 架构对比", link: "/architecture/modern-mmo-comparison" }
          ]
        },
        {
          text: "专题地图",
          collapsed: false,
          items: [
            { text: "完整专题大纲", link: "/architecture/outline" }
          ]
        },
        {
          text: "现代化专题",
          collapsed: false,
          items: [
            { text: "Python 3.12 路线图", link: "/migration/python-3-12" }
          ]
        },
        {
          text: "参考资料",
          collapsed: true,
          items: [
            { text: "原始 PDF 文档", link: "/references/original-docs" }
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
