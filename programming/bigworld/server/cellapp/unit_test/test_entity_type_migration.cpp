// Python.h must come before any standard headers (see lib/script/first_include.hpp).
#include "script/first_include.hpp"

#include "pch.hpp"

#include "../entity_type.hpp"

#include "../entity.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <thread>

BW_BEGIN_NAMESPACE

// The harness itself never initialises Python. Py_Initialize leaves Python's
// own allocator in place (Script::init is what installs the BigWorld one), so
// the interpreter is invisible to the leak tracker and may be left running
// until the test binary exits.
static void initInterpreter()
{
	if (!Py_IsInitialized())
	{
		Py_Initialize();
	}
}


/**
 *	A reloaded module set replaces the running one: modules that exist only in
 *	the running interpreter are dropped, the content is the snapshot's, and the
 *	interpreter's own sys module object survives the swap.
 */
TEST( EntityType_swapInModules )
{
	initInterpreter();

	PyObject * pModules = PySys_GetObject( "modules" );
	CHECK( pModules != NULL );

	// A copy of sys.modules stands in for the modules dictionary of a reload
	// that succeeded, then the running interpreter diverges from it.
	PyObject * pReloaded = PyDict_Copy( pModules );
	CHECK( pReloaded != NULL );

	PyObject * pStray = PyImport_AddModule( "cellappTestStray" ); // borrowed
	CHECK( pStray != NULL );
	CHECK_EQUAL( 0, PyDict_SetItemString( pModules, "cellappTestStray",
		pStray ) ); // steals a reference
	Py_INCREF( pStray );

	PyObject * pSys = PyDict_GetItemString( pModules, "sys" ); // borrowed
	CHECK( pSys != NULL );

	EntityType::swapInModules( pModules, pReloaded );

	// The module that existed only in the running interpreter is gone.
	CHECK( PyDict_GetItemString( pModules, "cellappTestStray" ) == NULL );

	// The content is the reload snapshot's.
	CHECK_EQUAL( PyDict_Size( pReloaded ), PyDict_Size( pModules ) );

	// The interpreter's sys module object is preserved.
	CHECK( pSys == PyDict_GetItemString( pModules, "sys" ) );

	Py_DECREF( pStray );
	Py_DECREF( pReloaded );
}


/**
 *	The sys module is owned across the clear that drops the dictionary's own
 *	reference: a pointer borrowed from a dictionary must not outlive the
 *	dictionary's hold on it, even where that hold was its last strong reference.
 */
TEST( EntityType_swapInModulesKeepsSysAliveAcrossTheClear )
{
	initInterpreter();

	// A module whose only strong reference is the one a modules dictionary
	// holds, standing in for the sys module about to be cleared away.
	PyObject * pSys = PyModule_New( "cellappTestLastRefSys" );
	CHECK( pSys != NULL );

	PyObject * pModules = PyDict_New();
	CHECK( pModules != NULL );
	CHECK_EQUAL( 0, PyDict_SetItemString( pModules, "sys", pSys ) );

	Py_DECREF( pSys ); // from here the dictionary is its only holder

	PyObject * pNewModules = PyDict_New();
	CHECK( pNewModules != NULL );

	EntityType::swapInModules( pModules, pNewModules );

	// The module the dictionary held is still alive and is the one the swap
	// restores: the borrowed pointer was owned across the clear.
	CHECK( PyDict_GetItemString( pModules, "sys" ) == pSys );

	Py_DECREF( pNewModules );
	Py_DECREF( pModules );
}


/**
 *	BigWorld is re-exposed on __main__ from the reloaded module set, so the
 *	scripts still see the package that the reload provided.
 */
TEST( EntityType_swapInModulesReExposesBigWorld )
{
	initInterpreter();

	PyObject * pModules = PySys_GetObject( "modules" );
	CHECK( pModules != NULL );

	// A module carrying the reloaded package name, installed as if the reload
	// had imported it.
	PyObject * pBigWorld = PyModule_New( "BigWorld" );
	CHECK( pBigWorld != NULL );
	CHECK_EQUAL( 0, PyDict_SetItemString( pModules, "BigWorld", pBigWorld ) );
	// pBigWorld stays owned by sys.modules from here on

	PyObject * pReloaded = PyDict_Copy( pModules );
	CHECK( pReloaded != NULL );

	// Drop the package from __main__ first, so the swap has something to do.
	PyObject * pMainDict = PyModule_GetDict(
		PyDict_GetItemString( pModules, "__main__" ) );
	CHECK( pMainDict != NULL );
	CHECK( PyDict_GetItemString( pMainDict, "BigWorld" ) == NULL );

	EntityType::swapInModules( pModules, pReloaded );

	CHECK( PyDict_GetItemString( pMainDict, "BigWorld" ) == pBigWorld );

	Py_DECREF( pReloaded );
}


// A fresh module whose dict the given source runs in; its borrowed dict is
// returned for fetching the names the source defined. CHECK is not usable in
// a free function, so the tests assert on the returned dict themselves.
static PyObject * runScript( const char * moduleName, const char * source )
{
	PyObject * pModule = PyImport_AddModule( moduleName );
	if (!pModule)
	{
		PyErr_Clear();
		return NULL;
	}

	PyObject * pDict = PyModule_GetDict( pModule );
	if (!pDict)
	{
		PyErr_Clear();
		return NULL;
	}

	PyObject * pResult = PyRun_String( source, Py_file_input, pDict, pDict );
	if (!pResult)
	{
		// Surface the script error, then leave the caller a usable dict.
		PyErr_Print();
	}

	Py_XDECREF( pResult );
	return pDict;
}


/**
 *	A successful swap moves the instance to the new script class and keeps the
 *	instance state: this is the migration contract Entity::migrate() relies
 *	on once the type-id guard has passed.
 */
TEST( Entity_swapScriptClass_movesInstanceToNewClass )
{
	initInterpreter();

	PyObject * pDict = runScript( "cellappSwapTest",
		"class OldKind:\n"
		"    pass\n"
		"class NewKind:\n"
		"    pass\n" );
	CHECK( pDict != NULL );
	if (!pDict)
	{
		return;
	}

	PyObject * pOldClass = PyMapping_GetItemString( pDict, "OldKind" );
	PyObject * pNewClass = PyMapping_GetItemString( pDict, "NewKind" );
	CHECK( pOldClass != NULL );
	CHECK( pNewClass != NULL );

	PyObject * pInstance = PyObject_CallObject( pOldClass, NULL );
	CHECK( pInstance != NULL );
	CHECK_EQUAL( 0, PyObject_SetAttrString( pInstance, "payload",
		PyLong_FromLong( 7 ) ) );

	CHECK( Entity::swapScriptClass( pInstance, pNewClass ) );

	// The instance now reports the new class, and the state it carried
	// survived the swap.
	CHECK( (PyObject *)Py_TYPE( pInstance ) == pNewClass );
	PyObject * pPayload = PyObject_GetAttrString( pInstance, "payload" );
	CHECK( pPayload != NULL );
	CHECK_EQUAL( 7, PyLong_AsLong( pPayload ) );
	Py_XDECREF( pPayload );

	// The old class object itself is untouched, as migrate()'s rollback
	// assumes.
	CHECK( (PyObject *)Py_TYPE( pInstance ) != pOldClass );

	Py_DECREF( pInstance );
	Py_DECREF( pNewClass );
	Py_DECREF( pOldClass );
}


/**
 *	A swap to something that is not a type is rejected with the instance
 *	untouched and the exception left pending for the caller - migrate()
 *	prints and rolls its C++ type pointer back on exactly this report.
 */
TEST( Entity_swapScriptClass_rejectsNonType )
{
	initInterpreter();

	PyObject * pDict = runScript( "cellappSwapTestNonType",
		"class OldKind:\n"
		"    pass\n" );
	CHECK( pDict != NULL );
	if (!pDict)
	{
		return;
	}

	PyObject * pOldClass = PyMapping_GetItemString( pDict, "OldKind" );
	CHECK( pOldClass != NULL );

	PyObject * pInstance = PyObject_CallObject( pOldClass, NULL );
	CHECK( pInstance != NULL );

	PyObject * pNotAType = PyLong_FromLong( 42 );
	CHECK( !Entity::swapScriptClass( pInstance, pNotAType ) );
	Py_DECREF( pNotAType );

	// The rejection is reported through the pending exception ...
	CHECK( PyErr_Occurred() != NULL );
	PyErr_Clear();

	// ... and __class__ is untouched: no half-migrated Python state.
	CHECK( (PyObject *)Py_TYPE( pInstance ) == pOldClass );

	Py_DECREF( pInstance );
	Py_DECREF( pOldClass );
}


/**
 *	A swap to a class with an incompatible instance layout is the realistic
 *	reload failure (the new script properties changed the layout). The swap
 *	is rejected and __class__ is untouched, so migrate()'s rollback returns
 *	the entity to a fully consistent old-typed object.
 */
TEST( Entity_swapScriptClass_rejectsIncompatibleLayout )
{
	initInterpreter();

	PyObject * pDict = runScript( "cellappSwapTestLayout",
		"class LayoutA:\n"
		"    __slots__ = ( 'x', )\n"
		"class LayoutB:\n"
		"    __slots__ = ( 'x', 'y' )\n" );
	CHECK( pDict != NULL );
	if (!pDict)
	{
		return;
	}

	PyObject * pClassA = PyMapping_GetItemString( pDict, "LayoutA" );
	PyObject * pClassB = PyMapping_GetItemString( pDict, "LayoutB" );
	CHECK( pClassA != NULL );
	CHECK( pClassB != NULL );

	PyObject * pInstance = PyObject_CallObject( pClassA, NULL );
	CHECK( pInstance != NULL );
	CHECK_EQUAL( 0, PyObject_SetAttrString( pInstance, "x",
		PyLong_FromLong( 1 ) ) );

	CHECK( !Entity::swapScriptClass( pInstance, pClassB ) );
	CHECK( PyErr_Occurred() != NULL );
	PyErr_Clear();

	CHECK( (PyObject *)Py_TYPE( pInstance ) == pClassA );

	Py_DECREF( pInstance );
	Py_DECREF( pClassB );
	Py_DECREF( pClassA );
}


/**
 *	After a rejected swap the object is not wedged: a later swap to a
 *	compatible class succeeds. This is the retry half of migrate()'s
 *	rollback contract - a failed reload leaves an entity that a fixed reload
 *	can still migrate.
 */
TEST( Entity_swapScriptClass_recoverAfterRejectedSwap )
{
	initInterpreter();

	PyObject * pDict = runScript( "cellappSwapTestRetry",
		"class LayoutA:\n"
		"    __slots__ = ( 'x', )\n"
		"class LayoutB:\n"
		"    __slots__ = ( 'x', 'y' )\n"
		"class LayoutA2:\n"
		"    __slots__ = ( 'x', )\n" );
	CHECK( pDict != NULL );
	if (!pDict)
	{
		return;
	}

	PyObject * pClassA = PyMapping_GetItemString( pDict, "LayoutA" );
	PyObject * pClassB = PyMapping_GetItemString( pDict, "LayoutB" );
	PyObject * pClassA2 = PyMapping_GetItemString( pDict, "LayoutA2" );
	CHECK( pClassA != NULL );
	CHECK( pClassB != NULL );
	CHECK( pClassA2 != NULL );

	PyObject * pInstance = PyObject_CallObject( pClassA, NULL );
	CHECK( pInstance != NULL );
	CHECK_EQUAL( 0, PyObject_SetAttrString( pInstance, "x",
		PyLong_FromLong( 3 ) ) );

	CHECK( !Entity::swapScriptClass( pInstance, pClassB ) );
	PyErr_Clear();

	// The retry targets a class with the same layout, as a corrected reload
	// would provide, and completes.
	CHECK( Entity::swapScriptClass( pInstance, pClassA2 ) );
	CHECK( (PyObject *)Py_TYPE( pInstance ) == pClassA2 );

	// The slot state survived both the rejection and the later swap.
	PyObject * pX = PyObject_GetAttrString( pInstance, "x" );
	CHECK( pX != NULL );
	CHECK_EQUAL( 3, PyLong_AsLong( pX ) );
	Py_XDECREF( pX );

	Py_DECREF( pInstance );
	Py_DECREF( pClassA2 );
	Py_DECREF( pClassB );
	Py_DECREF( pClassA );
}


/**
 *	Test access to the reload seam. The migrate() mutex and the new-modules
 *	pointer are private; the tests drive migrate() without a full script
 *	reload, so they set the pointer and hold the mutex directly.
 */
struct EntityTypeTestAccess
{
	static void setNewModules( PyObject * pNewModules )
	{
		EntityType::s_pNewModules_ = pNewModules;
	}

	static std::shared_mutex & typesMutex()
	{
		return EntityType::s_typesMutex_;
	}
};


/**
 *	The full reload swap takes the type-table mutex exclusively, so a reader
 *	that already holds it shared is not displaced mid-rewrite: migrate(true)
 *	blocks until the shared holder releases. The swap itself is inert here
 *	(the tables are empty), which is exactly the point - the test observes the
 *	mutex, not the migration.
 */
TEST( EntityType_fullReloadSwapBlocksWhileReadersHold )
{
	initInterpreter();

	// Point the new-modules seam at the running set so migrate() skips
	// swapInModules and reaches the full-swap block untouched.
	PyObject * pModules = PySys_GetObject( "modules" ); // borrowed
	CHECK( pModules != NULL );
	EntityTypeTestAccess::setNewModules( pModules );

	std::atomic< bool > swapped( false );

	// A reader holds the table shared for the whole attempt.
	{
		std::shared_lock< std::shared_mutex > readLock(
			EntityTypeTestAccess::typesMutex() );

		// migrate() touches sys.modules, so the migrator needs its own Python
		// thread state: PyGILState_Ensure gives it one. The main thread has
		// held the GIL since Py_Initialize, so it must release it while it
		// waits - otherwise the migrator would block on the GIL, not on the
		// mutex under test.
		std::thread migrator( [ &swapped ]()
		{
			PyGILState_STATE gil = PyGILState_Ensure();
			EntityType::migrate( true );
			swapped.store( true );
			PyGILState_Release( gil );
		} );

		// While the shared lock is held the swap cannot run. The GIL stays
		// released for the whole wait: the migrator holds it while it is
		// blocked on the mutex, and only joins once the lock is released.
		// The observation is captured here (not CHECKed) so a failure cannot
		// early-return and skip Py_END_ALLOW_THREADS.
		bool sawSwapWhileLocked = false;
		Py_BEGIN_ALLOW_THREADS
		std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
		sawSwapWhileLocked = swapped.load();

		readLock.unlock();
		migrator.join();
		Py_END_ALLOW_THREADS

		CHECK( !sawSwapWhileLocked );
	}

	// Once the shared holder released, the swap completed.
	CHECK( swapped.load() );

	EntityTypeTestAccess::setNewModules( NULL );
}


/**
 *	A lookup holds the type-table mutex shared, so the full reload's exclusive
 *	holder blocks it: while the swap is in progress, nameToIndex cannot read
 *	the name map. The converse of the test above, on the reader side.
 */
TEST( EntityType_nameToIndexBlocksWhileFullReloadSwaps )
{
	// No interpreter needed: nameToIndex only touches the table and the lock.
	std::atomic< bool > lookedUp( false );

	// Hold the table exclusively first, as the full reload swap does, so the
	// searcher is guaranteed to arrive at a held lock.
	std::unique_lock< std::shared_mutex > writeLock(
		EntityTypeTestAccess::typesMutex() );

	std::thread searcher( [ &lookedUp ]()
	{
		EntityType::nameToIndex( "NoSuchType" );
		lookedUp.store( true );
	} );

	std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
	CHECK( !lookedUp.load() );

	writeLock.unlock();
	searcher.join();

	CHECK( lookedUp.load() );
}


// test_entity_type_migration.cpp

BW_END_NAMESPACE
