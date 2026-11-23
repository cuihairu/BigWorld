/**
 * Unit tests for Python 3.13 migration regression surfaces.
 *
 * Each test pins the 3.13-era behaviour that a specific migration change
 * (recorded in docs/zh/python-313-migration.md and the CR batch in todo.md)
 * relies on. Where the old 2.7 behaviour differed, the comment says so.
 *
 * Case map:
 *   - CR-P2-10 arms: Script::setData int/int64 overflow arms (the bool
 *     overload's 2**200 cases live in test_script_infra.cpp; these cover
 *     the int and int64 overloads whose out-of-range paths used to leak a
 *     pending OverflowError).
 *   - CR-P2-11 arms: the PyUnicode_AsUTF8 NULL checks in py_logCommon
 *     (category/message/metadata) and ScriptString::getString. NULL is
 *     reachable deterministically with a lone-surrogate str, which 3.13
 *     refuses to UTF-8 encode.
 *   - CR-P2-04 surface: Script::initExceptionHook re-initialisation keeps
 *     a valid hook (PySys_SetObject INCREFs and does not steal; the fixed
 *     code DECREFs its local and leaves the hook untouched on failure).
 *   - str/bytes boundary invariants the migrated C++ leans on.
 */
#include "pch.hpp"

#include "test_harness.hpp"

#include "network/event_dispatcher.hpp"
#include "pyscript/py_traceback.hpp"
#include "pyscript/script.hpp"
#include "script/py_script_object.hpp"

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
 *	Fetches and consumes the exception currently raised, filling typeName
 *	and message (both "" if none). Use this whenever both are wanted -
 *	the single-value helpers below consume too, so they cannot be chained.
 */
void consumeCurrentException( BW::string & typeName, BW::string & message )
{
	if (!PyErr_Occurred())
	{
		return;
	}

	PyObject * pType = NULL;
	PyObject * pValue = NULL;
	PyObject * pTraceback = NULL;
	PyErr_Fetch( &pType, &pValue, &pTraceback );

	if (pValue != NULL)
	{
		PyObject * pStr = PyObject_Str( pValue );
		if (pStr != NULL)
		{
			message.assign( PyUnicode_AsUTF8( pStr ) );
			Py_DECREF( pStr );
		}
	}

	if (pType != NULL)
	{
		PyObject * pName = PyObject_GetAttrString( pType, "__name__" );
		if (pName != NULL)
		{
			typeName.assign( PyUnicode_AsUTF8( pName ) );
			Py_DECREF( pName );
		}
	}

	Py_XDECREF( pType );
	Py_XDECREF( pValue );
	Py_XDECREF( pTraceback );
}


/**
 *	Returns the __name__ of the exception type currently raised ("" if
 *	none) and consumes the exception.
 */
BW::string currentExceptionTypeName()
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
	if (pType != NULL)
	{
		PyObject * pName = PyObject_GetAttrString( pType, "__name__" );
		if (pName != NULL)
		{
			result.assign( PyUnicode_AsUTF8( pName ) );
			Py_DECREF( pName );
		}
	}

	Py_XDECREF( pType );
	Py_XDECREF( pValue );
	Py_XDECREF( pTraceback );

	return result;
}


/**
 *	The dispatcher the hook registers with (TraceBack keeps a process-wide
 *	pointer, so it must outlive the tests - same pattern as
 *	test_py_traceback.cpp).
 */
Mercury::EventDispatcher & regressionEventDispatcher()
{
	static Mercury::EventDispatcher dispatcher;
	return dispatcher;
}

/**
 *	Runs the sys.excepthook probe as statements (runString only compiles
 *	expressions - Py_eval_input) and reports whether it passed clean.
 */
bool assertHookInstalled()
{
	return !PyScriptUnitTestHarness::runAndClear(
		"import sys\n"
		"assert sys.excepthook.__name__ == 'printTraceBack'\n"
		"assert callable( sys.excepthook )\n" );
}

} // end namespace (anonymous)


// -----------------------------------------------------------------------------
// Section: Script::setData int/int64 overflow arms (CR-P2-10)
// -----------------------------------------------------------------------------

// The int overload must accept exactly the values that round-trip through a
// C int. 2.7 assigned first and tested second, so out-of-range PyLongs were
// reported as success with an OverflowError still pending. 3.13 drops the
// error (long overflow) or fails the round-trip (int overflow) and falls
// through to the TypeError path with nothing pending behind it.
TEST_F( PyScriptUnitTestHarness, Py313_setData_intOverflowArms )
{
	PyErr_Clear();

	int intValue = 0;

	// In-range boundaries round-trip exactly.
	ScriptObject pMax( evalObject( "2**31 - 1" ) );
	ScriptObject pMin( evalObject( "-2**31" ) );
	CHECK_EQUAL( 0, Script::setData( pMax.get(), intValue, "theInt" ) );
	CHECK_EQUAL( 2147483647, intValue );
	CHECK_EQUAL( 0, Script::setData( pMin.get(), intValue, "theInt" ) );
	CHECK_EQUAL( -2147483648, intValue );
	CHECK( !PyErr_Occurred() );

	// Fits a C long but not a C int: the round-trip check rejects it.
	ScriptObject pOver( evalObject( "2**31" ) );
	ScriptObject pUnder( evalObject( "-2**31 - 1" ) );
	CHECK_EQUAL( -1, Script::setData( pOver.get(), intValue, "theInt" ) );
	CHECK_EQUAL( BW::string( "theInt must be set to an int" ),
		currentExceptionMessage() );
	CHECK_EQUAL( -1, Script::setData( pUnder.get(), intValue, "theInt" ) );
	CHECK_EQUAL( BW::string( "theInt must be set to an int" ),
		currentExceptionMessage() );

	// Does not fit even a C long: PyLong_AsLong overflowed, and the leaked
	// OverflowError is exactly the 2.7 bug - it must be cleared and the call
	// must report the plain TypeError instead.
	ScriptObject pHuge( evalObject( "2**200" ) );
	CHECK_EQUAL( -1, Script::setData( pHuge.get(), intValue, "theInt" ) );
	CHECK_EQUAL( BW::string( "theInt must be set to an int" ),
		currentExceptionMessage() );
	CHECK_EQUAL( BW::string(), currentExceptionTypeName() );

	CHECK( !PyErr_Occurred() );
}


// The int64 overload accepts exactly the values that fit a C long long.
// The 2.7 fallback block returned success unconditionally with the
// OverflowError still pending; 3.13 clears it and reports failure.
TEST_F( PyScriptUnitTestHarness, Py313_setData_int64OverflowArms )
{
	PyErr_Clear();

	int64 int64Value = 0;

	// In-range boundaries round-trip exactly.
	ScriptObject pMax( evalObject( "2**63 - 1" ) );
	ScriptObject pMin( evalObject( "-2**63" ) );
	CHECK_EQUAL( 0, Script::setData( pMax.get(), int64Value, "theLong" ) );
	CHECK_EQUAL( 9223372036854775807LL, int64Value );
	CHECK_EQUAL( 0, Script::setData( pMin.get(), int64Value, "theLong" ) );
	CHECK_EQUAL( -9223372036854775807LL - 1, int64Value );
	CHECK( !PyErr_Occurred() );

	// Out of int64 range: failure with the plain TypeError, no leaked
	// OverflowError behind it.
	ScriptObject pOver( evalObject( "2**63" ) );
	ScriptObject pUnder( evalObject( "-2**63 - 1" ) );
	CHECK_EQUAL( -1, Script::setData( pOver.get(), int64Value, "theLong" ) );
	CHECK_EQUAL( BW::string( "theLong must be set to a long" ),
		currentExceptionMessage() );
	CHECK_EQUAL( -1, Script::setData( pUnder.get(), int64Value, "theLong" ) );
	CHECK_EQUAL( BW::string( "theLong must be set to a long" ),
		currentExceptionMessage() );

	ScriptObject pHuge( evalObject( "2**200" ) );
	CHECK_EQUAL( -1, Script::setData( pHuge.get(), int64Value, "theLong" ) );
	CHECK_EQUAL( BW::string( "theLong must be set to a long" ),
		currentExceptionMessage() );
	CHECK_EQUAL( BW::string(), currentExceptionTypeName() );

	CHECK( !PyErr_Occurred() );
}


// -----------------------------------------------------------------------------
// Section: Script::setData str/bytes boundary (3.13 string model)
// -----------------------------------------------------------------------------

// str crosses as UTF-8, bytes crosses raw (mirroring 2.7's byte-string str),
// everything else is a TypeError - and a str that cannot be UTF-8 encoded
// fails without being coerced.
TEST_F( PyScriptUnitTestHarness, Py313_setData_stringBytesBoundaries )
{
	PyErr_Clear();

	BW::string strValue;

	// Non-ASCII str arrives as its UTF-8 encoding.
	ScriptObject pText( evalObject( "'h\\u00e9llo'" ) );
	CHECK_EQUAL( 0, Script::setData( pText.get(), strValue, "theStr" ) );
	CHECK_EQUAL( BW::string( "h\xC3\xA9llo" ), strValue );

	// bytes is accepted directly, NULs included.
	ScriptObject pBytes( evalObject( "b'raw\\x00bytes\\xff'" ) );
	CHECK_EQUAL( 0, Script::setData( pBytes.get(), strValue, "theStr" ) );
	CHECK_EQUAL( 10, static_cast< int >( strValue.size() ) );
	CHECK_EQUAL( '\xff', strValue[ 9 ] );

	// An int is not a string.
	ScriptObject pInt( evalObject( "7" ) );
	CHECK_EQUAL( -1, Script::setData( pInt.get(), strValue, "theStr" ) );
	CHECK_EQUAL( BW::string( "theStr must be set to a string." ),
		currentExceptionMessage() );

	// A str holding a lone surrogate cannot be UTF-8 encoded; the call
	// fails and the UnicodeEncodeError stays pending (the caller sees the
	// real reason rather than a generic TypeError).
	ScriptObject pSurrogate( evalObject( "'\\ud800'" ) );
	CHECK_EQUAL( -1, Script::setData( pSurrogate.get(), strValue,
		"theStr" ) );
	BW::string surrogateType, surrogateMessage;
	consumeCurrentException( surrogateType, surrogateMessage );
	CHECK_EQUAL( BW::string( "UnicodeEncodeError" ), surrogateType );

	CHECK( !PyErr_Occurred() );
}


// -----------------------------------------------------------------------------
// Section: py_logCommon NULL-string arms (CR-P2-11)
// -----------------------------------------------------------------------------

// PyUnicode_AsUTF8 returns NULL for a lone-surrogate str (3.13 refuses the
// UTF-8 encode), which is the only deterministic way to reach the NULL
// checks the CR-P2-11 fix added. Each arm must raise its ValueError instead
// of forwarding NULL to LogMsg/metaAsJSON.
TEST_F( PyScriptUnitTestHarness, Py313_pyLog_surrogateNullArms )
{
	PyObjectPtr pBigWorld( PyImport_ImportModule( "BigWorld" ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pBigWorld.get() != NULL );
	if (pBigWorld.get() == NULL)
	{
		PyErr_Clear();
		return;
	}

	ScriptObject pSurrogate( evalObject( "'\\ud800'" ) );
	CHECK( pSurrogate.get() != NULL );
	ScriptObject pPlain( evalObject( "'msg'" ) );
	CHECK( pPlain.get() != NULL );

	PyObjectPtr pLogInfo( PyObject_GetAttrString( pBigWorld.get(),
		"logInfo" ), PyObjectPtr::STEAL_REFERENCE );
	CHECK( pLogInfo.get() != NULL );

	// Lone-surrogate category string.
	{
		PyObjectPtr pArgs( Py_BuildValue( "(OOO)", pSurrogate.get(),
			pPlain.get(), Py_None ), PyObjectPtr::STEAL_REFERENCE );
		PyErr_Clear();
		PyObjectPtr pRet( PyObject_CallObject( pLogInfo.get(), pArgs.get() ),
			PyObjectPtr::STEAL_REFERENCE );
		CHECK( pRet.get() == NULL );
		BW::string typeName, message;
		consumeCurrentException( typeName, message );
		CHECK_EQUAL( BW::string( "ValueError" ), typeName );
		CHECK_EQUAL( BW::string( "py_logInfo has invalid category string." ),
			message );
	}

	// Lone-surrogate message string.
	{
		PyObjectPtr pArgs( Py_BuildValue( "(OOO)", pPlain.get(),
			pSurrogate.get(), Py_None ), PyObjectPtr::STEAL_REFERENCE );
		PyErr_Clear();
		PyObjectPtr pRet( PyObject_CallObject( pLogInfo.get(), pArgs.get() ),
			PyObjectPtr::STEAL_REFERENCE );
		CHECK( pRet.get() == NULL );
		BW::string typeName, message;
		consumeCurrentException( typeName, message );
		CHECK_EQUAL( BW::string( "ValueError" ), typeName );
		CHECK_EQUAL( BW::string( "py_logInfo has invalid message string." ),
			message );
	}

	// Lone-surrogate metadata string (non-empty, so it takes the
	// metaAsJSON path).
	{
		PyObjectPtr pArgs( Py_BuildValue( "(OOO)", pPlain.get(),
			pPlain.get(), pSurrogate.get() ),
			PyObjectPtr::STEAL_REFERENCE );
		PyErr_Clear();
		PyObjectPtr pRet( PyObject_CallObject( pLogInfo.get(), pArgs.get() ),
			PyObjectPtr::STEAL_REFERENCE );
		CHECK( pRet.get() == NULL );
		BW::string typeName, message;
		consumeCurrentException( typeName, message );
		CHECK_EQUAL( BW::string( "ValueError" ), typeName );
		CHECK_EQUAL( BW::string( "py_logInfo has invalid metadata string." ),
			message );
	}

	CHECK( !PyErr_Occurred() );
}


// -----------------------------------------------------------------------------
// Section: ScriptString::getString NULL arm (CR-P2-11)
// -----------------------------------------------------------------------------

// getString must leave the caller's string empty and the error pending when
// the str cannot be UTF-8 encoded, instead of assigning NULL to a
// std::string (the 3.13 migration found assign(NULL, -1) wrapping size to
// SIZE_MAX). A plain str still round-trips.
TEST_F( PyScriptUnitTestHarness, Py313_ScriptString_getString_surrogate )
{
	PyErr_Clear();

	{
		ScriptObject pStr( evalObject( "'h\\u00e9llo'" ) );
		CHECK( pStr.get() != NULL );
		ScriptString string( pStr );	// shares the ScriptObject reference
		BW::string out( "sentinel" );
		string.getString( out );
		CHECK_EQUAL( BW::string( "h\xC3\xA9llo" ), out );
		CHECK( !PyErr_Occurred() );
	}

	{
		ScriptObject pSurrogate( evalObject( "'\\ud800'" ) );
		CHECK( pSurrogate.get() != NULL );
		ScriptString string( pSurrogate );	// shares the reference

		BW::string out( "sentinel" );
		string.getString( out );
		CHECK_EQUAL( BW::string(), out );
		CHECK( PyErr_GivenExceptionMatches( PyErr_Occurred(),
			PyExc_UnicodeEncodeError ) );
		PyErr_Clear();
	}

	CHECK( !PyErr_Occurred() );
}


// -----------------------------------------------------------------------------
// Section: initExceptionHook re-initialisation (CR-P2-04 surface)
// -----------------------------------------------------------------------------

// PySys_SetObject INCREFs the value and does not steal the caller's
// reference; the CR-P2-04 fix DECREFs the local PyCFunction after handing
// it to sys.excepthook. The observable contract: re-initialising keeps a
// live, callable printTraceBack hook (never clobbered to None, never left
// dangling), and the hook survives being invoked.
TEST_F( PyScriptUnitTestHarness, Py313_initExceptionHook_reinitSafe )
{
	PyErr_Clear();

	Script::initExceptionHook( &regressionEventDispatcher() );
	CHECK( assertHookInstalled() );
	CHECK( !PyErr_Occurred() );

	// Re-initialise: the old hook is released (PySys_SetObject's dict
	// assignment drops the previous value's reference) and the new one is
	// installed - the hook must remain valid either way.
	Script::initExceptionHook( &regressionEventDispatcher() );
	CHECK( assertHookInstalled() );
	CHECK( !PyErr_Occurred() );

	// Invoking the hook as sys.excepthook would must not raise or disturb
	// the installation.
	CHECK( !PyScriptUnitTestHarness::runAndClear(
		"import sys\n"
		"try:\n"
		"    raise RuntimeError( 'hook probe' )\n"
		"except RuntimeError:\n"
		"    sys.excepthook( *sys.exc_info() )\n"
		"assert sys.excepthook.__name__ == 'printTraceBack'\n" ) );

	CHECK( !PyScriptUnitTestHarness::runAndClear(
		"import sys; assert sys.excepthook.__name__ == 'printTraceBack'\n" ) );

	CHECK( !PyErr_Occurred() );
}


// -----------------------------------------------------------------------------
// Section: str/bytes iteration invariants (3.13 string model)
// -----------------------------------------------------------------------------

// The migrated C++ leans on these invariants: str is always Unicode (any
// code point is one str element), bytes is a distinct type whose iteration
// yields ints, and the two never mix implicitly. If CPython ever changed
// one of these, the binding surface's assumptions would need re-review.
TEST_F( PyScriptUnitTestHarness, Py313_strBytesIterationSemantics )
{
	PyErr_Clear();

	// str iterates as 1-char strs; a non-BMP code point is one element.
	CHECK( !PyScriptUnitTestHarness::runAndClear(
		"assert list( 'ab' ) == [ 'a', 'b' ]\n"
		"assert all( type( c ) is str for c in 'ab' )\n"
		"assert len( '\\U0001F600' ) == 1\n"
		"assert len( list( '\\U0001F600' ) ) == 1\n" ) );

	// bytes iterates as ints - code walking a "string" element-wise must
	// not assume str elements (the 2.7 str did).
	CHECK( !PyScriptUnitTestHarness::runAndClear(
		"assert list( b'ab' ) == [ 97, 98 ]\n" ) );

	// str and bytes never mix implicitly.
	CHECK( !PyScriptUnitTestHarness::runAndClear(
		"try:\n"
		"    'a' + b'b'\n"
		"    raise SystemError( 'mixed concat must raise' )\n"
		"except TypeError:\n"
		"    pass\n" ) );

	// There is no PyInt/PyLong split: every integer is an int, and the
	// C++ side reaches it through PyLong_* only.
	CHECK( !PyScriptUnitTestHarness::runAndClear(
		"assert type( 2**64 ) is int\n"
		"assert type( 0 ) is int\n" ) );

	CHECK( !PyErr_Occurred() );
}


BW_END_NAMESPACE

// test_py313_regressions.cpp
