# BigWorld 第三方依赖切换到 vcpkg

](/> [English)) | **中文**
> 任务：把 `programming/bigworld/third_party` 中可由 vcpkg 提供的库改为通过
> vcpkg manifest 引入；Make 链与 CMake 链改用 vcpkg toolchain/产物。
](/> 分支：dev。与 [python-313-migration)) 衔接
> （OpenSSL 升级即该文档风险 R1 的解法）。

## 1. 切换范围与决策

| 库 | 原状态 | 决策 | 说明 |
|---|---|---|---|
| OpenSSL | vendored 1.0.0d（gcc15 下无法编译） | **vcpkg 3.6.4** | 静态；恢复 Python `_ssl`/`_hashlib`（R1）。目录 `third_party/openssl` 已删除 |
| curl | vendored 7.x（configure 构建） | **vcpkg 8.22**（openssl 特性） | 链接名保持 `bwcurl` |
| jsoncpp | vendored 0.7.0 | **vcpkg 1.9.x** | 消费面只有 `Reader::parse`/`Value`/`FastWriter`（1.9 兼容，deprecated 警告）；目录 `third_party/jsoncpp` 已删除 |
| zlib（目录名 `zip`） | vendored 1.2.8（`MY_ZCALLOC` 分配钩子） | **vcpkg zlib 1.3.x** | 链接名保持 `zip`；zlib 分配不再进 BW 内存统计（见 §4） |
| libpng | vendored 1.6.2（源码组件构建） | **vcpkg 1.6.58** | `lib/moo/png.cpp` 弃用私有头，改公开 API（见 §4） |
| sqlite3 | vendored 3.6.23.1（amalgamation 并入 libsqlite） | **vcpkg 3.53.4** | `lib/sqlite` 只保留包装层；直接消费者设 `useSQLite := 1` |
| CPython | vendored 3.13.16（BWHooks 魔改树） | **保留 vendored** | vcpkg 的 python3 端口不带 BWHooks 内存跟踪补丁；但 `_ssl`/`_hashlib` 通过 vcpkg OpenSSL 恢复（R1） |
| MongoDB C++ driver | vendored legacy 预编译 | **保留 vendored** | vcpkg 的 mongo-cxx-driver 是 3.x API，与 legacy 驱动不兼容；仅在 message_logger 使用 |
| png/zip 的 vendored 源码树 | — | **暂留** | Windows CMake 工程（R3 未迁移）仍引用 `third_party/png`、`third_party/zip`（sqlite 走 `lib/sqlite`）；R3 落地后删除 |
| CppUnitLite2 / WTL / putty / nvtt / nedalloc / recast / openautomate 等 | vendored | **保留 vendored** | 引擎自有测试框架或 Windows/客户端专用，不在 Linux 服务端构建路径 |

### 1.1 manifest 钉版（builtin-baseline）

`programming/bigworld/vcpkg.json` 补了 `"builtin-baseline": "10541e317a660f4165ba4ac2851ab54a8d4577b1"`
（vcpkg 仓库 2026-09-25 的 HEAD）。作用：

- 依赖版本不再随 `VCPKG_ROOT` 里 vcpkg 的克隆时点漂移——manifest 解析固定到该
  baseline 的 ports 树（实测解析出 openssl 3.6.4 / curl 8.22.0 / sqlite3 3.53.4 等，
  与 §1 表一致）。
- 未钉 baseline 时 vcpkg 按 `VCPKG_ROOT` 当前 HEAD 解析版本，"同一份 vcpkg.json
  两台机器装出不同 openssl" 就是这么来的。
- **不建 `vcpkg-configuration.json`**：该文件用于 registry 覆盖 / overlay 登记
  （第三方 registry、default-registry 换轨）。本仓只用 builtin registry，无覆盖
  需求；自定义 triplet（`build/vcpkg-triplets/x64-linux-bw.cmake`）走
  `--overlay-triplets` 按 build 传入，不属于 manifest 层。空配置文件徒增解析面。
- 升级依赖 = 改 baseline（连同本机 `VCPKG_ROOT` 一起 `git -C ~/vcpkg checkout`
  到新 commit）再 `vcpkg upgrade`，锁定后再推。

## 2. 构建系统改动（Make 链）

- 新增 `vcpkg.json`（BigWorld 源根，`programming/bigworld/`）：声明 openssl、
  curl(openssl)、jsoncpp、zlib、libpng、sqlite3、bzip2（后者仅供 CPython
  `_bz2` 扩展）。
- 新增自定义 triplet `build/vcpkg-triplets/x64-linux-bw.cmake`：
  静态库、仅 Release（`VCPKG_BUILD_TYPE release`）。
  该目录**刻意不放在** `build/vcpkg/` 下——`.gitignore:41` 排除了
  `programming/bigworld/build/vcpkg/`，而 triplet 是要入库的构建输入，
  放在那里会让全新 clone 既拿不到它、也无法把它作为 make 前置依赖
  （`--overlay-triplets` 目录同样失效）。
- 新增 `build/make/third_party_vcpkg.mak`：
  - `VCPKG_ROOT`（环境变量或 make 变量）→ `vcpkg install`（manifest 模式）
    产物落 `build/<platform>/third_party/vcpkg_installed/x64-linux-bw/`；
    以 `vcpkg-install.stamp` 为 make 依赖锚点；
  - 各包把 vcpkg 静态库**staged** 成链接行历史名称：
    `libbwssl.a`/`libbwcrypto.a`/`libbwcurl.a`/`libjsoncpp.a`/`libzip.a`/
    `libpng.a`(自 libpng16)/`libsqlite3.a`，目标名不变，
    `thirdPartyDependsOn` 链无需改动；
  - `BW_INCLUDES += -I<vcpkg>/include`（zlib/png/sqlite3/jsoncpp 头文件单一根）。
- `third_party_openssl.mak` / `third_party_curl.mak` / `third_party_json.mak`
  重写为 vcpkg 消费者（保留 `OPENSSL_BUILD_DIR`/`BW_SSL_LIB`/`CURL_BUILD_DIR`/
  `JSON_DIR` 等外部变量名；`OPENSSL_BUILD_DIR` 即 vcpkg installed 根，
  同时是 Python `--with-openssl` 的根）。
- `third_party/Makefile.rules`：jsoncpp/png/zip 子构建移除（仅留 CppUnitLite2）。
- `common_footer_config.mak`：
  - `useCurl` 头文件路径 → vcpkg 单一根（不再有 vendored include）；
  - 新增 `useSQLite`（`lib/sqlite`、baseapp、transfer_db、consolidate_dbs
    及其单测设置：staging 依赖 + `-lpthread -ldl -lm`）；
  - 新增 `usePNG`（`lib/moo` 与链接 libmoo 的目标设置：staging 依赖 +
    `-lpng -lzip`，因为 libpng16 依赖 zlib）。三方依赖**不传递**
    （`bwLibraryDepsAsName` 只由本组件的 `dependsOn`/`thirdPartyDependsOn`
    构成），所以消费 libmoo 的目标必须自己声明。
- `common_header.mak`：`useSQLite`/`usePNG` 加入与 `useCurl`/`useOpenSSL`
  同列的逐组件复位列表。不复位的话，第一个开启它的组件会把标志泄漏给之后
  包含的每个组件，凭空多出一条三方库链接项。
- `lib/sqlite/Makefile.rules`：不再编译 amalgamation。
- `third_party_python.mak`（R1 恢复，详见 python-313-migration.md）：
  `--with-openssl` + `_ssl`/`_hashlib` 共享模块 + whole-archive
  `-lbwssl -lbwcrypto` + 符号采集恢复；另加 `--with-bz2=$(VCPKG_INSTALLED)`
  （`_bz2` 在 `pythonSharedMods` 列表内，本机无系统 bzip2 开发包，故同样
  走 vcpkg）。
- 构建：`VCPKG_ROOT=~/vcpkg make -C programming`（未设置 VCPKG_ROOT 时给出明确报错）。
- 跑单测需要额外装一次解释器标准库：
  `make -C programming user_shouldInstallPython=1 python_install`
  （否则用到 Python 的模块会报 `ModuleNotFoundError: No module named 'encodings'`）。
- `build/make/Makefile` 的 `bw-run-all-unit-tests` 配方同步修正：原配方的
  `|| FAILED_TESTS=… \` 续行把状态累并进了同一命令前缀、`$$?` 取不到模块
  退出码、`[ … == 1 ]` 是 bashism，失败模块既收集不全也无法中止；改为先取
  `MODULE_STATUS=$$?` 再各自判断，并把 `==` 换成 POSIX 的 `=`。
- 若干单测目录的 `Makefile.rules` 补 `dependsOn += zip`（及 terrain 的
  `usePNG`/`moo`/`resmgr`）：unit_test 类型置 `noDefaultDependencies`，
  `BW_DEPENDS_ON` 里的 `zip` 到不了链接行，vcpkg 化后 libnetwork 的
  zip_stream/compression_stream 需要 libzip.a 在场才能链接。

### 链接闭合性

vcpkg 静态库的传递依赖在 BW 链接行内闭合：curl→ssl/crypto+zlib（`bwssl`/
`bwcrypto`/`zip` 均 staged）；libpng16→zlib（`zip`）；sqlite3→pthread/dl/m
（`useSQLite` 注入）。curl 的 brotli/idn2 等为可选特性，未启用。

## 3. 构建系统改动（CMake 链）

- `vcpkg.json` 位于根 `CMakeLists.txt` 旁：用
  `-DCMAKE_TOOLCHAIN_FILE=<VCPKG_ROOT>/scripts/buildsystems/vcpkg.cmake`
  配置即自动进入 manifest 模式安装依赖。
- `build/cmake/FindBWOpenSSL.cmake` / `FindBWcurl.cmake`：非 MSVC 时优先
  `find_package(OpenSSL/CURL)`（vcpkg toolchain 提供结果）并映射到
  BW 前缀变量；MSVC 分支保留原 `third_party/openssl-vsXXXX`、预编译 curl
  路径（Windows 迁移 = python-313-migration.md R3，后续批次）。
- Windows 客户端/工具配置（`BWConfiguration_*.cmake`）仍引用
  `third_party/zip`、`third_party/png` 源码工程（sqlite 走 `lib/sqlite`，
  见 `BWConfiguration_server.cmake:71`），与 R3 一并处理。

### 3.1 Linux CMake 样板（cstdmf，与 make 双轨并存）

`cmake/samples/cstdmf/CMakeLists.txt` 是第一个 Linux 侧可独立构建的组件样板
（**不属于** `lib/*/CMakeLists.txt` 那套 BWStandardProject Windows 超级工程，
两者互不引用）。样板构成：

- 四个目标：`cstdmf`（静态库，57 TU）、`unit_test_lib`（4 TU）、
  `CppUnitLite2`（7 TU，从 `third_party/CppUnitLite2/src` 直编）、
  `cstdmf_test`（48 TU）。前两者与 CppUnitLite2 在 make 链里也是独立组件，
  样板里就地源码编译，使测试二进制链接面与 make 完全一致
  （`-lunit_test_lib -lCppUnitLite2 -lcstdmf -lm -lpthread` + `-rdynamic`）。
- 与 make 输出对齐的保证：编译选项/宏（`-std=c++23 -O3 -g -Wall -Wextra -Werror`、
  `MF_SERVER / MF_CONFIG="hybrid" / BW_BUILD_PLATFORM="el7" / CSTDMF_EXPORT|IMPORT /
  CODE_INLINE / MF_USE_ASSERTS / _HYBRID`、测试侧追加 `-Wno-float-equal`）逐字取自
  `make -n unit_test_cstdmf` 实测命令行；源清单逐条对照 `Makefile.rules`
  （排除 Windows/macOS TU 与 make 清单外的文件，如 `test_object_pool.cpp`
  孤儿登记——`object_pool.hpp:343` 需 `LookUpTable::reserve()` 本树无此成员）。
  依赖升级时改 `Makefile.rules` 必须同步此清单。
- 依赖走共享 manifest：`VCPKG_MANIFEST_DIR` 默认指 `programming/bigworld`，
  自定义 triplet `x64-linux-bw` 与 overlay 目录在 CMakeLists 内默认，首次
  configure 时 vcpkg manifest 模式把 7 个依赖装进构建目录（binary cache
  命中后为秒级）。
- 本地一条命令：`scripts/build-cstdmf-cmake.sh`（configure → build → ctest，
  缺省构建目录 `programming/bigworld/build/cmake-cstdmf/`，已进 .gitignore）。
- 验证记录（2026-10-12）：全新构建目录冷启动（依赖从 binary cache 恢复）
  configure/build 通过，`cstdmf_test` **398 用例全绿**，与 make 产出的
  cstdmf_test 用例数一致；make 侧回归复跑同样 398 全绿。
- CI 侧由 `.github/workflows/ci.yml` 在每次 push/PR 上以干净环境跑同一样板
  （vcpkg 按 baseline 克隆 + binary cache + ctest），防 manifest 与样板漂移。

### 3.2 resmgr 样板（自下而上第二块，2026-10-13）

`cmake/samples/resmgr/CMakeLists.txt` 是第二个样板，复用 3.1 的
`CppUnitLite2` + `unit_test_lib` + `cstdmf` 源码清单，自下而上叠加
`math`（12 TU）、`zip`（vcpkg zlib，make 里 staged 成 `libzip.a`）、
`resmgr`（25 TU，`HAVE_ZIP` + `BWENTITY_DLL_EXPORT`）与 `resmgr_test`
（12 TU）。编译选项/宏逐字取自 `make -n -B bw-run-test-resmgr` 实测命令行
（含 `-fPIC`、`-Wfloat-equal`，测试侧**不**加 `-Wno-float-equal`——与 3.1
不同，以 make 为准）；`-rdynamic -Wl,--export-dynamic` 与链接序
（`unit_test_lib CppUnitLite2 resmgr cstdmf math ZLIB::ZLIB m pthread`）与
make 链接行一致。本地一条命令 `scripts/build-resmgr-cmake.sh`。

两个样板级要点：

- `unit_test_lib` 的 `base_resmgr_unit_test_harness.cpp` 引用 `BWResource`
  符号，但静态库成员仅在最终链接需要时才会被拉入——所以 3.1 的 cstdmf
  样板不带 resmgr 也能链，而 resmgr 样板必须带 resmgr（与 make 的
  `--start-group` 同构）。
- resmgr 测试基类（`test_harness.hpp`）把 `--root` 直接拼成
  `<repo>/programming/bigworld/lib/resmgr/unit_test/res` 与
  `<repo>/game/res/bigworld`，因此样板的 `--root` 必须精确指向仓库根；
  3.1 的 `get_filename_component(BW_REPO_ROOT ...)` 原写 6 段 `..`（多一级，
  指向 `workspaces/`）——cstdmf 测试基类不读 `--root` 故一直无感，resmgr
  样板把两个样板一并修正为 5 段（仓库根），cstdmf 侧同步修正。

验证（2026-10-13）：两个样板各自 configure/build 通过；`ctest` 全绿，
`cstdmf_test` **427** 用例、`resmgr_test` **88** 用例，与 make 链同口径
（make 侧 `bw-run-test-cstdmf` 427 / `bw-run-test-resmgr` 88）。CI 的
`cmake-samples` job 改为 matrix（`[cstdmf, resmgr]`，`fail-fast: false`），
每组件独立步骤与日志。后续 network/connection/entitydef 按同一模式叠加
（connection 目前**无** unit_test，entitydef 依赖 8 个库，均需单独处理）。

### 3.3 network 样板（自下而上第三块，2026-10-14）

`cmake/samples/network/CMakeLists.txt` 是第三个样板，复用 3.1/3.2 的
`CppUnitLite2` + `unit_test_lib` + `cstdmf` + `math` + `resmgr` 源码清单，
再叠加三块：`connection`（26 TU——CMake 轨首次纳入；它**无** unit_test，
此前只能靠 network 的链接面间接覆盖）、`network`（78 TU）、
`network_test`（49 TU）。编译选项/宏逐字取自 `make -n -B bw-run-test-network`
实测命令行；本地一条命令 `scripts/build-network-cmake.sh`。

与 3.1/3.2 的三处差异（均以 make 实测为准，不是"顺手统一"）：

- **`USE_OPENSSL`**：`lib/connection` 与 `lib/network` 两个 Makefile.rules
  都是 `useOpenSSL = 1`，所以它进了全局宏面（其余样板没有）。make 把 vcpkg
  OpenSSL staged 成 `libbwssl.a`/`libbwcrypto.a`，样板侧等价映射为
  `find_package(OpenSSL REQUIRED)` + `OpenSSL::SSL`/`OpenSSL::Crypto`
  ——同一份 vcpkg 产物，只是链接名不同。
- **`-Wno-float-equal` 在库侧也有**：3.2 记录的是 resmgr 测试侧**不**加；
  network 则是**库与测试两侧都加**——`lib/network/Makefile.rules` 的
  `cxxFlags_$(binName) += -Wno-float-equal` 针对库本身（`bsd_snprintf.cpp`
  有浮点 `==` 比较），漏掉会让 `-Werror=float-equal` 直接红门禁。
  connection 侧没有该旗标（`client_interface.cpp` 之外的 26 TU 无浮点比较），
  保持不加。
- **静态库单次扫描链接序**：make 的链接行用
  `-Wl,--start-group ... -Wl,--end-group` 包住整个库组
  （`unit_test_lib CppUnitLite2 network connection resmgr math cstdmf zip
  bwssl bwcrypto`），因为 `connection` 引用 `network` 符号
  （`DeferredBundle`、`CompressionIStream`）而 `network` 引用 `resmgr` 符号
  （`DataSection::readInt`）——两种引用同时存在时**任何线性序都解不开**。
  样板侧不能用 `$<LINK_GROUP>`（CMake 3.24+ 才引入，且不接受普通目标），
  改为在 `target_link_libraries` 里写原始链接旗标项 `-Wl,--start-group` /
  `-Wl,--end-group`（CMake 对以 `-` 开头的项按字面透传，位置与库项一致），
  与 make 的拼法逐字对应。`connection` 在 CMake 里仍显式依赖 `network`，
  保证依赖图记录正确。

排除面（与 make 清单逐条对照）：`network` 排除 `net_360.cpp`（Xbox 360）
与 `pch.cpp`；`connection` 排除 `client_interface.cpp`（Windows/MFC 客户端
连接）与 `pch.cpp`；测试侧排除 `pch.cpp`。

验证（2026-10-14）：configure/build 通过，`ctest` 全绿，`network_test`
**210** 用例，与 make 侧同口径（make 产出的 `network_test` 同为 **210**
用例全绿）。network 用例耗时较重（单跑约 200 s，含真实 socket 与定时
时序），CI job 的 120 分钟超时留有充足余量。CI matrix 扩展为
`[cstdmf, resmgr, network]`。剩余 entitydef 依赖 14 个库，其中 Python 一腿
（vendored CPython 无 CMake 构建系统）先由 §3.4 铺路，见 §3.5。

### 3.4 vendored CPython 3.13 样板（entitydef 的前置，2026-10-14）

`cmake/samples/python/CMakeLists.txt` 是 CMake 轨第一块**非 BW 库**样板，
也是 3.1–3.3 与 §3.5 的共同地基。CPython 上游只提供 PCbuild（Windows）与
autotools（Unix），没有 CMake 构建系统，所以这里用 `ExternalProject` 包住
make 链本来就在跑的 configure+make，而不是手抄约 250 个 `.c`：

- configure 选项逐字取自 `build/make/third_party_python.mak` 的
  `pythonConfigureOpts`（`--with-openssl=` / `--with-bz2=` 指向 vcpkg manifest
  安装目录、`--with-suffix=.exe`、`--disable-test-modules`、`CPPFLAGS`），
  两者只有一个权威来源：改 make 侧选项必须同步此清单。
- 只构建 `libpython3.13.a EXTRA_CFLAGS="-m64 -fPIC"`。make 链在 archive 之外
  还有一段 sed 后处理（`pyconfig.h` 的 TERNAM undef、
  共享扩展模块的 `LINKFORSHARED` whole-archive + `-u,BW_Py_*`）——那些只影响
  `python.exe` 与扩展模块，样板两者都不建，因此整段跳过。
- **`INSTALL_COMMAND` 拷 stdlib**：`Py_Initialize` 按可执行文件前缀解析
  标准库，只出 archive 时嵌入解释器会在 `encodings` 处直接中止。样板把
  vendored `Lib/` 拷到 `<build>/bwpython-sample/python3.13/lib/python3.13`，
  等价于 make 链 `make install` 提供的同一棵树（entitydef_test 不 dlopen
  `lib-dynload`，故不需要共享模块）。
- **`BWPython::Python` 必须是 `IMPORTED GLOBAL`**：CMake 的 IMPORTED 目标默认
  只在创建它的目录可见，被 entitydef 样板 `add_subdirectory` 引入后会解析成
  未知目标——这一处不带 GLOBAL 就是 configure 期硬错。
- `check_symbols.cmake`（`-P` 脚本模式）作为构建门禁核对 8 个 BWHooks 符号
  （`BW_Py_malloc/free/calloc/realloc/getHooks/setHooks/memoryTrackingIgnoreBegin/End`）
  仍是归档内的定义符号。它们来自归档成员 `bwhooks.o`（`Python/bwhooks.c`），
  正是 vcpkg `python3` port 无法替代 vendored 解释器的唯一原因；一旦消失
  entitydef 样板就失去 Python，构建即失败。

本地一条命令：`scripts/build-python-cmake.sh`（并额外校验归档存在与
`bwhooks.o` 在内）。

### 3.5 entitydef 样板（自下而上第四块，2026-10-14）

`cmake/samples/entitydef/CMakeLists.txt` 是第四个样板，复用 3.1–3.3 已验证的
`CppUnitLite2` + `unit_test_lib` + `cstdmf` + `math` + `resmgr` + `network` +
`connection` 清单，`add_subdirectory` 纳入 3.4，再叠加 8 个新库：`physics2`
（7 TU）、`moo`（2 TU，`usePNG=1`）、`terrain`（18 TU）、`scene`（14 TU，
`pch.cpp` 在 make 清单内故保留）、`chunk`（34 TU，`usePython=1`）、`script`
（4 TU）、`pyscript`（18 TU）、`entitydef`（55 TU），以及 `entitydef_test`
（15 TU）。本地一条命令：`scripts/build-entitydef-cmake.sh`。

四样板共有的导出面（Linux 上不是装饰：`bwentity_api.hpp` 按这些宏在
`template class` 与 `extern template class` 之间分叉，取错会得到重复实例化
或缺失符号）。宏面逐个照 `Makefile.rules` 的 `exportFor` 推导：
`cstdmf`（`exportFor = cstdmf`）用 `CSTDMF_EXPORT`；
`entitydef` / `script` / `pyscript` / `network` / `connection` / `resmgr` /
`math`（`exportFor := bwentity`）用 `BWENTITY_DLL_EXPORT` + `CSTDMF_IMPORT`；
`chunk` / `scene` / `terrain` / `moo` / `physics2` / `unit_test_lib` /
`CppUnitLite2`（无 `exportFor`）两侧都 IMPORT；测试侧只 `CSTDMF_IMPORT`。

其余样板级要点：

- **vendored `png` / `zip` 两棵树已不在构建面**：`third_party/Makefile.rules`
  只递归 `CppUnitLite2/src`，所以 `third_party/png`、`third_party/zip` 的
  `Makefile.rules` 是死清单，`lib/moo/png.cpp` 与 `lib/resmgr/zip_*.cpp`
  实际链接的是 make 链 staging 的 `libpng.a`/`libzip.a`（vcpkg 产物）。
  样板侧等价映射为 `find_package(PNG REQUIRED)` +
  `find_package(ZLIB REQUIRED)`，与 3.2/3.3 一致。
- **链接组跨全部 14 个 BW 库**：3.3 记录了 `--start-group` 的原因（`connection`
  与 `network` 互相引用）。entitydef 侧引用面更宽——`chunk` 引用 `entitydef`
  的 vtable、`pyscript` 引用 `resmgr`/`math`、测试面同时触及两侧，所以整组
  包在 `--start-group` 内，`BWPython::Python` + `dl`/`m`/`pthread`/`util`
  放组外，与 `common_footer_config.mak` 的链接行同形。
- `-rdynamic -Wl,--export-dynamic`（`callstack.cpp` 的 `backtrace()` 符号名
  依赖它）、`-Wno-float-equal`（`pyscript` 与 `network` 库侧、测试侧）照 3.3
  的做法以 make 实测为准，不做"顺手统一"。

验证（2026-10-14）：`scripts/build-entitydef-cmake.sh` 一条命令 configure /
build / ctest 全通过，`entitydef_test` **318 用例全绿**，与 make 侧同口径
（`bw-run-test-entitydef` 同为 **318** 用例 0 失败）；make 侧全量门禁同步复跑
21 模块 / 1355 用例 / 0 失败。CI matrix 扩展为
`[cstdmf, resmgr, network, entitydef]`。至此 CMake 轨已覆盖
cstdmf/resmgr/network/entitydef 四条自下而上的链。


## 4. 引擎源码适配

- **libpng**（`lib/moo/png.cpp`）：`png/png.h` → `<png.h>`；删除私有头
  `pngstruct.h`/`pnginfo.h`；`png_ptr->io_ptr` → `png_get_io_ptr()`；
  `pngInfo->width/height/bit_depth/channels` → `png_get_image_width()`/
  `png_get_image_height()`/`png_get_bit_depth()`/`png_get_channels()`。
  自定义分配回调（`png_create_*_struct_2` + `bw_malloc/bw_free`）为公开 API，
  内存跟踪保持。
- **zlib**：5 处 `#include "zip/zlib.h"` → `<zlib.h>`。`MY_ZCALLOC`/
  `bw_zlib_mem.cpp` 随 vendored 组件构建一并移除：vcpkg zlib 为原版构建，
  zlib 内部分配不再计入 BW 内存统计（对功能无影响；`z_stream` 手工初始化
  处本就未走 `zcalloc`）。
- **sqlite**：7 处 `#include "sqlite/sqlite3.h"` → `<sqlite3.h>`；API 面
  （open/exec/prepare(_v2)/step/bind/column/backup）在 3.53 全部存在；
  磁盘格式前向兼容由 SQLite 官方保证。
- **OpenSSL 3**（低层 API 全部为 deprecated-but-present，无需 legacy
  provider——`BF_set_key`/`BF_ecb_encrypt` 为自带实现，已用 RFC 向量
  验证）：
  - 移除 3.0 中已删除的 `ERR_load_crypto_strings`/`ERR_free_strings` 调用
    （`public_key_cipher.cpp`、`elliptic_curve_checksum_scheme.cpp`；
    `ping_manager.cpp` 处本就在 `#if 0` 内）；
  - `endpoint.cpp`（Win32 清理路径）1.0 专用清理函数加
    `OPENSSL_VERSION_NUMBER < 0x10100000L` 版本门；
  - `RSA_*`/`ECDSA_*`/`SHA*_Init` 等沿用（deprecated 警告，行为不变）；
    线协议不变。
  - **`ECDSA_sign_setup` 只在私钥侧调用**（真实缺陷修复，见下）。
- **jsoncpp**：无源码改动（消费面 API 兼容）。
- **memhook**（gcc15 修复）：补 sized `operator delete(void*, size_t)`/
  `operator delete[](void*, size_t)`，转发到既有非尺寸钩子。
- **db_storage_mysql**（gcc15 + MySQL 8 客户端头，此前该目录从未在本机编译过）：
  `type_traits.hpp` 的 `return NULL;`（`basic_string(nullptr_t)` 已删除）改为
  `return BW::string();`；`database_tool_app.cpp` 两个 `unique_ptr` 成员的
  `0` 初始化改 `nullptr`（`nullptr_t`/`pointer` 两个构造函数二义）；
  `wrapper.cpp` 的 `my_bool` + `MYSQL_SECURE_AUTH` 用 `#ifdef MYSQL_SECURE_AUTH`
  版本门（MySQL 8 / MariaDB 客户端头已删除二者，且新客户端本就不支持
  4.1 前的旧口令协议）。
- **其余从未在本机编过的角落**（`all` 目标完整跑通后暴露）：
  `message_logger/metadata.cpp` 的 `long long` 赋给 `Json::Value` 在 1.9 的
  多个数值构造函数间二义，显式 `static_cast<Json::Value::Int64>`；
  `sync_db/mysql_upgrade_database.cpp` 匿名命名空间类成员的未用定义
  （`-Werror=unused-function`）标 `[[maybe_unused]]`；
  `examples/client_integration/python/simple` 的 `PyString_AsString`/
  `PyInt_*` 残留换 `PyUnicode_AsUTF8`/`PyLong_*`。

### 4.1 顺带修掉的真实缺陷

迁移把 `third_party` 的构建产物换成 vcpkg 静态库、并让单测真正跑起来之后，
暴露出三个与依赖版本无关、但会让功能直接不可用的缺陷：

1. **`EllipticCurveChecksumScheme` 的公钥（校验）路径完全失效**
   （`lib/network/elliptic_curve_checksum_scheme.cpp`）。
   构造函数无条件调用 `ECDSA_sign_setup()` 预计算签名用的 `(kinv, rp)`；该函数
   要求 `EC_KEY` 带私钥分量，传公钥必然失败（`missing private key`）。于是
   公钥分支提前 `return`，`maxSignatureSize_` 停在 0 → `streamSize()` 报 0 →
   `ChecksumIStream` 不剥离尾部签名 → 校验永远失败；`ECDSA_verify(NULL)` 还会
   直接段错误（`baseapp_test` 的崩溃栈即此）。修复：只在 `isPrivate` 时做
   预计算；`ECDSA_size()` 只依赖群，私钥/公钥一致，作为线上追加/剥离的字节数
   仍然正确，线协议不变。
2. **`watcherStreamToValue*` 少读一个 mode 字节**
   （`lib/cstdmf/watcher.hpp`、`lib/pyscript/pywatcher.cpp`）。
   `watcherValueToStream()` 写的是 `type, mode, len, value`
   （`WatcherProtocolDecoder::decodeNext()` 与
   `watcher_forwarding_collector.cpp` 都按这个格式读），但读取侧在拿到 `type`
   之后直接当 payload 读，把 `mode` 当成了长度前缀。结果：所有 watcher SET
   操作无法解流，`putPyObjectOnStream()`（Python 侧写）与服务端 callable
   watcher 读不对称。修复：新增 `watcherStreamSkipMode()`，在每个读取包装里
   消费并丢弃 mode 字节。
3. **CPython 的 `BW_Py_*` 钩子符号在解释器里是 hidden**
   （`third_party/python/Include/bwhooks.h`）。CPython 用
   `-fvisibility=hidden` 编译自身对象，`-export-dynamic` 无法导出 hidden 符号；
   3.12+ 的 `Tools/build/check_extension_modules.py` 会把导入失败的模块重命名为
   `_hashlib_failed*.so`，于是 `lib-dynload` 少一个模块、构建的模块存在性断言
   直接失败。修复：给 `BW_Py_*` 声明加
   `__attribute__((visibility("default")))`，并用 `-Wl,-u,...` 强制把
   `Python/bwhooks.o` 拉进 `python.exe`。
4. **`watcherStreamToValue(…, long&)` 在 LP64 上截断 int64**
   （`lib/cstdmf/watcher.hpp`）。LP64 平台 `int64_t` 是 `long` 的 typedef，
   int64 watcher 值经重载决议绑定到 `long&` 版本（非模板精确匹配优先于泛型
   模板），而它无条件转给 `int32&` 提取器——每个 64 位 watcher 值都被静默
   截成低 32 位（单测 `WatcherValueToStream_int64RoundTrip` 实测
   `-1122334455667788` 读回 `628319156`，恰为低 32 位）。修复：按
   `sizeof(long)` 选择 int64/int32 提取器。
5. **`snapshot_helper` 的 `lvremove` 参数是逗号表达式**
   （`server/tools/snapshot_helper/snapshot_helper.cpp`）。
   `("/dev/" + lvGroup, "/" + lvSnapshot).c_str()` 把 `"/dev/"+lvGroup`
   整个丢弃、只把 `"/"+lvSnapshot` 传给了 `lvremove`——快照卷永远删不掉。
   gcc 15 给 `operator+` 加的 `[[nodiscard]]` 把这个静默丢弃变成了编译错误，
   顺手修正为 `"/dev/" + lvGroup + "/" + lvSnapshot`。
6. **`Script::setData(…, int64&)` 成功返回却残留未捕获异常**
   （`lib/pyscript/script.cpp`）。2.7 时代的兜底分支无条件 `return 0`；LP64 上
   `long` 与 `int64` 同宽，该分支要么静默接受越界值（`2**63` 被读成
   `9223372036854775808`），要么带着未清除的 `OverflowError` 返回成功。
   泄漏的异常正是 `IntegerRangeChecker::findSameRange()` 报
   `findSameRange: PyErr_Occurred` 并中止的原因。修复：溢出时清错误并按越界
   报失败，与 `uint64` 重载的行为一致。
7. **`Script::setData(…, int&)` 完全没有做范围检查**
   （同上）。原实现是 `rInt = asLong; if (asLong == rInt) return 0;`——赋值之后
   再比较必然为真，于是**任何** `PyLong` 都被判为成功；`PyLong_AsLong` 溢出返回的
   `-1` 哨兵值也带着 `OverflowError` 一路“成功”。其后的第二个
   `PyLong_Check` 分支是死代码（第一个分支必返回），反向印证了这一点。
   修复：先判 `PyErr_Occurred()`，并要求经 `int` 往返无损才接受。
8. **`ScriptTuple::getItem` 用 `PyList_GET_SIZE` 给元组做越界检查**
   （`lib/script/py_script_tuple.ipp`）。2.7 里它是裸的 `ob_size` 字段读取，套在
   元组上恰好能用；3.13 里它变成内联函数并断言 `PyList_Check()`，于是直接
   `abort()`（`ScriptArgs_createForSingleScriptObjectIsATrap`）。
   `ScriptTuple` 恒为 `PyTuple`（`check()` 就是 `PyTuple_Check`），且同文件
   `setItem()`/`size()` 本来就用的 `PyTuple_GET_SIZE`——这是一处笔误。
9. **`MockFileProvider::FEOF()` 语义写反**
   （`server/baseapp/unit_test/test_recording.cpp`）。它返回 1 的条件是
   “**还剩**数据”，而 `ReplayTickLoader::loadFromFile()` 是在短读（已到文件尾）
   之后断言 `FEOF()` 非零——正好相反。修复：改为 `remainingLength() <= 0`。
10. **`PythonDataType::isExpression` 靠“是否以 `=` 结尾”区分 pickle**
    （`lib/entitydef/data_types/python_data_type.cpp`）。该假设对 2.7 的 cPickle
    成立，但 3.x 的 protocol 2 把 str 存成 `BINUNICODE('X')` 而非 cPickle 的
    `SHORT_BINSTRING('U')`，Base64 长度与填充随之改变，pickle 被误判成表达式，
    `createFromSection` 于是去 `eval()` Base64 文本
    （`NameError: name 'gAJdc…' is not defined`）。修复：先 Base64 解码再嗅探
    首字节（`(` = 文本协议，`0x80` = 二进制协议 PROTO 操作码）。不能只做字符集
    判断——`"543"` 本身是合法 Base64，但它在测试里的语义是表达式。
11. **`PyModuleMethodLink::init()` 把 `PyMethodDef` 放在栈上**
    （`lib/pyscript/script.cpp` + `script.hpp`）。3.x 移植时用
    `PyModule_AddFunctions()` 取代已移除的 `Py_InitModule()`，而该函数按名字字面
    意思只借用（borrowed）调用者给的 `PyMethodDef` 数组：它为每项建一个
    `builtin_function_or_method`，对象的 `m_ml` 指向那张表。原实现写成
    `PyMethodDef defs[2] = {...}`（自动存储期），`init()` 一返回表就没了，而这些
    函数对象长期活在模块字典里、并且是被 GC 跟踪的。后果是
    `Py_Finalize()` 收尾做 GC 时遍历到它们，`meth_traverse` →
    `PyCFunction_GET_CLASS` 读 `m_ml->ml_flags` 解引用已失效的栈内存 → 段错误，
    凡是用 Script 的单测（`script_test` / `entitydef_test` / `pyscript_test`）
    全部在收尾阶段崩。valgrind 直接指认：`Address 0x1ffefff8f1 is on thread 1's
    stack / 207 bytes below stack pointer`。修复：把表提升为
    `PyModuleMethodLink::defs_[2]` 成员（该类本就约定以静态/全局对象使用，
    生命周期足够）。
12. **`PyArrayDataInstance::pyCompare()` / `PyFixedDictDataInstance::pyCompare()`
    对异类型操作数直接断言**（`lib/entitydef/data_instances/array_data_instance.cpp`、
    `fixed_dict_data_instance.cpp`）。原注释声称“Python 保证两边都是本类型”，这是
    错的：当左操作数（如 `list` / `dict`）的 `__lt__` / `__eq__` 返回
    `NotImplemented` 时，Python 会调用**反射**操作 `array.__gt__(list)`，于是传进来的
    左操作数根本不是本类型。脚本里写 `someArray < 5` 就会 `abort()` 掉整个进程。
    修复：异类型时返回 -1（有序且不相等）且**不置异常**，既保住“异类型永不相等”
    的既有约定，也让调用方（如 `ScriptObject::compareTo()`、单测的转换回退路径）
    能继续走到它们自己的转换逻辑。
13. **`ScriptObject::compareTo()` 先问 `Py_LT` 再问 `Py_EQ`**
    （`lib/script/py_script_object.hpp`）。`a < b` 对 `dict` / `None` / `set` 这类
    普通类型本就未定义，会抛 `TypeError`；而 3.13 的
    `PyObject_RichCompareBool()` 出错返回 **-1**，旧代码只判 `== 1`，于是这个假异常
    被吞成“大于”，并且异常还留在现场——结果比较两个相等的 dict 也会被判失败
    （`entitydef_test` 的 FIXED_DICT 系列 24 处失败即此）。修复：先问 `Py_EQ`
    （对任何类型都有定义），只在已知不等时才问 `Py_LT`；若确实不可比较，清掉
    `TypeError` 并按“不相等”返回。
14. **单测里嵌的 Python 2 源码没跟着迁移**
    （`lib/pyscript/unit_test/test_py_output_writer.cpp`、
    `lib/entitydef/unit_test/test_stream.cpp`）。此前的 Py2 语法扫描只覆盖 `.py`
    文件，没覆盖 C++ 字符串字面量里的 Python 代码：
    `print 'x'` / `print >> sys.stderr, 'x'` 在 Py3 是 `SyntaxError`（34 处失败）；
    `9223372036854775807L` 的长整型 `L` 后缀在 Py3 已被移除（12 处失败）；
    `PYTHON_1/4/5/6` 期望的 BinaryStream 字节是 **Python 2 cPickle** 的
    protocol 2 编码（`U` = SHORT_BINSTRING、`M` = INT2），Py3 改用
    `X` = BINUNICODE、`J` = BININT，长度与内容都变（8 处失败）。修复：改成 Py3
    语法、去掉 `L`、按 `pickle.dumps(obj, protocol=2)` 重新生成期望字节
    （已用当前通过的 `PYTHON_2`/`PYTHON_3` 反向校验过生成器与引擎一致）。


## 5. 验证

（构建/测试结果见文末“验证记录”。）

## 6. 验证记录

| 日期 | 内容 | 结果 |
|---|---|---|
| 2026-09-25 | vcpkg manifest 安装（x64-linux-bw） | openssl 3.6.4 / curl 8.22 / jsoncpp / zlib / libpng 1.6.58 / sqlite3 3.53.4 全部成功 |
| 2026-09-25 | Blowfish KAT（静态 libcrypto，无 legacy provider） | 通过（`32 4e d0 fe f4 13 a2 03`） |
| 2026-09-25 | 追加 `bzip2`（`_bz2` 需要，本机无系统 bzip2 开发包） | bzip2 1.0.8 装入 vcpkg 树，CPython `--with-bz2` 生效 |
| 2026-09-25 | `make -C programming bw-unit-tests` | 22 个单测可执行文件全部编译链接通过（gcc15 `-Werror`） |
| 2026-09-25 | `make -C programming user_shouldInstallPython=1 python_install` | 59 个共享模块 + stdlib 安装完成，`_hashlib` 不再被重命名 |
| 2026-09-25 | `make -C programming bw-run-all-unit-tests` | 部分通过；`cstdmf_test` / `test_watcher.cpp` 仍有既有失败（见 §6.1） |
| 2026-09-26 | 修掉 §4.1 第 6–10 项共 5 个真实缺陷 | `baseapp_test` 由崩溃转 17/17 全通过；`script_test` 18/18 全通过；`entitydef_test` 的 PYTHON_1/2/3 转通过 |
| 2026-09-26 | 独立探针验证内嵌 CPython 本体 | 裸 `Py_Initialize` → 导入 stdlib 并持有 bound method → `Py_Finalize` **干净通过**（`/tmp/opencode/pyprobe.c`），证明 §6.2 的崩溃在大世界侧而非解释器侧 |
| 2026-09-26 | `make -C programming bw-run-all-unit-tests` | 18/21 通过；`script_test` / `entitydef_test` / `pyscript_test` 仍失败（见 §6.2） |
| 2026-09-26 | `make -C programming all` | **首次完整验证通过**（exit 0，0 错误）：`baseapp` / `serviceapp` / `cellapp` / `process_defs` / `sync_db` 等服务端二进制全部链接成功，链接行可见 `-lbwpython3.13 -lbwssl -lbwcrypto`（vcpkg 静态库）与 `-Wl,-export-dynamic` |
| 2026-09-26 | 修掉 §4.1 第 11–14 项（`PyModuleMethodLink` 栈上 `PyMethodDef`、两个 `pyCompare` 异类型断言、`compareTo` 先问 `Py_LT`、`pickler.cpp` 的 `s#`→`y#`、单测内嵌 Py2 代码与 Py2 pickle 期望字节） | **21/21 模块全绿，退出码 0**；`entitydef_test` 285 用例 0 失败、`pyscript_test` 24 用例 0 失败、`script_test` 18 用例 0 失败 |

### 6.1 最终状态：全绿

`make -C programming bw-run-all-unit-tests` **21/21 模块全部通过，退出码 0**
（`cstdmf_test` / `test_watcher.cpp` 的历史失败也已修完）。
`make -C programming all` 同样退出码 0、0 错误。

### 6.2 收尾阶段那三个崩溃是怎么定位的（过程记录）

`script_test` / `entitydef_test` / `pyscript_test` 曾在**单测全绿之后**于
`Py_Finalize()` 崩溃：

```
Py_Finalize -> finalize_modules (pylifecycle.c:1758 的 _PyGC_CollectNoFail)
            -> deduce_unreachable -> subtract_refs (gc.c:464)
            -> meth_traverse (methodobject.c:248) -> 读 Py_TYPE(op) 段错误
```

定位过程与被排除的方向：

- **不是内嵌 CPython 本身。** 独立探针（`/tmp/opencode/pyprobe.c`，直接链
  `libpython3.13.a`，`-rdynamic --export-dynamic`）跑 `Py_Initialize` -> 导入
  stdlib 并持有 bound method -> `while (PyGC_Collect() > 0);` -> `Py_Finalize`，
  **干净退出**。
- **不是自定义分配器。** 探针 2（`/tmp/opencode/pyprobe2.c`）逐字复刻
  `Script::init`（`script.cpp:419-456`）的 `PyMem_SetAllocator(PYMEM_DOMAIN_RAW)`
  + `PyObject_SetArenaAllocator`，并直接调用 libpython 里真实的 `BW_Py_*`，之后
  **同样干净退出**；不装分配器的对照组也干净。（`Script::init` 只注册了
  `ignoreAllocs*`，没注册 `mallocHook/freeHook/reallocHook`，所以 `BW_Py_*`
  全部转发到 libc，与 pymalloc 默认行为等价。）
- **不是 `PyObjectPlus` 被 GC 跟踪后泄漏。** `PyTypeObjectUtil::flags()` 返回
  `Py_TPFLAGS_DEFAULT`，不含 `Py_TPFLAGS_HAVE_GC`。
- **也不是任何 teardown 步骤。** 给 `Script::fini()` 临时加过 `BW_FINI_SKIP`
  位掩码逐项跳过（`Pickler::finalise` / `runFiniTimeJobs` /
  `s_pOurInitTimeModules` / `Watcher::fini` / 清 `__main__`）：**全部跳过后依旧
  崩溃**，只有跳过 `Py_Finalize` 才不崩。说明坏对象是在 `Script::init` 或单测
  执行期间产生的，`Py_Finalize` 的 GC 只是第一个撞上它的遍历。
- 崩溃对堆布局敏感（加一行 `printf` 就会“看起来修好”），所以 `-O2` 下 gdb 的栈帧
  不可信，`PYTHONMALLOC=debug` / `MALLOC_CHECK_=3` 也只字不吭。**决定性证据来自
  valgrind**（`apt-get install valgrind`）：`PyCFunction_GET_CLASS`
  （`methodobject.h:61`）读的地址 `is on thread 1's stack / 207 bytes below stack
  pointer` —— 即 `PyCFunctionObject::m_ml` 指向一段**已失效的栈内存**。顺着
  `m_ml` 就找到了 §4.1 第 11 项的 `PyMethodDef defs[2]`。

顺带确认了一件相关的事：`third_party_python.mak` 的 `libbwpython3.13` 目标**不依赖
CPython 源码**，所以 vendored 源码变了 make 也察觉不到（实测
`make -C programming libbwpython3.13` 直接 “Nothing to be done”）。本次没有因此
踩坑（增量 `make` 显示只有 `Python/gc.c` 一个 .o 过期，且重建后行为不变），
但这是干净 clone 之外的复现隐患，值得后续给该目标补上源码依赖。
