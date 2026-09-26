#include "pch.hpp"

#include "script.hpp"

#include "cstdmf/bw_memory.hpp"
#if !BWCLIENT_AS_PYTHON_MODULE && (!defined( _WIN64 ) && !defined( _WIN32 ))
#include "cstdmf/build_config.hpp"
#endif
#include "cstdmf/bgtask_manager.hpp"
#include "cstdmf/bw_platform_info.hpp"
#include "cstdmf/bw_util.hpp"
#include "cstdmf/concurrency.hpp"
#include "cstdmf/debug.hpp"
#include "cstdmf/guard.hpp"
#include "cstdmf/profiler.hpp"
#include "cstdmf/slot_tracker.hpp"
#include "cstdmf/watcher.hpp"

#include "math/vector2.hpp"
#include "math/vector3.hpp"
#include "math/vector4.hpp"

#include "network/basictypes.hpp"

#include "resmgr/bwresource.hpp"
#include "resmgr/file_system.hpp"
#include "resmgr/multi_file_system.hpp"

#include "entitydef/constants.hpp"

#include "script/py_script_output_writer.hpp"

#include "pickler.hpp"
#include "py_import_paths.hpp"
#include "script_math.hpp"
#include "cstdmf/string_builder.hpp"

#if !defined(PYMODULE)
#include "bwhooks.h"
#endif

#include <stdarg.h>

#include "frameobject.h"
#include "osdefs.h" // Needed for DELIM

DECLARE_DEBUG_COMPONENT2( "Script", 0 )


#ifndef _WIN32
	#define _stricmp strcasecmp
#endif


BW_BEGIN_NAMESPACE

#ifndef CODE_INLINE
#include "script.ipp"
#endif

extern int PyLogging_token;
extern int PyWatcher_token;
extern int PyDebugMessageFileLogger_token;

int PyScript_token = PyLogging_token
	| PyWatcher_token
	| PyDebugMessageFileLogger_token
;


MEMTRACKER_DECLARE( Script_ask, "Script::ask", 0 );

namespace Script
{
	const int MAX_ARGS = 20;
	int g_scriptArgc = 0;
	char* g_scriptArgv[ MAX_ARGS ];
}

namespace
{

// -----------------------------------------------------------------------------
// Section: Profiling and Stack tracking
// -----------------------------------------------------------------------------

static PyObject * s_pOurInitTimeModules;
static PyThreadState * s_pMainThreadState;
static THREADLOCAL( PyThreadState * ) s_defaultContext;

static bool s_isFinalised = false;
static bool s_isInitalised = false;
void NoOp() {}

#if ENABLE_STACK_TRACKER
class PythonStackTraker
{
public:
	static inline void pushStack( PyFrameObject * pFrame )
	{
		BW_GUARD_DISABLED;

		/* BIGWORLD_BEGIN(3.13 migration)
		 * PyFrameObject fields are no longer directly accessible; use the
		 * accessor functions and PyUnicode for code object names.
		 */
		PyCodeObject *code = PyFrame_GetCode( pFrame );

		const char * name = PyUnicode_AsUTF8( code->co_name );
		const char * filename = PyUnicode_AsUTF8( code->co_filename );
		const int line = PyCode_Addr2Line( code, PyFrame_GetLasti( pFrame ) );
		/* BIGWORLD_END */

		StackTracker::push( name, filename, line, true );
	}


	static inline void updateStackTop( PyFrameObject * pFrame )
	{
		BW_GUARD_DISABLED;

		if (!pFrame)
		{
			return;
		}

		uint stackSize = StackTracker::stackSize();
		if (stackSize > 0)
		{
			StackTracker::StackItem & top = StackTracker::getStackItem( 0 );

			/* BIGWORLD_BEGIN(3.13 migration) */
			PyCodeObject * code = PyFrame_GetCode( pFrame );
			const char * name = PyUnicode_AsUTF8( code->co_name );

			if (top.name != NULL && strcmp( top.name, name ) == 0)
			{
				const int line = PyCode_Addr2Line( code,
					PyFrame_GetLasti( pFrame ) );
				top.line = line;
			}
			/* BIGWORLD_END */
		}
	}

	static void handleStack( PyFrameObject * pFrame, int action  )
	{
		BW_GUARD_DISABLED;

		switch (action)
		{
		case PyTrace_CALL:
			/* BIGWORLD_BEGIN(3.13 migration)
			 * PyFrame_GetBack returns a strong reference.
			 */
			{
				PyFrameObject * pBack = PyFrame_GetBack( pFrame );
				updateStackTop( pBack );
				Py_XDECREF( pBack );
			}
			/* BIGWORLD_END */
			pushStack( pFrame );

			break;

		case PyTrace_RETURN:
			StackTracker::pop();
			break;

		case PyTrace_C_CALL:
			updateStackTop( pFrame );
			break;

		case PyTrace_C_RETURN:
			break;

		case PyTrace_C_EXCEPTION:
			break;
		}
	}
};
#endif /* ENABLE_STACK_TRACKER */

#if ENABLE_PROFILER
class PythonProfiler
{
public:
	static void handleStack( PyFrameObject * pFrame, int action, PyObject * pArg )
	{
		BW_GUARD_DISABLED;
		/* BIGWORLD_BEGIN(3.13 migration) */
		PyCodeObject * pCode = pFrame ? PyFrame_GetCode( pFrame ) : NULL;
		const char * codeName =
			pCode ? PyUnicode_AsUTF8( pCode->co_name ) : NULL;
		/* BIGWORLD_END */
		switch (action)
		{
		case PyTrace_CALL:
			g_profiler.addEntry( codeName,
				Profiler::EVENT_START, 0, Profiler::CATEGORY_PYTHON );
			break;
		case PyTrace_RETURN:
			g_profiler.addEntry( codeName,
				Profiler::EVENT_END, 0, Profiler::CATEGORY_PYTHON );
			break;
		case PyTrace_C_CALL:
			if (pArg && PyCFunction_Check( pArg ))
			{
				const char * functionName = ((PyCFunctionObject*)pArg)->m_ml->ml_name;
				g_profiler.addEntry( functionName, Profiler::EVENT_START, 0, Profiler::CATEGORY_PYTHON );
			}
			break;
		case PyTrace_C_RETURN:
		case PyTrace_C_EXCEPTION:
			if (pArg && PyCFunction_Check( pArg ))
			{
				const char * functionName = ((PyCFunctionObject*)pArg)->m_ml->ml_name;
				g_profiler.addEntry( functionName, Profiler::EVENT_END, 0, Profiler::CATEGORY_PYTHON );
			}
			break;
		}
	}
};
#endif /* ENABLE_PROFILER */


static int profileFunc( PyObject * /*pObject*/, PyFrameObject * pFrame,
	int traceType, PyObject * pArg )
{
	BW_GUARD_DISABLED;

	MF_ASSERT( pFrame );

#if ENABLE_STACK_TRACKER
	PythonStackTraker::handleStack( pFrame, traceType );
#endif /* ENABLE_STACK_TRACKER */

#if ENABLE_PROFILER
	PythonProfiler::handleStack( pFrame, traceType, pArg );
#endif /* ENABLE_PROFILER */

	return 0;
}


void __onThreadStart( const BW::string & threadname )
{
#if ENABLE_PROFILER
	g_profiler.addThisThread( threadname.c_str() );
#endif
#if ENABLE_STACK_TRACKER || ENABLE_PROFILER
	PyEval_SetProfile( &profileFunc, NULL );
#endif
}
PY_AUTO_MODULE_FUNCTION( RETVOID, __onThreadStart, ARG( BW::string, END ), BigWorld )


void __onThreadEnd()
{
#if ENABLE_PROFILER
	g_profiler.removeThread( OurThreadID() );
#endif
#if ENABLE_STACK_TRACKER || ENABLE_PROFILER
	PyEval_SetProfile( NULL, NULL );
#endif
}
PY_AUTO_MODULE_FUNCTION( RETVOID, __onThreadEnd, END, BigWorld )

}; // anonymous namespace


// -----------------------------------------------------------------------------
// Section: Script class methods
// -----------------------------------------------------------------------------

/*
 *  Wrap Python's memory allocations
 */

MEMTRACKER_DECLARE( Script_Python, "Script - Python", 0 );

namespace Python_Memhooks
{
	void* malloc( size_t size )
	{
		// Ignoring python leaks for now, until all other leaks are 
		// resolved.
		BW::Allocator::allocTrackingIgnoreBegin();

		MEMTRACKER_SCOPED( Script_Python );
		void* ptr = bw_malloc( size );

		BW::Allocator::allocTrackingIgnoreEnd();
		return ptr;
	}

	void free( void* mem )
	{
		MEMTRACKER_SCOPED( Script_Python );
		bw_free( mem );
	}

	void* realloc( void* mem, size_t size )
	{
		// Ignoring python leaks for now, until all other leaks are
		// resolved.
		BW::Allocator::allocTrackingIgnoreBegin();

		MEMTRACKER_SCOPED( Script_Python );
		void* ptr = bw_realloc( mem, size );

		BW::Allocator::allocTrackingIgnoreEnd();

		return ptr;
	}
}


#if !defined(PYMODULE)
/* BIGWORLD_BEGIN(3.13 migration)
 * Context-free adapters matching PyObjectArenaAllocator's signature. The
 * arena is carved out of one big block, so plain BW_Py_malloc/BW_Py_free
 * (which forward to libc when no hook is registered) are all that is
 * needed; the ctx pointer is unused.
 */
static void * bwPyArenaAlloc( void * /*ctx*/, size_t size )
{
	return BW_Py_malloc( size );
}

static void bwPyArenaFree( void * /*ctx*/, void * ptr, size_t /*size*/ )
{
	BW_Py_free( ptr );
}

/* BIGWORLD_BEGIN(3.13 migration)
 * PyMemAllocatorEx entry points take a leading context pointer; wrap the
 * BWHooks allocators to match.
 */
static void * bwPyRawMalloc( void * /*ctx*/, size_t size )
{
	return BW_Py_malloc( size );
}

static void * bwPyRawCalloc( void * /*ctx*/, size_t nelem, size_t elsize )
{
	return BW_Py_calloc( nelem, elsize );
}

static void * bwPyRawRealloc( void * /*ctx*/, void * ptr, size_t newSize )
{
	return BW_Py_realloc( ptr, newSize );
}

static void bwPyRawFree( void * /*ctx*/, void * ptr )
{
	BW_Py_free( ptr );
}
/* BIGWORLD_END */
#endif // !defined(PYMODULE)


/**
 *	This method prints out the current Python callstack as debug messages.
 *	Based on:
 *	http://stackoverflow.com/questions/1796510/accessing-a-python-traceback-from-the-c-api
 */
void Script::printStack()
{
	BW_GUARD;

	const PyThreadState * tstate = PyThreadState_GET();

	/* BIGWORLD_BEGIN(3.13 migration)
	 * PyThreadState.frame and PyFrameObject fields are no longer exposed:
	 * walk the stack with PyThreadState_GetFrame/PyFrame_GetBack and read
	 * attributes through the accessors. The frame references returned are
	 * strong references and must be released.
	 */
	PyFrameObject * frame = tstate ? PyThreadState_GetFrame(
		const_cast< PyThreadState * >( tstate ) ) : NULL;

	if (frame != NULL)
	{
		DEBUG_MSG( "Python stack trace:\n" );
		while (frame != NULL)
		{
			const int line = PyFrame_GetLineNumber( frame );
			PyCodeObject * pCode = PyFrame_GetCode( frame );
			const char * filename =
				PyUnicode_AsUTF8( pCode->co_filename );
			const char * funcname =
				PyUnicode_AsUTF8( pCode->co_name );

			DEBUG_MSG( "    %s(%d): %s\n", filename, line, funcname );

			PyFrameObject * pBack = PyFrame_GetBack( frame );
			Py_DECREF( frame );
			Py_XDECREF( pCode );
			frame = pBack;
		}
	}
	/* BIGWORLD_END */
}


/**
 *	This static method initialises the scripting system.
 *	The paths must be separated with semicolons.
 */
bool Script::init( const PyImportPaths & appPaths, const char * componentName )
{
	if (s_isInitalised)
	{
		return true;
	}


#if !defined(PYMODULE)
	// Assign the python memory hooks
	{
		BW_Py_Hooks pythonHooks;
		bw_zero_memory( &pythonHooks, sizeof( pythonHooks ) );

		//pythonHooks.mallocHook = Python_Memhooks::malloc;
		//pythonHooks.freeHook = Python_Memhooks::free;
		//pythonHooks.reallocHook = Python_Memhooks::realloc;
		pythonHooks.ignoreAllocsBeginHook =
			BW::Allocator::allocTrackingIgnoreBegin;
		pythonHooks.ignoreAllocsEndHook =
			BW::Allocator::allocTrackingIgnoreEnd;

		BW_Py_setHooks( &pythonHooks );

		/* BIGWORLD_BEGIN(3.13 migration)
		 * Route CPython's raw allocator and the pymalloc arenas through the
		 * BWHooks layer using the official embedding interfaces. This
		 * replaces the 2.7 #define malloc/realloc/free redirection inside
		 * obmalloc.c/pymem.h. Both must be installed before
		 * Py_Initialize(). When no malloc hook is registered these forward
		 * to libc exactly like the unhooked 2.7 build did.
		 */
		PyMemAllocatorEx rawAllocator;
		memset( &rawAllocator, 0, sizeof( rawAllocator ) );
		rawAllocator.malloc = bwPyRawMalloc;
		rawAllocator.calloc = bwPyRawCalloc;
		rawAllocator.realloc = bwPyRawRealloc;
		rawAllocator.free = bwPyRawFree;
		PyMem_SetAllocator( PYMEM_DOMAIN_RAW, &rawAllocator );

		PyObjectArenaAllocator arenaAllocator;
		memset( &arenaAllocator, 0, sizeof( arenaAllocator ) );
		arenaAllocator.alloc = bwPyArenaAlloc;
		arenaAllocator.free = bwPyArenaFree;
		PyObject_SetArenaAllocator( &arenaAllocator );
		/* BIGWORLD_END */
	}
#endif

	// Build the list of python code file paths relative to our IFileSystem
	PyImportPaths sysPaths( DELIM );

	sysPaths.append( appPaths );

#if !BWCLIENT_AS_PYTHON_MODULE
	// Add extra paths to the input ones
	BW::string commonPath( EntityDef::Constants::commonPath() );
	sysPaths.addResPath( commonPath );
	sysPaths.addResPath( commonPath + "/Lib" );
#if defined( _WIN64 ) || defined( _WIN32 )
	BW::string clientDLLsPath( EntityDef::Constants::entitiesClientPath() );
	clientDLLsPath += "/DLLs/";
	clientDLLsPath += BW::PlatformInfo::str();
#else
	BW::string serverCommonPath( EntityDef::Constants::serverCommonPath() );
	serverCommonPath += "/lib-dynload-";
	const BW::string shortPlatformName( BW::PlatformInfo::buildStr() );
	serverCommonPath += shortPlatformName;

	if (BW_COMPILE_TIME_CONFIG != BW_CONFIG_HYBRID)
	{
		serverCommonPath += "_";
		serverCommonPath += BW_COMPILE_TIME_CONFIG;
	}

	if (shortPlatformName != BW_BUILD_PLATFORM)
	{
		WARNING_MSG( "Script::init: "
			"Runtime platform doesn't match compile time platform: %s / %s\n",
			shortPlatformName.c_str(), BW_BUILD_PLATFORM );
	}

	sysPaths.addResPath( serverCommonPath );
#endif

#endif // if !BWCLIENT_AS_PYTHON_MODULE

	// Initialise python
	// Py_VerboseFlag = 2;
	Py_FrozenFlag = 1; // Suppress errors from getpath.c

	/* BIGWORLD_BEGIN(3.13 migration)
	 * Py_TabcheckFlag was removed: mixed tab/space indentation has been a
	 * SyntaxError in Python 3 since 3.x, so the flag has no equivalent.
	// Warn if tab and spaces are mixed in indentation.
	Py_TabcheckFlag = 1;
	 */
	/* BIGWORLD_END */
	Py_NoSiteFlag = 1;
	Py_IgnoreEnvironmentFlag = 1;

	Py_Initialize();

	// TODO: If BWCLIENT_AS_PYTHON_MODULE, still override stdio but
	// then add hooks to redirect output back to the original stdio objects
#if !BWCLIENT_AS_PYTHON_MODULE
	new ScriptOutputWriter();
#endif // !BWCLIENT_AS_PYTHON_MODULE

#if BWCLIENT_AS_PYTHON_MODULE
	if (g_scriptArgc)
#endif
	{
		/* BIGWORLD_BEGIN(3.13 migration)
		 * PySys_SetArgv was removed in Python 3.13. Assign sys.argv
		 * directly; note this drops the 2.7 side effect of prepending
		 * argv[0]'s directory to sys.path (PySys_SetArgvEx semantics),
		 * which is handled by PyConfig alone now.
		 */
		PyObject * pArgvList = PyList_New( g_scriptArgc );
		if (pArgvList != NULL)
		{
			for (int i = 0; i < g_scriptArgc; ++i)
			{
				PyObject * pArg = PyUnicode_FromString( g_scriptArgv[i] );
				if ((pArg == NULL) ||
						(PyList_SetItem( pArgvList, i, pArg ) != 0))
				{
					Py_XDECREF( pArg );
					break;
				}
			}

			PySys_SetObject( "argv", pArgvList );
			Py_DECREF( pArgvList );
		}
		/* BIGWORLD_END */
	}

#if !BWCLIENT_AS_PYTHON_MODULE

	PyObject * pSys = sysPaths.pathAsObject();
	int result = PySys_SetObject( "path", pSys );
	Py_DECREF( pSys );
	if (result != 0)
	{
		ERROR_MSG( "Script::init: Unable to assign sys.path\n" );
		return false;
	}

#endif // !BWCLIENT_AS_PYTHON_MODULE

	#if ENABLE_STACK_TRACKER || ENABLE_PROFILER
		PyEval_SetProfile( &profileFunc, NULL );
	#endif

	// Run any init time jobs, including creating modules and adding methods
	// Note: Personality::onInit is run later, when the Personality script is
	// imported.
	runInitTimeJobs();

	// Disable garbage collection
	disablePythonGarbage();

	ScriptModule bigWorld = ScriptModule::getOrCreate( "BigWorld",
		ScriptErrorPrint( "Failed to create BigWorld module" ) );
	/*~ attribute BigWorld.component
	 *  @components{ all }
	 *
	 *	This is the component that is executing the present python environment.
	 *	Possible values are (so far) 'cell', 'base', 'client', 'database', 'bot'
	 *	and 'editor'.
	 */
	bigWorld.setAttribute( "component", 
		ScriptString::create( componentName ),
		ScriptErrorPrint() );

	s_pOurInitTimeModules = PyDict_Copy( PySys_GetObject( "modules" ) );
	s_pMainThreadState = PyThreadState_Get();
	s_defaultContext = s_pMainThreadState;
	/* BIGWORLD_BEGIN(3.13 migration)
	 * PyEval_InitThreads() was removed in Python 3.13. The GIL is always
	 * enabled and initialisation-time thread setup is handled by
	 * Py_Initialize() itself.
	 */
	/* BIGWORLD_END */

	BWConcurrency::setMainThreadIdleFunctions(
		&Script::releaseLock, &Script::acquireLock );

	s_isInitalised = true;

	if (!ScriptModule::import( "bw_site", ScriptErrorPrint(
		"Script::init: Unable to import bw_site\n" ) ))
	{
		return false;
	}

	if (!Pickler::init())
	{
		ERROR_MSG( "Script::init: Pickler failed to initialise\n" );
		return false;
	}

	return true;
}


/**
 *	Turn off Python garbage collection, this is the default behaviour of
 *	BigWorld.
 *
 *	Calls gc.disable().
 */
void Script::disablePythonGarbage()
{
	BW_GUARD;

	// GC is not compiled in, clear the import error
	ScriptModule pGCModule = ScriptModule::import( "gc", ScriptErrorClear() ); 

	// Disable garbage collection
	if (pGCModule)
	{
		pGCModule.callMethod( "disable", ScriptErrorPrint() );
	}

	return;
}


/**
 *	This static method terminates the scripting system.
 */
void Script::fini( bool shouldFinalise )
{
	if (!s_isInitalised)
	{
		return;
	}

	#if ENABLE_STACK_TRACKER || ENABLE_PROFILER
		PyEval_SetProfile( NULL, NULL );
	#endif

	// TODO: Look into this further and ensure that tasks are stopped in a
	// single location, currently BgTaskManager's stopAll is being called from
	// WorldApp's fini as well
	// Stop background tasks before calling Py_Finalize()
	// because they might be loading PyObjects or have script callbacks
	if (FileIOTaskManager::pInstance() != NULL)
	{
		FileIOTaskManager::instance().stopAll();
	}

	if (BgTaskManager::pInstance() != NULL)
	{
		BgTaskManager::instance().stopAll();
	}

	BWConcurrency::setMainThreadIdleFunctions( &NoOp, &NoOp );
	Pickler::finalise();

	// Personality::onFini called as last item here
	runFiniTimeJobs();

	if (s_pOurInitTimeModules != NULL)
	{
		Py_DECREF( s_pOurInitTimeModules );
		s_pOurInitTimeModules = NULL;
	}

#if ENABLE_WATCHERS
	Watcher::fini();
#endif

#if !BWCLIENT_AS_PYTHON_MODULE
	if (shouldFinalise)
	{
		PyObject * modules = PyImport_GetModuleDict();
		while (PyGC_Collect() > 0);
		// Cleanup the __main__ module to remove some references
		PyObject * value = PyDict_GetItemString( modules, "__main__" );
		if (value != NULL && PyModule_Check( value ))
		{
			// BIGWORLD_BEGIN(3.13 migration)
			// _PyModule_Clear() is no longer exported. Replicate its core
			// behaviour: drop every module attribute (keeping __builtins__)
			// so reference cycles involving the module can be collected.
			PyObject * moduleDict = PyModule_GetDict( value );
			PyObject * key = NULL;
			PyObject * item = NULL;
			Py_ssize_t pos = 0;
			while (PyDict_Next( moduleDict, &pos, &key, &item ))
			{
				bool isBuiltins = false;
				if (PyUnicode_Check( key ))
				{
					const char * keyStr = PyUnicode_AsUTF8( key );
					isBuiltins = (keyStr != NULL) &&
						(strcmp( keyStr, "__builtins__" ) == 0);
				}
				if (!isBuiltins)
				{
					PyDict_SetItem( moduleDict, key, Py_None );
				}
			}
			// BIGWORLD_END
			PyDict_SetItemString( modules, "__main__", Py_None );
		}
		Py_Finalize();
	}
#endif // !BWCLIENT_AS_PYTHON_MODULE

	s_isFinalised = true;
}


/**
 *	This method returns whether the Script system has been finalised or not.
 *
 *	@return true if the Script system has been finalised, false otherwise.
 */
bool Script::isFinalised()
{
	return s_isFinalised;
}


/**
 *	This function creates a new interpreter for single-threaded
 * 	applications. The new interpreter will have the same state as
 * 	the default interpreter freshly created by Script::init().
 * 	You can switch between interpreters by calling Script::swapInterpreter().
 * 	Multi-threaded applications should use Script::initThread() instead.
 */
PyThreadState* Script::createInterpreter()
{
	PyThreadState* 	pCurInterpreter = PyThreadState_Get();
	PyObject * 		pCurPath = PySys_GetObject( "path" );

	PyThreadState* pNewInterpreter = Py_NewInterpreter();

	if (pNewInterpreter)
	{
		// New PythonOutputWriter, so that two interpreters don't interleave
		// each other's output. However, this means hooks need to be
		// re-added by the caller if necessary.
		new ScriptOutputWriter();

		PySys_SetObject( "path", pCurPath );
		PyDict_Merge( PySys_GetObject( "modules" ), s_pOurInitTimeModules, 0 );
		// Restore original interpreter.
		PyThreadState* pSwapped = PyThreadState_Swap( pCurInterpreter );

		IF_NOT_MF_ASSERT_DEV( pSwapped == pNewInterpreter )
		{
			MF_EXIT( "error creating new python interpreter" );
		}
	}

	return pNewInterpreter;
}


/**
 * 	This function destroys an interpreter created by
 * 	Script::createInterpreter().
 */
void Script::destroyInterpreter( PyThreadState* pInterpreter )
{
	// Can't destroy current interpreter.
	IF_NOT_MF_ASSERT_DEV( pInterpreter != PyThreadState_Get() )
	{
		MF_EXIT( "trying to destroy current interpreter" );
	}

	// Can't call Py_EndInterpreter() see Script::finiThread().
	PyInterpreterState_Clear( pInterpreter->interp );
	PyInterpreterState_Delete( pInterpreter->interp );
}


/**
 *	This function swaps the current interpreter with the one provided.
 * 	It returns the swapped out interpreter.
 */
PyThreadState* Script::swapInterpreter( PyThreadState* pInterpreter )
{
	s_defaultContext = pInterpreter;
	return PyThreadState_Swap( pInterpreter );
}


/**
 *	This static method initialises scripting for a new thread.
 *
 *	It should be called from the new thread, after the main thread has called
 *	the main 'init' method. If plusOwnInterpreter is true, then the new thread
 *	is set up with its own blank interpreter. Otherwise, the new thread is
 *	associated with the main thread's interpreter.
 *
 *	The function returns with the global lock acquired.
 */
void Script::initThread( bool plusOwnInterpreter )
{
	IF_NOT_MF_ASSERT_DEV( s_defaultContext == NULL )
	{
		MF_EXIT( "trying to initialise scripting when already initialised" );
	}


	// BIGWORLD_BEGIN(3.13 migration)
	// Was PyEval_AcquireLock(), removed in 3.13; re-entering the GIL now
	// goes through the main thread state. Callers arrive without the lock,
	// same contract the old function had.
	PyEval_RestoreThread( s_pMainThreadState );
	// BIGWORLD_END

	PyThreadState * newTState = NULL;

	if (plusOwnInterpreter)
	{
		// BIGWORLD_BEGIN(3.13 migration)
		// PyInterpreterState is opaque now; grab the main interpreter's
		// sys.path through the public sys API before Py_NewInterpreter()
		// switches us over.
		PyObject * pMainPyPath = PySys_GetObject( "path" );
		Py_XINCREF( pMainPyPath );
		// BIGWORLD_END

		newTState = Py_NewInterpreter();

		// set the path again
		PySys_SetObject( "path", pMainPyPath );
		Py_XDECREF( pMainPyPath );

		// put in any modules created by our init-time jobs
		PyDict_Merge( PySys_GetObject( "modules" ), s_pOurInitTimeModules,
			/*override:*/0 );
	}
	else
	{
		newTState = PyThreadState_New( s_pMainThreadState->interp );
	}

	IF_NOT_MF_ASSERT_DEV( newTState != NULL )
	{
		MF_EXIT( "failed to create a new thread object" );
	}

	// BIGWORLD(3.13 migration): Was PyEval_ReleaseLock(), removed in 3.13.
	PyEval_SaveThread();

	// and make our thread be the one global python one
	s_defaultContext = newTState;
	Script::acquireLock();
}


/**
 *	This static method finalises scripting for a thread (not the main one).
 *
 *	It must be called with the current thread in possession of the lock.
 *	(When it returns the lock is not held.)
 */
void Script::finiThread( bool plusOwnInterpreter )
{
	IF_NOT_MF_ASSERT_DEV( s_defaultContext == PyThreadState_Get() )
	{
		MF_EXIT( "trying to finalise script thread when not in default context" );
	}

	if (plusOwnInterpreter)
	{
		//Py_EndInterpreter( s_defaultContext );
		// for now we do not want our modules + sys dict destroyed...
		// ... really should make a way of migrating between threads
		// but for now this will do
		{
			//PyImport_Cleanup();	// this is the one we can't call
			PyInterpreterState_Clear( s_defaultContext->interp );
			PyThreadState_Clear( s_defaultContext );
			PyInterpreterState_Delete( s_defaultContext->interp );
		}

		// BIGWORLD(3.13 migration): Was PyThreadState_Swap( NULL ) plus
		// PyEval_ReleaseLock(), both gone or reshaped in 3.13; SaveThread
		// releases the GIL and clears the current thread state pointer.
		PyEval_SaveThread();
	}
	else
	{
		PyThreadState_Clear( s_defaultContext );
		PyThreadState_DeleteCurrent();	// releases GIL
	}

	s_defaultContext = NULL;


}


/**
 *	This static method acquires the lock for the current thread and
 *	sets the python context to the default one for this thread.
 */
void Script::acquireLock()
{
	if (s_defaultContext == NULL) return;

	//MF_ASSERT( PyThreadState_Get() != s_defaultContext );
	// can't do assert above because PyThreadState_Get can't (since 2.4)
	// be called when the thread state is null - it generates a fatal
	// error. NULL is what we expect it to be as set by releaseLock anyway...
	// there doesn't appear to be a good way to assert this here. Oh well.
	PyEval_RestoreThread( s_defaultContext );
}


/**
 *	This static method releases the lock on the python context held by
 *	the current thread and allows other threads the execute (python) code
 */
void Script::releaseLock()
{
	if (s_defaultContext == NULL) return;

	PyThreadState * oldState = PyEval_SaveThread();
	IF_NOT_MF_ASSERT_DEV( oldState == s_defaultContext )
	{
		MF_EXIT( "releaseLock: default context is incorrect" );
	}
}



namespace
{
#if ENABLE_PROFILER

/**
 *	This function outputs Python function module's name and function's name
 *  as string in MODULE_NAME.FUNCTION_NAME format for profiling purposes
 *  Function takes PyCallable pointer as an input, it also 
 *  From Python point of view a callable is anything that can be called.
 *  - an instance of a class with a __call__ method or
 *  - is of a type that has a non null tp_call (c struct) member which indicates
 *    callability otherwise (such as in functions, methods etc.)
 */
void resolvePythonModuleAndFunctionNames( PyObject * pFunction,
									   char * outputBuffer, size_t outputBufferSize )
{
	const char * moduleName   = NULL;
	const char * functionName = NULL;

	/* BIGWORLD_BEGIN(3.13 migration)
	 * PyFunctionObject internals are no longer public and the memory behind
	 * PyUnicode_AsUTF8() belongs to the string object, so strong references
	 * to the name objects are held until the StringBuilder below has
	 * consumed them (the 2.7 code relied on interning after an early
	 * Py_DECREF).
	 */
	PyObject * pModuleNameHolder = NULL;
	PyObject * pFunctionNameHolder = NULL;
	/* BIGWORLD_END */

	// code is based on PyEval_GetFuncName code from third_party/python/Python/ceval.c
	if (PyMethod_Check(pFunction))
	{
		// no allocation code path
		return resolvePythonModuleAndFunctionNames( PyMethod_GET_FUNCTION(pFunction),
												outputBuffer, outputBufferSize );
	}
	else if (PyFunction_Check(pFunction))
	{
		pFunctionNameHolder = PyObject_GetAttrString( pFunction, "__name__" );
		if (pFunctionNameHolder != NULL)
		{
			if (PyUnicode_Check( pFunctionNameHolder ))
			{
				functionName = PyUnicode_AsUTF8( pFunctionNameHolder );
			}
			else
			{
				Py_CLEAR( pFunctionNameHolder );
				PyErr_Clear();
			}
		}
		else
		{
			PyErr_Clear();
		}
	}
	else if (PyCFunction_Check(pFunction))
	{
		// no allocation code path
		functionName = ((PyCFunctionObject*)pFunction)->m_ml->ml_name;
		pModuleNameHolder = ((PyCFunctionObject*)pFunction)->m_module;
		Py_XINCREF( pModuleNameHolder );
	}
	else if (PyType_Check(pFunction))
	{
		// The tp_name can already have the class name, for heap types this
		// needs to be gotten with __module__
		functionName = ((PyTypeObject*)pFunction)->tp_name;
	}

	if (pModuleNameHolder && PyUnicode_Check(pModuleNameHolder))
	{
		moduleName = PyUnicode_AsUTF8( pModuleNameHolder );
	}

	// Expensive code path, run it only it all previous attempts to resolve function and module names failed.
	// Uses PyObject_GetAttrString functions which do 2 memory allocation
	if (!moduleName && (!functionName || strchr( functionName, '.' ) == NULL) &&
		PyObject_HasAttrString( pFunction, "__module__" ))
	{
		pModuleNameHolder = PyObject_GetAttrString( pFunction, "__module__" );
		if (pModuleNameHolder)
		{
			if (PyUnicode_Check( pModuleNameHolder ))
			{
				moduleName = PyUnicode_AsUTF8( pModuleNameHolder );
			}
			else
			{
				Py_CLEAR( pModuleNameHolder );
				PyErr_Clear();
			}
		}
		else
		{
			PyErr_Clear();
		}
	}
	if (!functionName && PyObject_HasAttrString( pFunction, "__name__" ))
	{
		pFunctionNameHolder = PyObject_GetAttrString( pFunction, "__name__" );
		if (pFunctionNameHolder)
		{
			if (PyUnicode_Check( pFunctionNameHolder ))
			{
				functionName = PyUnicode_AsUTF8( pFunctionNameHolder );
			}
			else
			{
				Py_CLEAR( pFunctionNameHolder );
				PyErr_Clear();
			}
		}
		else
		{
			PyErr_Clear();
		}
	}
	// output resolved module and function names in MODULE_NAME.FUNCTION_NAME format
	BW::StringBuilder strBuilder( outputBuffer, outputBufferSize );
	if (moduleName)
	{
		// prepend function's name with module's name
		strBuilder.append( moduleName );
		if (functionName)
		{
			strBuilder.append( '.' );
		}
	}

	if (functionName)
	{
		strBuilder.append( functionName );
	}

	Py_XDECREF( pModuleNameHolder );
	Py_XDECREF( pFunctionNameHolder );
}

#endif // ENABLE_PROFILER

}
/**
 *	Static function to call a callable object with error checking.
 *	Note that it decrements the reference counts of both of its arguments.
 *	Any error generates an exception, which is printed out if printException is
 *	true, otherwise it is left as the set python exception.
 *	If it is OK that the function is NULL, and an error occurred, then we
 *	assume that the error occurred during the pushing of the parameters onto
 *	the stack, and we clear the error ( e.g. PyObject_GetAttrString( func ) is
 *	used in the parameter list. )
 *	
 *	@note take care when using this in destructors or in situations where the
 *		error flag has been set as this could clear the error flag.
 *		Eg. If an object's destructor calls Script::ask while a function is 
 *		trying to return with an error.
 *	
 *	@param pFunction Python function to call.
 *	@param pArgs arguments for the function to be called.
 *	@param errorPrefix what to prefix on any errors that are printed.
 *	@param okIfFunctionNull clear the error flag if it's ok that the function
 *		name did not exist in the script.
 *	@param printException print any exceptions that occurred.
 *
 *	@return the result of the call, or NULL if some problem occurred
 */
PyObject * Script::ask( PyObject * pFunction,
	PyObject * pArgs,
	const char * errorPrefix,
	bool okIfFunctionNull,
	bool printException )
{
	MF_ASSERT( !Script::isFinalised() );

	MEMTRACKER_SCOPED( Script_ask );

	// There will have been an attribute error if pFunction is NULL
	// Print a stack trace if it's not NULL or not ok to be NULL
	if (PyErr_Occurred() && !(okIfFunctionNull && pFunction == NULL))
	{
		PyErr_PrintEx(0);
	}
	MF_ASSERT_DEV(!PyErr_Occurred() || (okIfFunctionNull && pFunction == NULL));


	PyObject * pResult = NULL;

	if (pFunction != NULL  && pArgs != NULL)
	{
		if (PyCallable_Check( pFunction ) && PyTuple_Check( pArgs ))
		{
#if ENABLE_PROFILER
			char profilerString[1024];
			resolvePythonModuleAndFunctionNames( pFunction, profilerString,
											ARRAY_SIZE( profilerString ) );
			PROFILER_SCOPED_DYNAMIC_STRING_CATEGORY( profilerString,
				Profiler::CATEGORY_PYTHON );
#endif // ENABLE_PROFILER

			pResult = PyObject_CallObject( pFunction, pArgs );
			// may set an exception - we fall through and print it out later
		}
		else
		{
			PyErr_Format( PyExc_TypeError,
				"%sScript call attempted on a non-callable object or "
				" pArgs is not a tuple.",
				errorPrefix );
		}
	}
	else
	{
		if (pArgs == NULL || !okIfFunctionNull)
		{
			PyErr_Format( PyExc_ValueError,
				"%sScript call attempted with a"
				" NULL function or argument pointers:\n"
				"\tpFunction is 0x%p, pArgs is 0x%p",
				errorPrefix, pFunction, pArgs );
		}
		else
		{
			// the function is NULL but it's allowed, so clear
			// any error occurring from trying to find it.
			PyErr_Clear();
		}
	}

	Py_XDECREF( pFunction );
	Py_XDECREF( pArgs );

	if (printException)
	{
		PyObject * pErr = PyErr_Occurred();
		if (pErr != NULL)
		{
			PyObject *pType, *pValue, *pTraceback;
			PyErr_Fetch( &pType, &pValue, &pTraceback );

			BW::string finalError;

			if ( pValue != NULL )
			{
				// there is extended error info, so put it, and put the generic
				// python error in pErr between parenthesis.
				PyObject * pErrStr = PyObject_Str( pValue );
				if ( pErrStr != NULL )
				{
					finalError +=
						BW::string( PyUnicode_AsUTF8( pErrStr ) ) + " (";
					Py_DECREF( pErrStr );
				}
			}

			// add the default python error
			PyObject * pErrStr = PyObject_Str( pErr );
			if ( pErrStr != NULL )
			{
				finalError += PyUnicode_AsUTF8( pErrStr );
				Py_DECREF( pErrStr );
			}

			if ( pValue != NULL )
			{
				// there is extended error info, so close the parenthesis.
				finalError += ")";
			}

			// and output the error.
			ERROR_MSG( "%s %s\n",
				errorPrefix,
				finalError.c_str() );

			PyErr_Restore( pType, pValue, pTraceback );
			PyErr_PrintEx(0);
		}
		PyErr_Clear();
	}

	return pResult;
}


/**
 *	This static utility function returns a new instance of the
 *	class pClass, without calling its __init__.
 */
PyObject * Script::newClassInstance( PyObject * pClass )
{
	/* BIGWORLD_BEGIN(3.13 migration)
	 * This was modelled on newmodule.c's new_instance for 2.x old-style
	 * classes (PyInstanceObject/PyClassObject, both removed in Python 3).
	 * The closest equivalent is object.__new__(cls): allocates the instance
	 * without running cls.__init__.
	 */
	PyObject * pNewObject = PyObject_CallMethod( pClass,
		const_cast<char *>( "__new__" ), const_cast<char *>( "(O)" ), pClass );

	if (pNewObject == NULL)
	{
		PyErr_Print();
	}

	return pNewObject;
	/* BIGWORLD_END */
}


/**
 *	This static utility function unloads a module by deleting its entry in
 * 	sys.modules. Returns true if successful, false if the module is not loaded.
 */
bool Script::unloadModule( const char * moduleName )
{
	PyObject* pModulesDict = PyImport_GetModuleDict();
	MF_ASSERT( PyDict_Check( pModulesDict ) );
	return PyDict_DelItemString( pModulesDict, moduleName ) == 0;
}


/**
 *	Script utility function to run an expression string. It calls
 *	another PyRun_String using the __main__ dictionary and the
 *	Py_eval_input or Py_single_input start symbol.
 *
 *	If printResult is false, then neither the result nor any
 *	exception is printed - just like PyObject_CallObject. However,
 *	statements such as 'print ...' and 'import ...' are not allowed.
 *	If printResult is true, then those statements are allowed,
 *	the result is printed, but None is always returned.
 *
 *	If anyone can find a way to fix this (so that the result is
 *	never printed, but 'print' and 'import' are allowed) then it
 *	would be most wonderful!
 *
 *	@return	The result of the input expression, or None if printResult is true
 */
PyObject * Script::runString( const char * expr, bool printResult )
{
	ScriptModule m = ScriptModule::getOrCreate( "__main__",
		ScriptErrorPrint() );
	if (!m)
	{
		PyErr_SetString( PyExc_SystemError, "Module __main__ not found!" );
		return NULL;
	}

	ScriptDict d = m.getDict();

	// BIGWORLD(3.13 migration): PyCompilerFlags gained new fields; zero
	// initialise so it stays forward compatible.
	PyCompilerFlags cf;
	memset( &cf, 0, sizeof( cf ) );
	cf.cf_flags = PyCF_SOURCE_IS_UTF8;
	return PyRun_StringFlags( const_cast<char*>( expr ),
		printResult ? Py_single_input : Py_eval_input,
		d.get(), d.get(), &cf );

	/*
	OK, here's what the different exported start symbols do:
	(they're used in compiling, not executing)

	Py_single_input:	None returned, appends PRINT_EXPR instruction to code
	Py_file_input:		None returned, pops result from stack and discards it
	Py_eval_input:		Result returned, doesn't allow statements (only expressions)

	See compile_node in Python/compile.c for the proof
	*/
}


// -----------------------------------------------------------------------------
// Section: PyModuleMethodLink
// -----------------------------------------------------------------------------

/**
 *	Constructor taking PyCFunction.
 */
PyModuleMethodLink::PyModuleMethodLink( const char * moduleName,
		const char * methodName, PyCFunction method,
		const char * docString ) :
	Script::InitTimeJob( 0 ),
	moduleName_( moduleName ),
	methodName_( methodName )
{
	defs_[ 0 ].ml_name = const_cast< char * >( methodName_ );
	defs_[ 0 ].ml_meth = method;
	defs_[ 0 ].ml_flags = METH_VARARGS;
	defs_[ 0 ].ml_doc = const_cast< char * >( docString );

	// NULL/0 terminated sentinel required by PyModule_AddFunctions
	defs_[ 1 ].ml_name = NULL;
	defs_[ 1 ].ml_meth = NULL;
	defs_[ 1 ].ml_flags = 0;
	defs_[ 1 ].ml_doc = NULL;
}


/**
 *	Constructor taking PyCFunctionWithKeywords.
 */
PyModuleMethodLink::PyModuleMethodLink( const char * moduleName,
		const char * methodName, PyCFunctionWithKeywords method,
		const char * docString ) :
	Script::InitTimeJob( 0 ),
	moduleName_( moduleName ),
	methodName_( methodName )
{
	defs_[ 0 ].ml_name = const_cast< char * >( methodName_ );
	defs_[ 0 ].ml_meth = BW_PYCFUNCTION_CAST( method );
	defs_[ 0 ].ml_flags = METH_VARARGS | METH_KEYWORDS;
	defs_[ 0 ].ml_doc = const_cast< char * >( docString );

	// NULL/0 terminated sentinel required by PyModule_AddFunctions
	defs_[ 1 ].ml_name = NULL;
	defs_[ 1 ].ml_meth = NULL;
	defs_[ 1 ].ml_flags = 0;
	defs_[ 1 ].ml_doc = NULL;
}


/**
 *	Destructor
 */
PyModuleMethodLink::~PyModuleMethodLink()
{
}


/**
 *	This method adds this factory method to the module
 */
void PyModuleMethodLink::init()
{
	/* BIGWORLD_BEGIN(3.13 migration)
	 * Py_InitModule was removed in Python 3.x. As with PyModuleAttrLink the
	 * module is expected to exist already (its owner creates it during
	 * initialisation); attach the method with PyModule_AddFunctions.
	 *
	 * NOTE: defs_ is a *member*, not a local. The builtin_function_or_method
	 * objects this call creates store a borrowed pointer to the table
	 * (PyCFunctionObject::m_ml) and stay alive in the module dict, so a
	 * stack-allocated table would leave them pointing at dead stack memory -
	 * harmless until Py_Finalize()'s GC walks them and dereferences m_ml.
	 */
	PyObject * pModule =
		PyImport_AddModule( const_cast<char *>( moduleName_ ) );
	if (pModule == NULL)
	{
		PyErr_Clear();
		ERROR_MSG( "PyModuleMethodLink::init: Module '%s' not found while "
			"adding method '%s'\n", moduleName_, methodName_ );
		return;
	}

	if (PyModule_AddFunctions( pModule, defs_ ) < 0)
	{
		PyErr_Print();
		ERROR_MSG( "PyModuleMethodLink::init: Failed to add method '%s' to "
			"module '%s'\n", methodName_, moduleName_ );
	}
	/* BIGWORLD_END */
}


// -----------------------------------------------------------------------------
// Section: PyModuleAttrLink
// -----------------------------------------------------------------------------

/**
 *	Constructor
 */
PyModuleAttrLink::PyModuleAttrLink( const char * moduleName,
		const char * objectName, PyObject * pObject ) :
	Script::InitTimeJob( 0 ),
	moduleName_( moduleName ),
	objectName_( objectName ),
	pObject_( pObject )
{
	MF_ASSERT( moduleName_ );
	MF_ASSERT( objectName_ );
}


/**
 *	This method adds the object to the module.
 */
void PyModuleAttrLink::init()
{
	PyObject_SetAttrString(
		PyImport_AddModule( const_cast<char *>( moduleName_ ) ),
		const_cast<char *>( objectName_ ),
		pObject_ );
	Py_DECREF( pObject_ );
}



// -----------------------------------------------------------------------------
// Section: PyModuleResultLink
// -----------------------------------------------------------------------------

/**
 *	Constructor
 */
PyModuleResultLink::PyModuleResultLink( const char * moduleName,
		const char * objectName, PyObject * (*pFunction)() ) :
	Script::InitTimeJob( 1 ),
	moduleName_( moduleName ),
	objectName_( objectName ),
	pFunction_( pFunction )
{
	MF_ASSERT( moduleName_ );
	MF_ASSERT( objectName_ );
}


/**
 *	This method adds the object to the module.
 */
void PyModuleResultLink::init()
{
	ScriptObject pObject ( (*pFunction_)(),
			ScriptObject::FROM_NEW_REFERENCE );
	if (pObject == NULL)
	{
		ERROR_MSG( "Error initialising object '%s' in module '%s'\n",
			objectName_, moduleName_ );
		PyErr_PrintEx(0);
		PyErr_Clear();
	}
	else
	{
		ScriptModule module = ScriptModule::getOrCreate( moduleName_,
			ScriptErrorPrint() );
		module.setAttribute( objectName_, pObject, ScriptErrorPrint() );
	}
}



// -----------------------------------------------------------------------------
// Section: Pickling helper functions
// -----------------------------------------------------------------------------

/**
 *	This function builds the result tuple that is expected from __reduce__
 *	style methods. It is used by the PY_PICKLING_METHOD_DECLARE macro.
 */
PyObject * Script::buildReduceResult( const char * consName,
	PyObject * pConsArgs )
{
	if (pConsArgs == NULL) return NULL;	// error already set

	static PyObject * s_pBWPicklingModule = PyImport_AddModule( "_BWp" );

	PyObject * pConsFunc =
		PyObject_GetAttrString( s_pBWPicklingModule, (char*)consName );
	if (pConsFunc == NULL)
	{
		Py_DECREF( pConsArgs );
		return NULL;					// error already set
	}

	PyObject * pRes = PyTuple_New( 2 );
	PyTuple_SET_ITEM( pRes, 0, pConsFunc );
	PyTuple_SET_ITEM( pRes, 1, pConsArgs );
	return pRes;
}


// -----------------------------------------------------------------------------
// Section: setData and getData utility functions
// -----------------------------------------------------------------------------

/**
 *	This function tries to interpret its argument as a boolean,
 *	setting it if it is, and generating an exception otherwise.
 *
 *	@return 0 for success, -1 for error (like pySetAttribute)
 */
int Script::setData( PyObject * pObject, bool & rBool,
	const char * varName )
{
	if (PyLong_Check( pObject ))
	{
		rBool = PyLong_AsLong( pObject ) != 0;
		return 0;
	}

	if (PyUnicode_Check( pObject ))
	{
		const char * pStr = PyUnicode_AsUTF8( pObject );
		if (!_stricmp( pStr, "true" ))
		{
			rBool = true;
			return 0;
		}
		else if(!_stricmp( pStr, "false" ))
		{
			rBool = false;
			return 0;
		}
	}

	PyErr_Format( PyExc_TypeError, "%s must be set to a bool", varName );
	return -1;
}


/**
 *	This function tries to interpret its argument as an integer,
 *	setting it if it is, and generating an exception otherwise.
 *
 *	@return 0 for success, -1 for error (like pySetAttribute)
 */
int Script::setData( PyObject * pObject, int & rInt,
	const char * varName )
{
	if (PyLong_Check( pObject ))
	{
		long asLong = PyLong_AsLong( pObject );

		/* BIGWORLD_BEGIN(3.13 migration)
		 * PyLong_AsLong() returns (long)-1 *and* sets OverflowError when the
		 * value does not fit a C long. The 2.7 code assigned rInt before
		 * testing anything ("rInt = asLong; if (asLong == rInt) return 0;"),
		 * so the test was trivially true and the function returned success
		 * for every PyLong - including out-of-range ones, which it reported
		 * with the OverflowError still pending. Callers such as
		 * IntegerRangeChecker::findSameRange() then aborted on the leaked
		 * error.
		 *
		 * So: drop the error if it overflowed, and only accept the value when
		 * it round-trips through int unchanged. The old second PyLong_Check
		 * block below was unreachable (the first block always returned) and
		 * is gone.
		 */
		if (PyErr_Occurred())
		{
			PyErr_Clear();	// does not fit a C long; treat as out of range
		}
		else
		{
			rInt = int( asLong );

			if (int64( asLong ) == int64( rInt ))
			{
				return 0;
			}
		}
		/* BIGWORLD_END */
	}

	if (PyFloat_Check( pObject ))
	{
		rInt = (int)PyFloat_AsDouble( pObject );
		return 0;
	}

	PyErr_Format( PyExc_TypeError, "%s must be set to an int", varName );
	return -1;

}


// This is required as Mac OS X compiler considers int and long to be distinct
// types.
#if defined( __APPLE__ )
/**
 *	This function tries to interpret its argument as an integer,
 *	setting it if it is, and generating an exception otherwise.
 *
 *	@return 0 for success, -1 for error (like pySetAttribute)
 */
int Script::setData( PyObject * pObject, long & rInt,
					const char * varName )
{
	int64 value;
	int result = Script::setData( pObject, value, varName );
	if (result == 0)
	{
		rInt = value;
	}
	return result;
}
#endif


/**
 *	This function tries to interpret its argument as an integer,
 *	setting it if it is, and generating an exception otherwise.
 *
 *	@return 0 for success, -1 for error (like pySetAttribute)
 */
int Script::setData( PyObject * pObject, int64 & rInt,
	const char * varName )
{
	if (PyLong_Check( pObject ))
	{
		rInt = PyLong_AsLongLong( pObject );
		if (!PyErr_Occurred()) return 0;

		/* BIGWORLD_BEGIN(3.13 migration)
		 * The value does not fit in an int64, so PyLong_AsLongLong() left an
		 * OverflowError behind.
		 *
		 * The 2.7 code fell through to a second PyLong_Check() block that
		 * called PyLong_AsLong() and returned 0 *unconditionally*. That block
		 * only made sense on 32-bit builds, where a C long is narrower than an
		 * int64; on LP64 long and int64 are the same width, so it either
		 *   - accepted a value that does not fit (2**63 came back as
		 *     9223372036854775808, no error, return 0), or
		 *   - returned success with an OverflowError still set (anything
		 *     outside even a long, e.g. -2**64-1). The second case leaked a
		 *     pending exception into callers - which is what made
		 *     IntegerRangeChecker::findSameRange() abort with
		 *     "findSameRange: PyErr_Occurred".
		 *
		 * Python 3 has no PyInt/PyLong split, so an int that does not fit in
		 * an int64 is simply out of range. Drop the error and report failure
		 * like any other non-convertible value, matching the uint64 overload
		 * below (whose fallback only ever accepts non-negative values).
		 */
		PyErr_Clear();
		/* BIGWORLD_END */
	}

	if (PyFloat_Check( pObject ))
	{
		rInt = (int64)PyFloat_AsDouble( pObject );
		return 0;
	}

	PyErr_Format( PyExc_TypeError, "%s must be set to a long", varName );
	return -1;
}

/**
 *	This function tries to interpret its argument as an unsigned
 *	64-bit integer, setting it if it is, and generating an exception
 *	otherwise.
 *
 *	@return 0 for success, -1 for error (like pySetAttribute)
 */
int Script::setData( PyObject * pObject, uint64 & rUint,
	const char * varName )
{
	if (PyLong_Check( pObject ))
	{
		rUint = PyLong_AsUnsignedLongLong( pObject );
		if (!PyErr_Occurred()) return 0;
	}

	if (PyLong_Check( pObject ))
	{
		long intValue = PyLong_AsLong( pObject );
		if (intValue >= 0)
		{
			rUint = (uint64)intValue;
			return 0;
		}
		else
		{
			PyErr_Format( PyExc_ValueError,
				"Cannot set %s of type unsigned long to %d",
				varName, int(intValue) );
			return -1;
		}
	}

	if (PyFloat_Check( pObject ))
	{
		rUint = (uint64)PyFloat_AsDouble( pObject );
		return 0;
	}

	PyErr_Format( PyExc_TypeError,
			"%s must be set to a unsigned long", varName );
	return -1;
}


/**
 *	This function tries to interpret its argument as an unsigned integer,
 *	setting it if it is, and generating an exception otherwise.
 *
 *	@return 0 for success, -1 for error (like pySetAttribute)
 */
int Script::setData( PyObject * pObject, uint & rUint,
	const char * varName )
{
	if (PyLong_Check( pObject ))
	{
		long longValue = PyLong_AsLong( pObject );
		rUint = longValue;

		if ((longValue >= 0) && (static_cast< long >( rUint ) == longValue))
		{
			return 0;
		}
	}

	if (PyFloat_Check( pObject ))
	{
		rUint = (int)PyFloat_AsDouble( pObject );
		return 0;
	}

	if (PyLong_Check( pObject ))
	{
		unsigned long asUnsignedLong = PyLong_AsUnsignedLong( pObject );
		rUint = uint( asUnsignedLong );
		if (!PyErr_Occurred() &&
				(rUint == asUnsignedLong))
		{
			return 0;
		}
		PyErr_Clear();

		long asLong = PyLong_AsLong( pObject );
		rUint = uint( asLong );
		if (!PyErr_Occurred() &&
				(asLong >= 0) &&
				(asLong == static_cast< long >( rUint ) ))
		{
			return 0;
		}
	}

	PyErr_Format( PyExc_TypeError, "%s must be set to an uint", varName );
	return -1;

}


/**
 *	This function tries to interpret its argument as a float,
 *	setting it if it is, and generating an exception otherwise.
 *
 *	@return 0 for success, -1 for error (like pySetAttribute)
 */
int Script::setData( PyObject * pObject, float & rFloat,
	const char * varName )
{
	double	d;
	int ret = Script::setData( pObject, d, varName );
	if (ret == 0) rFloat = float(d);
	return ret;
}


/**
 *	This function tries to interpret its argument as a double,
 *	setting it if it is, and generating an exception otherwise.
 *
 *	@return 0 for success, -1 for error (like pySetAttribute)
 */
int Script::setData( PyObject * pObject, double & rDouble,
	const char * varName )
{
	if (PyFloat_Check( pObject ))
	{
		rDouble = PyFloat_AsDouble( pObject );
		return 0;
	}

	if (PyLong_Check( pObject ))
	{
		rDouble = PyLong_AsLong( pObject );
		return 0;
	}

	if (PyLong_Check( pObject ))
	{
		rDouble = PyLong_AsUnsignedLong( pObject );
		if (!PyErr_Occurred()) return 0;
	}

	PyErr_Format( PyExc_TypeError, "%s must be set to a float", varName );
	return -1;

}


/**
 *	This function tries to interpret its argument as a Vector2,
 *	setting it if it is, and generating an exception otherwise.
 *
 *	@return 0 for success, -1 for error (like pySetAttribute)
 */
int Script::setData( PyObject * pObject, Vector2 & rVector,
	const char * varName )
{
	if (PyVector<Vector2>::Check( pObject ))
	{
		rVector = ((PyVector<Vector2>*)pObject)->getVector();
		return 0;
	}
	PyErr_Clear();

	float	a,	b;

	if (PyArg_ParseTuple( pObject, "ff", &a, &b ))
	{
		rVector[0] = a;
		rVector[1] = b;
		return 0;
	}

	PyErr_Format( PyExc_TypeError,
		"%s must be set to a Vector2 or tuple of 2 floats", varName );
	return -1;
}


/**
 *	This function tries to interpret its argument as a Vector3,
 *	setting it if it is, and generating an exception otherwise.
 *
 *	@return 0 for success, -1 for error (like pySetAttribute)
 */
int Script::setData( PyObject * pObject, Vector3 & rVector,
	const char * varName )
{
	if (PyVector<Vector3>::Check( pObject ))
	{
		rVector = ((PyVector<Vector3>*)pObject)->getVector();
		return 0;
	}
	PyErr_Clear();

	if (PyArg_ParseTuple( pObject, "fff", &rVector.x, &rVector.y, &rVector.z ))
	{
		return 0;
	}

	PyErr_Format( PyExc_TypeError,
		"%s must be set to a Vector3 or a tuple of 3 floats", varName );
	return -1;
}

/**
 *	This function tries to interpret its argument as a Vector4,
 *	setting it if it is, and generating an exception otherwise.
 *
 *	@return 0 for success, -1 for error (like pySetAttribute)
 */
int Script::setData( PyObject * pObject, Vector4 & rVector,
	const char * varName )
{
	if (PyVector<Vector4>::Check( pObject ))
	{
		rVector = ((PyVector<Vector4>*)pObject)->getVector();
		return 0;
	}
	PyErr_Clear();

	float	a,	b,	c,	d;

	if (PyArg_ParseTuple( pObject, "ffff", &a, &b, &c, &d ))
	{
		rVector[0] = a;
		rVector[1] = b;
		rVector[2] = c;
		rVector[3] = d;
		return 0;
	}

	PyErr_Format( PyExc_TypeError,
		"%s must be set to a Vector4 or tuple of 4 floats", varName );
	return -1;
}

/**
 *	This function tries to interpret its argument as a Matrix,
 *	setting it if it is, and generating an exception otherwise.
 *
 *	@return 0 for success, -1 for error (like pySetAttribute)
 */
int Script::setData( PyObject * pObject, Matrix & rMatrix,
	const char * varName )
{
	if (MatrixProvider::Check( pObject ))
	{
		((MatrixProvider*) pObject)->matrix(rMatrix);
		return 0;
	}

	PyErr_Format( PyExc_TypeError, "%s must be a MatrixProvider", varName );
	return -1;
}

/**
 *	This function tries to interpret its argument as a PyObject,
 *	setting it if it is, and generating an exception otherwise.
 *
 *	It handles reference counting properly, but it does not check
 *	the type of the input object.
 *
 *	None is translated into NULL.
 *
 *	@return 0 for success, -1 for error (like pySetAttribute)
 */
int Script::setData( PyObject * pObject, PyObject * & rPyObject,
	const char * /*varName*/ )
{
	PyObject * inputObject = rPyObject;

	rPyObject = (pObject != Py_None) ? pObject : NULL;

	Py_XINCREF( rPyObject );

	if (inputObject)
	{
		WARNING_MSG( "Script::setData( pObject , rPyObject ): "
			"rPyObject is not NULL and is DECREFed and replaced by pObject\n" );
	}
	Py_XDECREF( inputObject );

	return 0;
}


/**
 *	This function tries to interpret its argument as a SmartPointer<PyObject>,
 *	setting it if it is, and generating an exception otherwise.
 *
 *	@see setData of PyObject * &
 */
int Script::setData( PyObject * pObject, SmartPointer<PyObject> & rPyObject,
	const char * /*varName*/ )
{
	PyObject * pSet = (pObject != Py_None) ? pObject : NULL;

	if (rPyObject.getObject() != pSet) rPyObject = pSet;

	return 0;
}


/**
 *	This function tries to interpret its argument as a Capabilities set,
 *	setting it if it is, and generating an exception otherwise.
 *
 *	@return 0 for success, -1 for error (like pySetAttribute)
 */
int Script::setData( PyObject * pObject, Capabilities & rCaps,
	const char * varName )
{
	bool good = true;

	Capabilities	wantCaps;

	// type check
	if (!PyList_Check( pObject ))
	{
		good = false;
	}

	// accumulate new caps
	Py_ssize_t ncaps = PyList_Size( pObject );
	for (Py_ssize_t i = 0; i < ncaps && good; i++)
	{
		PyObject * argElt = PyList_GetItem( pObject, i );	// borrowed
		if (PyLong_Check( argElt ))
		{
			wantCaps.add( PyLong_AsLong( argElt ) );
		}
		else
		{
			good = false;
		}
	}

	if (!good)
	{
		PyErr_Format( PyExc_TypeError,
			"%s must be set to a list of ints", varName );
		return -1;
	}

	rCaps = wantCaps;
	return 0;
}


/**
 *	This function tries to interpret its argument as a string
 *	setting it if it is, and generating an exception otherwise.
 *
 *	@return 0 for success, -1 for error (like pySetAttribute)
 */
int Script::setData( PyObject * pObject, BW::string & rString,
	const char * varName )
{
	PyObjectPtr pUTF8String;

	if (PyUnicode_Check( pObject ))
	{
		pUTF8String = PyObjectPtr( PyUnicode_AsUTF8String( pObject ),
			PyObjectPtr::STEAL_REFERENCE );
		pObject = pUTF8String.get();
		if (pObject == NULL)
		{
			return -1;
		}
	}

	/* BIGWORLD_BEGIN(3.13 migration)
	 * PyString became PyUnicode (with PyUnicode_AsUTF8String above handing
	 * us a bytes object), and PyBytes is accepted directly, mirroring the
	 * 2.7 behaviour where str objects were byte strings.
	 */
	if (!PyBytes_Check( pObject ))
	{
		PyErr_Format( PyExc_TypeError, "%s must be set to a string.", varName );
		return -1;
	}

	char *ptr_cs;
	Py_ssize_t len_cs;
	PyBytes_AsStringAndSize( pObject, &ptr_cs, &len_cs );
	rString.assign( ptr_cs, len_cs );
	return 0;
	/* BIGWORLD_END */
}


/**
 *	This function tries to interpret its argument as a wide string
 *	setting it if it is, and generating an exception otherwise.
 *
 *	@return 0 for success, -1 for error (like pySetAttribute)
 */
int Script::setData( PyObject * pObject, BW::wstring & rString,
	const char * varName )
{
	/* BIGWORLD_BEGIN(3.13 migration)
	 * PyUnicodeObject internals (PyUnicode_GET_DATA_SIZE) are gone. Accept
	 * str, bytes and unicode-convertible objects, then use
	 * PyUnicode_AsWideCharString for the conversion and size in one go.
	 */
	PyObject * pUnicode = NULL;

	if (PyUnicode_Check( pObject ))
	{
		pUnicode = pObject;
		Py_INCREF( pUnicode );
	}
	else if (PyBytes_Check( pObject ))
	{
		pUnicode = PyUnicode_FromEncodedObject( pObject, NULL, "strict" );
	}
	else
	{
		// BIGWORLD(3.13 migration): PyObject_Unicode() was removed;
		// PyObject_Str() is the equivalent entry point.
		pUnicode = PyObject_Str( pObject );
	}

	if (pUnicode != NULL)
	{
		// BIGWORLD(3.13 migration): PyUnicode_AsWideCharString() hands the
		// buffer back as its return value and reports the length via the
		// out-parameter.
		Py_ssize_t ulen = 0;
		wchar_t * pBuffer =
			PyUnicode_AsWideCharString( pUnicode, &ulen );

		if (pBuffer != NULL)
		{
			rString.assign( pBuffer, ulen );
			PyMem_Free( pBuffer );
			Py_DECREF( pUnicode );
			return 0;
		}

		Py_DECREF( pUnicode );
	}
	else
	{
		PyErr_Clear();
	}
	/* BIGWORLD_END */

	PyErr_Format( PyExc_TypeError,
			"%s must be set to a wide string.", varName );
	return -1;

}



/**
 *	This function tries to interpret its argument as a Mercury address.
 *
 *	@return 0 for success, -1 for error (like pySetAttribute)
 */
int Script::setData( PyObject * pObject, Mercury::Address & rAddr,
					const char * varName )
{
	if (PyTuple_Check( pObject ) &&
		PyTuple_Size( pObject ) == 2 &&
		PyLong_Check( PyTuple_GET_ITEM( pObject, 0 ) ) &&
		PyLong_Check( PyTuple_GET_ITEM( pObject, 1 ) ))
	{
		rAddr.ip   = PyLong_AsLong( PyTuple_GET_ITEM( pObject, 0 ) );
		rAddr.port = uint16( PyLong_AsLong( PyTuple_GET_ITEM( pObject, 1 ) ) );
		return 0;
	}
	else
	{
		PyErr_Format( PyExc_TypeError,
			"%s must be a tuple of two ints", varName );
		return -1;
	}
}


/**
 *	Script converter for space entry ids
 */
int Script::setData( PyObject * pObject, SpaceEntryID & entryID,
	const char * varName )
{
	if (!PyTuple_Check( pObject ) || PyTuple_Size( pObject ) != 2 ||
		!PyLong_Check( PyTuple_GetItem( pObject, 0 )) ||
		!PyLong_Check( PyTuple_GetItem( pObject, 1 )))
	{
		PyErr_Format( PyExc_TypeError,
			"%s must be set to a SpaceEntryID", varName );
		return -1;
	}

	((uint32*)&entryID)[0] = PyLong_AsLong( PyTuple_GET_ITEM( pObject, 0 ) );
	((uint32*)&entryID)[1] = PyLong_AsLong( PyTuple_GET_ITEM( pObject, 1 ) );

	return 0;
}


// -----------------------------------------------------------------------------
// Section: Script::getData
// -----------------------------------------------------------------------------

/**
 * This function makes a PyObject from a bool
 */
PyObject * Script::getData( const bool data )
{
	return PyBool_FromLong( data );
}


/**
 * This function makes a PyObject from an int
 */
PyObject * Script::getData( const int data )
{
	return PyLong_FromLong( data );
}


// This is required for Mac OS X compilers that treat long and int as distinct
// types.
#if defined( __APPLE__ )

/**
 * This function makes a PyObject from an int
 */
PyObject * Script::getData( const long data )
{
	return PyLong_FromLong( data );
}

#endif


/**
 * This function makes a PyObject from an unsigned int
 */
PyObject * Script::getData( const uint data )
{
	unsigned long asULong = data;

	return (long(asULong) < 0) ?
		PyLong_FromUnsignedLong( asULong ) :
		PyLong_FromLong( asULong );
}


/**
 * This function makes a PyObject form an int64
 */
PyObject * Script::getData( const int64 data )
{
	if (sizeof( int64 ) == sizeof( long ))
	{
		return PyLong_FromLong( (long)data );
	}
	else
	{
		return PyLong_FromLongLong( data );
	}
}


/**
 * This function makes a PyObject form an uint64
 */
PyObject * Script::getData( const uint64 data )
{
	if (sizeof( int64 ) == sizeof( long ))
	{
		unsigned long asULong = (unsigned long)data;

		if (long( asULong ) >= 0)
		{
			return PyLong_FromLong( asULong );
		}
	}

	return PyLong_FromUnsignedLongLong( data );
}


/**
 * This function makes a PyObject from a float
 */
PyObject * Script::getData( const float data )
{
	return PyFloat_FromDouble( data );
}


/**
 * This function makes a PyObject from a double
 */
PyObject * Script::getData( const double data )
{
	return PyFloat_FromDouble( data );
}


/**
 * This function makes a PyObject from a Vector2
 */
PyObject * Script::getData( const Vector2 & data )
{
	return new PyVectorCopy< Vector2 >( data );
}


/**
 * This function makes a PyObject from a Vector3
 */
PyObject * Script::getData( const Vector3 & data )
{
	return new PyVectorCopy< Vector3 >( data );
}


/**
 * This function makes a PyObject from a Vector4
 */
PyObject * Script::getData( const Vector4 & data )
{
	return new PyVectorCopy< Vector4 >( data );
}


/**
 * This function makes a PyObject from a Direction3D
 */
PyObject * Script::getData( const Direction3D & data )
{
	return getData( data.asVector3() );
}


/**
 * This function makes a read-only PyObject from a Vector2
 */
PyObject * Script::getReadOnlyData( const Vector2 & data )
{
	return new PyVectorCopy< Vector2 >( data, /*isReadOnly:*/ true );
}


/**
 * This function makes a PyObject from a Vector3
 */
PyObject * Script::getReadOnlyData( const Vector3 & data )
{
	return new PyVectorCopy< Vector3 >( data, /*isReadOnly:*/ true );
}


/**
 * This function makes a PyObject from a Vector4
 */
PyObject * Script::getReadOnlyData( const Vector4 & data )
{
	return new PyVectorCopy< Vector4 >( data, /*isReadOnly:*/ true );
}


/**
 * This function makes a PyVector2 that is a reference to a Vector2 member.
 */
PyObject * Script::getDataRef( PyObject * pOwner, Vector2 * pData )
{
	return new PyVectorRef< Vector2 >( pOwner, pData );
}


/**
 * This function makes a PyVector3 that is a reference to a Vector2 member.
 */
PyObject * Script::getDataRef( PyObject * pOwner, Vector3 * pData )
{
	return new PyVectorRef< Vector3 >( pOwner, pData );
}


/**
 * This function makes a PyVector4 that is a reference to a Vector2 member.
 */
PyObject * Script::getDataRef( PyObject * pOwner, Vector4 * pData )
{
	return new PyVectorRef< Vector4 >( pOwner, pData );
}

/**
 * This function makes a PyObject from a Matrix
 */
PyObject * Script::getData( const Matrix & data )
{
	PyMatrix * pyM = new PyMatrix();
	pyM->set( data );

	return pyM;
}

/**
 * This function makes a PyObject from a PyObject,
 *	and it handles reference counting properly.
 *
 *	NULL is translated into None
 */
PyObject * Script::getData( const PyObject * data )
{
	PyObject * ret = (data != NULL) ? const_cast<PyObject*>( data ) : Py_None;
	Py_INCREF( ret );
	return ret;
}


/**
 * This function makes a PyObject from a ConstSmartPointer<PyObject>,
 *	and it handles reference counting properly.
 *
 *	@see getData for const PyObject *
 */
PyObject * Script::getData( ConstSmartPointer<PyObject> data )
{
	PyObject * ret = (data ?
		const_cast<PyObject*>( data.getObject() ) : Py_None);
	Py_INCREF( ret );
	return ret;
}


/**
 * This function makes a PyObject from a Capabilities set
 */
PyObject * Script::getData( const Capabilities & data )
{
	PyObject * ret = PyList_New( 0 );
	for (uint i=0; i <= Capabilities::s_maxCap_; i++)
	{
		if (data.has( i ))
		{
			PyObject * pBit = PyLong_FromLong( i );
			PyList_Append( ret, pBit );
			Py_DECREF( pBit );
		}
	}

	return ret;
}


/**
 *	This function makes a PyObject from a string.
 */
PyObject * Script::getData( const BW::string & data )
{
	PyObject * pRet = PyUnicode_FromStringAndSize(
		const_cast<char *>( data.data() ), data.size() );

	return pRet;
}


/**
 *	This function makes a PyObject from a wide string.
 */
PyObject * Script::getData( const BW::wstring & data )
{
	PyObject * pRet = PyUnicode_FromWideChar(
		const_cast<wchar_t *>( data.c_str() ), data.size() );

	return pRet;
}


/**
 *	This function makes a PyObject from a const char *.
 */
PyObject * Script::getData( const char * data )
{
	PyObject * pRet = PyUnicode_FromString( const_cast<char *>( data ) );

	return pRet;
}


/**
 *	This function makes a PyObject from a Mercury address.
 */
PyObject * Script::getData( const Mercury::Address & addr )
{
	PyObject * pTuple = PyTuple_New( 2 );
	PyTuple_SET_ITEM( pTuple, 0, Script::getData( addr.ip ) );
	PyTuple_SET_ITEM( pTuple, 1, Script::getData( addr.port ) );
	return pTuple;
}


/**
 *	Script converter for space entry ids
 */
PyObject * Script::getData( const SpaceEntryID & entryID )
{
	PyObject * pTuple = PyTuple_New( 2 );
	PyTuple_SET_ITEM( pTuple, 0, PyLong_FromLong( ((uint32*)&entryID)[0] ) );
	PyTuple_SET_ITEM( pTuple, 1, PyLong_FromLong( ((uint32*)&entryID)[1] ) );
	return pTuple;
}


// -----------------------------------------------------------------------------
// Section: Helpers
// -----------------------------------------------------------------------------

PyObject * Script::argCountError( const char * fn, int optas, int allas, ... )
{
	BW::string argTypes;

	va_list val;
	va_start( val, allas );
	for (int i = 0; i < allas; i++)
	{
		if (i > 0) argTypes.append( ", " );

		char * argType = va_arg( val, char * );
		size_t argTypeLen = strlen( argType );
		if (argTypeLen > 13 && !strncmp( argType, "SmartPointer<", 13 ))
		{
			argTypes.append( argType+13, argTypeLen-13-1 );
		}
		else if (argTypeLen > 3 && !strncmp( argType+argTypeLen-3, "Ptr", 3 ))
		{
			argTypes.append( argType, argTypeLen-3 );
		}
		else
		{
			while (argTypeLen>0 && argType[argTypeLen-1] == '*')
				argTypeLen--;
			argTypes.append( argType, argTypeLen );
		}
	}
	va_end( val );

	if (allas == 0)
	{
		PyErr_Format( PyExc_TypeError, "%s() takes no arguments.", fn );
	}
	else if (optas == allas)
	{
		const char * plural = (allas != 1) ? "s" : "";
		PyErr_Format( PyExc_TypeError,
			"%s() expects %d argument%s of type%s %s",
			fn, allas, plural, plural, argTypes.c_str() );
	}
	else
	{
		PyErr_Format( PyExc_TypeError,
			"%s() expects between %d and %d arguments of types %s",
			fn, optas, allas, argTypes.c_str() );
	}

	return NULL;
}


#if BWCLIENT_AS_PYTHON_MODULE

/**
 *
 */
const BW::string Script::getMainScriptPath()
{
	BW::string result;
	PyObject * pPath = PySys_GetObject( "path" );

	if (PyList_Size( pPath ) > 0)
	{
		result = PyUnicode_AsUTF8( PyList_GetItem( pPath, 0 ) );
	}
	return result;
}

#endif // BWCLIENT_AS_PYTHON_MODULE


namespace Script
{
template <> const char * zeroValueName<int>()
{
	return "0";
}

template <> const char * zeroValueName<float>()
{
	return "0.0";
}
} // namespace Script

BW_END_NAMESPACE

// script.cpp

