# Python C API 迁移指南

<div class="arch-hero">

BigWorld 大量使用 Python 2 C API，迁移到 Python 3 需要系统性替换。本文档提供完整的 API 映射和迁移策略。

</div>

## 核心 API 变更

### 字符串 API

Python 3 统一使用 Unicode 字符串，bytes 类型独立。

| Python 2 API | Python 3 API | 说明 |
|--------------|--------------|------|
| `PyString_FromString()` | `PyUnicode_FromString()` | 创建字符串 |
| `PyString_AsString()` | `PyUnicode_AsUTF8()` | 获取 C 字符串 |
| `PyString_Size()` | `PyUnicode_GetLength()` | 获取长度 |
| `PyString_Concat()` | `PyUnicode_Concat()` | 拼接字符串 |
| `PyString_Format()` | `PyUnicode_Format()` | 格式化 |

**迁移策略**:

```cpp
// Python 2
PyObject* name = PyString_FromString("player");

// Python 3
PyObject* name = PyUnicode_FromString("player");
```

### 整数 API

Python 3 统一使用 `PyLong` 类型。

| Python 2 API | Python 3 API | 说明 |
|--------------|--------------|------|
| `PyInt_FromLong()` | `PyLong_FromLong()` | 创建整数 |
| `PyInt_AsLong()` | `PyLong_AsLong()` | 转换为 C long |
| `PyInt_Check()` | `PyLong_Check()` | 类型检查 |

**迁移策略**:

```cpp
// Python 2
PyObject* id = PyInt_FromLong(12345);

// Python 3
PyObject* id = PyLong_FromLong(12345);
```

### 模块初始化

Python 3 使用新的模块初始化机制。

| Python 2 API | Python 3 API | 说明 |
|--------------|--------------|------|
| `Py_InitModule()` | `PyModule_Create()` | 创建模块 |
| `Py_InitModule3()` | `PyModule_Create2()` | 创建模块 (带状态) |

**迁移策略**:

```cpp
// Python 2
static PyMethodDef methods[] = {
    {"getEntity", getEntity, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}
};
Py_InitModule("BigWorld", methods);

// Python 3
static PyMethodDef methods[] = {
    {"getEntity", getEntity, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}
};
static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT,
    "BigWorld",
    NULL,
    -1,
    methods
};
PyModule_Create(&module);
```

### 异常处理

| Python 2 API | Python 3 API | 说明 |
|--------------|--------------|------|
| `PyErr_Format()` | `PyErr_Format()` | 无变化 |
| `PyErr_SetString()` | `PyErr_SetString()` | 无变化 |
| `PyErr_Occurred()` | `PyErr_Occurred()` | 无变化 |

### Pickle 序列化

| Python 2 API | Python 3 API | 说明 |
|--------------|--------------|------|
| `cPickle.dumps()` | `pickle.dumps()` | 序列化 |
| `cPickle.loads()` | `pickle.loads()` | 反序列化 |

**迁移策略**:

```cpp
// Python 2
PyObject* pickle = PyImport_ImportModule("cPickle");

// Python 3
PyObject* pickle = PyImport_ImportModule("pickle");
```

## 兼容层设计

### 方案一：宏定义

```cpp
// bw_python_compat.h
#if PY_MAJOR_VERSION >= 3
  #define BW_PY_STRING_FROM_STRING PyUnicode_FromString
  #define BW_PY_STRING_AS_STRING  PyUnicode_AsUTF8
  #define BW_PY_INT_FROM_LONG     PyLong_FromLong
  #define BW_PY_INT_AS_LONG       PyLong_AsLong
#else
  #define BW_PY_STRING_FROM_STRING PyString_FromString
  #define BW_PY_STRING_AS_STRING  PyString_AsString
  #define BW_PY_INT_FROM_LONG     PyInt_FromLong
  #define BW_PY_INT_AS_LONG       PyInt_AsLong
#endif
```

### 方案二：内联函数

```cpp
// bw_python_compat.hpp
namespace BW {

inline PyObject* pyStringFromString(const char* v) {
#if PY_MAJOR_VERSION >= 3
    return PyUnicode_FromString(v);
#else
    return PyString_FromString(v);
#endif
}

inline const char* pyStringAsString(PyObject* o) {
#if PY_MAJOR_VERSION >= 3
    return PyUnicode_AsUTF8(o);
#else
    return PyString_AsString(o);
#endif
}

} // namespace BW
```

**推荐方案二**，原因：
- 类型安全
- 调试友好
- 可以添加空指针检查

## 迁移步骤

### 第一步：建立兼容层

1. 在 `lib/pyscript/` 创建 `bw_python_compat.hpp`
2. 在 `lib/script/` 创建 `bw_python_compat.hpp`
3. 统一收口所有 Python API 调用

### 第二步：逐模块替换

按依赖顺序替换：

1. `lib/cstdmf` - 基础工具
2. `lib/network` - 网络栈
3. `lib/entitydef` - 实体定义
4. `lib/pyscript` - Python 桥接
5. `server/*` - 服务端进程
6. `client/*` - 客户端
7. `tools/*` - 工具

### 第三步：测试验证

每个模块替换后：
1. 编译通过
2. 单元测试通过
3. 集成测试通过

## 常见陷阱

### 1. 字符串编码

Python 2 字符串是 bytes，Python 3 字符串是 Unicode。

```cpp
// 错误：假设字符串是 bytes
char* data = PyString_AsString(obj);

// 正确：明确编码
const char* utf8 = PyUnicode_AsUTF8(obj);
```

### 2. 整数溢出

Python 3 整数无上限，C long 有上限。

```cpp
// 错误：不检查溢出
long id = PyLong_AsLong(obj);

// 正确：检查溢出
if (PyErr_Occurred()) {
    return NULL;
}
long id = PyLong_AsLong(obj);
```

### 3. 模块状态

Python 3 支持模块状态，Python 2 不支持。

```cpp
// Python 3 模块状态
typedef struct {
    int initialized;
    PyObject* config;
} BigWorldModuleState;
```

## 参考资料

- [Python 3 迁移指南](https://docs.python.org/3/howto/cporting.html)
- [Python C API 参考](https://docs.python.org/3/c-api/)
- [BigWorld Python 绑定源码](/analysis/source-code-guide)
