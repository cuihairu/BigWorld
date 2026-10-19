// Python.h must come before any standard headers (see lib/script/first_include.hpp).
#include "script/first_include.hpp"

#include "pch.hpp"

#include "../entity_type.hpp"

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


// test_entity_type_migration.cpp

BW_END_NAMESPACE
