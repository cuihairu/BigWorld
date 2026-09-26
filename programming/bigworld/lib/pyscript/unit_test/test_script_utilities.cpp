#include "pch.hpp"
#include "test_harness.hpp"

#include "pyscript/py_import_paths.hpp"
#include "pyscript/python_input_substituter.hpp"
#include "pyscript/script.hpp"

#include "resmgr/bwresource.hpp"


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

/**
 *	Evaluates the given Python source in the __main__ module namespace.
 *	Prints the failure reason and returns false on error.
 */
bool runPython( const char * source )
{
	PyObject * pMain = PyImport_AddModule( "__main__" );
	PyObject * pGlobals = PyModule_GetDict( pMain );

	PyObject * pResult = PyRun_String( source, Py_file_input,
		pGlobals, pGlobals );

	if (pResult == NULL)
	{
		PyObject * pType = NULL;
		PyObject * pValue = NULL;
		PyObject * pTraceback = NULL;
		PyErr_Fetch( &pType, &pValue, &pTraceback );
		PyErr_NormalizeException( &pType, &pValue, &pTraceback );

		fprintf( stderr, "runPython failed: type=%s value=%s\n",
			(pType != NULL) ? ((PyTypeObject *)pType)->tp_name : "(none)",
			(pValue != NULL) ?
				PyUnicode_AsUTF8( PyObject_Str( pValue ) ) : "(no value)" );
		Py_XDECREF( pType );
		Py_XDECREF( pValue );
		Py_XDECREF( pTraceback );
		return false;
	}

	Py_DECREF( pResult );
	return true;
}


/**
 *	Returns the name of the exception currently raised ("" if none) and
 *	consumes it, so a pending error never leaks into the next check.
 */
BW::string currentExceptionType()
{
	if (!PyErr_Occurred())
	{
		return BW::string();
	}

	PyObject * pType = NULL;
	PyObject * pValue = NULL;
	PyObject * pTraceback = NULL;
	PyErr_Fetch( &pType, &pValue, &pTraceback );

	BW::string result( (pType != NULL) ?
		((PyTypeObject*)pType)->tp_name : "" );

	Py_XDECREF( pType );
	Py_XDECREF( pValue );
	Py_XDECREF( pTraceback );

	return result;
}


/**
 *	Creates a throwaway module registered under the given name.
 */
PyObject * scratchModule( const char * name )
{
	return PyImport_AddModule( name );
}


} // end namespace (anonymous)


// -----------------------------------------------------------------------------
// Section: PyImportPaths
// -----------------------------------------------------------------------------

// Non-res paths are kept in insertion order, deduplicated, and joined with
// the configured delimiter.
TEST_F( PyScriptUnitTestHarness, PyImportPaths_joinAndDedupe )
{
	PyImportPaths paths;
	CHECK( paths.empty() );
	CHECK_EQUAL( BW::string(), paths.pathsAsString() );

	paths.addNonResPath( "alpha" );
	paths.addNonResPath( "beta" );
	paths.addNonResPath( "alpha" );	// duplicate is ignored

	CHECK( !paths.empty() );
	CHECK_EQUAL( BW::string( "alpha;beta" ), paths.pathsAsString() );

	// The delimiter is configurable.
	paths.setDelimiter( ':' );
	CHECK_EQUAL( BW::string( "alpha:beta" ), paths.pathsAsString() );

	// append() keeps the other paths after ours, again deduplicated.
	PyImportPaths other;
	other.addNonResPath( "gamma" );
	other.addNonResPath( "beta" );	// duplicate of ours

	paths.append( other );
	paths.setDelimiter( PyImportPaths::DEFAULT_DELIMITER );
	CHECK_EQUAL( BW::string( "alpha;beta;gamma" ), paths.pathsAsString() );
}


// A res path expands to one entry per BWResource path.
TEST_F( PyScriptUnitTestHarness, PyImportPaths_resPathExpansion )
{
	const int numResPaths = BWResource::getPathNum();
	CHECK( numResPaths >= 1 );

	PyImportPaths paths;
	paths.addResPath( "scripts/subdir" );

	// One entry per res path, each ending with the given sub path.
	BW::vector< BW::string > expected;
	for (int i = 0; i < numResPaths; ++i)
	{
		expected.push_back(
			BWResource::getPath( i ) + "/scripts/subdir" );
	}

	// (Compare over a stored copy: begin()/end() must come from the same
	// string, and pathsAsString() hands back a fresh temporary each call.)
	BW::string joined = paths.pathsAsString();
	CHECK_EQUAL( expected.size(),
		size_t( std::count( joined.begin(), joined.end(), ';' ) ) + 1 );

	// Re-adding the same res path changes nothing (deduplicated).
	paths.addResPath( "scripts/subdir" );
	joined = paths.pathsAsString();
	CHECK_EQUAL( expected.size(),
		size_t( std::count( joined.begin(), joined.end(), ';' ) ) + 1 );
}


// pathAsObject exposes the same paths as a Python list of strings.
TEST_F( PyScriptUnitTestHarness, PyImportPaths_pathAsObject )
{
	PyImportPaths paths;
	paths.addNonResPath( "one" );
	paths.addNonResPath( "two" );

	PyObjectPtr pList( paths.pathAsObject(), PyObjectPtr::STEAL_REFERENCE );
	CHECK( pList.get() != NULL );
	CHECK( PyList_Check( pList.get() ) );
	CHECK_EQUAL( 2, int( PyList_Size( pList.get() ) ) );

	CHECK_EQUAL( BW::string( "one" ),
		BW::string( PyUnicode_AsUTF8( PyList_GetItem( pList.get(), 0 ) ) ) );
	CHECK_EQUAL( BW::string( "two" ),
		BW::string( PyUnicode_AsUTF8( PyList_GetItem( pList.get(), 1 ) ) ) );

	// An empty collection yields an empty list.
	PyImportPaths empty;
	PyObjectPtr pEmpty( empty.pathAsObject(), PyObjectPtr::STEAL_REFERENCE );
	CHECK( pEmpty.get() != NULL );
	CHECK_EQUAL( 0, int( PyList_Size( pEmpty.get() ) ) );
}


// -----------------------------------------------------------------------------
// Section: PythonInputSubstituter
// -----------------------------------------------------------------------------

// Dollar-substitution consults the named function on the given module; a
// missing function falls through with the line untouched (leaving its
// lookup error pending for the caller to clear), while unusable results
// produce an empty string.
TEST_F( PyScriptUnitTestHarness, InputSubstituter_behaviour )
{
	const char * MODULE = "subst_scratch_module";

	// --- Function missing: line comes back unchanged. ---
	PyObject * pModule = scratchModule( MODULE );
	BW::string line = PythonInputSubstituter::substitute(
		"no expansion here", pModule, "missingFn" );
	CHECK_EQUAL( BW::string( "no expansion here" ), line );
	// The failed attribute lookup leaves its exception pending.
	PyErr_Clear();

	// --- Function present but not callable: empty string. ---
	CHECK( runPython(
		"import subst_scratch_module\n"
		"subst_scratch_module.notCallable = 42\n" ) );
	line = PythonInputSubstituter::substitute( "x", pModule, "notCallable" );
	CHECK_EQUAL( BW::string(), line );
	CHECK( !PyErr_Occurred() );

	// --- Function that raises: empty string. ---
	CHECK( runPython(
		"def raises( line ):\n"
		"\traise RuntimeError( 'boom' )\n"
		"subst_scratch_module.raises = raises\n" ) );
	line = PythonInputSubstituter::substitute( "x", pModule, "raises" );
	CHECK_EQUAL( BW::string(), line );
	CHECK( !PyErr_Occurred() );

	// --- Function returning a non-string: empty string. ---
	CHECK( runPython(
		"def notAString( line ):\n"
		"\treturn 7\n"
		"subst_scratch_module.notAString = notAString\n" ) );
	line = PythonInputSubstituter::substitute( "x", pModule, "notAString" );
	CHECK_EQUAL( BW::string(), line );
	CHECK( !PyErr_Occurred() );

	// --- The happy path: the line goes in, the expansion comes out. ---
	CHECK( runPython(
		"def expand( line ):\n"
		"\treturn 'expanded:' + line\n"
		"subst_scratch_module.expand = expand\n" ) );
	line = PythonInputSubstituter::substitute( "in", pModule, "expand" );
	CHECK_EQUAL( BW::string( "expanded:in" ), line );

	// --- No module at all: with no personality module loaded the line is
	// returned unchanged (no error left behind). ---
	line = PythonInputSubstituter::substitute(
		"as is", NULL, "expand" );
	CHECK_EQUAL( BW::string( "as is" ), line );
}


// -----------------------------------------------------------------------------
// Section: PyLogging
// -----------------------------------------------------------------------------

// The BigWorld.log* module functions accept (category, message, metaData)
// where metaData may be None or a JSON string; malformed arguments are
// rejected with TypeError.
TEST_F( PyScriptUnitTestHarness, PyLogging_moduleFunctions )
{
	PyObjectPtr pBigWorld( PyImport_ImportModule( "BigWorld" ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pBigWorld.get() != NULL );
	if (pBigWorld.get() == NULL)
	{
		PyErr_Clear();
		return;
	}

	// All eight severities exist and accept a well-formed call.
	static const char * severities[] =
	{
		"logTrace", "logDebug", "logInfo", "logNotice",
		"logWarning", "logError", "logCritical", "logHack"
	};
	const int numSeverities = sizeof( severities ) / sizeof( severities[0] );

	for (int i = 0; i < numSeverities; ++i)
	{
		PyErr_Clear();
		PyObjectPtr pRet( PyObject_CallMethod( pBigWorld.get(),
			severities[ i ], "ssO", "TestCategory", "test message",
			Py_None ),
			PyObjectPtr::STEAL_REFERENCE );
		CHECK( pRet.get() == Py_None );
		if (pRet.get() != Py_None)
		{
			CHECK_EQUAL( BW::string(), currentExceptionType() );
		}
	}

	// A JSON string as the meta data takes the metaAsJSON path.
	PyObjectPtr pMeta( PyObject_CallMethod( pBigWorld.get(), "logInfo",
		"sss", "TestCategory", "with meta", "{\"k\": 1}" ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pMeta.get() == Py_None );

	// --- Error paths ---

	// Wrong number of elements.
	{
		PyObjectPtr pFn( PyObject_GetAttrString( pBigWorld.get(),
			"logInfo" ), PyObjectPtr::STEAL_REFERENCE );
		CHECK( pFn.get() != NULL );
		PyObjectPtr pTwoArgs( Py_BuildValue( "(ss)", "cat", "msg" ),
			PyObjectPtr::STEAL_REFERENCE );
		PyErr_Clear();
		CHECK( PyObject_Call( pFn.get(), pTwoArgs.get(), NULL ) == NULL );
		CHECK_EQUAL( BW::string( "TypeError" ), currentExceptionType() );
	}

	// Non-string category / message / metaData (None is only allowed for
	// metaData).
	PyErr_Clear();
	CHECK( PyObject_CallMethod( pBigWorld.get(), "logInfo",
		"isi", 1, "msg", 2 ) == NULL );
	CHECK_EQUAL( BW::string( "TypeError" ), currentExceptionType() );

	PyErr_Clear();
	CHECK( PyObject_CallMethod( pBigWorld.get(), "logInfo",
		"sss", "cat", "msg", "meta" ) != NULL );

	PyErr_Clear();
	CHECK( PyObject_CallMethod( pBigWorld.get(), "logInfo",
		"ssO", "cat", "msg", Py_None ) != NULL );

	// metaData as a non-string non-None object is rejected.
	PyErr_Clear();
	CHECK( PyObject_CallMethod( pBigWorld.get(), "logInfo",
		"ssi", "cat", "msg", 5 ) == NULL );
	CHECK_EQUAL( BW::string( "TypeError" ), currentExceptionType() );

	CHECK( !PyErr_Occurred() );
}


BW_END_NAMESPACE

// test_script_utilities.cpp
