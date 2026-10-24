const rootSidebarSets = {
  gettingStarted: [
    { text: "Overview", link: "/getting-started/" },
    { text: "Build from source", link: "/getting-started/build-from-source" },
    { text: "Project structure", link: "/getting-started/project-structure" },
    { text: "Core API", link: "/getting-started/api-reference" }
  ],
  analysis: [
    { text: "Overview", link: "/analysis/overview" },
    { text: "Modules", link: "/analysis/modules" },
    { text: "Build and runtime", link: "/analysis/build-and-runtime" },
    { text: "Risks", link: "/analysis/risks" },
    { text: "Source-code guide", link: "/analysis/source-code-guide" }
  ],
  references: [
    { text: "Original PDF documents", link: "/references/original-docs" },
    { text: "Docs-code consistency audit", link: "/doc-consistency-audit" }
  ]
}

const zhSidebarSets = {
  gettingStarted: [
    { text: "概述", link: "/zh/getting-started/" },
    { text: "从源码构建", link: "/zh/getting-started/build-from-source" },
    { text: "项目结构", link: "/zh/getting-started/project-structure" },
    { text: "核心 API", link: "/zh/getting-started/api-reference" }
  ],
  analysis: [
    { text: "总体画像", link: "/zh/analysis/overview" },
    { text: "目录与模块", link: "/zh/analysis/modules" },
    { text: "构建与运行体系", link: "/zh/analysis/build-and-runtime" },
    { text: "源码风险边界", link: "/zh/analysis/risks" },
    { text: "源码导读", link: "/zh/analysis/source-code-guide" }
  ],
  references: [
    { text: "原始 PDF 文档", link: "/zh/references/original-docs" },
    { text: "文档-代码一致性审计", link: "/zh/doc-consistency-audit" }
  ]
}

const architectureGroups = [
  {
    text: "Foundation",
    items: [
      { text: "Research method and evidence", link: "/architecture/research-method" },
      { text: "Process topology", link: "/architecture/process-topology" },
      { text: "Event loop and ticks", link: "/architecture/event-loop" },
      { text: "Network I/O models", link: "/architecture/network-io-model" }
    ]
  },
  {
    text: "Communication and protocols",
    items: [
      { text: "Mercury reliable UDP", link: "/architecture/mercury-reliable-udp" },
      { text: "Communication and RPC", link: "/architecture/communication-rpc" },
      { text: "Serialization and EntityDef", link: "/architecture/serialization-entitydef" },
      { text: "Backpressure and fault injection", link: "/architecture/network-backpressure-fault-injection" }
    ]
  },
  {
    text: "Game state",
    items: [
      { text: "Entity model Base/Cell/Client", link: "/architecture/entity-model" },
      { text: "EntityDef contract generation", link: "/architecture/entitydef-contract-generation" },
      { text: "AOI, Witness, and Ghost", link: "/architecture/aoi-witness-ghost" },
      { text: "Cell partitioning and load balance", link: "/architecture/cell-partition-load-balance" },
      { text: "Entity migration and offload", link: "/architecture/entity-migration-offload" },
      { text: "Entity lifecycle state machine", link: "/architecture/entity-lifecycle-state-machine" },
      { text: "Persistence and DB model", link: "/architecture/persistence-db-model" }
    ]
  },
  {
    text: "Runtime engineering",
    items: [
      { text: "Login, sessions, and proxies", link: "/architecture/login-session-proxy-flow" },
      { text: "Threading and background tasks", link: "/architecture/threading-background-tasks" },
      { text: "Hot reload and script migration", link: "/architecture/hot-reload-script-migration" },
      { text: "Testing and controllable time", link: "/architecture/testing-time-control" },
      { text: "Scaling and fault tolerance", link: "/architecture/scaling-fault-tolerance" },
      { text: "machined control plane", link: "/architecture/machined-control-plane" },
      { text: "Fault injection and coverage", link: "/architecture/testing-fault-injection-coverage" },
      { text: "Security, limits, and crypto", link: "/architecture/security-rate-limit" },
      { text: "Watcher, Profiler, and logs", link: "/architecture/observability-watcher-profiler-logs" },
      { text: "Memory and lifecycle", link: "/architecture/memory-lifecycle" },
      { text: "Build, platform, dependencies", link: "/architecture/build-platform-dependencies" },
      { text: "Modern MMO comparison", link: "/architecture/modern-mmo-comparison" },
      { text: "Complete topic outline", link: "/architecture/outline" }
    ]
  },
  { text: "Reference", items: rootSidebarSets.references }
]

const zhArchitectureGroups = [
  {
    text: "基础架构",
    items: [
      { text: "研究方法与证据分级", link: "/zh/architecture/research-method" },
      { text: "进程拓扑与职责切分", link: "/zh/architecture/process-topology" },
      { text: "主循环、Tick 与事件分发", link: "/zh/architecture/event-loop" },
      { text: "网络 I/O 模型选择", link: "/zh/architecture/network-io-model" }
    ]
  },
  {
    text: "通信与协议",
    items: [
      { text: "Mercury 可靠 UDP", link: "/zh/architecture/mercury-reliable-udp" },
      { text: "通信抽象与 RPC", link: "/zh/architecture/communication-rpc" },
      { text: "序列化与 EntityDef", link: "/zh/architecture/serialization-entitydef" },
      { text: "网络背压与故障注入", link: "/zh/architecture/network-backpressure-fault-injection" }
    ]
  },
  {
    text: "游戏状态模型",
    items: [
      { text: "实体模型 Base/Cell/Client", link: "/zh/architecture/entity-model" },
      { text: "EntityDef 契约与协议生成", link: "/zh/architecture/entitydef-contract-generation" },
      { text: "AOI、Witness 与 Ghost", link: "/zh/architecture/aoi-witness-ghost" },
      { text: "Cell 分区与负载均衡", link: "/zh/architecture/cell-partition-load-balance" },
      { text: "实体迁移与 Offload", link: "/zh/architecture/entity-migration-offload" },
      { text: "实体生命周期状态机", link: "/zh/architecture/entity-lifecycle-state-machine" },
      { text: "持久化与 DB 线程模型", link: "/zh/architecture/persistence-db-model" }
    ]
  },
  {
    text: "运行时工程",
    items: [
      { text: "登录、会话与 Proxy 接管", link: "/zh/architecture/login-session-proxy-flow" },
      { text: "线程架构与后台任务", link: "/zh/architecture/threading-background-tasks" },
      { text: "脚本热更新与迁移", link: "/zh/architecture/hot-reload-script-migration" },
      { text: "测试体系与可控时间", link: "/zh/architecture/testing-time-control" },
      { text: "动态扩展与容灾", link: "/zh/architecture/scaling-fault-tolerance" },
      { text: "machined 控制面与进程发现", link: "/zh/architecture/machined-control-plane" },
      { text: "测试体系、故障注入与覆盖边界", link: "/zh/architecture/testing-fault-injection-coverage" },
      { text: "安全、限流与加密", link: "/zh/architecture/security-rate-limit" },
      { text: "Watcher、Profiler 与日志", link: "/zh/architecture/observability-watcher-profiler-logs" },
      { text: "内存、对象生命周期与资源管理", link: "/zh/architecture/memory-lifecycle" },
      { text: "构建、平台与依赖治理", link: "/zh/architecture/build-platform-dependencies" },
      { text: "MMO 架构边界对比", link: "/zh/architecture/modern-mmo-comparison" },
      { text: "完整专题大纲", link: "/zh/architecture/outline" }
    ]
  },
  { text: "参考资料", items: zhSidebarSets.references }
]

const rootUpgradePlan = [
  { text: "Migration and coverage status", link: "/upgrade-plan/migration-status" },
  { text: "Python upgrade roadmap", link: "/upgrade-plan/python-upgrade-plan" },
  { text: "Python C API migration", link: "/upgrade-plan/python-c-api-guide" },
  { text: "Third-party dependency upgrades", link: "/upgrade-plan/third-party-deps" },
  { text: "Switch third-party deps to vcpkg", link: "/vcpkg-migration" },
  { text: "C++23 migration", link: "/cpp23-migration" },
  { text: "Build-system modernization", link: "/upgrade-plan/build-system-modernization" },
  { text: "Hot-reload productionization", link: "/upgrade-plan/hot-reload-productionization" },
  { text: "Packaging and daily-build analysis", link: "/upgrade-plan/packaging-and-daily-build" },
  { text: "Python 3.13 migration ledger", link: "/python-313-migration" }
]

const rootAdr = [
  { text: "Index", link: "/adr/" },
  { text: "ADR-0001 Embedded Python 2.7 → 3.13", link: "/adr/adr-0001-python-313-embedded-interpreter" },
  { text: "ADR-0002 Third-party deps via vcpkg", link: "/adr/adr-0002-vcpkg-third-party-deps" },
  { text: "ADR-0003 make/CMake dual-track build", link: "/adr/adr-0003-make-cmake-dual-track" }
]

const zhUpgradePlan = [
  { text: "迁移与覆盖率现状", link: "/zh/upgrade-plan/migration-status" },
  { text: "Python 升级路线图", link: "/zh/upgrade-plan/python-upgrade-plan" },
  { text: "Python C API 迁移", link: "/zh/upgrade-plan/python-c-api-guide" },
  { text: "第三方依赖升级", link: "/zh/upgrade-plan/third-party-deps" },
  { text: "第三方依赖切换到 vcpkg", link: "/zh/vcpkg-migration" },
  { text: "C++23 迁移", link: "/zh/cpp23-migration" },
  { text: "构建系统现代化", link: "/zh/upgrade-plan/build-system-modernization" },
  { text: "热重载生产化设计", link: "/zh/upgrade-plan/hot-reload-productionization" },
  { text: "打包与每日构建分析", link: "/zh/upgrade-plan/packaging-and-daily-build" },
  { text: "Python 3.13 迁移全量台账", link: "/zh/python-313-migration" }
]

const zhAdr = [
  { text: "索引", link: "/zh/adr/" },
  { text: "0001 嵌入式 Python 2.7 → 3.13", link: "/zh/adr/adr-0001-python-313-embedded-interpreter" },
  { text: "0002 第三方依赖切换 vcpkg", link: "/zh/adr/adr-0002-vcpkg-third-party-deps" },
  { text: "0003 make/cmake 双轨构建", link: "/zh/adr/adr-0003-make-cmake-dual-track" }
]

const rootUpgradeSidebar = [
  { text: "Migration engineering", items: rootUpgradePlan },
  { text: "ADR decisions", items: rootAdr }
]

const zhUpgradeSidebar = [
  { text: "迁移工程", items: zhUpgradePlan },
  { text: "ADR 决策记录", items: zhAdr }
]

export default {
  base: "/BigWorld/",
  cleanUrls: false,
  lastUpdated: true,
  // VitePress does not apply `base` to head entries; the project-site base is
  // pinned below so the favicon resolves on GitHub Pages (/BigWorld/).
  head: [
    ["link", { rel: "icon", type: "image/svg+xml", href: "/BigWorld/logo.svg" }]
  ],
  ignoreDeadLinks: false,
  locales: {
    root: {
      label: "English",
      lang: "en-US",
      title: "BigWorld Engine Architecture Research",
      description: "BigWorld architecture notes for MMO server learning and source-evidence analysis.",
      themeConfig: {
        logo: "/logo.svg",
        siteTitle: "BigWorld Architecture Research",
        search: { provider: "local" },
        nav: [
          { text: "Overview", link: "/" },
          { text: "Getting started", link: "/getting-started/" },
          { text: "Architecture", link: "/architecture/research-method" },
          { text: "Analysis", link: "/analysis/overview" },
          { text: "Migration", link: "/upgrade-plan/migration-status" }
        ],
        sidebar: {
          "/getting-started/": [{ text: "Getting started", items: rootSidebarSets.gettingStarted }],
          "/architecture/": architectureGroups,
          "/analysis/": [{ text: "Project analysis", items: rootSidebarSets.analysis }],
          "/upgrade-plan/": rootUpgradeSidebar,
          "/python-313-migration": rootUpgradeSidebar,
          "/references/": [{ text: "Reference", items: rootSidebarSets.references }]
        },
        socialLinks: [{ icon: "github", link: "https://sourceforge.net/p/bigworld/code/HEAD/tree/" }],
        footer: {
          message: "Organized around source evidence, source trade-offs, and verification boundaries.",
          copyright: "BigWorld Open-Source Edition Architecture Notes"
        }
      }
    },
    zh: {
      label: "简体中文",
      lang: "zh-CN",
      title: "BigWorld 引擎架构研究",
      description: "面向 MMO 游戏服务器架构学习和源码证据链分析的 BigWorld 文档站",
      themeConfig: {
        logo: "/logo.svg",
        siteTitle: "BigWorld 架构研究",
        search: { provider: "local" },
        nav: [
          { text: "总览", link: "/zh/" },
          { text: "快速开始", link: "/zh/getting-started/" },
          { text: "架构研究", link: "/zh/architecture/research-method" },
          { text: "项目分析", link: "/zh/analysis/overview" },
          { text: "迁移工程", link: "/zh/upgrade-plan/migration-status" }
        ],
        sidebar: {
          "/zh/getting-started/": [{ text: "快速开始", items: zhSidebarSets.gettingStarted }],
          "/zh/architecture/": zhArchitectureGroups,
          "/zh/analysis/": [{ text: "项目分析", items: zhSidebarSets.analysis }],
          "/zh/upgrade-plan/": zhUpgradeSidebar,
          "/zh/python-313-migration": zhUpgradeSidebar,
          "/zh/references/": [{ text: "参考资料", items: zhSidebarSets.references }]
        },
        socialLinks: [{ icon: "github", link: "https://sourceforge.net/p/bigworld/code/HEAD/tree/" }],
        footer: {
          message: "按源码证据链、源码取舍和验证边界组织，而不是按目录机械罗列。",
          copyright: "BigWorld Open-Source Edition Architecture Notes"
        }
      }
    }
  },
  markdown: {
    container: {
      tipLabel: "Tip",
      warningLabel: "Warning",
      dangerLabel: "Danger",
      infoLabel: "Info",
      detailsLabel: "Details"
    }
  }
}
