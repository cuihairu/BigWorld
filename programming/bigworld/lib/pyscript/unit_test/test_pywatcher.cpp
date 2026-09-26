#include "pch.hpp"
#include "test_harness.hpp"
#include "cstdmf/bw_namespace.hpp"
#include "cstdmf/memory_stream.hpp"
#include "cstdmf/watcher.hpp"
#include "pyscript/py_to_stl.hpp"
#include "pyscript/pywatcher.hpp"
#include "pyscript/script.hpp"


namespace BW
{

namespace
{
	int s_pyWatcherValue = 42;


	/**
	 *	Calls a BigWorld watcher module function. Both the function reference
	 *	and the argument tuple are consumed, exactly like Script::ask does.
	 */
	PyObject * askBigWorld( PyObject * pBigWorld, const char * functionName,
		PyObject * pArgs, const char * errorPrefix )
	{
		return Script::ask( PyObject_GetAttrString( pBigWorld, functionName ),
			pArgs, errorPrefix, false );
	}
}


// -----------------------------------------------------------------------------
// Section: BigWorld.getWatcher / setWatcher
// -----------------------------------------------------------------------------

// MF_WATCH'ed C++ variable is readable from Python via BigWorld.getWatcher,
// which returns the value rendered as a string.
TEST_F( PyScriptUnitTestHarness, PyWatcher_getWatcher )
{
	// Other tests in this file mutate the shared static; pin the value this
	// test asserts on.
	s_pyWatcherValue = 42;

	MF_WATCH( "test/pywatcher/value", s_pyWatcherValue,
		Watcher::WT_READ_WRITE, "test value" );

	PyObject * pBigWorld = PyImport_ImportModule( "BigWorld" );
	CHECK( pBigWorld );

	PyObject * pResult = askBigWorld( pBigWorld, "getWatcher",
		Py_BuildValue( "(s)", "test/pywatcher/value" ),
		"PyWatcher_getWatcher" );
	CHECK( pResult );
	CHECK( PyUnicode_Check( pResult ) );
	CHECK_EQUAL( BW::string( "42" ), BW::string( PyUnicode_AsUTF8( pResult ) ) );
	Py_DECREF( pResult );

	// A non-existent path raises the documented TypeError.
	pResult = askBigWorld( pBigWorld, "getWatcher",
		Py_BuildValue( "(s)", "test/pywatcher/nonexistent" ),
		"PyWatcher_getWatcherMissing" );
	CHECK( pResult == NULL );
	PyErr_Clear();

	Py_DECREF( pBigWorld );
}


// BigWorld.setWatcher writes through to the watched C++ variable.
TEST_F( PyScriptUnitTestHarness, PyWatcher_setWatcher )
{
	MF_WATCH( "test/pywatcher/settable", s_pyWatcherValue,
		Watcher::WT_READ_WRITE, "test value" );

	PyObject * pBigWorld = PyImport_ImportModule( "BigWorld" );
	CHECK( pBigWorld );

	PyObject * pResult = askBigWorld( pBigWorld, "setWatcher",
		Py_BuildValue( "(ss)", "test/pywatcher/settable", "43" ),
		"PyWatcher_setWatcher" );
	CHECK( pResult );
	Py_DECREF( pResult );

	CHECK_EQUAL( 43, s_pyWatcherValue );

	// A non-existent path raises the documented TypeError.
	pResult = askBigWorld( pBigWorld, "setWatcher",
		Py_BuildValue( "(ss)", "test/pywatcher/nonexistent", "1" ),
		"PyWatcher_setWatcherMissing" );
	CHECK( pResult == NULL );
	PyErr_Clear();

	Py_DECREF( pBigWorld );
}


// getWatcherDir lists the children of a directory watcher as
// (mode, label, value) tuples.
TEST_F( PyScriptUnitTestHarness, PyWatcher_getWatcherDir )
{
	// Other tests in this file mutate the shared static; pin the value this
	// test asserts on.
	s_pyWatcherValue = 42;

	MF_WATCH( "test/pywatcherdir/entry", s_pyWatcherValue,
		Watcher::WT_READ_WRITE, "test value" );

	PyObject * pBigWorld = PyImport_ImportModule( "BigWorld" );
	CHECK( pBigWorld );

	PyObject * pResult = askBigWorld( pBigWorld, "getWatcherDir",
		Py_BuildValue( "(s)", "test/pywatcherdir" ),
		"PyWatcher_getWatcherDir" );
	CHECK( pResult );
	CHECK( PyList_Check( pResult ) );
	CHECK_EQUAL( 1, PyList_Size( pResult ) );

	PyObject * pEntry = PyList_GetItem( pResult, 0 );
	CHECK( pEntry );
	CHECK( PyTuple_Check( pEntry ) );
	CHECK_EQUAL( 3, PyTuple_Size( pEntry ) );

	CHECK_EQUAL( Watcher::WT_READ_WRITE,
		PyLong_AsLong( PyTuple_GetItem( pEntry, 0 ) ) );
	CHECK_EQUAL( BW::string( "entry" ),
		BW::string( PyUnicode_AsUTF8( PyTuple_GetItem( pEntry, 1 ) ) ) );
	CHECK_EQUAL( BW::string( "42" ),
		BW::string( PyUnicode_AsUTF8( PyTuple_GetItem( pEntry, 2 ) ) ) );

	Py_DECREF( pResult );

	// A missing directory raises the documented TypeError.
	pResult = askBigWorld( pBigWorld, "getWatcherDir",
		Py_BuildValue( "(s)", "test/pywatcherdir/nonexistent" ),
		"PyWatcher_getWatcherDirMissing" );
	CHECK( pResult == NULL );
	PyErr_Clear();

	Py_DECREF( pBigWorld );
}


// -----------------------------------------------------------------------------
// Section: BigWorld.addWatcher (PyAccessorWatcher) and delWatcher
// -----------------------------------------------------------------------------

// addWatcher installs a Python getter/setter pair that getWatcher/setWatcher
// then drive through the watcher tree; delWatcher removes it again.
TEST_F( PyScriptUnitTestHarness, PyWatcher_addAndDelWatcher )
{
	PyObject * pBigWorld = PyImport_ImportModule( "BigWorld" );
	CHECK( pBigWorld );

	PyObject * pModule = PyImport_ImportModule( "test_pywatcher" );
	CHECK( pModule );

	PyObject * pGetFunc = PyObject_GetAttrString( pModule, "watcherGet" );
	CHECK( pGetFunc );

	PyObject * pSetFunc = PyObject_GetAttrString( pModule, "watcherSet" );
	CHECK( pSetFunc );

	PyObject * pResult = askBigWorld( pBigWorld, "addWatcher",
		Py_BuildValue( "(sOOs)", "test/pywatcher/accessor",
			pGetFunc, pSetFunc, "accessor watcher" ),
		"PyWatcher_addWatcher" );
	CHECK( pResult );
	Py_DECREF( pResult );

	Py_DECREF( pGetFunc );
	Py_DECREF( pSetFunc );

	// Read through the accessor watcher.
	pResult = askBigWorld( pBigWorld, "getWatcher",
		Py_BuildValue( "(s)", "test/pywatcher/accessor" ),
		"PyWatcher_accessorGet" );
	CHECK( pResult );
	CHECK_EQUAL( BW::string( "hello" ), BW::string( PyUnicode_AsUTF8( pResult ) ) );
	Py_DECREF( pResult );

	// Write through the accessor watcher; the Python global must change.
	pResult = askBigWorld( pBigWorld, "setWatcher",
		Py_BuildValue( "(ss)", "test/pywatcher/accessor", "world" ),
		"PyWatcher_accessorSet" );
	CHECK( pResult );
	Py_DECREF( pResult );

	PyObject * pValue = PyObject_GetAttrString( pModule, "g_value" );
	CHECK( pValue );
	CHECK_EQUAL( BW::string( "world" ), BW::string( PyUnicode_AsUTF8( pValue ) ) );
	Py_DECREF( pValue );

	// addWatcher rejects non-callable getters.
	pResult = askBigWorld( pBigWorld, "addWatcher",
		Py_BuildValue( "(sOOs)", "test/pywatcher/bad", Py_None, Py_None,
			"desc" ),
		"PyWatcher_addWatcherBad" );
	CHECK( pResult == NULL );
	PyErr_Clear();

	// delWatcher removes the path; a subsequent read fails.
	pResult = askBigWorld( pBigWorld, "delWatcher",
		Py_BuildValue( "(s)", "test/pywatcher/accessor" ),
		"PyWatcher_delWatcher" );
	CHECK( pResult );
	Py_DECREF( pResult );

	pResult = askBigWorld( pBigWorld, "getWatcher",
		Py_BuildValue( "(s)", "test/pywatcher/accessor" ),
		"PyWatcher_afterDel" );
	CHECK( pResult == NULL );
	PyErr_Clear();

	// Deleting it again fails with the documented ValueError.
	pResult = askBigWorld( pBigWorld, "delWatcher",
		Py_BuildValue( "(s)", "test/pywatcher/accessor" ),
		"PyWatcher_delWatcherAgain" );
	CHECK( pResult == NULL );
	PyErr_Clear();

	Py_DECREF( pModule );
	Py_DECREF( pBigWorld );
}


// -----------------------------------------------------------------------------
// Section: BigWorld.addFunctionWatcher (PyFunctionWatcher)
// -----------------------------------------------------------------------------

// A function watcher registered with a single string argument is invoked via
// the watcher stream protocol, and its side effects are visible in the
// module global it recorded.
TEST_F( PyScriptUnitTestHarness, PyWatcher_addFunctionWatcher )
{
	PyObject * pBigWorld = PyImport_ImportModule( "BigWorld" );
	CHECK( pBigWorld );

	PyObject * pModule = PyImport_ImportModule( "test_pywatcher" );
	CHECK( pModule );

	PyObject * pTarget = PyObject_GetAttrString( pModule, "functionWatcher" );
	CHECK( pTarget );

	// The argument list is a sequence of (name, type) tuples.
	PyObject * pArgList = Py_BuildValue( "[(s,O)]", "arg",
		(PyObject *)&PyUnicode_Type );
	CHECK( pArgList );

	PyObject * pResult = askBigWorld( pBigWorld, "addFunctionWatcher",
		Py_BuildValue( "(sOOis)", "command/testPyWatcherEcho", pTarget,
			pArgList, 0 /* EXPOSE_NONE */, "echo function watcher" ),
		"PyWatcher_addFunctionWatcher" );
	CHECK( pResult );
	Py_DECREF( pResult );

	Py_DECREF( pArgList );
	Py_DECREF( pTarget );

	// Invoke the function watcher through the stream protocol: the argument
	// list is streamed as a WATCHER_TYPE_TUPLE whose entries are encoded
	// like any other watcher value.
	{
		MemoryOStream valueStream;
		valueStream << (char)WATCHER_TYPE_TUPLE;
		valueStream.writePackedInt( 0 );	// payload size, unused on success
		valueStream.writePackedInt( 1 );	// argument count
		watcherValueToStream( valueStream, BW::string( "payload" ),
			Watcher::WT_READ_WRITE );

		WatcherPathRequestV2 pathRequest( "command/testPyWatcherEcho" );
		CHECK( pathRequest.setPacketData( valueStream ) );

		CHECK( Watcher::rootWatcher().setFromStream( NULL,
			"command/testPyWatcherEcho", pathRequest ) );

		// The callable ran and recorded its argument.
		PyObject * pLastArg = PyObject_GetAttrString( pModule, "g_lastArg" );
		CHECK( pLastArg );
		CHECK_EQUAL( BW::string( "payload" ),
		BW::string( PyUnicode_AsUTF8( pLastArg ) ) );
		Py_DECREF( pLastArg );
	}

	// Clean up the function watcher.
	pResult = askBigWorld( pBigWorld, "delWatcher",
		Py_BuildValue( "(s)", "command/testPyWatcherEcho" ),
		"PyWatcher_delFunctionWatcher" );
	CHECK( pResult );
	Py_DECREF( pResult );

	Py_DECREF( pModule );
	Py_DECREF( pBigWorld );
}


// -----------------------------------------------------------------------------
// Section: PyObjectWatcher
// -----------------------------------------------------------------------------

// PyObjectWatcher renders the watched object through str() and replaces it
// by evaluating set strings as Python source. Sets go through a
// PyObjectPtrRef, so only the reference is swapped, never the object itself.
TEST_F( PyScriptUnitTestHarness, PyWatcher_pyObjectWatcher )
{
	PyObject * pObject = PyLong_FromLong( 42 );
	CHECK( pObject );

	{
		PyObjectPtrRefSimple ref( pObject );
		PyObjectWatcher watcher( ref );

		// getAsString renders the current object and reports it read-write.
		{
			BW::string result;
			BW::string desc;
			Watcher::Mode mode = Watcher::WT_READ_ONLY;

			CHECK( watcher.getAsString( NULL, "", result, desc, mode ) );
			CHECK_EQUAL( BW::string( "42" ), result );
			CHECK_EQUAL( Watcher::WT_READ_WRITE, mode );
		}

		// setFromString evaluates the value as Python source.
		{
			CHECK( watcher.setFromString( NULL, "", "43" ) );

			PyObject * pCurrent = ref;
			CHECK( pCurrent != NULL );
			CHECK_EQUAL( 43, PyLong_AsLong( pCurrent ) );
			Py_DECREF( pCurrent );
		}

		// No coercion into the previous type is attempted (py_to_stl.cpp
		// leaves the object as whatever the source evaluated to).
		{
			CHECK( watcher.setFromString( NULL, "", "'text'" ) );

			PyObject * pCurrent = ref;
			CHECK( PyUnicode_Check( pCurrent ) );
			Py_DECREF( pCurrent );
		}

		// A string-typed SET2 payload takes the same evaluation path.
		{
			MemoryOStream valueStream;
			watcherValueToStream( valueStream, BW::string( "44" ),
				Watcher::WT_READ_WRITE );

			WatcherPathRequestV2 pathRequest( "value" );
			CHECK( pathRequest.setPacketData( valueStream ) );

			CHECK( watcher.setFromStream( NULL, "", pathRequest ) );

			PyObject * pCurrent = ref;
			CHECK( pCurrent != NULL );
			CHECK_EQUAL( 44, PyLong_AsLong( pCurrent ) );
			Py_DECREF( pCurrent );
		}

		// getAsStream streams the rendered value out.
		{
			WatcherPathRequestV2 pathRequest( "value" );
			CHECK( watcher.getAsStream( NULL, "", pathRequest ) );
			CHECK( pathRequest.getDataSize() > 0 );
		}
	}

	// The watcher only ever swapped the reference; our own reference to the
	// initial object is still the sole one left.
	Py_DECREF( pObject );
}

} // namespace BW

// test_pywatcher.cpp
