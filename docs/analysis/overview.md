# 总体画像

## 一句话判断

BigWorld 开源版不是一个“基于 Python 的项目”，而是一个以 `C/C++` 为主、以嵌入式 `Python 2.x` 为脚本层和扩展层的大型 MMO 引擎代码仓库。

## 仓库层级

从根目录观察，项目的高价值内容主要集中在以下区域：

- `programming/bigworld`
  BigWorld 主体源码，包含客户端、服务端、编辑器工具、库、第三方依赖和构建系统。
- `programming/fantasydemo`
  示例内容与部分 Web 集成示例，偏演示和资源侧。
- `docs/pdf`
  现有 PDF 文档，包括安装、构建和技术白皮书。

## 技术栈现状

结合源码与构建脚本，当前技术画像如下：

- 主要语言：`C++`、`C`、`Python`
- 平台假设：`CentOS 7`、`Windows + Visual Studio`、部分 `WSL2/Vagrant/Docker`
- 构建体系：`GNU Make`、`CMake 2.8.x`、部分批处理脚本与 shell 脚本
- Python 模式：内嵌解释器 + 自编译 `libpython` + 自带共享模块与第三方 Python 包

## 项目规模特征

基于目录扫描可得到以下事实：

- `programming/bigworld/lib` 下有 `57` 个一级库目录。
- `programming/bigworld/server` 下有 `12` 个一级服务端目录。
- `programming/bigworld/server/tools` 下有 `10` 个一级工具目录。
- `programming/bigworld/third_party` 下有 `24` 个一级第三方目录。
- 排除 `third_party` 与内部测试脚本后，首方 Python 脚本约 `37` 个。

这说明：

- Python 业务脚本量并不大。
- 真正的复杂度主要在 C/C++ 与 Python C API 的耦合层。
- 迁移难点不是 `.py` 文件改语法，而是运行时边界、扩展模块和构建链路。

## 架构判断

从职责上，这个仓库大体可以分成五层：

1. 引擎基础库层
2. 服务端进程层
3. 客户端与编辑器层
4. 脚本与嵌入式 Python 层
5. 平台构建与第三方依赖层

这五层里，现代化改造最敏感的是第 4 层和第 5 层。

## 当前文档缺口

原仓库文档主要依赖 PDF，缺少：

- 可导航的架构文档
- 面向维护者的目录职责说明
- Python 升级的技术债清单
- 现代化迁移的分阶段策略

本 VitePress 站点就是用来补这个缺口。
