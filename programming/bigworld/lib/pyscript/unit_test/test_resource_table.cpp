#include "pch.hpp"
#include "test_harness.hpp"

#include "pyscript/resource_table.hpp"
#include "pyscript/script.hpp"
#include "resmgr/bwresource.hpp"


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

const char RESOURCE_TABLE_XML[] = "test_resource_table.xml";


/**
 *	Opens the fixture resource table as a new reference, or NULL.
 */
PyObject * openTable()
{
	return ResourceTable::New( RESOURCE_TABLE_XML, NULL );
}


/**
 *	Evaluates the given Python source in the __main__ module namespace.
 *	Prints the traceback and returns false on error.
 */
bool runPython( const char * source )
{
	PyObject * pMain = PyImport_AddModule( "__main__" );
	PyObject * pGlobals = PyModule_GetDict( pMain );

	PyObject * pResult = PyRun_String( source, Py_file_input,
		pGlobals, pGlobals );

	if (pResult == NULL)
	{
		PyErr_Print();
		return false;
	}

	Py_DECREF( pResult );
	return true;
}


/**
 *	Reads a string attribute off an object; empty on failure.
 */
BW::string attributeString( PyObject * pObj, const char * name )
{
	PyObject * pValue = PyObject_GetAttrString( pObj, name );
	if ((pValue == NULL) || !PyUnicode_Check( pValue ))
	{
		Py_XDECREF( pValue );
		return BW::string();
	}

	BW::string result( PyUnicode_AsUTF8( pValue ) );
	Py_DECREF( pValue );
	return result;
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

} // end namespace (anonymous)


// The table loads from the XML fixture: one entry per <key> section,
// addressable by index (with negative indices) and by key name.
TEST_F( PyScriptUnitTestHarness, ResourceTable_openNavigateMapping )
{
	ResourceTablePtr pTable( (ResourceTable *)openTable(), /* alreadyIncremented */ true );
	CHECK( pTable.get() != NULL );

	// Two <key> sections in the fixture.
	CHECK_EQUAL( 2, int( PyObject_Size( pTable.get() ) ) );

	// at() hands out sub-tables whose key/index attributes agree.
	PyObjectPtr pAlpha( pTable->at( 0 ), PyObjectPtr::STEAL_REFERENCE );
	CHECK( pAlpha.get() != NULL );
	CHECK_EQUAL( BW::string( "alpha" ),
		attributeString( pAlpha.get(), "key" ) );
	PyObjectPtr pAlphaIndex( PyObject_GetAttrString( pAlpha.get(), "index" ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pAlphaIndex.get() != NULL );
	CHECK( PyLong_AsLong( pAlphaIndex.get() ) == 0 );

	PyObjectPtr pBeta( pTable->at( 1 ), PyObjectPtr::STEAL_REFERENCE );
	CHECK( pBeta.get() != NULL );
	CHECK_EQUAL( BW::string( "beta" ),
		attributeString( pBeta.get(), "key" ) );

	// Negative indices are relative to the end.
	PyObjectPtr pLast( pTable->at( -1 ), PyObjectPtr::STEAL_REFERENCE );
	CHECK( pLast.get() == pBeta.get() );

	// Out of range is a ValueError.
	PyErr_Clear();
	CHECK( pTable->at( 2 ) == NULL );
	CHECK_EQUAL( BW::string( "ValueError" ), currentExceptionType() );

	PyErr_Clear();
	CHECK( pTable->at( -3 ) == NULL );
	CHECK_EQUAL( BW::string( "ValueError" ), currentExceptionType() );

	// sub() looks up by key text.
	PyObjectPtr pSubBeta( pTable->sub( "beta" ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pSubBeta.get() == pBeta.get() );

	PyErr_Clear();
	CHECK( pTable->sub( "nope" ) == NULL );
	CHECK_EQUAL( BW::string( "ValueError" ), currentExceptionType() );
	PyErr_Clear();

	// Mapping subscript accepts both integers and key strings.
	PyObjectPtr pOne( PyLong_FromLong( 1 ), PyObjectPtr::STEAL_REFERENCE );
	PyObjectPtr pIndexed( PyObject_GetItem( pTable.get(), pOne.get() ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pIndexed.get() == pBeta.get() );

	PyObjectPtr pKeyStr( PyUnicode_FromString( "alpha" ),
		PyObjectPtr::STEAL_REFERENCE );
	PyObjectPtr pByKey( PyObject_GetItem( pTable.get(), pKeyStr.get() ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pByKey.get() == pAlpha.get() );

	// A key that is neither index nor string fails (the last conversion
	// attempt's error stays set).
	PyErr_Clear();
	PyObjectPtr pFloatKey( PyFloat_FromDouble( 3.5 ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( PyObject_GetItem( pTable.get(), pFloatKey.get() ) == NULL );
	CHECK( PyErr_Occurred() != NULL );
	PyErr_Clear();

	// keyOfIndex/indexOfKey conversions, with the documented error
	// sentinels (empty string / -1) and cleared errors.
	CHECK_EQUAL( BW::string( "alpha" ), pTable->keyOfIndex( 0 ) );
	CHECK_EQUAL( BW::string( "beta" ), pTable->keyOfIndex( 1 ) );
	CHECK_EQUAL( BW::string(), pTable->keyOfIndex( 9 ) );
	CHECK( !PyErr_Occurred() );
	CHECK_EQUAL( 0, pTable->indexOfKey( "alpha" ) );
	CHECK_EQUAL( 1, pTable->indexOfKey( "beta" ) );
	CHECK_EQUAL( -1, pTable->indexOfKey( "nope" ) );
	CHECK( !PyErr_Occurred() );

	// The root table itself has no key text.
	CHECK_EQUAL( BW::string(), pTable->key() );
}


// New() hands back the census-cached object for the same resource.
TEST_F( PyScriptUnitTestHarness, ResourceTable_censusIdentity )
{
	PyObjectPtr pFirst( openTable(), PyObjectPtr::STEAL_REFERENCE );
	PyObjectPtr pSecond( openTable(), PyObjectPtr::STEAL_REFERENCE );
	CHECK( pFirst.get() != NULL );
	CHECK( pFirst.get() == pSecond.get() );
}


// The value attribute exposes the <value> section as a ResTblStruct with
// best-effort type guessing; sub-table values inherit from their parent
// table's value.
TEST_F( PyScriptUnitTestHarness, ResourceTable_values )
{
	ResourceTablePtr pTable( (ResourceTable *)openTable(), /* alreadyIncremented */ true );
	CHECK( pTable.get() != NULL );

	// The root's value struct has no backing section: every lookup falls
	// through to the default.
	PyObjectPtr pRootValue( PyObject_GetAttrString( pTable.get(), "value" ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pRootValue.get() != NULL );

	PyObjectPtr pMissing( PyObject_CallMethod( pRootValue.get(), "get",
		"s", "anything" ), PyObjectPtr::STEAL_REFERENCE );
	CHECK( pMissing.get() == Py_None );

	PyObjectPtr pDefault( PyObject_CallMethod( pRootValue.get(), "get",
		"ss", "anything", "dflt" ), PyObjectPtr::STEAL_REFERENCE );
	CHECK_EQUAL( BW::string( "dflt" ),
		BW::string( PyUnicode_AsUTF8( pDefault.get() ) ) );

	// Alpha's values are guessed from their text forms.
	PyObjectPtr pAlpha( pTable->at( 0 ), PyObjectPtr::STEAL_REFERENCE );
	PyObjectPtr pAlphaValue( PyObject_GetAttrString( pAlpha.get(), "value" ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pAlphaValue.get() != NULL );

	// Attribute access goes through the value lookup too.
	CHECK_EQUAL( BW::string( "Alpha Table" ),
		attributeString( pAlphaValue.get(), "name" ) );

	PyObjectPtr pNum( PyObject_CallMethod( pAlphaValue.get(), "get",
		"s", "num" ), PyObjectPtr::STEAL_REFERENCE );
	CHECK( pNum.get() != NULL );
	CHECK( PyLong_Check( pNum.get() ) );
	CHECK( PyLong_AsLong( pNum.get() ) == 42 );

	PyObjectPtr pRatio( PyObject_CallMethod( pAlphaValue.get(), "get",
		"s", "ratio" ), PyObjectPtr::STEAL_REFERENCE );
	CHECK( pRatio.get() != NULL );
	CHECK( PyFloat_Check( pRatio.get() ) );
	CHECK_CLOSE( 1.5, PyFloat_AsDouble( pRatio.get() ), 0.0001 );

	PyObjectPtr pFlag( PyObject_CallMethod( pAlphaValue.get(), "get",
		"s", "flag" ), PyObjectPtr::STEAL_REFERENCE );
	CHECK( pFlag.get() == Py_True );

	PyObjectPtr pGreeting( PyObject_CallMethod( pAlphaValue.get(), "get",
		"s", "greeting" ), PyObjectPtr::STEAL_REFERENCE );
	CHECK_EQUAL( BW::string( "hello world" ),
		BW::string( PyUnicode_AsUTF8( pGreeting.get() ) ) );

	// Missing keys fall back to None or the given default.
	PyObjectPtr pAlphaMissing( PyObject_CallMethod( pAlphaValue.get(),
		"get", "s", "missing" ), PyObjectPtr::STEAL_REFERENCE );
	CHECK( pAlphaMissing.get() == Py_None );

	PyObjectPtr pAlphaDefault( PyObject_CallMethod( pAlphaValue.get(),
		"get", "ss", "missing", "fallback" ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK_EQUAL( BW::string( "fallback" ),
		BW::string( PyUnicode_AsUTF8( pAlphaDefault.get() ) ) );

	// Attributes that are neither values nor real attributes raise
	// AttributeError.
	PyErr_Clear();
	CHECK( PyObject_GetAttrString( pAlphaValue.get(), "missing" ) == NULL );
	CHECK_EQUAL( BW::string( "AttributeError" ), currentExceptionType() );

	// Beta's value struct inherits from the root's (empty) one: keys beta
	// does not define are not found.
	PyObjectPtr pBeta( pTable->at( 1 ), PyObjectPtr::STEAL_REFERENCE );
	PyObjectPtr pBetaValue( PyObject_GetAttrString( pBeta.get(), "value" ),
		PyObjectPtr::STEAL_REFERENCE );

	CHECK_EQUAL( BW::string( "Beta Table" ),
		attributeString( pBetaValue.get(), "name" ) );

	PyObjectPtr pBetaNum( PyObject_CallMethod( pBetaValue.get(), "get",
		"s", "num" ), PyObjectPtr::STEAL_REFERENCE );
	CHECK( pBetaNum.get() == Py_None );
}


// link()/unlink() manage the update-fn set; link() with callNow (the
// Python-facing default) invokes it immediately with the table.
TEST_F( PyScriptUnitTestHarness, ResourceTable_linkUnlink )
{
	ResourceTablePtr pTable( (ResourceTable *)openTable(), /* alreadyIncremented */ true );
	CHECK( pTable.get() != NULL );

	CHECK( runPython(
		"rt_calls = []\n"
		"def upd( tbl ):\n"
		"\trt_calls.append( tbl )\n" ) );

	PyObject * pMain = PyImport_AddModule( "__main__" );
	PyObjectPtr pUpd( PyObject_GetAttrString( pMain, "upd" ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pUpd.get() != NULL );

	// link() calls the update fn immediately with the table itself.
	PyObjectPtr pLinkRet( PyObject_CallMethod( pTable.get(), "link", "O",
		pUpd.get() ), PyObjectPtr::STEAL_REFERENCE );
	CHECK( pLinkRet.get() != NULL );

	PyObjectPtr pCalls( PyObject_GetAttrString( pMain, "rt_calls" ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pCalls.get() != NULL );
	CHECK_EQUAL( 1, int( PyList_Size( pCalls.get() ) ) );
	CHECK( PyList_GetItem( pCalls.get(), 0 ) == pTable.get() );

	// A non-callable is rejected with TypeError.
	PyErr_Clear();
	CHECK( PyObject_CallMethod( pTable.get(), "link", "i", 42 ) == NULL );
	CHECK_EQUAL( BW::string( "TypeError" ), currentExceptionType() );

	// unlink() is silent about whether the fn was linked.
	PyObjectPtr pUnlinkRet( PyObject_CallMethod( pTable.get(), "unlink",
		"O", pUpd.get() ), PyObjectPtr::STEAL_REFERENCE );
	CHECK( pUnlinkRet.get() != NULL );

	// New() with an update fn links it without calling; the census also
	// means we get the same table object back.
	ResourceTablePtr pRelinked( (ResourceTable *)ResourceTable::New(
		RESOURCE_TABLE_XML, pUpd.get() ), true );
	CHECK( pRelinked.get() == pTable.get() );
	CHECK_EQUAL( 1, int( PyList_Size( pCalls.get() ) ) );

	// New() with a non-callable update fn is a TypeError...
	PyObjectPtr pNotCallable( PyLong_FromLong( 7 ),
		PyObjectPtr::STEAL_REFERENCE );
	PyErr_Clear();
	CHECK( ResourceTable::New( RESOURCE_TABLE_XML,
		pNotCallable.get() ) == NULL );
	CHECK_EQUAL( BW::string( "TypeError" ), currentExceptionType() );

	// ...and a missing resource is a ValueError.
	PyErr_Clear();
	CHECK( ResourceTable::New( "no_such_resource_table_xyz.xml",
		NULL ) == NULL );
	CHECK_EQUAL( BW::string( "ValueError" ), currentExceptionType() );
}


BW_END_NAMESPACE

// test_resource_table.cpp
