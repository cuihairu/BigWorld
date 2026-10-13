/**
 *	Coverage gaps in pywatcher.cpp that test_pywatcher.cpp never reached.
 *	Every case here targets a line the precise-instrumentation pass reported
 *	as executable-but-uncovered:
 *
 *	- PyObjectWatcher's non-empty-path dispatch arms (setFromString /
 *	  getAsStream / setFromStream through the sequence/mapping special
 *	  watchers) and its always-false addChild;
 *	- SimplePythonWatcher, a public class with a public constructor that is
 *	  instantiated nowhere in the tree but is fully drivable from a test TU:
 *	  its complete getAsString / getAsString-path-recursion / getAsStream /
 *	  setFromString / setFromStream / visitChildren surface (dict keys,
 *	  __dict__ instances, recursion, break arm, NULL base);
 *	- PyAccessorWatcher's stream paths: getAsStream including the __doc__
 *	  arm, setFromStream with its NULL-stream / bad-payload guards, and the
 *	  error arms of getAsString / getAsStream (getter raises, str() raises,
 *	  str() yields a lone surrogate that PyUnicode_AsUTF8 rejects);
 *	- PyFunctionWatcher's argument extraction arms (INT / UINT / FLOAT /
 *	  BOOL / unsupported type falling back to None), its return-value
 *	  encoding arms (int, float, bool, bytes, plain object, and an int too
 *	  large for int64 that fails the int64 conversion), the non-tuple
 *	  packet guard, stdout collection via PyFunctionStdioHook, call
 *	  failure, and the Twisted-deferred detour through PyDeferredWatcher
 *	  (addCallbacks wiring, callback, errback with and without a value);
 *	- the module function error arms: py_setWatcher with an object whose
 *	  str() raises or yields a lone surrogate, py_addWatcher with a
 *	  non-callable setter, and py_addFunctionWatcher's non-callable /
 *	  bad-argument-list / bad-argument-tuple / duplicate-path arms.
 *
 *	Targets that stay uncovered here by construction:
 *	- ~PyAccessorWatcher's post-finalize warning arm (Python is always
 *	  initialized inside these tests);
 *	- PyFunctionWatcher::init's non-callable arm (py_addFunctionWatcher
 *	  checks callability first, so init never sees one) and the
 *	  PySequence_GetItem / PySequence_GetItem-on-2-tuple NULL arms (both
 *	  bounded by the sequence size that was just read);
 *	- PyObject_Str returning NULL inside SimplePythonWatcher::getAsString /
 *	  getAsStream: PyUnicode_Check then dereferences NULL and crashes, so
 *	  that arm is a crash hazard, not a testable branch (see the fixture
 *	  note on RaisingStr);
 *	- SimplePythonWatcher::visitChildren on anything that is not a real
 *	  dict or a __dict__-carrying instance. PyMapping_Check is true for
 *	  list/tuple/str/range too (mp_subscript), so the mapping arm runs
 *	  first, PyMapping_Keys returns NULL, PySequence_Fast returns NULL,
 *	  and Py_DECREF dereferences that NULL at pywatcher.cpp :1934-1938
 *	  (gdb SIGSEGV at :1936). The pure-sequence arm just below (:1943-
 *	  1946) is structurally unreachable -- every PySequence_Check builtin
 *	  also passes PyMapping_Check. Source-level hazard, registered not
 *	  fixed (same precedent as the batch-26 operator* arm);
 *	- PyObjectWatcher's mapping special watcher cannot resolve string
 *	  names: with no key converter, MapWatcher::stringToKey falls through
 *	  to the generic pointer parser (watcher.hpp :115 via :1754-1792) and
 *	  dict name lookups always miss. The dispatch arms still execute (and
 *	  are covered); the tests assert the clean-failure behaviour.
 *	- py_addFunctionWatcher's omitted-comment arm: the comment argument is
 *	  declared optional ("sOOi|s"), but the PyFunctionWatcher is built
 *	  before init() validates the argument list and Watcher's constructor
 *	  turns the NULL comment into std::string( NULL ), which throws
 *	  std::logic_error (watcher.cpp :546). Every registration here passes
 *	  a comment; omitting one can never succeed. Source-level hazard,
 *	  registered not fixed;
 *	- putPyObjectOnStream's FLOAT/BOOL/STRING failure arms, its default
 *	  arm, and getPyObjectWatcherData's NULL arm (:747-748): its only
 *	  caller is PyFunctionWatcher::finishSetFromStream (:1252-1268),
 *	  which ignores the return status, so every failure arm of that
 *	  path is structurally dead through the sole caller;
 *	- extractPyObjectFromStream's STRING break arm (:616): the STRING
 *	  extractor is self-describing and cannot fail, so its bail-out is
 *	  unreachable;
 *	- getPyObjectWatcherData's PyUnicode branch asserts on
 *	  PyUnicode_AsUTF8String returning NULL (:757-763, MF_ASSERT on
 *	  pUTF8String): a function watcher returning a lone-surrogate str
 *	  aborts the process. Only accessor paths that NULL-check
 *	  PyObject_Str/AsUTF8 before this point (PyAccessorWatcher) can
 *	  carry the surrogate payload -- SimplePythonWatcher could not be
 *	  driven with one either;
 *	- the PyFunctionWatcher::init PySequence_GetItem NULL arms
 *	  (:1186/:1191/:1207) and watcherStreamToStream's PyTuple_New
 *	  failure arms (:1338/:1340): bounded-unreachable (the sequence
 *	  size was just read from the same packet, and PyTuple_New cannot
 *	  fail for the tiny counts involved);
 *	- PyAccessorWatcher::getAsStream's non-unicode-str fallback arm
 *	  (:1870): a Python 2 heritage arm. PyObject_Str in Python 3
 *	  returns either a str (PyUnicode_Check passes) or NULL (which
 *	  crashes at the unchecked PyUnicode_Check), so the else branch
 *	  cannot execute post-3.13-migration;
 *	- PyFunctionWatcher::setFromStream's hookScriptOutput NULL arm
 *	  (:1393/:1395-1396) and the onOutputWriterDestroyed bookkeeping
 *	  arm (:1300): only reachable with a live stdout hook installed,
 *	  which the test harness never sets up;
 *	- SimplePythonWatcher's failure paths interact with the Python error
 *	  indicator in a way every driver here must respect: a missed mapping
 *	  child (pythonChildBase's final PyObject_GetAttrString) leaves the
 *	  KeyError / AttributeError pending, and a lone-surrogate encode
 *	  failure whose traceback is printed to the captured stderr can leave
 *	  a fresh UnicodeEncodeError pending (the write of U+D800 itself
 *	  fails). While ANY error is pending, every later
 *	  PyImport_ImportModule of the fixture fails, and a silently-skipped
 *	  test looks green. Hence the PyErr_Clear() after each such case and
 *	  fixtureModule()'s print-and-clear on failure; the CHECK-guarded
 *	  fixture fetches make any residual poisoning loud;
 *	- the WATCHER_TYPE_TUPLE branch of putPyObjectOnStream's default arm:
 *	  getPyObjectWatcherData only ever reports INT/UINT/FLOAT/BOOL/STRING/
 *	  UNKNOWN, so no other type can reach the switch;
 *	- the #else // ENABLE_WATCHERS stubs at the bottom of the file (not
 *	  compiled in the server build).
 */

#include "pch.hpp"
#include "test_harness.hpp"

#include <cassert>
#include <cstdio>

#include "cstdmf/bw_namespace.hpp"
#include "cstdmf/memory_stream.hpp"
#include "cstdmf/watcher.hpp"
#include "cstdmf/watcher_path_request.hpp"
#include "pyscript/py_to_stl.hpp"
#include "pyscript/pywatcher.hpp"
#include "pyscript/script.hpp"

BW_BEGIN_NAMESPACE

namespace
{

PyObject * askBigWorld( PyObject * pBigWorld, const char * functionName,
	PyObject * pArgs, const char * errorPrefix )
{
	return Script::ask( PyObject_GetAttrString( pBigWorld, functionName ),
		pArgs, errorPrefix, false );
}


/**
 *	Recorder that captures what SimplePythonWatcher::visitChildren reports
 *	about a Python object's children.
 */
class B27VisitRecorder : public WatcherPathRequest
{
public:
	B27VisitRecorder() :
		WatcherPathRequest( "b27visit" ),
		count_( -1 ),
		stopAfter_( -1 )
	{}

	virtual void addWatcherCount( int32 count )
	{
		count_ = count;
	}

	virtual bool addWatcherPath( const void * base, const char * path,
		BW::string & label, Watcher & watcher )
	{
		BW::string valueStr;
		BW::string desc;
		Watcher::Mode mode = Watcher::WT_READ_ONLY;

		// No CHECK here: this macro only works inside a test body. A failed
		// getAsString is recorded as a sentinel value the assertions catch.
		if (!watcher.getAsString( base, path, valueStr, desc, mode ))
		{
			valueStr = "<GETFAILED>";
		}
		labels_.push_back( label );
		values_.push_back( valueStr );

		// Returning false aborts the visitor loop in visitChildren.
		return (stopAfter_ < 0) ||
			(int( labels_.size() ) < stopAfter_);
	}

	int32 count_;
	int stopAfter_;
	BW::vector<BW::string> labels_;
	BW::vector<BW::string> values_;
};


PyObject * fixtureModule()
{
	PyObject * pModule = PyImport_ImportModule( "test_pywatcher" );
	if (pModule == NULL)
	{
		// A failed fixture import is always a hard error; print the
		// traceback (it lands in the test's captured stdio) and clear the
		// indicator so one poisoned import cannot cascade into every later
		// test failing its fixture silently.
		PyErr_Print();
	}
	return pModule;
}


PyObject * bigWorldModule()
{
	return PyImport_ImportModule( "BigWorld" );
}


/**
 *	Fetch an attribute from a module, failing the test if it is missing.
 */
PyObject * moduleAttr( PyObject * pModule, const char * attr )
{
	PyObject * pValue = PyObject_GetAttrString( pModule, attr );
	assert( pValue != NULL );
	return pValue;
}


/**
 *	Register a function watcher via BigWorld.addFunctionWatcher with the
 *	default expose hint. A comment string is mandatory in practice:
 *	py_addFunctionWatcher builds the PyFunctionWatcher before validating the
 *	argument list, and Watcher's constructor turns a NULL comment into
 *	std::string( NULL ), which throws std::logic_error.
 */
bool addB27FunctionWatcher( PyObject * pBigWorld, const char * path,
	PyObject * pFunction, PyObject * pArgList )
{
	PyObject * pArgs = Py_BuildValue( "(sOOis)", path, pFunction, pArgList,
		0, "b27" );
	assert( pArgs != NULL );
	PyObject * pResult = askBigWorld( pBigWorld, "addFunctionWatcher",
		pArgs, "addFunctionWatcher" );
	Py_DECREF( pArgs );
	return (pResult != NULL);
}


/**
 *	Register a plain accessor watcher via BigWorld.addWatcher. A NULL setter
 *	omits the optional argument entirely: py_addWatcher treats Py_None as a
 *	non-callable setter rather than "no setter".
 */
bool addB27AccessorWatcher( PyObject * pBigWorld, const char * path,
	PyObject * pGetFunction, PyObject * pSetFunction )
{
	PyObject * pArgs = (pSetFunction != NULL) ?
		Py_BuildValue( "(sOO)", path, pGetFunction, pSetFunction ) :
		Py_BuildValue( "(sO)", path, pGetFunction );
	assert( pArgs != NULL );
	PyObject * pResult = askBigWorld( pBigWorld, "addWatcher", pArgs,
		"addWatcher" );
	Py_DECREF( pArgs );
	return (pResult != NULL);
}


/**
 *	Build a SET2 function-watcher packet with the given argument count
 *	already started; callers append the tuple elements afterwards.
 */
void beginTupleStream( MemoryOStream & stream, int argCount )
{
	stream << (char)WATCHER_TYPE_TUPLE;
	stream.writePackedInt( 0 );		// payload size, unused on success
	stream.writePackedInt( argCount );
}


/**
 *	Append a WATCHER_TYPE_UINT element carrying a full 8 byte payload, so
 *	the uint64 extractor takes its same-size branch.
 */
void appendUint64Element( MemoryOStream & stream, uint64 value )
{
	stream << (char)WATCHER_TYPE_UINT;
	stream << (char)Watcher::WT_READ_WRITE;
	stream.writePackedInt( sizeof( uint64 ) );
	stream << value;
}


/**
 *	Append a WATCHER_TYPE_UNKNOWN element, which the argument extractor
 *	cannot decode and substitutes with None.
 */
void appendUnknownElement( MemoryOStream & stream )
{
	stream << (char)WATCHER_TYPE_UNKNOWN;
	stream << (char)Watcher::WT_READ_WRITE;
	stream.writePackedInt( 0 );
}


/**
 *	Append a tuple element whose declared payload size never matches what
 *	its type's extractor accepts (int64 wants 8 or 4, double wants 8 or 4,
 *	bool wants 1), so the extractor bails out.
 */
void appendBadSizeElement( MemoryOStream & stream, char wireType,
	int payloadSize )
{
	stream << wireType;
	stream << (char)Watcher::WT_READ_WRITE;
	stream.writePackedInt( payloadSize );
	for (int i = 0; i < payloadSize; ++i)
	{
		stream << (char)0;
	}
}


/**
 *	Invoke the function watcher registered at 'path' with the value stream
 *	already carrying its argument tuple. Returns setFromStream's result.
 */
bool invokeFunctionWatcher( const char * path, MemoryOStream & valueStream )
{
	WatcherPathRequestV2 pathRequest( path );
	assert( pathRequest.setPacketData( valueStream ) );
	return Watcher::rootWatcher().setFromStream( NULL, path, pathRequest );
}


/**
 *	Verify that a getAsStream result stream starts with a string element of
 *	the given value and mode, exactly as watcherValueToStream wrote it.
 *	Returns false (with a stderr note) on any mismatch: this helper cannot
 *	use the CHECK macros, which only work inside a test body.
 */
bool checkStreamedString( WatcherPathRequestV2 & pathRequest,
	const char * expected, Watcher::Mode expectedMode )
{
	MemoryOStream & os = pathRequest.getResultStream();
	MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );

	char cType = -1;
	char cMode = -1;
	is >> cType >> cMode;

	BW::string value;
	is >> value;

	if ((cType != (char)WATCHER_TYPE_STRING) ||
			(cMode != (char)expectedMode) ||
			(value != BW::string( expected )))
	{
		fprintf( stderr, "checkStreamedString failed: type=%d mode=%d "
			"value='%s' (wanted type=%d mode=%d value='%s')\n",
			int( cType ), int( cMode ), value.c_str(),
			int( WATCHER_TYPE_STRING ), int( expectedMode ), expected );
		return false;
	}

	return true;
}


/**
 *	Read a Python object's string form, failing the test on conversion
 *	errors instead of dereferencing them.
 */
BW::string objectStr( PyObject * pObject )
{
	assert( pObject != NULL );

	PyObject * pStr = PyObject_Str( pObject );
	assert( pStr != NULL );
	if (pStr == NULL)
	{
		return "<NULL>";
	}

	const char * pCStr = PyUnicode_AsUTF8( pStr );
	assert( pCStr != NULL );
	BW::string result( (pCStr != NULL) ? pCStr : "" );
	Py_DECREF( pStr );
	return result;
}


/**
 *	Look up a mapping entry by C string key and return its string form.
 */
BW::string mappingValueStr( PyObject * pMapping, const char * key )
{
	PyObject * pValue = PyMapping_GetItemString( pMapping, key );
	assert( pValue != NULL );
	if (pValue == NULL)
	{
		return "<MISSING>";
	}
	BW::string result = objectStr( pValue );
	Py_DECREF( pValue );
	return result;
}


/**
 *	Read a sequence element by index and return its string form.
 */
BW::string sequenceValueStr( PyObject * pSequence, Py_ssize_t index )
{
	PyObject * pValue = PySequence_GetItem( pSequence, index );
	assert( pValue != NULL );
	if (pValue == NULL)
	{
		return "<MISSING>";
	}
	BW::string result = objectStr( pValue );
	Py_DECREF( pValue );
	return result;
}

} // anonymous namespace


// -----------------------------------------------------------------------------
// Section: PyObjectWatcher non-empty-path dispatch
// -----------------------------------------------------------------------------

TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_PyObjectWatcherSpecialArms )
{
	// The mapping half: every non-empty-path dispatch arm runs and hands the
	// request to the mapping special watcher, but that watcher cannot resolve
	// string names -- MapWatcher::stringToKey has no key converter here and
	// falls through to the generic pointer parser, so name lookups miss and
	// each call fails cleanly. Directory enumeration is the direction that
	// does work.
	PyObject * pDict = Py_BuildValue( "{s:i,s:s}", "a", 1, "b", "x" );
	CHECK( pDict != NULL );

	PyObjectPtrRefSimple ref( pDict );
	PyObjectWatcher watcher( ref );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_READ_ONLY;
	CHECK( !watcher.getAsString( NULL, "b", result, desc, mode ) );

	CHECK( !watcher.setFromString( NULL, "b", "7" ) );

	WatcherPathRequestV2 pathRequest( "a" );
	CHECK( !watcher.getAsStream( NULL, "a", pathRequest ) );

	MemoryOStream valueStream;
	watcherValueToStream( valueStream, BW::string( "nine" ),
		Watcher::WT_READ_WRITE );
	WatcherPathRequestV2 setRequest( "b" );
	CHECK( setRequest.setPacketData( valueStream ) );
	CHECK( !watcher.setFromStream( NULL, "b", setRequest ) );
	CHECK_EQUAL( BW::string( "x" ), mappingValueStr( pDict, "b" ) );

	B27VisitRecorder recorder;
	CHECK( watcher.visitChildren( NULL, "", recorder ) );
	CHECK_EQUAL( 2, recorder.count_ );
	CHECK_EQUAL( 2, int( recorder.labels_.size() ) );
	CHECK_EQUAL( BW::string( "a" ), recorder.labels_[ 0 ] );
	CHECK_EQUAL( BW::string( "1" ), recorder.values_[ 0 ] );
	CHECK_EQUAL( BW::string( "b" ), recorder.labels_[ 1 ] );
	CHECK_EQUAL( BW::string( "x" ), recorder.values_[ 1 ] );

	// The sequence half: SequenceWatcher::findChild falls back to an atoi
	// index parse, so numeric names resolve against a list.
	PyObject * pList = Py_BuildValue( "[i,s]", 10, "y" );
	CHECK( pList != NULL );

	PyObjectPtrRefSimple listRef( pList );
	PyObjectWatcher listWatcher( listRef );

	CHECK( listWatcher.getAsString( NULL, "0", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "10" ), result );
	CHECK_EQUAL( Watcher::WT_READ_WRITE, mode );

	CHECK( listWatcher.setFromString( NULL, "0", "11" ) );
	CHECK_EQUAL( BW::string( "11" ), sequenceValueStr( pList, 0 ) );

	WatcherPathRequestV2 listRequest( "1" );
	CHECK( listWatcher.getAsStream( NULL, "1", listRequest ) );
	CHECK( checkStreamedString( listRequest, "y", Watcher::WT_READ_WRITE ) );

	// addChild is refused on every python object watcher.
	PyObjectPtrRefSimple childRef( pList );
	WatcherPtr pChild = new PyObjectWatcher( childRef );
	CHECK( !listWatcher.addChild( "child", pChild, NULL ) );

	Py_DECREF( pList );
	Py_DECREF( pDict );
}


// -----------------------------------------------------------------------------
// Section: SimplePythonWatcher
// -----------------------------------------------------------------------------

TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_SimpleGetAsStringScalars )
{
	SimplePythonWatcher watcher;

	// NULL base.
	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_READ_WRITE;
	CHECK( watcher.getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "NULL" ), result );
	CHECK_EQUAL( Watcher::WT_READ_ONLY, mode );

	// bytes.
	PyObject * pBytes = PyBytes_FromString( "raw" );
	CHECK( watcher.getAsString( pBytes, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "raw" ), result );
	CHECK_EQUAL( Watcher::WT_READ_ONLY, mode );

	// unicode.
	PyObject * pStr = PyUnicode_FromString( "text" );
	CHECK( watcher.getAsString( pStr, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "text" ), result );

	// A lone-surrogate str fails its UTF-8 encode.
	PyObject * pModule = fixtureModule();
	CHECK( pModule != NULL );
	if (pModule == NULL) return;
	PyObject * pSurrogate = moduleAttr( pModule, "SURROGATE" );
	CHECK( !watcher.getAsString( pSurrogate, "", result, desc, mode ) );
	// The failed-encode traceback itself carries U+D800, so printing it to
	// the captured stderr can leave a fresh UnicodeEncodeError pending; and
	// the str()-fallback case below fails its own encode without printing
	// at all. Either way the indicator must be cleared or every later
	// PyImport_ImportModule silently fails.
	PyErr_Clear();

	// A plain int falls all the way through to the str() fallback.
	PyObject * pInt = PyLong_FromLong( 42 );
	CHECK( watcher.getAsString( pInt, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "42" ), result );
	CHECK_EQUAL( Watcher::WT_READ_ONLY, mode );

	// An object with no __dict__ whose str() yields a lone surrogate
	// leaves the result empty (the UTF-8 decode of the str() result fails).
	PyObject * pBadStr = PyObject_CallObject(
		moduleAttr( pModule, "BadStrNoDict" ), NULL );
	CHECK( pBadStr != NULL );
	CHECK( watcher.getAsString( pBadStr, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string(), result );
	PyErr_Clear();

	Py_DECREF( pBadStr );
	Py_DECREF( pInt );
	Py_DECREF( pSurrogate );
	Py_DECREF( pStr );
	Py_DECREF( pBytes );
}


TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_SimpleGetAsStringDirsAndPaths )
{
	SimplePythonWatcher watcher;

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_READ_ONLY;

	// A mapping reports as a directory.
	PyObject * pDict = Py_BuildValue( "{s:i,s:s}", "a", 1, "b", "x" );
	CHECK( pDict != NULL );
	CHECK( watcher.getAsString( pDict, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "<DIR>" ), result );
	CHECK_EQUAL( Watcher::WT_DIRECTORY, mode );

	// A plain instance reports as a directory via its __dict__.
	PyObject * pModule = fixtureModule();
	CHECK( pModule != NULL );
	if (pModule == NULL) return;
	PyObject * pWidget = PyObject_CallObject(
		moduleAttr( pModule, "Widget" ), NULL );
	CHECK( pWidget != NULL );
	CHECK( watcher.getAsString( pWidget, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "<DIR>" ), result );
	CHECK_EQUAL( Watcher::WT_DIRECTORY, mode );

	// A list reports as a directory via the sequence probe.
	PyObject * pList = Py_BuildValue( "[i,s]", 10, "y" );
	CHECK( pList != NULL );
	CHECK( watcher.getAsString( pList, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "<DIR>" ), result );

	// Non-empty path recursion: the mapping child is resolved and re-served.
	CHECK( watcher.getAsString( pDict, "a", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "1" ), result );
	CHECK_EQUAL( Watcher::WT_READ_ONLY, mode );

	// Missing child resolves to an empty read-only string.
	CHECK( watcher.getAsString( pDict, "zz", result, desc, mode ) );
	CHECK_EQUAL( BW::string(), result );
	CHECK_EQUAL( Watcher::WT_READ_ONLY, mode );
	// pythonChildBase's final PyObject_GetAttrString leaves the KeyError /
	// AttributeError from the miss pending; clear it or later imports fail.
	PyErr_Clear();

	Py_DECREF( pList );
	Py_DECREF( pWidget );
	Py_DECREF( pDict );
}


TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_SimpleSetFromStringAndStream )
{
	SimplePythonWatcher watcher;
	PyObject * pDict = Py_BuildValue( "{s:i}", "a", 1 );
	CHECK( pDict != NULL );

	// setFromString is a stub that always reports success, empty path or not.
	CHECK( watcher.setFromString( pDict, "", "anything" ) );
	CHECK( watcher.setFromString( pDict, "a", "42" ) );

	// setFromStream is a stub that always reports success too.
	WatcherPathRequestV2 pathRequest( "value" );
	CHECK( watcher.setFromStream( pDict, "", pathRequest ) );

	Py_DECREF( pDict );
}


TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_SimpleGetAsStreamScalars )
{
	SimplePythonWatcher watcher;

	// NULL base.
	WatcherPathRequestV2 nullRequest( "value" );
	CHECK( watcher.getAsStream( NULL, "", nullRequest ) );
	CHECK( checkStreamedString(  nullRequest, "NULL", Watcher::WT_READ_ONLY ) );

	// bytes.
	PyObject * pBytes = PyBytes_FromString( "raw" );
	WatcherPathRequestV2 bytesRequest( "value" );
	CHECK( watcher.getAsStream( pBytes, "", bytesRequest ) );
	CHECK( checkStreamedString(  bytesRequest, "raw", Watcher::WT_READ_ONLY ) );

	// unicode.
	PyObject * pStr = PyUnicode_FromString( "text" );
	WatcherPathRequestV2 strRequest( "value" );
	CHECK( watcher.getAsStream( pStr, "", strRequest ) );
	CHECK( checkStreamedString(  strRequest, "text", Watcher::WT_READ_ONLY ) );

	// A lone-surrogate str fails its UTF-8 encode.
	PyObject * pModule = fixtureModule();
	CHECK( pModule != NULL );
	if (pModule == NULL) return;
	PyObject * pSurrogate = moduleAttr( pModule, "SURROGATE" );
	WatcherPathRequestV2 surrogateRequest( "value" );
	CHECK( !watcher.getAsStream( pSurrogate, "", surrogateRequest ) );
	// Clear the pending encode error, as in the getAsString twin above.
	PyErr_Clear();

	// A plain int takes the str() fallback.
	PyObject * pInt = PyLong_FromLong( 42 );
	WatcherPathRequestV2 intRequest( "value" );
	CHECK( watcher.getAsStream( pInt, "", intRequest ) );
	CHECK( checkStreamedString(  intRequest, "42", Watcher::WT_READ_ONLY ) );

	// An object with no __dict__ whose str() yields a lone surrogate
	// streams an empty string.
	PyObject * pBadStr = PyObject_CallObject(
		moduleAttr( pModule, "BadStrNoDict" ), NULL );
	CHECK( pBadStr != NULL );
	WatcherPathRequestV2 badStrRequest( "value" );
	CHECK( watcher.getAsStream( pBadStr, "", badStrRequest ) );
	CHECK( checkStreamedString(  badStrRequest, "", Watcher::WT_READ_ONLY ) );
	PyErr_Clear();

	Py_DECREF( pBadStr );
	Py_DECREF( pInt );
	Py_DECREF( pSurrogate );
	Py_DECREF( pStr );
	Py_DECREF( pBytes );
}


TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_SimpleGetAsStreamDirsAndPaths )
{
	SimplePythonWatcher watcher;

	// A mapping reports as a read-only directory.
	PyObject * pDict = Py_BuildValue( "{s:i,s:s}", "a", 1, "b", "x" );
	CHECK( pDict != NULL );
	WatcherPathRequestV2 dirRequest( "value" );
	CHECK( watcher.getAsStream( pDict, "", dirRequest ) );
	CHECK( checkStreamedString(  dirRequest, "<DIR>", Watcher::WT_DIRECTORY ) );

	// A plain instance reports as a directory via its __dict__.
	PyObject * pModule = fixtureModule();
	CHECK( pModule != NULL );
	if (pModule == NULL) return;
	PyObject * pWidget = PyObject_CallObject(
		moduleAttr( pModule, "Widget" ), NULL );
	CHECK( pWidget != NULL );
	WatcherPathRequestV2 widgetRequest( "value" );
	CHECK( watcher.getAsStream( pWidget, "", widgetRequest ) );
	CHECK( checkStreamedString(  widgetRequest, "<DIR>", Watcher::WT_DIRECTORY ) );

	// Non-empty path recursion streams the child's value.
	WatcherPathRequestV2 childRequest( "value" );
	CHECK( watcher.getAsStream( pDict, "b", childRequest ) );
	CHECK( checkStreamedString(  childRequest, "x", Watcher::WT_READ_ONLY ) );

	// Missing child streams an empty read-only string.
	WatcherPathRequestV2 missingRequest( "value" );
	CHECK( watcher.getAsStream( pDict, "zz", missingRequest ) );
	CHECK( checkStreamedString(  missingRequest, "", Watcher::WT_READ_ONLY ) );
	// Same pending KeyError / AttributeError from the child miss as in the
	// getAsString twin; clear it.
	PyErr_Clear();

	Py_DECREF( pWidget );
	Py_DECREF( pDict );
}


TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_SimpleVisitChildrenDict )
{
	SimplePythonWatcher watcher;
	PyObject * pDict = Py_BuildValue( "{s:i,s:s}", "a", 1, "b", "x" );
	CHECK( pDict != NULL );

	B27VisitRecorder recorder;
	CHECK( watcher.visitChildren( pDict, "", recorder ) );
	CHECK_EQUAL( 2, recorder.count_ );
	CHECK_EQUAL( 2, int( recorder.labels_.size() ) );
	CHECK_EQUAL( BW::string( "a" ), recorder.labels_[ 0 ] );
	CHECK_EQUAL( BW::string( "1" ), recorder.values_[ 0 ] );
	CHECK_EQUAL( BW::string( "b" ), recorder.labels_[ 1 ] );
	CHECK_EQUAL( BW::string( "x" ), recorder.values_[ 1 ] );

	Py_DECREF( pDict );
}


// PyWatcherGaps_SimpleVisitChildrenSequence is deliberately absent:
// SimplePythonWatcher::visitChildren crashes on any list/tuple/str base,
// because PyMapping_Check is true for all of them (mp_subscript), so the
// mapping arm runs first, PyMapping_Keys returns NULL, PySequence_Fast
// returns NULL, and Py_DECREF dereferences that NULL at pywatcher.cpp
// :1934-1938 (gdb SIGSEGV at :1936). Registered as a source-level hazard;
// the pure-sequence arm right below it is structurally unreachable since
// every PySequence_Check builtin also passes PyMapping_Check.


TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_SimpleVisitChildrenObject )
{
	SimplePythonWatcher watcher;
	PyObject * pModule = fixtureModule();
	CHECK( pModule != NULL );
	if (pModule == NULL) return;

	// A plain instance is examined through its __dict__.
	PyObject * pWidget = PyObject_CallObject(
		moduleAttr( pModule, "Widget" ), NULL );
	CHECK( pWidget != NULL );

	B27VisitRecorder recorder;
	CHECK( watcher.visitChildren( pWidget, "", recorder ) );
	CHECK_EQUAL( 2, recorder.count_ );
	CHECK_EQUAL( 2, int( recorder.labels_.size() ) );
	CHECK_EQUAL( BW::string( "alpha" ), recorder.labels_[ 0 ] );
	CHECK_EQUAL( BW::string( "beta" ), recorder.labels_[ 1 ] );

	Py_DECREF( pWidget );
}


TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_SimpleVisitChildrenRecursion )
{
	SimplePythonWatcher watcher;
	PyObject * pDict = Py_BuildValue( "{s:{s:i}}", "sub", "a", 5 );
	CHECK( pDict != NULL );

	// A non-empty path descends into the mapping child first.
	B27VisitRecorder recorder;
	CHECK( watcher.visitChildren( pDict, "sub", recorder ) );
	CHECK_EQUAL( 1, recorder.count_ );
	CHECK_EQUAL( 1, int( recorder.labels_.size() ) );
	CHECK_EQUAL( BW::string( "a" ), recorder.labels_[ 0 ] );
	CHECK_EQUAL( BW::string( "5" ), recorder.values_[ 0 ] );

	Py_DECREF( pDict );
}


TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_SimpleVisitChildrenBreakAndNull )
{
	SimplePythonWatcher watcher;
	PyObject * pDict = Py_BuildValue( "{s:i,s:i,s:i}", "a", 1, "b", 2, "c", 3 );
	CHECK( pDict != NULL );

	// A recorder that stops after one entry halts the walk (the count
	// callback still reports the full child count).
	B27VisitRecorder stopper;
	stopper.stopAfter_ = 1;
	CHECK( watcher.visitChildren( pDict, "", stopper ) );
	CHECK_EQUAL( 3, stopper.count_ );
	CHECK_EQUAL( 1, int( stopper.labels_.size() ) );

	// A NULL base is refused outright.
	B27VisitRecorder nullRecorder;
	CHECK( !watcher.visitChildren( NULL, "", nullRecorder ) );

	Py_DECREF( pDict );
}


// -----------------------------------------------------------------------------
// Section: PyAccessorWatcher stream paths and error arms
// -----------------------------------------------------------------------------

TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_AccessorGetAsStreamArms )
{
	PyObject * pBigWorld = bigWorldModule();
	CHECK( pBigWorld != NULL );
	if (pBigWorld == NULL) return;
	PyObject * pModule = fixtureModule();
	CHECK( pModule != NULL );
	if (pModule == NULL) return;

	// Normalise the fixture state: earlier tests leave g_value holding
	// something else.
	{
		PyObject * pArgs = Py_BuildValue( "(s)", "hello" );
		assert( pArgs != NULL );
		PyObject * pResult = askBigWorld( pModule, "watcherSet", pArgs,
			"restore" );
		Py_XDECREF( pResult );
		Py_DECREF( pArgs );
	}

	// Read-only accessor.
	CHECK( addB27AccessorWatcher( pBigWorld, "test27/ro",
		moduleAttr( pModule, "watcherGet" ), NULL ) );

	WatcherPathRequestV2 roRequest( "test27/ro" );
	CHECK( Watcher::rootWatcher().getAsStream( NULL, "test27/ro", roRequest ) );
	CHECK( checkStreamedString(  roRequest, "hello", Watcher::WT_READ_ONLY ) );

	// The __doc__ path streams the watcher comment.
	WatcherPathRequestV2 docRequest( "test27/ro/__doc__" );
	CHECK( Watcher::rootWatcher().getAsStream( NULL, "test27/ro/__doc__",
		docRequest ) );
	CHECK( checkStreamedString(  docRequest, "", Watcher::WT_READ_ONLY ) );

	// A getter that returns a lone-surrogate str fails the UTF-8 encode in
	// both the string and stream forms.
	CHECK( addB27AccessorWatcher( pBigWorld, "test27/surro",
		moduleAttr( pModule, "surrogateStr" ), NULL ) );
	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_READ_WRITE;
	CHECK( !Watcher::rootWatcher().getAsString( NULL, "test27/surro",
		result, desc, mode ) );
	WatcherPathRequestV2 surrogateRequest( "test27/surro" );
	CHECK( !Watcher::rootWatcher().getAsStream( NULL, "test27/surro",
		surrogateRequest ) );
	// The surrogate traceback printed to the captured stderr can leave a
	// fresh UnicodeEncodeError pending (the write of U+D800 itself fails);
	// clear it or later imports silently fail.
	PyErr_Clear();

	// A getter whose str() raises fails the string conversion in both forms.
	CHECK( addB27AccessorWatcher( pBigWorld, "test27/badstr",
		moduleAttr( pModule, "badStrGetter" ), NULL ) );
	CHECK( !Watcher::rootWatcher().getAsString( NULL, "test27/badstr",
		result, desc, mode ) );
	WatcherPathRequestV2 badStrRequest( "test27/badstr" );
	CHECK( !Watcher::rootWatcher().getAsStream( NULL, "test27/badstr",
		badStrRequest ) );
	PyErr_Clear();

	// A getter that raises fails the call in both forms.
	CHECK( addB27AccessorWatcher( pBigWorld, "test27/raising",
		moduleAttr( pModule, "raisingGetter" ), NULL ) );
	CHECK( !Watcher::rootWatcher().getAsString( NULL, "test27/raising",
		result, desc, mode ) );
	WatcherPathRequestV2 raisingRequest( "test27/raising" );
	CHECK( !Watcher::rootWatcher().getAsStream( NULL, "test27/raising",
		raisingRequest ) );

	// A getter whose value's str() itself raises takes the PyObject_Str
	// NULL arm in both forms (unlike SimplePythonWatcher, the accessor
	// checks the result for NULL).
	CHECK( addB27AccessorWatcher( pBigWorld, "test27/rstr",
		moduleAttr( pModule, "raisingStrGetter" ), NULL ) );
	CHECK( !Watcher::rootWatcher().getAsString( NULL, "test27/rstr",
		result, desc, mode ) );
	WatcherPathRequestV2 rStrRequest( "test27/rstr" );
	CHECK( !Watcher::rootWatcher().getAsStream( NULL, "test27/rstr",
		rStrRequest ) );

	CHECK( Watcher::rootWatcher().removeChild( "test27/ro" ) );
	CHECK( Watcher::rootWatcher().removeChild( "test27/surro" ) );
	CHECK( Watcher::rootWatcher().removeChild( "test27/badstr" ) );
	CHECK( Watcher::rootWatcher().removeChild( "test27/raising" ) );
	CHECK( Watcher::rootWatcher().removeChild( "test27/rstr" ) );
}


TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_AccessorSurrogateStream )
{
	PyObject * pBigWorld = bigWorldModule();
	CHECK( pBigWorld != NULL );
	if (pBigWorld == NULL) return;
	PyObject * pModule = fixtureModule();
	CHECK( pModule != NULL );
	if (pModule == NULL) return;

	// A getter returning a lone-surrogate str fails PyUnicode_AsUTF8String
	// inside getAsStream's own str arm -- a different guard than the
	// PyObject_Str NULL arm driven by raisingStrGetter.
	CHECK( addB27AccessorWatcher( pBigWorld, "test27/sstream",
		moduleAttr( pModule, "surrogateStr" ), NULL ) );

	BW::string sResult;
	BW::string sDesc;
	Watcher::Mode sMode = Watcher::WT_READ_ONLY;
	CHECK( !Watcher::rootWatcher().getAsString( NULL, "test27/sstream",
		sResult, sDesc, sMode ) );

	WatcherPathRequestV2 sRequest( "test27/sstream" );
	CHECK( !Watcher::rootWatcher().getAsStream( NULL, "test27/sstream",
		sRequest ) );
	// Clear the pending encode error, as in the accessor arms test above.
	PyErr_Clear();

	CHECK( Watcher::rootWatcher().removeChild( "test27/sstream" ) );
}


TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_AccessorSetFromStream )
{
	PyObject * pBigWorld = bigWorldModule();
	CHECK( pBigWorld != NULL );
	if (pBigWorld == NULL) return;
	PyObject * pModule = fixtureModule();
	CHECK( pModule != NULL );
	if (pModule == NULL) return;

	CHECK( addB27AccessorWatcher( pBigWorld, "test27/rw",
		moduleAttr( pModule, "watcherGet" ),
		moduleAttr( pModule, "watcherSet" ) ) );

	// A set without packet data is refused on the NULL value stream.
	WatcherPathRequestV2 emptyRequest( "test27/rw" );
	CHECK( !Watcher::rootWatcher().setFromStream( NULL, "test27/rw",
		emptyRequest ) );

	// A set whose payload cannot be decoded is refused too.
	{
		MemoryOStream valueStream;
		appendUnknownElement( valueStream );
		WatcherPathRequestV2 badRequest( "test27/rw" );
		CHECK( badRequest.setPacketData( valueStream ) );
		CHECK( !Watcher::rootWatcher().setFromStream( NULL, "test27/rw",
			badRequest ) );
	}

	// A good set decodes the string payload, calls the setter, and re-fetches
	// the value into the result stream.
	{
		MemoryOStream valueStream;
		watcherValueToStream( valueStream, BW::string( "set27" ),
			Watcher::WT_READ_WRITE );
		WatcherPathRequestV2 setRequest( "test27/rw" );
		CHECK( setRequest.setPacketData( valueStream ) );
		CHECK( Watcher::rootWatcher().setFromStream( NULL, "test27/rw",
			setRequest ) );
		CHECK_EQUAL( BW::string( "set27" ),
			objectStr( moduleAttr( pModule, "g_value" ) ) );
	}

	// A setter that raises fails the set.
	CHECK( addB27AccessorWatcher( pBigWorld, "test27/badset",
		moduleAttr( pModule, "watcherGet" ),
		moduleAttr( pModule, "failingSetter" ) ) );
	{
		MemoryOStream valueStream;
		watcherValueToStream( valueStream, BW::string( "nope" ),
			Watcher::WT_READ_WRITE );
		WatcherPathRequestV2 badSetRequest( "test27/badset" );
		CHECK( badSetRequest.setPacketData( valueStream ) );
		CHECK( !Watcher::rootWatcher().setFromStream( NULL, "test27/badset",
			badSetRequest ) );
	}

	// A set that succeeds but leaves a lone surrogate behind fails the
	// post-set re-fetch.
	CHECK( addB27AccessorWatcher( pBigWorld, "test27/sset",
		moduleAttr( pModule, "surrogateStr" ),
		moduleAttr( pModule, "surrogateSetter" ) ) );
	{
		MemoryOStream valueStream;
		watcherValueToStream( valueStream, BW::string( "whatever" ),
			Watcher::WT_READ_WRITE );
		WatcherPathRequestV2 surroSetRequest( "test27/sset" );
		CHECK( surroSetRequest.setPacketData( valueStream ) );
		CHECK( !Watcher::rootWatcher().setFromStream( NULL, "test27/sset",
			surroSetRequest ) );
	}

	// Restore the fixture state for the other tests.
	PyObject * pArgs = Py_BuildValue( "(s)", "hello" );
	PyObject * pResult = askBigWorld( pModule, "watcherSet", pArgs, "restore" );
	Py_XDECREF( pResult );
	Py_DECREF( pArgs );

	CHECK( Watcher::rootWatcher().removeChild( "test27/rw" ) );
	CHECK( Watcher::rootWatcher().removeChild( "test27/badset" ) );
	CHECK( Watcher::rootWatcher().removeChild( "test27/sset" ) );
}


// -----------------------------------------------------------------------------
// Section: PyFunctionWatcher argument extraction
// -----------------------------------------------------------------------------

TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_FunctionWatcherArgExtraction )
{
	PyObject * pBigWorld = bigWorldModule();
	CHECK( pBigWorld != NULL );
	if (pBigWorld == NULL) return;
	PyObject * pModule = fixtureModule();
	CHECK( pModule != NULL );
	if (pModule == NULL) return;

	CHECK( addB27FunctionWatcher( pBigWorld, "command/test27Echo",
		moduleAttr( pModule, "echoFunction" ),
		moduleAttr( pModule, "ECHO_ARG_LIST" ) ) );

	// INT and STRING tuple elements decode into the python call.
	MemoryOStream valueStream;
	beginTupleStream( valueStream, 2 );
	watcherValueToStream( valueStream, (int64)7, Watcher::WT_READ_WRITE );
	watcherValueToStream( valueStream, BW::string( "x" ),
		Watcher::WT_READ_WRITE );

	CHECK( invokeFunctionWatcher( "command/test27Echo", valueStream ) );
	CHECK_EQUAL( 1, PyObject_IsTrue(
		moduleAttr( pModule, "g_lastArgsOk" ) ) );

	CHECK( Watcher::rootWatcher().removeChild( "command/test27Echo" ) );
}


TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_FunctionWatcherArgMixed )
{
	PyObject * pBigWorld = bigWorldModule();
	CHECK( pBigWorld != NULL );
	if (pBigWorld == NULL) return;
	PyObject * pModule = fixtureModule();
	CHECK( pModule != NULL );
	if (pModule == NULL) return;

	CHECK( addB27FunctionWatcher( pBigWorld, "command/test27Quad",
		moduleAttr( pModule, "quadFunction" ),
		moduleAttr( pModule, "QUAD_ARG_LIST" ) ) );

	// UINT (full 8 byte payload), FLOAT and BOOL decode; the unsupported
	// element type degrades to None.
	MemoryOStream valueStream;
	beginTupleStream( valueStream, 4 );
	appendUint64Element( valueStream, 5 );
	watcherValueToStream( valueStream, (double)2.5, Watcher::WT_READ_WRITE );
	watcherValueToStream( valueStream, true, Watcher::WT_READ_WRITE );
	appendUnknownElement( valueStream );

	CHECK( invokeFunctionWatcher( "command/test27Quad", valueStream ) );
	CHECK_EQUAL( 1, PyObject_IsTrue(
		moduleAttr( pModule, "g_lastArgsOk" ) ) );

	CHECK( Watcher::rootWatcher().removeChild( "command/test27Quad" ) );
}


// -----------------------------------------------------------------------------
// Section: PyFunctionWatcher argument payload size mismatch
// -----------------------------------------------------------------------------

TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_FunctionWatcherBadPayload )
{
	PyObject * pBigWorld = bigWorldModule();
	CHECK( pBigWorld != NULL );
	if (pBigWorld == NULL) return;
	PyObject * pModule = fixtureModule();
	CHECK( pModule != NULL );
	if (pModule == NULL) return;

	CHECK( addB27FunctionWatcher( pBigWorld, "command/test27BadPayload",
		moduleAttr( pModule, "echoFunction" ),
		moduleAttr( pModule, "ECHO_ARG_LIST" ) ) );

	// Every type extractor refuses a payload size it does not know, so the
	// element falls back to None and the call is aborted before it runs.
	// (int64 accepts 8 or 4, double accepts 8 or 4, bool accepts only 1.)
	const int wireTypes[] = { WATCHER_TYPE_INT, WATCHER_TYPE_UINT,
		WATCHER_TYPE_FLOAT, WATCHER_TYPE_BOOL };
	int sizes[] = { 3, 3, 3, 2 };

	for (uint i = 0; i < sizeof( sizes ) / sizeof( sizes[ 0 ] ); ++i)
	{
		MemoryOStream valueStream;
		beginTupleStream( valueStream, 1 );
		appendBadSizeElement( valueStream, (char)wireTypes[ i ], sizes[ i ] );
		CHECK( !invokeFunctionWatcher( "command/test27BadPayload",
			valueStream ) );
	}

	CHECK( Watcher::rootWatcher().removeChild( "command/test27BadPayload" ) );
}


// -----------------------------------------------------------------------------
// Section: PyFunctionWatcher return-value encoding
// -----------------------------------------------------------------------------

TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_FunctionWatcherReturnTypes )
{
	PyObject * pBigWorld = bigWorldModule();
	CHECK( pBigWorld != NULL );
	if (pBigWorld == NULL) return;
	PyObject * pModule = fixtureModule();
	CHECK( pModule != NULL );
	if (pModule == NULL) return;

	PyObject * pEmptyArgs = PyTuple_New( 0 );
	CHECK( pEmptyArgs != NULL );

	// Each target returns a different python type so the response encoder
	// takes a different arm: int, float, bool, bytes, plain object.
	const char * names[] =
	{
		"intFunction", "floatFunction", "boolFunction",
		"bytesFunction", "objectFunction"
	};
	for (uint i = 0; i < sizeof( names ) / sizeof( names[0] ); ++i)
	{
		BW::string path = BW::string( "command/test27Ret" ) + names[ i ];
		CHECK( addB27FunctionWatcher( pBigWorld, path.c_str(),
			moduleAttr( pModule, names[ i ] ), pEmptyArgs ) );

		MemoryOStream valueStream;
		beginTupleStream( valueStream, 0 );
		CHECK( invokeFunctionWatcher( path.c_str(), valueStream ) );
	}

	// An int too large for int64 fails the conversion inside the response
	// encoder; the watcher call itself still reports success.
	CHECK( addB27FunctionWatcher( pBigWorld, "command/test27RetBigInt",
		moduleAttr( pModule, "bigIntFunction" ), pEmptyArgs ) );
	MemoryOStream bigValueStream;
	beginTupleStream( bigValueStream, 0 );
	CHECK( invokeFunctionWatcher( "command/test27RetBigInt", bigValueStream ) );

	for (uint i = 0; i < sizeof( names ) / sizeof( names[0] ); ++i)
	{
		BW::string path = BW::string( "command/test27Ret" ) + names[ i ];
		CHECK( Watcher::rootWatcher().removeChild( path.c_str() ) );
	}
	CHECK( Watcher::rootWatcher().removeChild( "command/test27RetBigInt" ) );

	Py_DECREF( pEmptyArgs );
}


TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_FunctionWatcherStdioAndFailure )
{
	PyObject * pBigWorld = bigWorldModule();
	CHECK( pBigWorld != NULL );
	if (pBigWorld == NULL) return;
	PyObject * pModule = fixtureModule();
	CHECK( pModule != NULL );
	if (pModule == NULL) return;

	PyObject * pEmptyArgs = PyTuple_New( 0 );
	CHECK( pEmptyArgs != NULL );

	// A printing function routes its output through the stdio hook into
	// the reply.
	CHECK( addB27FunctionWatcher( pBigWorld, "command/test27Print",
		moduleAttr( pModule, "printingFunction" ), pEmptyArgs ) );
	MemoryOStream printStream;
	beginTupleStream( printStream, 0 );
	CHECK( invokeFunctionWatcher( "command/test27Print", printStream ) );

	// A raising function fails the set operation.
	CHECK( addB27FunctionWatcher( pBigWorld, "command/test27Fail",
		moduleAttr( pModule, "failingFunction" ), pEmptyArgs ) );
	MemoryOStream failStream;
	beginTupleStream( failStream, 0 );
	CHECK( !invokeFunctionWatcher( "command/test27Fail", failStream ) );

	// A non-tuple packet is refused before the call.
	CHECK( addB27FunctionWatcher( pBigWorld, "command/test27NonTuple",
		moduleAttr( pModule, "intFunction" ), pEmptyArgs ) );
	MemoryOStream nonTupleStream;
	watcherValueToStream( nonTupleStream, BW::string( "nope" ),
		Watcher::WT_READ_WRITE );
	CHECK( !invokeFunctionWatcher( "command/test27NonTuple", nonTupleStream ) );

	CHECK( Watcher::rootWatcher().removeChild( "command/test27Print" ) );
	CHECK( Watcher::rootWatcher().removeChild( "command/test27Fail" ) );
	CHECK( Watcher::rootWatcher().removeChild( "command/test27NonTuple" ) );

	Py_DECREF( pEmptyArgs );
}


TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_FunctionWatcherDeferred )
{
	PyObject * pBigWorld = bigWorldModule();
	CHECK( pBigWorld != NULL );
	if (pBigWorld == NULL) return;
	PyObject * pModule = fixtureModule();
	CHECK( pModule != NULL );
	if (pModule == NULL) return;

	CHECK( addB27FunctionWatcher( pBigWorld, "command/test27Deferred",
		moduleAttr( pModule, "deferredFunction" ), PyTuple_New( 0 ) ) );

	// The path request must stay alive past the set call: the deferred
	// watcher keeps a reference to it and writes the response into it when
	// the callback / errback fires below.
	MemoryOStream valueStream;
	beginTupleStream( valueStream, 0 );
	WatcherPathRequestV2 pathRequest( "command/test27Deferred" );
	CHECK( pathRequest.setPacketData( valueStream ) );
	CHECK( Watcher::rootWatcher().setFromStream( NULL,
		"command/test27Deferred", pathRequest ) );

	// The deferred-shaped result detours into the addCallbacks arm, handing
	// the bound callback / errback pair to the fake deferred.
	CHECK_EQUAL( BW::string( "added" ),
		objectStr( moduleAttr( pModule, "g_fired" ) ) );

	// The callback finishes the response with the provided value.
	{
		PyObject * pArgs = Py_BuildValue( "(i)", 5 );
		PyObject * pResult = askBigWorld( pModule, "fireCallback", pArgs,
			"fireCallback" );
		Py_XDECREF( pResult );
		Py_DECREF( pArgs );
		CHECK_EQUAL( BW::string( "cb" ),
			objectStr( moduleAttr( pModule, "g_fired" ) ) );
	}

	// The errback finishes the response with the failure's value attribute.
	{
		PyObject * pResult = askBigWorld( pModule, "fireErrbackFake", PyTuple_New( 0 ),
			"fireErrbackFake" );
		Py_XDECREF( pResult );
		CHECK_EQUAL( BW::string( "eb" ),
			objectStr( moduleAttr( pModule, "g_fired" ) ) );
	}

	// An errback without a value attribute skips the response.
	{
		PyObject * pResult = askBigWorld( pModule, "fireErrbackNoValue", PyTuple_New( 0 ),
			"fireErrbackNoValue" );
		Py_XDECREF( pResult );
		CHECK_EQUAL( BW::string( "eb2" ),
			objectStr( moduleAttr( pModule, "g_fired" ) ) );
	}

	// A deferred whose addCallbacks wiring itself fails takes the
	// printError arm; the call still reports success.
	CHECK( addB27FunctionWatcher( pBigWorld, "command/test27DeferredBad",
		moduleAttr( pModule, "deferredBadFunction" ), PyTuple_New( 0 ) ) );
	{
		MemoryOStream badStream;
		beginTupleStream( badStream, 0 );
		CHECK( invokeFunctionWatcher( "command/test27DeferredBad",
			badStream ) );
	}

	// Release the deferred callbacks while the path request is still alive.
	PyObject * pClearResult = askBigWorld( pModule, "clearDeferred", PyTuple_New( 0 ),
		"clearDeferred" );
	Py_XDECREF( pClearResult );

	CHECK( Watcher::rootWatcher().removeChild( "command/test27Deferred" ) );
}


// -----------------------------------------------------------------------------
// Section: module function error arms
// -----------------------------------------------------------------------------

TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_SetWatcherErrorArms )
{
	PyObject * pBigWorld = bigWorldModule();
	CHECK( pBigWorld != NULL );
	if (pBigWorld == NULL) return;
	PyObject * pModule = fixtureModule();
	CHECK( pModule != NULL );
	if (pModule == NULL) return;

	// An object whose str() raises leaves PyObject_Str NULL.
	{
		PyObject * pArgs = Py_BuildValue( "(sN)", "test27/nope",
			PyObject_CallObject( moduleAttr( pModule, "RaisingStr" ), NULL ) );
		CHECK( pArgs != NULL );
		CHECK( askBigWorld( pBigWorld, "setWatcher", pArgs,
			"setWatcher" ) == NULL );
		Py_DECREF( pArgs );
	}

	// An object whose str() yields a lone surrogate fails its UTF-8 encode.
	{
		PyObject * pArgs = Py_BuildValue( "(sN)", "test27/nope",
			PyObject_CallObject( moduleAttr( pModule, "BadStr" ), NULL ) );
		CHECK( pArgs != NULL );
		CHECK( askBigWorld( pBigWorld, "setWatcher", pArgs,
			"setWatcher" ) == NULL );
		Py_DECREF( pArgs );
		// The surrogate traceback can leave a fresh UnicodeEncodeError
		// pending on the captured stderr; clear it for later tests.
		PyErr_Clear();
	}
}


TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_AddWatcherSetNotCallable )
{
	PyObject * pBigWorld = bigWorldModule();
	CHECK( pBigWorld != NULL );
	if (pBigWorld == NULL) return;
	PyObject * pModule = fixtureModule();
	CHECK( pModule != NULL );
	if (pModule == NULL) return;

	PyObject * pArgs = Py_BuildValue( "(sOO)", "test27/bad",
		moduleAttr( pModule, "watcherGet" ), PyLong_FromLong( 42 ) );
	CHECK( pArgs != NULL );
	CHECK( askBigWorld( pBigWorld, "addWatcher", pArgs,
		"addWatcher" ) == NULL );
	Py_DECREF( pArgs );
}


TEST_F( PyScriptUnitTestHarness, PyWatcherGaps_AddFunctionWatcherBadArgs )
{
	PyObject * pBigWorld = bigWorldModule();
	CHECK( pBigWorld != NULL );
	if (pBigWorld == NULL) return;
	PyObject * pModule = fixtureModule();
	CHECK( pModule != NULL );
	if (pModule == NULL) return;

	PyObject * pFunction = moduleAttr( pModule, "intFunction" );

	// Non-callable function.
	{
		PyObject * pArgs = Py_BuildValue( "(sOOis)", "command/test27Bad",
			PyLong_FromLong( 42 ), PyTuple_New( 0 ), 0, "b27" );
		CHECK( pArgs != NULL );
		CHECK( askBigWorld( pBigWorld, "addFunctionWatcher", pArgs,
			"addFunctionWatcher" ) == NULL );
		Py_DECREF( pArgs );
	}

	// Argument list that is not a sequence.
	{
		PyObject * pArgs = Py_BuildValue( "(sOOis)", "command/test27Bad",
			pFunction, Py_None, 0, "b27" );
		CHECK( pArgs != NULL );
		CHECK( askBigWorld( pBigWorld, "addFunctionWatcher", pArgs,
			"addFunctionWatcher" ) == NULL );
		Py_DECREF( pArgs );
	}

	// Argument element that is neither a type nor a 2-tuple.
	{
		PyObject * pArgs = Py_BuildValue( "(sOOis)", "command/test27Bad",
			pFunction, moduleAttr( pModule, "BAD_ELEMENT_ARG_LIST" ), 0, "b27" );
		CHECK( pArgs != NULL );
		CHECK( askBigWorld( pBigWorld, "addFunctionWatcher", pArgs,
			"addFunctionWatcher" ) == NULL );
		Py_DECREF( pArgs );
	}

	// Argument type that is not a watcher protocol primitive.
	{
		PyObject * pArgs = Py_BuildValue( "(sOOis)", "command/test27Bad",
			pFunction, moduleAttr( pModule, "BAD_TYPE_ARG_LIST" ), 0, "b27" );
		CHECK( pArgs != NULL );
		CHECK( askBigWorld( pBigWorld, "addFunctionWatcher", pArgs,
			"addFunctionWatcher" ) == NULL );
		Py_DECREF( pArgs );
	}

	// Named argument whose name is not a string.
	{
		PyObject * pArgs = Py_BuildValue( "(sOOis)", "command/test27Bad",
			pFunction, moduleAttr( pModule, "BAD_NAME_TYPE_ARG_LIST" ), 0, "b27" );
		CHECK( pArgs != NULL );
		CHECK( askBigWorld( pBigWorld, "addFunctionWatcher", pArgs,
			"addFunctionWatcher" ) == NULL );
		Py_DECREF( pArgs );
	}

	// Named argument whose name fails its UTF-8 encode.
	{
		PyObject * pArgs = Py_BuildValue( "(sOOis)", "command/test27Bad",
			pFunction, moduleAttr( pModule, "BAD_NAME_ARG_LIST" ), 0, "b27" );
		CHECK( pArgs != NULL );
		CHECK( askBigWorld( pBigWorld, "addFunctionWatcher", pArgs,
			"addFunctionWatcher" ) == NULL );
		Py_DECREF( pArgs );
	}

	// A valid named argument list registers fine.
	CHECK( addB27FunctionWatcher( pBigWorld, "command/test27Named",
		pFunction, moduleAttr( pModule, "NAMED_ARG_LIST" ) ) );
	CHECK( Watcher::rootWatcher().removeChild( "command/test27Named" ) );

	// A duplicate path fails to add to the watcher tree.
	CHECK( addB27FunctionWatcher( pBigWorld, "command/test27Dup",
		pFunction, PyTuple_New( 0 ) ) );
	{
		PyObject * pArgs = Py_BuildValue( "(sOOis)", "command/test27Dup",
			pFunction, PyTuple_New( 0 ), 0, "b27" );
		CHECK( pArgs != NULL );
		CHECK( askBigWorld( pBigWorld, "addFunctionWatcher", pArgs,
			"addFunctionWatcher" ) == NULL );
		Py_DECREF( pArgs );
	}
	CHECK( Watcher::rootWatcher().removeChild( "command/test27Dup" ) );
}

BW_END_NAMESPACE

// test_pywatcher_gaps.cpp
