#include "pch.hpp"
#include "test_harness.hpp"

#include "pyscript/keyword_parser.hpp"
#include "pyscript/pickler.hpp"
#include "pyscript/script.hpp"


BW_BEGIN_NAMESPACE

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


BW_END_NAMESPACE

// test_script_infra.cpp
