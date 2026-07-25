# 第三方依赖升级清单

<div class="arch-hero">

BigWorld 内嵌了大量第三方库，版本普遍老旧。本文档盘点当前依赖状态，提供升级建议。

</div>

## 依赖现状

### Python 相关

| 依赖 | 当前版本 | Python 3 兼容 | 建议 |
|------|----------|---------------|------|
| Python | 2.7.x | ❌ | 升级到 3.12 |
| SQLAlchemy | 0.6.6 | ❌ | 升级到 2.0+ |
| oursql | 0.9.2 | ❌ | 替换为 mysqlclient |
| Pympler | 0.3.0 | ❌ | 升级到 1.0+ |
| pika | 0.9.13 | ❌ | 升级到 1.0+ |

### C/C++ 依赖

| 依赖 | 当前版本 | 状态 | 建议 |
|------|----------|------|------|
| OpenSSL | 1.0.x | ⚠️ EOL | 升级到 3.x |
| curl | 7.x | ✅ | 保持或升级 |
| SQLite | 3.x | ✅ | 保持 |
| MongoDB C Driver | 旧版 | ⚠️ | 评估是否需要 |
| jsoncpp | 旧版 | ✅ | 可选升级 |
| re2 | 旧版 | ✅ | 可选升级 |
| recastnavigation | 旧版 | ✅ | 保持 |

### 构建工具

| 工具 | 当前版本 | 建议 |
|------|----------|------|
| CMake | 2.8.12 | 升级到 3.20+ |
| GCC | 4.8.x | 升级到 11+ |
| Make | GNU Make | 保持 |

## 升级优先级

### P0 - 必须升级

1. **Python 2.7 → 3.12**
   - 影响范围：全仓库
   - 详见 [Python 3.12 路线图](/migration/python-3-12)

2. **OpenSSL 1.0 → 3.x**
   - 原因：安全漏洞，不再维护
   - 影响：网络加密、证书处理
   - 风险：API 变更较大

### P1 - 建议升级

3. **SQLAlchemy 0.6 → 2.0**
   - 原因：Python 3 兼容
   - 影响：数据库操作
   - 风险：API 变更

4. **CMake 2.8 → 3.20**
   - 原因：现代 C++ 支持
   - 影响：构建系统
   - 风险：低

5. **GCC 4.8 → 11**
   - 原因：C++17/20 支持
   - 影响：编译器
   - 风险：可能引入新警告

### P2 - 可选升级

6. **curl、SQLite、jsoncpp**
   - 原因：功能更新
   - 影响：低
   - 风险：低

## 替换方案

### oursql → mysqlclient

**原因**: oursql 不再维护，mysqlclient 是 MySQL 官方推荐。

```python
# 旧代码
import oursql
conn = oursql.connect(host='localhost', user='root', passwd='')

# 新代码
import mysql.connector
conn = mysql.connector.connect(host='localhost', user='root', password='')
```

### SQLAlchemy 0.6 → 2.0

**主要变更**:
- Session API 变更
- 查询语法变更
- 类型系统变更

```python
# 旧代码 (0.6)
session.query(User).filter_by(name='Alice').first()

# 新代码 (2.0)
stmt = select(User).where(User.name == 'Alice')
result = session.execute(stmt).first()
```

### OpenSSL 1.0 → 3.x

**主要变更**:
- 废弃旧 API
- 新的 Provider 机制
- 默认安全级别提高

```cpp
// 旧代码
SSL_CTX_new(SSLv23_method());

// 新代码
SSL_CTX_new(TLS_method());
```

## 兼容性矩阵

| 依赖 | Python 2.7 | Python 3.8 | Python 3.12 |
|------|------------|------------|-------------|
| SQLAlchemy 0.6 | ✅ | ❌ | ❌ |
| SQLAlchemy 2.0 | ❌ | ✅ | ✅ |
| oursql 0.9 | ✅ | ❌ | ❌ |
| mysqlclient | ✅ | ✅ | ✅ |
| OpenSSL 1.0 | ✅ | ⚠️ | ❌ |
| OpenSSL 3.x | ⚠️ | ✅ | ✅ |

## 迁移策略

### 阶段一：评估

1. 扫描源码，确认每个依赖的实际使用点
2. 评估替换成本
3. 确定升级顺序

### 阶段二：替换

按依赖顺序替换：

1. Python 解释器
2. Python 标准库模块
3. Python 第三方包
4. C/C++ 依赖

### 阶段三：验证

1. 编译测试
2. 单元测试
3. 集成测试
4. 性能测试

## 风险提示

::: warning 注意
- 不要同时升级多个依赖
- 每次升级后充分测试
- 保留旧版本回退能力
:::

## 参考资料

- [Python 3.12 路线图](/migration/python-3-12)
- [构建系统现代化](/migration/build-system-modernization)
- [技术债与风险](/analysis/risks)
