# Python C API Migration Guide

> **English** | [中文](python-c-api-guide.md)

<div class="arch-hero">

> **Status (2026-10-13 audit)**: the migration is complete (the embedded
> interpreter is now Python 3.13.16). In the end **no** compatibility
> wrapper layer was created — the compatibility files such as
> `bw_python_compat.hpp` mentioned in this page's migration steps do not
> exist in the repository; the actual approach was point-by-point direct
> replacement (Py2 symbols survive only in comments). This page is kept as
> an API-mapping reference.

BigWorld makes heavy use of the Python 2 C API, and migrating to Python 3
requires systematic replacement. This document provides the complete API
mapping and migration strategy.

</div>

## Core API changes

### String API

Python 3 uses Unicode strings uniformly, with a separate bytes type.

| Python 2 API | Python 3 API | Notes |
|--------------|--------------|------|
| `PyString_FromString()` | `PyUnicode_FromString()` | Create a string |
| `PyString_AsString()` | `PyUnicode_AsUTF8()` | Get a C string |
| `PyString_Size()` | `PyUnicode_GetLength()` | Get the length |
| `PyString_Concat()` | `PyUnicode_Concat()` | Concatenate strings |
| `PyString_Format()` | `PyUnicode_Format()` | Format |

**Migration strategy**:

```cpp
// Python 2
PyObject* name = PyString_FromString("player");

// Python 3
PyObject* name = PyUnicode_FromString("player");
```

### Integer API

Python 3 uses the `PyLong` type uniformly.

| Python 2 API | Python 3 API | Notes |
|--------------|--------------|------|
| `PyInt_FromLong()` | `PyLong_FromLong()` | Create an integer |
| `PyInt_AsLong()` | `PyLong_AsLong()` | Convert to C long |
| `PyInt_Check()` | `PyLong_Check()` | Type check |

**Migration strategy**:

```cpp
// Python 2
PyObject* id = PyInt_FromLong(12345);

// Python 3
PyObject* id = PyLong_FromLong(12345);
```

### Module initialization

Python 3 uses a new module-initialization mechanism.

| Python 2 API | Python 3 API | Notes |
|--------------|--------------|------|
| `Py_InitModule()` | `PyModule_Create()` | Create a module |
| `Py_InitModule3()` | `PyModule_Create2()` | Create a module (with state) |

**Migration strategy**:

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

### Exception handling

| Python 2 API | Python 3 API | Notes |
|--------------|--------------|------|
| `PyErr_Format()` | `PyErr_Format()` | No change |
| `PyErr_SetString()` | `PyErr_SetString()` | No change |
| `PyErr_Occurred()` | `PyErr_Occurred()` | No change |

### Pickle serialization

| Python 2 API | Python 3 API | Notes |
|--------------|--------------|------|
| `cPickle.dumps()` | `pickle.dumps()` | Serialize |
| `cPickle.loads()` | `pickle.loads()` | Deserialize |

**Migration strategy**:

```cpp
// Python 2
PyObject* pickle = PyImport_ImportModule("cPickle");

// Python 3
PyObject* pickle = PyImport_ImportModule("pickle");
```

## Compatibility-layer design

### Option 1: macros

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

### Option 2: inline functions

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

**Option 2 is recommended**, because:
- it is type safe;
- it is debug friendly;
- null-pointer checks can be added.

## Migration steps

### Step 1: build the compatibility layer

1. Create `bw_python_compat.hpp` under `lib/pyscript/`
2. Create `bw_python_compat.hpp` under `lib/script/`
3. Funnel all Python API calls through it

### Step 2: replace module by module

Replace in dependency order:

1. `lib/cstdmf` — foundation utilities
2. `lib/network` — network stack
3. `lib/entitydef` — entity definitions
4. `lib/pyscript` — Python bridge
5. `server/*` — server processes
6. `client/*` — client
7. `tools/*` — tools

### Step 3: test and validate

After each module replacement:
1. It compiles
2. Unit tests pass
3. Integration tests pass

## Common pitfalls

### 1. String encoding

Python 2 strings are bytes; Python 3 strings are Unicode.

```cpp
// Wrong: assuming the string is bytes
char* data = PyString_AsString(obj);

// Right: make the encoding explicit
const char* utf8 = PyUnicode_AsUTF8(obj);
```

### 2. Integer overflow

Python 3 integers are unbounded; C long has a limit.

```cpp
// Wrong: no overflow check
long id = PyLong_AsLong(obj);

// Right: check for overflow
if (PyErr_Occurred()) {
    return NULL;
}
long id = PyLong_AsLong(obj);
```

### 3. Module state

Python 3 supports module state; Python 2 does not.

```cpp
// Python 3 module state
typedef struct {
    int initialized;
    PyObject* config;
} BigWorldModuleState;
```

## References

- [Python 3 migration guide](https://docs.python.org/3/howto/cporting.html)
- [Python C API reference](https://docs.python.org/3/c-api/)
- [BigWorld Python binding source](/analysis/source-code-guide)
