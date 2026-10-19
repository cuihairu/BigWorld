#ifndef BW_PYTHON_HOOKS_HPP
#define BW_PYTHON_HOOKS_HPP

#ifdef __cplusplus
extern "C" {
#endif

typedef void * ( * BW_Py_MallocFunc )( size_t );
typedef void * ( * BW_Py_CallocFunc )( size_t, size_t );
typedef void ( * BW_Py_FreeFunc)( void * );
typedef void * ( * BW_Py_ReallocFunc)( void * , size_t );
typedef void ( * BW_Py_IgnoreAllocsFunc)( void );

typedef struct
{
	BW_Py_MallocFunc mallocHook;
	BW_Py_FreeFunc freeHook;
	BW_Py_ReallocFunc reallocHook;
	BW_Py_IgnoreAllocsFunc ignoreAllocsBeginHook;
	BW_Py_IgnoreAllocsFunc ignoreAllocsEndHook;
} BW_Py_Hooks;

/* BIGWORLD_BEGIN
 * These entry points have to be visible in the dynamic symbol table of the
 * program that embeds libpython, because _hashlib.so is a separately loaded
 * extension that calls BW_Py_memoryTrackingIgnoreBegin/End and has to bind
 * them against the process image.
 *
 * CPython compiles its own objects with -fvisibility=hidden, which turns these
 * into STB_LOCAL symbols, and -export-dynamic cannot export a hidden symbol -
 * so _hashlib failed to import ("undefined symbol: BW_Py_memoryTrackingIgnoreBegin")
 * and 3.12+'s Tools/build/check_extension_modules.py renamed it to
 * _hashlib_failed...so. Marking them default visibility restores the 2.7
 * behaviour, where the hooks lived in the interpreter image where any module
 * could reach them.
 *
 * The Windows port has the same shape: PCbuild extension modules
 * (_hashlib.pyd et al) link only python313.lib, so the hooks have to be
 * exported from python313.dll itself. PCbuild/Directory.Build.targets compiles
 * bwhooks.c into pythoncore with BW_PY_BUILDING_PYTHONCORE defined, which
 * makes BW_PY_EXPORT dllexport here and lands every entry point in
 * python313.lib. Consumers that see neither macro get plain declarations,
 * unchanged. */
#if defined( __GNUC__ ) && !defined( _WIN32 )
#define BW_PY_EXPORT __attribute__( ( visibility( "default" ) ) )
#elif defined( _MSC_VER ) && defined( BW_PY_BUILDING_PYTHONCORE )
#define BW_PY_EXPORT __declspec( dllexport )
#else
#define BW_PY_EXPORT
#endif
/* BIGWORLD_END */

/* BIGWORLD_BEGIN
 * BW_Py_calloc is new in the Python 3.13 port: PyMem_SetAllocator's
 * PyMemAllocatorEx requires a calloc entry point which the 2.7 hook
 * interface did not have. */
BW_PY_EXPORT void* BW_Py_calloc( size_t nelem, size_t elsize );
/* BIGWORLD_END */

BW_PY_EXPORT void* BW_Py_malloc( size_t size );
BW_PY_EXPORT void BW_Py_free( void * mem );
BW_PY_EXPORT void* BW_Py_realloc( void * mem, size_t size );

BW_PY_EXPORT void BW_Py_memoryTrackingIgnoreBegin( void );
BW_PY_EXPORT void BW_Py_memoryTrackingIgnoreEnd( void );

BW_PY_EXPORT void BW_Py_setHooks( BW_Py_Hooks * hooks );
BW_PY_EXPORT void BW_Py_getHooks( BW_Py_Hooks * hooks );

#ifdef __cplusplus
} /* extern "C" */
#endif


#endif /* BW_PYTHON_HOOKS_HPP */
