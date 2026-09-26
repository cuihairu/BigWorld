#include "pch.hpp"
#include "test_harness.hpp"

#include "pyscript/keyword_parser.hpp"
#include "pyscript/pickler.hpp"
#include "pyscript/personality.hpp"
#include "pyscript/py_debug_message_file_logger.hpp"
#include "pyscript/py_factory_method_link.hpp"
#include "pyscript/script.hpp"
#include "resmgr/bwresource.hpp"


BW_BEGIN_NAMESPACE

#if !defined( BW_BLOB_CONFIG )
// The ResMgr module functions are registered by res_mgr_script.o, but no
// test here references its C++ symbols - the archive member would never be
// pulled into the link and the Python module would not exist. Its token is
// the only non-static symbol in that file, so force the link with it.
extern int ResMgr_token;
volatile int g_p7ForceResMgrLink = ResMgr_token;
#endif // !defined( BW_BLOB_CONFIG )

namespace // (anonymous)
{

/**
 *	Evaluates the expression in __main__ and hands back the result as a
 *	ScriptObject (adopting the new reference runString returns).
 */
ScriptObject evalObject( const char * expr )
{
	return ScriptObject( Script::runString( expr, false ),
		ScriptObject::STEAL_REFERENCE );
}


/**
 *	Returns whether two Python objects compare equal, ignoring any error
 *	left behind by the comparison.
 */
bool pythonEqual( PyObject * lhs, PyObject * rhs )
{
	int result = PyObject_RichCompareBool( lhs, rhs, Py_EQ );
	if (result == -1)
	{
		PyErr_Clear();
		return false;
	}
	return result == 1;
}


/**
 *	Returns the message of the exception currently raised ("" if none) and
 *	consumes it, so a pending error never leaks into the next check.
 */
BW::string currentExceptionMessage()
{
	if (!PyErr_Occurred())
	{
		return BW::string();
	}

	PyObject * pType = NULL;
	PyObject * pValue = NULL;
	PyObject * pTraceback = NULL;
	PyErr_Fetch( &pType, &pValue, &pTraceback );

	BW::string result;
	if (pValue != NULL)
	{
		PyObject * pStr = PyObject_Str( pValue );
		if (pStr != NULL)
		{
			result.assign( PyUnicode_AsUTF8( pStr ) );
			Py_DECREF( pStr );
		}
	}

	Py_XDECREF( pType );
	Py_XDECREF( pValue );
	Py_XDECREF( pTraceback );

	return result;
}


/**
 *	Builds a dict of keyword arguments: an int, a float and a string, plus
 *	optionally an extra unregistered key.
 */
PyObjectPtr makeKeywordDict( bool withExtra )
{
	PyObjectPtr pDict( PyDict_New(), PyObjectPtr::STEAL_REFERENCE );

	PyObjectPtr pInt( PyLong_FromLong( 7 ),
		PyObjectPtr::STEAL_REFERENCE );
	PyObjectPtr pFloat( PyFloat_FromDouble( 2.5 ),
		PyObjectPtr::STEAL_REFERENCE );
	PyObjectPtr pStr( PyUnicode_FromString( "hello" ),
		PyObjectPtr::STEAL_REFERENCE );

	PyDict_SetItemString( pDict.get(), "anInt", pInt.get() );
	PyDict_SetItemString( pDict.get(), "aFloat", pFloat.get() );
	PyDict_SetItemString( pDict.get(), "aString", pStr.get() );

	if (withExtra)
	{
		PyDict_SetItemString( pDict.get(), "nope", Py_True );
	}

	return pDict;
}

} // end namespace (anonymous)


// -----------------------------------------------------------------------------
// Section: KeywordParser
// -----------------------------------------------------------------------------

// A full dictionary parses to ALL_FOUND: every registered keyword is pulled
// through its Script::setData converter into the caller's variables, and the
// consumed entries are removed from the dict by default.
TEST_F( PyScriptUnitTestHarness, KeywordParser_allFoundRemovesEntries )
{
	PyObjectPtr pDict = makeKeywordDict( /*withExtra*/ false );

	int intValue = 0;
	float floatValue = 0.f;
	BW::string stringValue;

	KeywordParser parser;
	parser.add< int >( "anInt", intValue );
	parser.add< float >( "aFloat", floatValue );
	parser.add< BW::string >( "aString", stringValue );

	CHECK_EQUAL( KeywordParser::ALL_FOUND, parser.parse( pDict.get() ) );
	CHECK_EQUAL( 7, intValue );
	CHECK_CLOSE( 2.5f, floatValue, 0.0001f );
	CHECK_EQUAL( BW::string( "hello" ), stringValue );
	CHECK_EQUAL( 0, int( PyDict_Size( pDict.get() ) ) );
	CHECK( !PyErr_Occurred() );
}


// Partial dictionaries report SOME_MISSING (earlier keywords are still
// extracted), while empty and NULL dictionaries report NONE_FOUND and touch
// nothing.
TEST_F( PyScriptUnitTestHarness, KeywordParser_missingAndNone )
{
	PyObjectPtr pDict = makeKeywordDict( false );
	PyDict_DelItemString( pDict.get(), "aFloat" );
	PyDict_DelItemString( pDict.get(), "aString" );

	int intValue = 0;
	float floatValue = 42.f;
	BW::string stringValue( "untouched" );

	KeywordParser parser;
	parser.add< int >( "anInt", intValue );
	parser.add< float >( "aFloat", floatValue );
	parser.add< BW::string >( "aString", stringValue );

	CHECK_EQUAL( KeywordParser::SOME_MISSING, parser.parse( pDict.get() ) );
	CHECK_EQUAL( 7, intValue );
	// Missing keywords leave their outputs alone.
	CHECK_CLOSE( 42.f, floatValue, 0.0001f );
	CHECK_EQUAL( BW::string( "untouched" ), stringValue );

	// An empty dictionary finds nothing.
	PyObjectPtr pEmpty( PyDict_New(), PyObjectPtr::STEAL_REFERENCE );
	CHECK_EQUAL( KeywordParser::NONE_FOUND, parser.parse( pEmpty.get() ) );

	// Neither does a NULL dictionary.
	CHECK_EQUAL( KeywordParser::NONE_FOUND, parser.parse( NULL ) );

	CHECK( !PyErr_Occurred() );
}


// A value that will not convert raises EXCEPTION_RAISED, with the keyword
// name in the pending TypeError.
TEST_F( PyScriptUnitTestHarness, KeywordParser_badValueRaises )
{
	PyObjectPtr pDict = makeKeywordDict( false );
	PyObjectPtr pBad( PyUnicode_FromString( "not a number" ),
		PyObjectPtr::STEAL_REFERENCE );
	PyDict_SetItemString( pDict.get(), "anInt", pBad.get() );

	int intValue = 0;

	KeywordParser parser;
	parser.add< int >( "anInt", intValue );

	CHECK_EQUAL( KeywordParser::EXCEPTION_RAISED,
		parser.parse( pDict.get() ) );

	PyObject * pType = NULL;
	PyObject * pValue = NULL;
	PyObject * pTraceback = NULL;
	PyErr_Fetch( &pType, &pValue, &pTraceback );
	CHECK( pType == (PyObject *)PyExc_TypeError );

	BW::string message;
	if (pValue != NULL)
	{
		PyObject * pStr = PyObject_Str( pValue );
		if (pStr != NULL)
		{
			message.assign( PyUnicode_AsUTF8( pStr ) );
			Py_DECREF( pStr );
		}
	}

	Py_XDECREF( pType );
	Py_XDECREF( pValue );
	Py_XDECREF( pTraceback );
	CHECK( strstr( message.c_str(), "anInt" ) != NULL );
}


// Unregistered keys are left alone by default. When other arguments are not
// allowed, the parse reports the first remaining dict entry that is not a
// registered keyword - so the blocks below register everything but "nope" to
// pin the report on the foreign key itself.
TEST_F( PyScriptUnitTestHarness, KeywordParser_extraArguments )
{
	int intValue = 0;
	float floatValue = 0.f;
	BW::string stringValue;

	// Extra keys are neither extracted nor removed when other arguments are
	// allowed; a parser registered for a subset still reports ALL_FOUND,
	// because SOME_MISSING is relative to the registered keywords only.
	{
		PyObjectPtr pDict = makeKeywordDict( /*withExtra*/ true );

		KeywordParser parser;
		parser.add< int >( "anInt", intValue );

		CHECK_EQUAL( KeywordParser::ALL_FOUND,
			parser.parse( pDict.get() ) );
		CHECK_EQUAL( 7, intValue );
		// aFloat/aString were never registered, so only anInt was removed.
		CHECK_EQUAL( 3, int( PyDict_Size( pDict.get() ) ) );
		PyErr_Clear();
	}

	// With shouldRemove the found entries are gone, so anything left in the
	// dict is unregistered and must be reported.
	{
		PyObjectPtr pDict = makeKeywordDict( true );

		KeywordParser parser;
		parser.add< int >( "anInt", intValue );
		parser.add< float >( "aFloat", floatValue );
		parser.add< BW::string >( "aString", stringValue );

		CHECK_EQUAL( KeywordParser::EXCEPTION_RAISED,
			parser.parse( pDict.get(), /*shouldRemove*/ true,
				/*allowOtherArguments*/ false ) );
		CHECK_EQUAL(
			BW::string( "Invalid keyword argument: \"nope\"" ),
			currentExceptionMessage() );
	}

	// Without shouldRemove the registered keys remain but are recognised as
	// ours; only the genuinely foreign key is reported.
	{
		PyObjectPtr pDict = makeKeywordDict( true );

		KeywordParser parser;
		parser.add< int >( "anInt", intValue );
		parser.add< float >( "aFloat", floatValue );
		parser.add< BW::string >( "aString", stringValue );

		CHECK_EQUAL( KeywordParser::EXCEPTION_RAISED,
			parser.parse( pDict.get(), /*shouldRemove*/ false,
				/*allowOtherArguments*/ false ) );
		CHECK_EQUAL(
			BW::string( "Invalid keyword argument: \"nope\"" ),
			currentExceptionMessage() );
	}
}


// shouldRemove=false leaves the dictionary intact, so the same dict can be
// parsed twice with the same result.
TEST_F( PyScriptUnitTestHarness, KeywordParser_shouldRemoveKeepsDict )
{
	PyObjectPtr pDict = makeKeywordDict( false );

	int intValue = 0;

	KeywordParser parser;
	parser.add< int >( "anInt", intValue );

	CHECK_EQUAL( KeywordParser::ALL_FOUND,
		parser.parse( pDict.get(), /*shouldRemove*/ false ) );
	CHECK_EQUAL( 7, intValue );
	CHECK_EQUAL( 3, int( PyDict_Size( pDict.get() ) ) );

	// A second parse over the untouched dict extracts the same value again.
	CHECK_EQUAL( KeywordParser::ALL_FOUND,
		parser.parse( pDict.get(), false ) );
	CHECK_EQUAL( 7, intValue );
	CHECK( !PyErr_Occurred() );
}


// Bytes keys can never name a registered keyword; the strict parse must
// reject them with a message instead of dereferencing the NULL that
// PyUnicode_AsUTF8() returns for non-string keys.
TEST_F( PyScriptUnitTestHarness, KeywordParser_nonStringKeysAreSafe )
{
	PyObjectPtr pDict( PyDict_New(), PyObjectPtr::STEAL_REFERENCE );
	PyObjectPtr pValue( PyLong_FromLong( 7 ), PyObjectPtr::STEAL_REFERENCE );
	PyDict_SetItemString( pDict.get(), "anInt", pValue.get() );
	PyObjectPtr pBytesKey( PyBytes_FromString( "nope" ),
		PyObjectPtr::STEAL_REFERENCE );
	PyDict_SetItem( pDict.get(), pBytesKey.get(), Py_True );

	int intValue = 0;

	KeywordParser parser;
	parser.add< int >( "anInt", intValue );

	CHECK_EQUAL( KeywordParser::EXCEPTION_RAISED,
		parser.parse( pDict.get(), /*shouldRemove*/ false,
			/*allowOtherArguments*/ false ) );
	CHECK_EQUAL( BW::string( "Invalid keyword argument: \"<non-string key>\"" ),
		currentExceptionMessage() );

	CHECK( !PyErr_Occurred() );
}


// -----------------------------------------------------------------------------
// Section: Script::ask
// -----------------------------------------------------------------------------

// ask() calls the function with the argument tuple and returns the result as
// a new reference. Both the function and the arguments are stolen: after the
// call our extra reference count is all that remains.
TEST_F( PyScriptUnitTestHarness, ScriptAsk_callAndStealRefs )
{
	PyErr_Clear();

	PyObject * pFn = Script::runString( "lambda a, b: a + b", false );
	CHECK( pFn != NULL );
	if (pFn == NULL)
	{
		PyErr_Clear();
		return;
	}
	Py_INCREF( pFn );	// the reference we watch after ask() consumes its own

	PyObjectPtr pTwo( PyLong_FromLong( 2 ), PyObjectPtr::STEAL_REFERENCE );
	PyObjectPtr pThree( PyLong_FromLong( 3 ), PyObjectPtr::STEAL_REFERENCE );
	PyObject * pArgs = PyTuple_Pack( 2, pTwo.get(), pThree.get() );
	CHECK( pArgs != NULL );

	Py_ssize_t refsBefore = Py_REFCNT( pFn );

	PyObjectPtr pResult( Script::ask( pFn, pArgs, "TestAsk: " ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pResult.get() != NULL );
	CHECK_EQUAL( 5L, PyLong_AsLong( pResult.get() ) );

	// ask() consumed exactly one reference to the function (and to args).
	CHECK( Py_REFCNT( pFn ) == refsBefore - 1 );
	Py_DECREF( pFn );
	CHECK( !PyErr_Occurred() );
}


// Malformed calls are refused with the error prefix in the message, and a
// NULL function is only tolerated when explicitly allowed. printException is
// switched off throughout so the raised errors stay pending for inspection.
TEST_F( PyScriptUnitTestHarness, ScriptAsk_errorPaths )
{
	PyErr_Clear();

	// A non-callable function object.
	{
		PyObject * pFn = PyLong_FromLong( 42 );
		PyObject * pArgs = PyTuple_New( 0 );
		CHECK( Script::ask( pFn, pArgs, "NonCall: ", true,
			/*printException*/ false ) == NULL );
		BW::string message = currentExceptionMessage();
		CHECK( strstr( message.c_str(), "NonCall: " ) != NULL );
		CHECK( strstr( message.c_str(), "non-callable" ) != NULL );
	}

	// A non-tuple argument container.
	{
		PyObject * pFn = Script::runString( "lambda: 1", false );
		PyObject * pArgs = PyList_New( 0 );
		CHECK( Script::ask( pFn, pArgs, "NonTuple: ", true,
			/*printException*/ false ) == NULL );
		BW::string message = currentExceptionMessage();
		CHECK( strstr( message.c_str(), "non-callable" ) != NULL ||
			strstr( message.c_str(), "not a tuple" ) != NULL );
	}

	// NULL function with NULL arguments, not allowed: ValueError.
	CHECK( Script::ask( NULL, NULL, "NullFn: ",
		/*okIfFunctionNull*/ false, /*printException*/ false ) == NULL );
	CHECK( strstr( currentExceptionMessage().c_str(), "NULL function" ) !=
		NULL );

	// NULL function with a live argument tuple, allowed: no error at all.
	{
		PyObject * pArgs = PyTuple_New( 0 );
		CHECK( Script::ask( NULL, pArgs, "NullFn: ",
			/*okIfFunctionNull*/ true, /*printException*/ false ) == NULL );
		CHECK( !PyErr_Occurred() );
	}
}


// An exception from inside the function leaves the error pending for the
// caller unless ask() was asked to print it, in which case it is consumed.
TEST_F( PyScriptUnitTestHarness, ScriptAsk_printExceptionHandling )
{
	PyErr_Clear();

	// Without printException the ValueError stays pending.
	{
		PyObject * pFn = Script::runString(
			"lambda: (_ for _ in ()).throw( ValueError( 'boom' ) )", false );
		PyObject * pArgs = PyTuple_New( 0 );
		CHECK( pFn != NULL );
		CHECK( Script::ask( pFn, pArgs, "Boom: ",
			true, /*printException*/ false ) == NULL );
		CHECK( strstr( currentExceptionMessage().c_str(), "boom" ) != NULL );
	}

	// With printException the error is printed into the BW log and consumed.
	{
		PyObject * pFn = Script::runString(
			"lambda: (_ for _ in ()).throw( ValueError( 'boom' ) )", false );
		PyObject * pArgs = PyTuple_New( 0 );
		CHECK( pFn != NULL );
		CHECK( Script::ask( pFn, pArgs, "Boom: ",
			true, /*printException*/ true ) == NULL );
		CHECK( !PyErr_Occurred() );
	}
}


// -----------------------------------------------------------------------------
// Section: Script::runString
// -----------------------------------------------------------------------------

// runString evaluates expressions in __main__ and hands back the value; with
// printResult it runs in interactive mode instead, which also accepts
// statements (and returns None).
TEST_F( PyScriptUnitTestHarness, ScriptRunString_evalAndStatements )
{
	PyErr_Clear();

	// Expressions evaluate to the value.
	PyObjectPtr pAnswer( Script::runString( "21 * 2", false ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pAnswer.get() != NULL );
	CHECK_EQUAL( 42L, PyLong_AsLong( pAnswer.get() ) );

	// Eval mode rejects statements.
	{
		PyObjectPtr pResult( Script::runString( "batch6_x = 5", false ),
			PyObjectPtr::STEAL_REFERENCE );
		CHECK( pResult.get() == NULL );
		CHECK( PyErr_GivenExceptionMatches( PyErr_Occurred(),
			PyExc_SyntaxError ) );
		PyErr_Clear();
	}

	// Interactive mode accepts statements, returns None, and the variable
	// lands in __main__.
	{
		PyObjectPtr pResult( Script::runString( "batch6_x = 5",
			/*printResult*/ true ), PyObjectPtr::STEAL_REFERENCE );
		CHECK( pResult.get() == Py_None );

		PyObjectPtr pReadBack( Script::runString( "batch6_x", false ),
			PyObjectPtr::STEAL_REFERENCE );
		CHECK( pReadBack.get() != NULL );
		CHECK_EQUAL( 5L, PyLong_AsLong( pReadBack.get() ) );
	}

	// Broken syntax leaves a SyntaxError pending.
	{
		PyObjectPtr pResult( Script::runString( "def (", false ),
			PyObjectPtr::STEAL_REFERENCE );
		CHECK( pResult.get() == NULL );
		CHECK( PyErr_GivenExceptionMatches( PyErr_Occurred(),
			PyExc_SyntaxError ) );
		PyErr_Clear();
	}

	CHECK( !PyErr_Occurred() );
}


// -----------------------------------------------------------------------------
// Section: Script::setData scalar converters
// -----------------------------------------------------------------------------

// The scalar converters pin the migration-era semantics: bools accept ints
// and the strings true/false (case-insensitive) but nothing else; ints take
// ints and truncating floats but nothing else.
TEST_F( PyScriptUnitTestHarness, ScriptPrimitives_setDataScalars )
{
	PyErr_Clear();

	PyObjectPtr pTrue( PyLong_FromLong( 1 ), PyObjectPtr::STEAL_REFERENCE );
	PyObjectPtr pFalse( PyLong_FromLong( 0 ), PyObjectPtr::STEAL_REFERENCE );
	PyObjectPtr pStrTrue( PyUnicode_FromString( "TRUE" ),
		PyObjectPtr::STEAL_REFERENCE );
	PyObjectPtr pStrFalse( PyUnicode_FromString( "false" ),
		PyObjectPtr::STEAL_REFERENCE );
	PyObjectPtr pStrOther( PyUnicode_FromString( "yes" ),
		PyObjectPtr::STEAL_REFERENCE );

	bool boolValue = false;
	CHECK_EQUAL( 0, Script::setData( pTrue.get(), boolValue, "b" ) );
	CHECK( boolValue );
	CHECK_EQUAL( 0, Script::setData( pFalse.get(), boolValue, "b" ) );
	CHECK( !boolValue );
	CHECK_EQUAL( 0, Script::setData( pStrTrue.get(), boolValue, "b" ) );
	CHECK( boolValue );
	CHECK_EQUAL( 0, Script::setData( pStrFalse.get(), boolValue, "b" ) );
	CHECK( !boolValue );

	CHECK_EQUAL( -1, Script::setData( pStrOther.get(), boolValue, "theBool" ) );
	CHECK_EQUAL( BW::string( "theBool must be set to a bool" ),
		currentExceptionMessage() );

	PyObjectPtr pInt( PyLong_FromLong( 9 ), PyObjectPtr::STEAL_REFERENCE );
	PyObjectPtr pFloat( PyFloat_FromDouble( 1.75 ),
		PyObjectPtr::STEAL_REFERENCE );

	int intValue = 0;
	CHECK_EQUAL( 0, Script::setData( pInt.get(), intValue, "i" ) );
	CHECK_EQUAL( 9, intValue );
	CHECK_EQUAL( 0, Script::setData( pFloat.get(), intValue, "i" ) );
	CHECK_EQUAL( 1, intValue );

	CHECK_EQUAL( -1, Script::setData( pStrTrue.get(), intValue, "theInt" ) );
	CHECK_EQUAL( BW::string( "theInt must be set to an int" ),
		currentExceptionMessage() );

	CHECK( !PyErr_Occurred() );
}


// -----------------------------------------------------------------------------
// Section: Pickler
// -----------------------------------------------------------------------------

// A round trip through Pickler preserves the value: pickling emits a binary
// protocol-2 stream (PROTO opcode 0x80 - never valid UTF-8, which is why the
// unpickle side must take bytes, not str) and unpickling restores an object
// that compares equal to the original. Raw bytes payloads survive untouched.
TEST_F( PyScriptUnitTestHarness, Pickler_roundTrip )
{
	ScriptObject pOriginal( evalObject(
		"( 42, 2.5, 'plain', u'h\\u00e9llo', [ 1, 2 ], { 'k': 'v' } )" ) );
	CHECK( pOriginal.get() != NULL );
	if (pOriginal.get() == NULL)
	{
		PyErr_Clear();
		return;
	}

	BW::string pickled = Pickler::pickle( pOriginal );
	CHECK( pickled.size() > 2 );
	CHECK_EQUAL( '\x80', pickled[ 0 ] );	// PROTO opcode
	CHECK_EQUAL( '\x02', pickled[ 1 ] );	// protocol 2, as requested

	ScriptObject pRestored = Pickler::unpickle( pickled );
	CHECK( pRestored.get() != NULL );
	CHECK( pythonEqual( pOriginal.get(), pRestored.get() ) );

	// Raw bytes survive the round trip untouched, NULs included.
	ScriptObject pBytes( evalObject( "b'raw\\x00bytes\\xff'" ) );
	CHECK( pBytes.get() != NULL );
	ScriptObject pBytesBack = Pickler::unpickle(
		Pickler::pickle( pBytes ) );
	CHECK( pBytesBack.get() != NULL );
	CHECK( pythonEqual( pBytes.get(), pBytesBack.get() ) );

	CHECK( !PyErr_Occurred() );
}


// Unpickling garbage yields the FailedUnpickle stand-in, which keeps the raw
// payload: pickling the stand-in hands the original bytes straight back
// instead of trying to serialise the wrapper. The empty payload has no
// protocol header either, so it lands on the stand-in path as well.
TEST_F( PyScriptUnitTestHarness, Pickler_failedUnpickleStandIn )
{
	const BW::string GARBAGE = "definitely not a pickle stream";

	ScriptObject pStandIn = Pickler::unpickle( GARBAGE );
	CHECK( pStandIn.get() != NULL );
	CHECK( strstr( pStandIn.get()->ob_type->tp_name, "FailedUnpickle" ) !=
		NULL );
	CHECK_EQUAL( GARBAGE, Pickler::pickle( pStandIn ) );

	ScriptObject pEmpty = Pickler::unpickle( BW::string() );
	CHECK( pEmpty.get() != NULL );
	CHECK( strstr( pEmpty.get()->ob_type->tp_name, "FailedUnpickle" ) !=
		NULL );

	CHECK( !PyErr_Occurred() );
}


// Pickling a null ScriptObject is refused with an empty string.
TEST_F( PyScriptUnitTestHarness, Pickler_pickleNull )
{
	CHECK( Pickler::pickle( ScriptObject() ).empty() );
	CHECK( !PyErr_Occurred() );
}


// finalise() drops the cached pickle methods - pickling yields nothing and
// unpickling falls back to the stand-in - and init() makes them usable again,
// so the harness is back to its initial state for later tests.
TEST_F( PyScriptUnitTestHarness, Pickler_finaliseReinit )
{
	ScriptObject pOriginal( evalObject( "[ 'payload', 3 ]" ) );
	CHECK( pOriginal.get() != NULL );
	if (pOriginal.get() == NULL)
	{
		PyErr_Clear();
		return;
	}

	Pickler::finalise();
	CHECK( Pickler::pickle( pOriginal ).empty() );
	CHECK( strstr( Pickler::unpickle( "x" ).get()->ob_type->tp_name,
		"FailedUnpickle" ) != NULL );

	CHECK( Pickler::init() );

	BW::string pickled = Pickler::pickle( pOriginal );
	CHECK( pickled.size() > 2 );
	CHECK( pythonEqual( pOriginal.get(), Pickler::unpickle( pickled ).get() ) );
	CHECK( !PyErr_Occurred() );
}


// -----------------------------------------------------------------------------
// Section: ResMgr module (res_mgr_script.cpp)
// -----------------------------------------------------------------------------

namespace // (anonymous)
{

/**
 *	Evaluates an expression against the ResMgr module and reports the
 *	exception ("TypeName: message") instead of letting it escape, or "" when
 *	the expression succeeded.
 */
BW::string raisedError( const char * expr )
{
	BW::string code( "p7_exc = None\ntry:\n\t" );
	code += expr;
	code += "\nexcept Exception as e:\n"
			"\tp7_exc = type( e ).__name__ + ': ' + str( e )\n";
	PyRun_SimpleString( code.c_str() );
	PyErr_Clear();

	ScriptObject pMessage = evalObject( "p7_exc" );
	if ((pMessage.get() == NULL) || !PyUnicode_Check( pMessage.get() ))
	{
		PyErr_Clear();
		// Either the eval failed or the expression did not raise.
		return BW::string();
	}
	BW::string result( PyUnicode_AsUTF8( pMessage.get() ) );
	return result;
}


/**
 *	Reads a str attribute off an object into a BW::string ("" when missing
 *	or not a string; the caller's CHECK reports the mismatch).
 */
BW::string attributeString( PyObject * pObj, const char * name )
{
	PyObject * pValue = PyObject_GetAttrString( pObj, name );
	if ((pValue == NULL) || !PyUnicode_Check( pValue ))
	{
		Py_XDECREF( pValue );
		PyErr_Clear();
		return BW::string();
	}
	BW::string result( PyUnicode_AsUTF8( pValue ) );
	Py_DECREF( pValue );
	return result;
}

} // end namespace (anonymous)


// The path predicates route straight into the resource system: a known
// fixture file is a file and not a directory, anything else is neither.
TEST_F( PyScriptUnitTestHarness, ResMgr_fileQueries )
{
	ScriptObject pIsFile( evalObject(
		"__import__('ResMgr').isFile( 'test_py_data_section.xml' )" ) );
	CHECK( pIsFile.get() != NULL );
	CHECK_EQUAL( 1, PyObject_IsTrue( pIsFile.get() ) );

	ScriptObject pNotFile( evalObject(
		"__import__('ResMgr').isFile( 'no_such_p7.bin' )" ) );
	CHECK( pNotFile.get() != NULL );
	CHECK_EQUAL( 0, PyObject_IsTrue( pNotFile.get() ) );

	ScriptObject pNotDir( evalObject(
		"__import__('ResMgr').isDir( 'test_py_data_section.xml' )" ) );
	CHECK( pNotDir.get() != NULL );
	CHECK_EQUAL( 0, PyObject_IsTrue( pNotDir.get() ) );

	ScriptObject pNoDir( evalObject(
		"__import__('ResMgr').isDir( 'no_such_p7.bin' )" ) );
	CHECK( pNoDir.get() != NULL );
	CHECK_EQUAL( 0, PyObject_IsTrue( pNoDir.get() ) );

	CHECK( !PyErr_Occurred() );
}


// openSection resolves an existing resource into a PyDataSection (its type
// was published by PyFactoryMethodLink as "ResMgr.DataSection"), purge
// forgets it from the cache (recursive or not) and it can be reopened.
TEST_F( PyScriptUnitTestHarness, ResMgr_openSectionFoundPurgeReopen )
{
	ScriptObject pSection( evalObject(
		"__import__('ResMgr').openSection( 'test_py_data_section.xml' )" ) );
	CHECK( pSection.get() != NULL );
	CHECK( strstr( pSection.get()->ob_type->tp_name, "DataSection" ) !=
		NULL );

	PyObject * pPurge = Script::runString(
		"__import__('ResMgr').purge( 'test_py_data_section.xml' )", false );
	CHECK( pPurge != NULL );
	Py_XDECREF( pPurge );

	PyObject * pPurgeAll = Script::runString(
		"__import__('ResMgr').purge( 'test_py_data_section.xml', True )",
		false );
	CHECK( pPurgeAll != NULL );
	Py_XDECREF( pPurgeAll );

	ScriptObject pReopened( evalObject(
		"__import__('ResMgr').openSection( 'test_py_data_section.xml' )" ) );
	CHECK( pReopened.get() != NULL );
	CHECK( strstr( pReopened.get()->ob_type->tp_name, "DataSection" ) !=
		NULL );

	// The module attribute 'root' hands out the root data section.
	ScriptObject pRoot( evalObject( "__import__('ResMgr').root" ) );
	CHECK( pRoot.get() != NULL );
	CHECK( strstr( pRoot.get()->ob_type->tp_name, "DataSection" ) != NULL );

	CHECK( !PyErr_Occurred() );
}


// A missing resource opens as None. With makeNewSection a missing
// intermediate directory is materialised and a fresh section handed out,
// while a path under an existing FILE yields a virtual child section; the
// directory created here is erased again so the res tree stays clean.
TEST_F( PyScriptUnitTestHarness, ResMgr_openSectionMissingAndCreateFailure )
{
	ScriptObject pMissing( evalObject(
		"__import__('ResMgr').openSection( 'no_such_p7_dir/child.xml' )" ) );
	CHECK( pMissing.get() == Py_None );

	ScriptObject pCreated( evalObject(
		"__import__('ResMgr').openSection( 'no_such_p7_dir/child.xml', True )"
		) );
	CHECK( pCreated.get() != NULL );
	CHECK( strstr( pCreated.get()->ob_type->tp_name, "DataSection" ) !=
		NULL );

	// makeNewSection under an existing FILE creates a virtual child section
	// of that file's DataSection - no exception, and nothing touches disk.
	ScriptObject pNested( evalObject(
		"__import__('ResMgr').openSection( "
		"'test_py_data_section.xml/sub/child.xml', True )" ) );
	CHECK( pNested.get() != NULL );
	CHECK( strstr( pNested.get()->ob_type->tp_name, "DataSection" ) !=
		NULL );

	// Clean up the directory the successful creation left behind.
	BW::string createdDir = BWResource::getPath( 0 ) + "/no_such_p7_dir";
	CHECK( remove( createdDir.c_str() ) == 0 );

	CHECK( !PyErr_Occurred() );
}


// save() of a resource that is not in memory reports an IO error through
// the RETOK wrapper, which turns the false return into a raised exception.
TEST_F( PyScriptUnitTestHarness, ResMgr_saveUnknownResource )
{
	BW::string error = raisedError(
		"__import__('ResMgr').save( 'no_such_p7.xml' )" );
	// IOError is spelled OSError since Python 3.3.
	CHECK_EQUAL( BW::string( "OSError: Save of no_such_p7.xml failed" ),
		error );

	CHECK( !PyErr_Occurred() );
}


// resolveToAbsolutePath returns an absolute path for resources that exist,
// and falls back to the first res path (or the input itself when already
// absolute) for ones that do not.
TEST_F( PyScriptUnitTestHarness, ResMgr_resolveToAbsolutePath )
{
	ScriptObject pExisting( evalObject(
		"__import__('ResMgr').resolveToAbsolutePath( "
		"'test_py_data_section.xml' )" ) );
	CHECK( pExisting.get() != NULL );
	CHECK( PyUnicode_Check( pExisting.get() ) );
	BW::string existing( PyUnicode_AsUTF8( pExisting.get() ) );
	CHECK( existing.find( "test_py_data_section.xml" ) !=
		BW::string::npos );
	CHECK( !existing.empty() && existing[ 0 ] == '/' );

	ScriptObject pFallback( evalObject(
		"__import__('ResMgr').resolveToAbsolutePath( 'no_such_p7.bin' )" ) );
	CHECK( pFallback.get() != NULL );
	BW::string fallback( PyUnicode_AsUTF8( pFallback.get() ) );
	CHECK( fallback.find( "no_such_p7.bin" ) != BW::string::npos );
	CHECK( !fallback.empty() && fallback[ 0 ] == '/' );

	ScriptObject pAbsolute( evalObject(
		"__import__('ResMgr').resolveToAbsolutePath( "
		"'/tmp/no_such_p7.bin' )" ) );
	CHECK( pAbsolute.get() != NULL );
	CHECK( strcmp( PyUnicode_AsUTF8( pAbsolute.get() ),
		"/tmp/no_such_p7.bin" ) == 0 );

	CHECK( !PyErr_Occurred() );
}


// localise hands unknown keys straight back (RETURN_PARAM_IF_NOT_EXISTING)
// and rejects non-string arguments with a TypeError.
TEST_F( PyScriptUnitTestHarness, ResMgr_localise )
{
	ScriptObject pUnknown( evalObject(
		"__import__('ResMgr').localise( 'p7 no such key' )" ) );
	CHECK( pUnknown.get() != NULL );
	CHECK( PyUnicode_Check( pUnknown.get() ) );
	CHECK( strcmp( PyUnicode_AsUTF8( pUnknown.get() ),
		"p7 no such key" ) == 0 );

	ScriptObject pEmpty( evalObject( "__import__('ResMgr').localise( '' )" ) );
	CHECK( pEmpty.get() != NULL );
	CHECK( strcmp( PyUnicode_AsUTF8( pEmpty.get() ), "" ) == 0 );

	BW::string error = raisedError(
		"__import__('ResMgr').localise( 42 )" );
	CHECK( error.find( "TypeError" ) == 0 );

	CHECK( !PyErr_Occurred() );
}


// -----------------------------------------------------------------------------
// Section: Personality (personality.cpp)
// -----------------------------------------------------------------------------

// A failed import leaves no personality instance behind, consumes the import
// error, and getMember degrades to a null ScriptObject in that state.
TEST_F( PyScriptUnitTestHarness, Personality_importMissingLeavesStateEmpty )
{
	// Nothing else in this binary imports a personality module before us.
	CHECK( Personality::instance().get() == NULL );

	ScriptModule imported = Personality::import( "p7_no_such_module_zz" );
	CHECK( imported.get() == NULL );
	CHECK( !PyErr_Occurred() );

	CHECK( Personality::instance().get() == NULL );

	ScriptObject pMember = Personality::getMember( "anything" );
	CHECK( pMember.get() == NULL );

	ScriptObject pTwoName = Personality::getMember( "a", "b" );
	CHECK( pTwoName.get() == NULL );

	CHECK( !PyErr_Occurred() );
}


// A real personality module is imported once (re-imports return the same
// instance), its members are reachable with the deprecated-name fallback,
// and callOnInit forwards isReload to onInit.
TEST_F( PyScriptUnitTestHarness, Personality_importMembersAndOnInit )
{
	CHECK( !runAndClear(
		"import sys, types\n"
		"_p7mod = types.ModuleType( 'p7_personality_mod' )\n"
		"_p7mod.onInitCalls = []\n"
		"def _p7onInit( isReload ):\n"
		"\t_p7mod.onInitCalls.append( isReload )\n"
		"_p7mod.onInit = _p7onInit\n"
		"_p7mod.onFiniCalls = []\n"
		"def _p7onFini():\n"
		"\t_p7mod.onFiniCalls.append( 1 )\n"
		"_p7mod.onFini = _p7onFini\n"
		"_p7mod.greeting = 'hello'\n"
		"_p7mod.nickname = 'old nick'\n"
		"sys.modules['p7_personality_mod'] = _p7mod\n" ) );
	CHECK( !PyErr_Occurred() );

	ScriptModule imported = Personality::import( "p7_personality_mod" );
	CHECK( imported.get() != NULL );
	CHECK( Personality::instance().get() == imported.get() );

	// The second import warns and hands back the stored instance.
	ScriptModule again = Personality::import( "p7_personality_mod" );
	CHECK( again.get() == imported.get() );

	ScriptObject pGreeting = Personality::getMember( "greeting" );
	CHECK( pGreeting.get() != NULL );
	CHECK( PyUnicode_Check( pGreeting.get() ) );
	CHECK( strcmp( PyUnicode_AsUTF8( pGreeting.get() ), "hello" ) == 0 );

	// The current name is missing, so the deprecated one is used instead.
	ScriptObject pFallback = Personality::getMember( "absent", "nickname" );
	CHECK( pFallback.get() != NULL );
	CHECK( strcmp( PyUnicode_AsUTF8( pFallback.get() ), "old nick" ) == 0 );

	ScriptObject pNothing = Personality::getMember( "absent", "alsoAbsent" );
	CHECK( pNothing.get() == NULL );

	CHECK( Personality::callOnInit( /* isReload */ false ) );
	CHECK( Personality::callOnInit( /* isReload */ true ) );

	ScriptObject pCalls( evalObject( "_p7mod.onInitCalls" ) );
	CHECK( pCalls.get() != NULL );
	if (pCalls.get() != NULL)
	{
		CHECK_EQUAL( Py_ssize_t( 2 ), PyList_Size( pCalls.get() ) );
		CHECK_EQUAL( 0, PyObject_IsTrue( PyList_GetItem( pCalls.get(), 0 ) ) );
		CHECK_EQUAL( 1, PyObject_IsTrue( PyList_GetItem( pCalls.get(), 1 ) ) );
	}

	CHECK( !PyErr_Occurred() );
}


// -----------------------------------------------------------------------------
// Section: PyDebugMessageFileLogger (py_debug_message_file_logger.cpp)
// -----------------------------------------------------------------------------

// BigWorld.FileLogger applies its defaults: the given file name, append
// mode, empty category, disabled logging - and 'enable' is writable.
TEST_F( PyScriptUnitTestHarness, FileLogger_factoryDefaultsAndEnable )
{
	ScriptObject pLogger( evalObject(
		"__import__('BigWorld').FileLogger( '/tmp/p7.log' )" ) );
	CHECK( pLogger.get() != NULL );
	if (pLogger.get() == NULL)
	{
		PyErr_Clear();
		return;
	}

	CHECK_EQUAL( "/tmp/p7.log",
		attributeString( pLogger.get(), "fileName" ) );
	CHECK_EQUAL( "a", attributeString( pLogger.get(), "openMode" ) );
	CHECK_EQUAL( "", attributeString( pLogger.get(), "category" ) );

	// enable defaults to False and round-trips through the RW accessor.
	PyObject * pEnabled = PyObject_GetAttrString( pLogger.get(), "enable" );
	CHECK( pEnabled != NULL );
	CHECK_EQUAL( 0, PyObject_IsTrue( pEnabled ) );
	Py_DECREF( pEnabled );

	CHECK_EQUAL( 0, PyObject_SetAttrString( pLogger.get(), "enable",
		Py_True ) );
	pEnabled = PyObject_GetAttrString( pLogger.get(), "enable" );
	CHECK( pEnabled != NULL );
	CHECK_EQUAL( 1, PyObject_IsTrue( pEnabled ) );
	Py_DECREF( pEnabled );

	CHECK( !PyErr_Occurred() );
}


// Explicit severities/sources/category/openMode are converted from their
// Python spellings into masks and rendered back as ';'-joined names.
TEST_F( PyScriptUnitTestHarness, FileLogger_explicitSeveritiesSources )
{
	ScriptObject pLogger( evalObject(
		"__import__('BigWorld').FileLogger( '/tmp/p7.log', "
		"severities = ( 'WARNING', 'ERROR' ), "
		"category = 'P7Cat', "
		"sources = 'SCRIPT', "
		"openMode = 'w' )" ) );
	CHECK( pLogger.get() != NULL );
	if (pLogger.get() == NULL)
	{
		PyErr_Clear();
		return;
	}

	BW::string severities = attributeString( pLogger.get(), "severities" );
	CHECK( severities.find( "WARNING" ) != BW::string::npos );
	CHECK( severities.find( "ERROR" ) != BW::string::npos );
	CHECK( severities.find( "TRACE" ) == BW::string::npos );

	BW::string sources = attributeString( pLogger.get(), "sources" );
	CHECK( sources.find( "SCRIPT" ) != BW::string::npos );
	CHECK( sources.find( "CPP" ) == BW::string::npos );

	// DebugMessageFileLogger::config() case-folds the category so log
	// filtering is case-insensitive.
	CHECK_EQUAL( BW::string( "p7cat" ),
		attributeString( pLogger.get(), "category" ) );
	CHECK_EQUAL( BW::string( "w" ),
		attributeString( pLogger.get(), "openMode" ) );

	// A None severities/sources argument means "leave at ALL" rather than
	// "no severities", and openMode accepts append as well as overwrite.
	ScriptObject pNoneArgs( evalObject(
		"__import__('BigWorld').FileLogger( '/tmp/p7.log', "
		"severities = None, sources = None, openMode = 'a' )" ) );
	CHECK( pNoneArgs.get() != NULL );
	CHECK( attributeString( pNoneArgs.get(), "severities" ).find(
		"TRACE" ) != BW::string::npos );
	CHECK( attributeString( pNoneArgs.get(), "sources" ).find(
		"CPP" ) != BW::string::npos );

	CHECK( !PyErr_Occurred() );
}


// Bad argument types raise TypeError, unknown names and modes raise
// ValueError, and the required fileName cannot be omitted.
TEST_F( PyScriptUnitTestHarness, FileLogger_invalidArguments )
{
	BW::string error;

	error = raisedError( "__import__('BigWorld').FileLogger()" );
	CHECK( error.find( "TypeError" ) == 0 );

	error = raisedError(
		"__import__('BigWorld').FileLogger( '/tmp/p7.log', severities = 42 )" );
	CHECK( error.find( "TypeError" ) == 0 );
	CHECK( error.find( "severities" ) != BW::string::npos );

	error = raisedError(
		"__import__('BigWorld').FileLogger( '/tmp/p7.log', "
		"severities = ( 'BOGUS', ) )" );
	CHECK( error.find( "ValueError" ) == 0 );

	error = raisedError(
		"__import__('BigWorld').FileLogger( '/tmp/p7.log', sources = 42 )" );
	CHECK( error.find( "TypeError" ) == 0 );
	CHECK( error.find( "sources" ) != BW::string::npos );

	error = raisedError(
		"__import__('BigWorld').FileLogger( '/tmp/p7.log', "
		"sources = ( 'NOPE', ) )" );
	CHECK( error.find( "ValueError" ) == 0 );

	error = raisedError(
		"__import__('BigWorld').FileLogger( '/tmp/p7.log', openMode = 'x' )" );
	CHECK( error.find( "ValueError" ) == 0 );

	CHECK( !PyErr_Occurred() );
}


// No config file was applied in the test binary, so defaultLoggers hands
// back an empty list.
TEST_F( PyScriptUnitTestHarness, FileLogger_defaultLoggersEmpty )
{
	ScriptObject pLoggers( evalObject(
		"__import__('BigWorld').defaultLoggers()" ) );
	CHECK( pLoggers.get() != NULL );
	CHECK( PyList_Check( pLoggers.get() ) );
	CHECK_EQUAL( Py_ssize_t( 0 ), PyList_Size( pLoggers.get() ) );

	CHECK( !PyErr_Occurred() );
}


// The C++ registry caps the config-created loggers at MAX_FILE_LOGGERS,
// indexes below the size return what was added, and anything past the end
// is a null pointer.
TEST_F( PyScriptUnitTestHarness, FileLoggers_registryLimit )
{
	// The class constant is only declared in-class, so copy it into a local
	// before passing it anywhere - taking its address directly would need an
	// out-of-line definition.
	const size_t MAX_LOGGERS = ConfigCreatedFileLoggers::MAX_FILE_LOGGERS;

	ConfigCreatedFileLoggers registry;

	PyDebugMessageFileLoggerPtr pFirst(
		new PyDebugMessageFileLogger(),
		PyDebugMessageFileLoggerPtr::FROM_NEW_REFERENCE );
	CHECK( registry.addFileLogger( pFirst ) );

	for (size_t i = 1; i < MAX_LOGGERS; ++i)
	{
		PyDebugMessageFileLoggerPtr pLogger(
			new PyDebugMessageFileLogger(),
			PyDebugMessageFileLoggerPtr::FROM_NEW_REFERENCE );
		CHECK( registry.addFileLogger( pLogger ) );
	}
	CHECK_EQUAL( MAX_LOGGERS, registry.len() );

	// Full house: the next add is refused.
	PyDebugMessageFileLoggerPtr pOverflow(
		new PyDebugMessageFileLogger(),
		PyDebugMessageFileLoggerPtr::FROM_NEW_REFERENCE );
	CHECK( !registry.addFileLogger( pOverflow ) );
	CHECK_EQUAL( MAX_LOGGERS, registry.len() );

	CHECK( registry[ 0 ].get() == pFirst.get() );
	CHECK( registry[ MAX_LOGGERS ].get() == NULL );
	CHECK( registry[ MAX_LOGGERS + 1 ].get() == NULL );

	CHECK( !PyErr_Occurred() );
}


// -----------------------------------------------------------------------------
// Section: PyFactoryMethodLink (py_factory_method_link.cpp)
// -----------------------------------------------------------------------------

namespace // (anonymous)
{

// A bare type object the link can rename and publish; nothing else in the
// process references it, so mutating tp_name is safe. The initialiser
// struct fills in the fields PyType_Ready needs (mimicking
// PyVarObject_HEAD_INIT: one reference so the module's reference never
// drops the static object to zero, and the metatype itself) - it must be
// ready before Script::init runs the link's init() job, which is long
// before any test body executes.
static PyTypeObject g_p7LinkType;

struct P7LinkTypeInitialiser
{
	P7LinkTypeInitialiser()
	{
		Py_SET_REFCNT( &g_p7LinkType, 1 );
		Py_SET_TYPE( &g_p7LinkType, &PyType_Type );
		g_p7LinkType.tp_name = const_cast<char *>( "p7.RawLink" );
		g_p7LinkType.tp_basicsize = sizeof( PyObject );
		g_p7LinkType.tp_flags = Py_TPFLAGS_DEFAULT;
	}
};
static P7LinkTypeInitialiser g_p7LinkTypeInitialiser;

// Constructed at static-init time exactly like the engine's own links -
// the InitTimeJob base refuses construction once Script::init has run.
static PyFactoryMethodLink g_p7Link( "p7linkmod", "P7LinkedType",
	&g_p7LinkType );

} // end namespace (anonymous)


// Script::init ran the link's job: the type was readied, renamed to
// module.method and published on the module. fini() restores the original
// name, and a repeated fini is a no-op.
TEST_F( PyScriptUnitTestHarness, FactoryMethodLink_initAndFini )
{
	PyTypeObject * pType = &g_p7LinkType;

	// The type was published under the method name.
	PyObject * pModule = PyImport_AddModule( "p7linkmod" );
	CHECK( pModule != NULL );
	PyObject * pPublished = PyObject_GetAttrString( pModule,
		"P7LinkedType" );
	CHECK( pPublished == (PyObject *)pType );
	Py_XDECREF( pPublished );

	// tp_name now carries the qualified name.
	CHECK( strcmp( pType->tp_name, "p7linkmod.P7LinkedType" ) == 0 );

	g_p7Link.fini();
	CHECK( strcmp( pType->tp_name, "p7.RawLink" ) == 0 );

	// A second fini has nothing left to restore.
	g_p7Link.fini();
	CHECK( strcmp( pType->tp_name, "p7.RawLink" ) == 0 );

	CHECK( !PyErr_Occurred() );
}


BW_END_NAMESPACE

// test_script_infra.cpp
