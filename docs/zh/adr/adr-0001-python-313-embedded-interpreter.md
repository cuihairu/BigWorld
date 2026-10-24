# ADR-0001：嵌入式解释器 Python 2.7 → 3.13


- **状态**：已接受（2026-09-26 起分批落地，2026-10-12 升至 3.13.16）
- **范围**：`programming/bigworld/third_party/python`（vendored CPython）、
  `lib/pyscript`、构建链 `third_party_python.mak`、运行期资源树

## 背景

引擎把 Python 作为实体脚本与工具层嵌在服务进程里（`Script::init()` 自建
sys.path、冻结 stdlib 之外的加载面），原为 CPython 2.7 魔改树：

- 2.7 已于 2020 年 EOL，现代工具链（gcc 15 / glibc 新环境）与安全面都不可维；
- 引擎通过在 `obmalloc.c`/`pymem.h` 里 `#define malloc BW_Py_malloc` 的方式
  把 CPython 全部堆分配接入 BigWorld 内存统计（BWHooks）——这是对解释器
  **私有实现细节**的侵入，跨大版本不可移植；
- 服务端 Python 生态（twisted/sqlalchemy 等，见 R2）与脚本资产都停在 2.x。

## 决策

把 vendored 解释器整体换为 **CPython 3.13（跟随 3.13.x 安全补丁序列）**，
保留 vendored 方式（不取发行版 python），BWHooks 改走 **官方嵌入接口**：

1. `PyMem_SetAllocator(PYMEM_DOMAIN_RAW, ...)` 把 raw 域四个入口全部转发到
   BW 钩子层（钩子表为空时直通 libc，保持 size==0→1 语义）；
2. `PyObject_SetArenaAllocator` 覆盖 pymalloc arena 分配；
3. 两者都在 `Script::init()` 里、`Py_Initialize()` 之前安装；
4. 钩子符号加 `visibility("default")` 并用 `-Wl,-u` 强拉 `bwhooks.o`
   （3.13 内部不再引用钩子，否则 `_hashlib` 等共享模块导入失败）。

配套：`bw_site.py` 空模块随源码跟踪并安装进资源树；`PlatformInfo` 增加
Debian→el7 回退使 `lib-dynload` 目录名一致（否则全部 C 扩展导入失败）。

## 取舍

| 备选 | 否掉的原因 |
|---|---|
| 留在 2.7 只做工具链修补 | gcc15/C23 下编译不过、无安全更新；等于冻结债务 |
| 升 3.8/3.10 等中间版本 | 仍需再迁一次；3.13 的自由线程/embedding 改造离当前需求近且维护期长 |
| 用发行版/conda Python | 无法带 BWHooks 补丁、无法控制 stdlib 冻结面，破坏离线可复现构建 |
| 保持 `#define malloc` 侵入式钩子 | 3.13 obmalloc 结构完全变化，不可移植；官方 allocator 接口等价且稳定 |

## 后果

- **正面**：`_ssl`/`_hashlib` 经 vcpkg OpenSSL 恢复（联动
  [ADR-0002](adr-0002-vcpkg-third-party-deps)）；21 模块单测门禁在 3.13 上
  全绿；现代 Py3 生态可选。
- **代价/风险**：
  - R2——资源树里的 Py2 生态包（twisted 11 / sqlalchemy 0.6 / zope 2.11 等）
    不可用，需逐包升级后再启用安装规则（暂缓，不在编译关键路径）；
  - R3——Windows（PCbuild/VS）侧未迁移，工具链断裂面在 Windows 构建。
- **后续**：3.13.x 安全补丁跟随（3.13.15→3.13.16 先例）；R2/R3 单独立项。

过程与逐批证据：[Python 3.13 迁移全量台账](/zh/python-313-migration)。
