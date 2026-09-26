#include "pch.hpp"

#include "cstdmf/watcher.hpp"
#include "cstdmf/memory_stream.hpp"
#include "cstdmf/bw_map.hpp"
#include "cstdmf/bw_vector.hpp"
#include "cstdmf/concurrency.hpp"


BW_BEGIN_NAMESPACE

// -----------------------------------------------------------------------------
// Section: Path helpers
// -----------------------------------------------------------------------------

TEST( Watcher_partitionPath )
{
	BW::string name;
	BW::string directory;

	Watcher::partitionPath( "root/child/grandchild", name, directory );
	CHECK_EQUAL( BW::string( "grandchild" ), name );
	CHECK_EQUAL( BW::string( "root/child/" ), directory );

	Watcher::partitionPath( "child", name, directory );
	CHECK_EQUAL( BW::string( "child" ), name );
	CHECK_EQUAL( BW::string( "" ), directory );

	Watcher::partitionPath( "a/b", name, directory );
	CHECK_EQUAL( BW::string( "b" ), name );
	CHECK_EQUAL( BW::string( "a/" ), directory );
}


TEST( Watcher_tail )
{
	CHECK_EQUAL( 0, strcmp( "b/c", DirectoryWatcher::tail( "a/b/c" ) ) );
	CHECK_EQUAL( 0, strcmp( "c", DirectoryWatcher::tail( "ab/c" ) ) );
	CHECK( DirectoryWatcher::tail( "no_separator" ) == NULL );
	CHECK( DirectoryWatcher::tail( NULL ) == NULL );
	CHECK_EQUAL( 0, strcmp( "", DirectoryWatcher::tail( "a/" ) ) );
}


// -----------------------------------------------------------------------------
// Section: DirectoryWatcher
// -----------------------------------------------------------------------------

/**
 *	A visitor that records each child visited.
 */
class TestVisitor : public WatcherVisitor
{
public:
	TestVisitor() : count_( 0 )
	{
	}

	virtual bool visit( Watcher::Mode mode,
		const BW::string & label,
		const BW::string & desc,
		const BW::string & valueStr )
	{
		++count_;
		labels_.push_back( label );
		values_.push_back( valueStr );
		return true;
	}

	int count_;
	BW::vector< BW::string > labels_;
	BW::vector< BW::string > values_;
};


TEST( DirectoryWatcher_addAndGetChild )
{
	DirectoryWatcherPtr pDir = new DirectoryWatcher();

	int value = 42;
	WatcherPtr pValue = new DataWatcher< int >( value );

	CHECK( pDir->addChild( "value", pValue ) );

	CHECK( pDir->getChild( "value" ) == pValue );
	CHECK( pDir->getChild( "nonExistent" ) == NULL );

	// Adding a child with the same label is rejected.
	CHECK( !pDir->addChild( "value", pValue ) );

	// Adding with an empty path is rejected.
	CHECK( !pDir->addChild( "", pValue ) );
}


TEST( DirectoryWatcher_addNestedPath )
{
	DirectoryWatcherPtr pDir = new DirectoryWatcher();

	int value = 1;
	WatcherPtr pValue = new DataWatcher< int >( value );

	// This creates the intermediate directory "sub".
	CHECK( pDir->addChild( "sub/value", pValue ) );

	CHECK( pDir->getChild( "sub" ) != NULL );
	CHECK( pDir->getChild( "sub/value" ) == pValue );

	// Adding under a non directory watcher fails.
	CHECK( !pDir->addChild( "sub/value/deeper", pValue ) );
}


TEST( DirectoryWatcher_removeChild )
{
	DirectoryWatcherPtr pDir = new DirectoryWatcher();

	int value = 1;
	WatcherPtr pValue = new DataWatcher< int >( value );

	CHECK( pDir->addChild( "value", pValue ) );
	CHECK( pDir->removeChild( "value" ) );
	CHECK( pDir->getChild( "value" ) == NULL );

	// Removing again fails.
	CHECK( !pDir->removeChild( "value" ) );

	// NULL path fails.
	CHECK( !pDir->removeChild( NULL ) );

	// Nested removal.
	DirectoryWatcherPtr pDir2 = new DirectoryWatcher();
	CHECK( pDir2->addChild( "sub/value", pValue ) );
	CHECK( pDir2->removeChild( "sub/value" ) );
	CHECK( pDir2->getChild( "sub" ) != NULL );
	CHECK( pDir2->getChild( "sub/value" ) == NULL );
}


TEST( DirectoryWatcher_getAsString )
{
	DirectoryWatcherPtr pDir = new DirectoryWatcher();

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	// An empty path on a directory gives "<DIR>".
	CHECK( pDir->getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "<DIR>" ), result );
	CHECK_EQUAL( Watcher::WT_DIRECTORY, mode );

	// Add a value watcher and read it through the directory.
	int value = 7;
	WatcherPtr pValue = new DataWatcher< int >( value );
	pDir->addChild( "value", pValue );

	CHECK( pDir->getAsString( NULL, "value", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "7" ), result );
	CHECK_EQUAL( Watcher::WT_READ_ONLY, mode );

	// Unknown children fail.
	CHECK( !pDir->getAsString( NULL, "nonExistent", result, desc, mode ) );
	CHECK( !pDir->getAsString( NULL, "value/sub", result, desc, mode ) );
}


TEST( DirectoryWatcher_setFromString )
{
	DirectoryWatcherPtr pDir = new DirectoryWatcher();

	int value = 0;
	WatcherPtr pValue = new DataWatcher< int >( value, Watcher::WT_READ_WRITE );
	pDir->addChild( "value", pValue );

	CHECK( pDir->setFromString( NULL, "value", "13" ) );
	CHECK_EQUAL( 13, value );

	// Unknown children fail.
	CHECK( !pDir->setFromString( NULL, "nonExistent", "1" ) );

	// Read only watchers cannot be set.
	int roValue = 0;
	WatcherPtr pRoValue = new DataWatcher< int >( roValue );
	pDir->addChild( "roValue", pRoValue );

	CHECK( !pDir->setFromString( NULL, "roValue", "99" ) );
	CHECK_EQUAL( 0, roValue );
}


TEST( DirectoryWatcher_visitChildren )
{
	DirectoryWatcherPtr pDir = new DirectoryWatcher();

	int first = 1;
	int second = 2;
	int third = 3;

	pDir->addChild( "first", new DataWatcher< int >( first ) );
	pDir->addChild( "second", new DataWatcher< int >( second ) );
	pDir->addChild( "third", new DataWatcher< int >( third ) );

	TestVisitor visitor;
	Watcher * pDirBase = pDir.get();
	CHECK( pDirBase->visitChildren( NULL, "", visitor ) );

	// Children are stored in sorted label order.
	CHECK_EQUAL( 3, visitor.count_ );
	CHECK_EQUAL( 3u, visitor.labels_.size() );
	CHECK_EQUAL( BW::string( "first" ), visitor.labels_[ 0 ] );
	CHECK_EQUAL( BW::string( "second" ), visitor.labels_[ 1 ] );
	CHECK_EQUAL( BW::string( "third" ), visitor.labels_[ 2 ] );

	CHECK_EQUAL( BW::string( "1" ), visitor.values_[ 0 ] );
	CHECK_EQUAL( BW::string( "2" ), visitor.values_[ 1 ] );
	CHECK_EQUAL( BW::string( "3" ), visitor.values_[ 2 ] );

	// Descending into a child re-targets the visit at that child. "first" is
	// a DataWatcher, not a directory, so it has no children to visit and
	// visitChildren() reports false (see Watcher::visitChildren: "false if
	// the specified watcher could not be found or is not a directory
	// watcher"). The visitor is left untouched.
	CHECK( !pDirBase->visitChildren( NULL, "first", visitor ) );
	CHECK_EQUAL( 3, visitor.count_ );

	// An unknown child is not found at all.
	CHECK( !pDirBase->visitChildren( NULL, "nope", visitor ) );
	CHECK_EQUAL( 3, visitor.count_ );
}


// -----------------------------------------------------------------------------
// Section: DataWatcher
// -----------------------------------------------------------------------------

TEST( DataWatcher_getAndSetAsString )
{
	int value = 5;
	WatcherPtr pWatcher = new DataWatcher< int >( value, Watcher::WT_READ_WRITE );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( pWatcher->getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "5" ), result );
	CHECK_EQUAL( Watcher::WT_READ_WRITE, mode );

	// Non empty paths fail.
	CHECK( !pWatcher->getAsString( NULL, "sub", result, desc, mode ) );

	CHECK( pWatcher->setFromString( NULL, "", "10" ) );
	CHECK_EQUAL( 10, value );

	// Invalid values are not set.
	CHECK( !pWatcher->setFromString( NULL, "", "not_a_number" ) );
	CHECK_EQUAL( 10, value );

	// Non empty paths cannot be set.
	CHECK( !pWatcher->setFromString( NULL, "sub", "1" ) );
}


TEST( DataWatcher_readOnlyMode )
{
	int value = 5;
	WatcherPtr pWatcher = new DataWatcher< int >( value );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( pWatcher->getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( Watcher::WT_READ_ONLY, mode );

	CHECK( !pWatcher->setFromString( NULL, "", "6" ) );
	CHECK_EQUAL( 5, value );
}


TEST( DataWatcher_invalidModeRevertsToReadOnly )
{
	int value = 5;
	// A directory mode on a data watcher is coerced to read only.
	WatcherPtr pWatcher = new DataWatcher< int >( value, Watcher::WT_DIRECTORY );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( pWatcher->getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( Watcher::WT_READ_ONLY, mode );
}


/**
 *	An element type for base offset tests.
 */
struct BaseOffsetStruct
{
	int padding;
	int value;
};


TEST( DataWatcher_getAsStringWithBaseOffset )
{
	BaseOffsetStruct s;
	s.padding = 0;
	s.value = 99;

	// Watcher created from a member pointer; the object address is supplied
	// as the base.
	WatcherPtr pWatcher = makeWatcher( &BaseOffsetStruct::value );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( pWatcher->getAsString( &s, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "99" ), result );
}


TEST( DataWatcher_stringValues )
{
	BW::string value( "hello" );
	WatcherPtr pWatcher =
		new DataWatcher< BW::string >( value, Watcher::WT_READ_WRITE );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( pWatcher->getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "hello" ), result );

	CHECK( pWatcher->setFromString( NULL, "", "goodbye" ) );
	CHECK_EQUAL( BW::string( "goodbye" ), value );
}


TEST( DataWatcher_boolValues )
{
	bool value = false;
	WatcherPtr pWatcher =
		new DataWatcher< bool >( value, Watcher::WT_READ_WRITE );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( pWatcher->getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "false" ), result );

	CHECK( pWatcher->setFromString( NULL, "", "true" ) );
	CHECK_EQUAL( true, value );

	CHECK( pWatcher->setFromString( NULL, "", "false" ) );
	CHECK_EQUAL( false, value );

	// Invalid bool strings are rejected.
	CHECK( !pWatcher->setFromString( NULL, "", "not_a_bool" ) );
}


// -----------------------------------------------------------------------------
// Section: Stream based value conversion helpers
// -----------------------------------------------------------------------------

TEST( WatcherValueToStream_intRoundTrip )
{
	MemoryOStream os;

	int outValue = -12345;
	Watcher::Mode mode = Watcher::WT_READ_WRITE;

	watcherValueToStream( os, outValue, mode );

	MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );

	int inValue = 0;
	CHECK( watcherStreamToValue( is, inValue ) );
	CHECK_EQUAL( -12345, inValue );
}


TEST( WatcherValueToStream_uintRoundTrip )
{
	MemoryOStream os;

	unsigned int outValue = 4000000000u;
	Watcher::Mode mode = Watcher::WT_READ_ONLY;

	watcherValueToStream( os, outValue, mode );

	MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );

	unsigned int inValue = 0;
	CHECK( watcherStreamToValue( is, inValue ) );
	CHECK_EQUAL( 4000000000u, inValue );
}


TEST( WatcherValueToStream_int64RoundTrip )
{
	MemoryOStream os;

	int64 outValue = -1122334455667788LL;
	Watcher::Mode mode = Watcher::WT_READ_WRITE;

	watcherValueToStream( os, outValue, mode );

	MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );

	int64 inValue = 0;
	CHECK( watcherStreamToValue( is, inValue ) );
	CHECK_EQUAL( outValue, inValue );
}


TEST( WatcherValueToStream_floatRoundTrip )
{
	MemoryOStream os;

	float outValue = 3.5f;
	Watcher::Mode mode = Watcher::WT_READ_ONLY;

	watcherValueToStream( os, outValue, mode );

	MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );

	float inValue = 0.f;
	CHECK( watcherStreamToValue( is, inValue ) );
	CHECK_CLOSE( 3.5f, inValue, 0.000001f );

	// A double on the stream can be downcast to a float.
	MemoryOStream os2;
	double outDouble = 1234.25;
	watcherValueToStream( os2, outDouble, mode );

	MemoryIStream is2( os2.retrieve( os2.size() ), (int)os2.size() );

	float inFloat = 0.f;
	CHECK( watcherStreamToValue( is2, inFloat ) );
	CHECK_CLOSE( 1234.25f, inFloat, 0.000001f );
}


TEST( WatcherValueToStream_doubleRoundTrip )
{
	MemoryOStream os;

	double outValue = 1234.5678;
	Watcher::Mode mode = Watcher::WT_READ_ONLY;

	watcherValueToStream( os, outValue, mode );

	MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );

	double inValue = 0.0;
	CHECK( watcherStreamToValue( is, inValue ) );
	CHECK_CLOSE( 1234.5678, inValue, 0.000001 );

	// A float on the stream can be upcast to a double.
	MemoryOStream os2;
	float outFloat = 8.75f;
	watcherValueToStream( os2, outFloat, mode );

	MemoryIStream is2( os2.retrieve( os2.size() ), (int)os2.size() );

	double inDouble = 0.0;
	CHECK( watcherStreamToValue( is2, inDouble ) );
	CHECK_CLOSE( 8.75, inDouble, 0.000001 );
}


TEST( WatcherValueToStream_boolRoundTrip )
{
	MemoryOStream os;

	bool outValue = true;
	Watcher::Mode mode = Watcher::WT_READ_WRITE;

	watcherValueToStream( os, outValue, mode );

	MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );

	bool inValue = false;
	CHECK( watcherStreamToValue( is, inValue ) );
	CHECK_EQUAL( true, inValue );
}


TEST( WatcherValueToStream_stringRoundTrip )
{
	MemoryOStream os;

	BW::string outValue( "a watcher string" );
	Watcher::Mode mode = Watcher::WT_READ_ONLY;

	watcherValueToStream( os, outValue, mode );

	MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );

	BW::string inValue;
	CHECK( watcherStreamToValue( is, inValue ) );
	CHECK_EQUAL( BW::string( "a watcher string" ), inValue );
}


TEST( WatcherValueToStream_stringStreamToOtherTypes )
{
	// A string typed stream can be converted into other value types.
	MemoryOStream os;

	BW::string outValue( "77" );
	Watcher::Mode mode = Watcher::WT_READ_WRITE;

	watcherValueToStream( os, outValue, mode );

	MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );

	int inValue = 0;
	CHECK( watcherStreamToValue( is, inValue ) );
	CHECK_EQUAL( 77, inValue );
}


TEST( WatcherValueToStream_mismatchedSizeFails )
{
	// Write a float typed stream and read it as a bool; the string fallback
	// path consumes the value and fails.
	MemoryOStream os;

	float outValue = 1.5f;
	Watcher::Mode mode = Watcher::WT_READ_ONLY;

	watcherValueToStream( os, outValue, mode );

	MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );

	bool inValue = false;
	CHECK( !watcherStreamToValue( is, inValue ) );
}


TEST( WatcherValueToStream_emptyStreamFails )
{
	MemoryOStream os;

	MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );

	int inValue = 0;
	CHECK( !watcherStreamToValue( is, inValue ) );
}


TEST( WatcherStringToValue_conversions )
{
	int intValue = 0;
	CHECK( watcherStringToValue( "42", intValue ) );
	CHECK_EQUAL( 42, intValue );

	CHECK( !watcherStringToValue( "not_a_number", intValue ) );

	bool boolValue = false;
	CHECK( watcherStringToValue( "true", boolValue ) );
	CHECK_EQUAL( true, boolValue );

	CHECK( watcherStringToValue( "FALSE", boolValue ) );
	CHECK_EQUAL( false, boolValue );

	// Bool values also accept numeric strings via stringstream.
	CHECK( watcherStringToValue( "1", boolValue ) );
	CHECK_EQUAL( true, boolValue );

	BW::string stringValue;
	CHECK( watcherStringToValue( "a string", stringValue ) );
	CHECK_EQUAL( BW::string( "a string" ), stringValue );

	// The const char * overload always fails.
	const char * charPtrValue = NULL;
	CHECK( !watcherStringToValue( "anything", charPtrValue ) );
}


TEST( WatcherValueToString_conversions )
{
	CHECK_EQUAL( BW::string( "123" ), watcherValueToString( 123 ) );
	CHECK_EQUAL( BW::string( "1.5" ), watcherValueToString( 1.5 ) );
	CHECK_EQUAL( BW::string( "true" ), watcherValueToString( true ) );
	CHECK_EQUAL( BW::string( "false" ), watcherValueToString( false ) );
	CHECK_EQUAL( BW::string( "str" ), watcherValueToString( BW::string( "str" ) ) );
}


// -----------------------------------------------------------------------------
// Section: getAsStream / setFromStream via WatcherPathRequestV2
// -----------------------------------------------------------------------------

TEST( DataWatcher_getAsStream )
{
	int value = 55;
	WatcherPtr pWatcher = new DataWatcher< int >( value );

	WatcherPathRequestV2 pathRequest( "value" );

	CHECK( pWatcher->getAsStream( NULL, "", pathRequest ) );

	// The result data contains the streamed watcher value.
	CHECK( pathRequest.getDataSize() > 0 );
	CHECK( pathRequest.getData() != NULL );

	// Non empty paths fail.
	WatcherPathRequestV2 failedPathRequest( "value/sub" );
	CHECK( !pWatcher->getAsStream( NULL, "sub", failedPathRequest ) );
}


TEST( DataWatcher_getAsStreamDocPath )
{
	int value = 55;
	WatcherPtr pWatcher = new DataWatcher< int >( value );

	// The __doc__ path returns the comment via the stream API.
	WatcherPathRequestV2 pathRequest( "__doc__" );

	CHECK( pWatcher->getAsStream( NULL, "__doc__", pathRequest ) );
}


TEST( DataWatcher_setFromStream )
{
	int value = 0;
	WatcherPtr pWatcher =
		new DataWatcher< int >( value, Watcher::WT_READ_WRITE );

	// Build the set value stream in watcher protocol format.
	MemoryOStream valueStream;
	watcherValueToStream( valueStream, 21, Watcher::WT_READ_WRITE );

	WatcherPathRequestV2 pathRequest( "value" );
	CHECK( pathRequest.setPacketData( valueStream ) );

	CHECK( pWatcher->setFromStream( NULL, "", pathRequest ) );
	CHECK_EQUAL( 21, value );
}


TEST( DataWatcher_setFromStreamReadOnly )
{
	int value = 0;
	WatcherPtr pWatcher = new DataWatcher< int >( value );

	// Build the set value stream in watcher protocol format.
	MemoryOStream valueStream;
	watcherValueToStream( valueStream, 21, Watcher::WT_READ_WRITE );

	WatcherPathRequestV2 pathRequest( "value" );
	CHECK( pathRequest.setPacketData( valueStream ) );

	// Read only watchers reject the set.
	CHECK( !pWatcher->setFromStream( NULL, "", pathRequest ) );
	CHECK_EQUAL( 0, value );
}


TEST( DirectoryWatcher_getAsStreamAndDoc )
{
	DirectoryWatcherPtr pDir = new DirectoryWatcher();

	int value = 9;
	pDir->addChild( "value", new DataWatcher< int >( value ) );

	// Directory get as stream.
	{
		WatcherPathRequestV2 pathRequest( "" );
		CHECK( pDir->getAsStream( NULL, "", pathRequest ) );
		CHECK( pathRequest.getDataSize() > 0 );
	}

	// Unknown children fail.
	{
		WatcherPathRequestV2 pathRequest( "nonExistent" );
		CHECK( !pDir->getAsStream( NULL, "nonExistent", pathRequest ) );
	}
}


// -----------------------------------------------------------------------------
// Section: MemberWatcher and makeWatcher
// -----------------------------------------------------------------------------

/**
 *	A test object with member accessors.
 */
class TestObject
{
public:
	TestObject() : value_( 10 ), name_( "test" )
	{
	}

	int value() const { return value_; }
	void setValue( int v ) { value_ = v; }

	const BW::string & name() const { return name_; }
	void setName( const BW::string & n ) { name_ = n; }

private:
	int value_;
	BW::string name_;
};


TEST( MemberWatcher_readWriteMember )
{
	TestObject obj;

	WatcherPtr pWatcher = makeNonRefWatcher( obj, &TestObject::value,
		&TestObject::setValue );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( pWatcher->getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "10" ), result );
	CHECK_EQUAL( Watcher::WT_READ_WRITE, mode );

	CHECK( pWatcher->setFromString( NULL, "", "20" ) );
	CHECK_EQUAL( 20, obj.value() );
}


TEST( MemberWatcher_readOnlyMember )
{
	TestObject obj;

	WatcherPtr pWatcher = makeWatcher( obj, &TestObject::value );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( pWatcher->getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "10" ), result );
	CHECK_EQUAL( Watcher::WT_READ_ONLY, mode );

	// No set method means the value cannot be set.
	CHECK( !pWatcher->setFromString( NULL, "", "20" ) );
	CHECK_EQUAL( 10, obj.value() );
}


TEST( MemberWatcher_stringMember )
{
	TestObject obj;

	WatcherPtr pWatcher = makeWatcher( obj, &TestObject::name,
		&TestObject::setName );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( pWatcher->getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "test" ), result );

	CHECK( pWatcher->setFromString( NULL, "", "renamed" ) );
	CHECK_EQUAL( BW::string( "renamed" ), obj.name() );
}


TEST( MemberWatcher_memberPointerWithoutObject )
{
	// Watcher created from member pointers only, object supplied via base.
	TestObject obj;

	WatcherPtr pWatcher = makeWatcher(
		(int (TestObject::*)() const)(&TestObject::value),
		(void (TestObject::*)(int))(&TestObject::setValue) );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	// The base pointer is the object address.
	CHECK( pWatcher->getAsString( &obj, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "10" ), result );

	CHECK( pWatcher->setFromString( &obj, "", "30" ) );
	CHECK_EQUAL( 30, obj.value() );
}


TEST( DataWatcher_dataMemberPointerWithBase )
{
	// Watcher created from a data member pointer; the object address is
	// supplied as the base.
	BaseOffsetStruct s;
	s.padding = 0;
	s.value = 50;

	WatcherPtr pWatcher = makeWatcher( &BaseOffsetStruct::value,
		Watcher::WT_READ_WRITE );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( pWatcher->getAsString( &s, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "50" ), result );

	CHECK( pWatcher->setFromString( &s, "", "60" ) );
	CHECK_EQUAL( 60, s.value );
}


// -----------------------------------------------------------------------------
// Section: FunctionWatcher
// -----------------------------------------------------------------------------

namespace // anonymous
{

int g_functionWatcherValue = 5;

int getFunctionWatcherValue()
{
	return g_functionWatcherValue;
}

void setFunctionWatcherValue( int value )
{
	g_functionWatcherValue = value;
}

}


TEST( FunctionWatcher_getAndSet )
{
	g_functionWatcherValue = 5;

	WatcherPtr pWatcher = new FunctionWatcher< int >(
		getFunctionWatcherValue, setFunctionWatcherValue );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( pWatcher->getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "5" ), result );
	CHECK_EQUAL( Watcher::WT_READ_WRITE, mode );

	CHECK( pWatcher->setFromString( NULL, "", "15" ) );
	CHECK_EQUAL( 15, g_functionWatcherValue );

	// Invalid values are rejected.
	CHECK( !pWatcher->setFromString( NULL, "", "not_a_number" ) );
}


TEST( FunctionWatcher_readOnly )
{
	g_functionWatcherValue = 6;

	WatcherPtr pWatcher = new FunctionWatcher< int >(
		getFunctionWatcherValue );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( pWatcher->getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "6" ), result );
	CHECK_EQUAL( Watcher::WT_READ_ONLY, mode );

	CHECK( !pWatcher->setFromString( NULL, "", "1" ) );
	CHECK_EQUAL( 6, g_functionWatcherValue );

	// Non empty paths fail.
	CHECK( !pWatcher->getAsString( NULL, "sub", result, desc, mode ) );
}


// -----------------------------------------------------------------------------
// Section: SequenceWatcher
// -----------------------------------------------------------------------------

/**
 *	An element type for sequence and map watchers.
 */
struct SequenceElement
{
	int value;
};


TEST( SequenceWatcher_indexesAndLabels )
{
	typedef BW::vector< SequenceElement > ElementVector;

	static const char * labels[] = { "first", "second", NULL };

	ElementVector values;
	SequenceElement e1 = { 10 };
	SequenceElement e2 = { 20 };
	values.push_back( e1 );
	values.push_back( e2 );

	WatcherPtr pWatcher = new SequenceWatcher< ElementVector >( values, labels );

	// The child of a sequence watcher covers a single element and is named
	// by convention "*"; it has to be a directory, so that the tail of the
	// path (e.g. the "value" in "first/value") can be resolved inside it.
	// This mirrors the canonical engine usage, e.g. InterfaceTable::pWatcherByID
	// or Cell's "reals" watcher.
	DirectoryWatcher * pElementWatcher = new DirectoryWatcher();
	pElementWatcher->addChild( "value", makeWatcher( &SequenceElement::value,
		Watcher::WT_READ_WRITE ) );

	pWatcher->addChild( "*", pElementWatcher );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	// Directory style access.
	CHECK( pWatcher->getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "<DIR>" ), result );
	CHECK_EQUAL( Watcher::WT_DIRECTORY, mode );

	// Access by label.
	CHECK( pWatcher->getAsString( NULL, "first/value", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "10" ), result );

	// Access by index.
	CHECK( pWatcher->getAsString( NULL, "1/value", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "20" ), result );

	// Setting through the sequence.
	CHECK( pWatcher->setFromString( NULL, "second/value", "25" ) );
	CHECK_EQUAL( 25, values[1].value );

	// A name that matches no label and is not numeric falls through
	// findChild()'s atoi() to index 0 - a long-standing quirk of the label /
	// index lookup, kept as is.
	CHECK( pWatcher->getAsString( NULL, "unknown/value", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "10" ), result );

	// An out-of-range index fails.
	CHECK( !pWatcher->getAsString( NULL, "99/value", result, desc, mode ) );

	// addChild with an empty path fails.
	CHECK( !pWatcher->addChild( "", makeWatcher( &SequenceElement::value ) ) );
}


TEST( SequenceWatcher_visitChildren )
{
	typedef BW::vector< SequenceElement > ElementVector;

	static const char * labels[] = { "first", "second", NULL };

	ElementVector values;
	SequenceElement e1 = { 10 };
	SequenceElement e2 = { 20 };
	values.push_back( e1 );
	values.push_back( e2 );

	WatcherPtr pWatcher = new SequenceWatcher< ElementVector >( values, labels );

	pWatcher->addChild( "value", makeWatcher( &SequenceElement::value ), NULL );

	TestVisitor visitor;
	CHECK( pWatcher->visitChildren( NULL, "", visitor ) );

	CHECK_EQUAL( 2, visitor.count_ );
	CHECK_EQUAL( BW::string( "first" ), visitor.labels_[ 0 ] );
	CHECK_EQUAL( BW::string( "second" ), visitor.labels_[ 1 ] );
	CHECK_EQUAL( BW::string( "10" ), visitor.values_[ 0 ] );
	CHECK_EQUAL( BW::string( "20" ), visitor.values_[ 1 ] );
}


// -----------------------------------------------------------------------------
// Section: MapWatcher
// -----------------------------------------------------------------------------

TEST( MapWatcher_mapAccess )
{
	typedef BW::map< BW::string, SequenceElement > TestMap;

	TestMap values;
	values[ "one" ].value = 1;
	values[ "two" ].value = 2;

	WatcherPtr pWatcher = new MapWatcher< TestMap >( values );

	// As with SequenceWatcher: the map's child covers a single element and
	// has to be a directory for the "key/value" path tail to resolve.
	DirectoryWatcher * pElementWatcher = new DirectoryWatcher();
	pElementWatcher->addChild( "value", makeWatcher( &SequenceElement::value,
		Watcher::WT_READ_WRITE ) );

	pWatcher->addChild( "*", pElementWatcher );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	// Directory style access.
	CHECK( pWatcher->getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "<DIR>" ), result );
	CHECK_EQUAL( Watcher::WT_DIRECTORY, mode );

	// Access by key.
	CHECK( pWatcher->getAsString( NULL, "one/value", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "1" ), result );

	CHECK( pWatcher->getAsString( NULL, "two/value", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "2" ), result );

	// Setting through the map.
	CHECK( pWatcher->setFromString( NULL, "one/value", "11" ) );
	CHECK_EQUAL( 11, values[ "one" ].value );

	// Unknown keys fail.
	CHECK( !pWatcher->getAsString( NULL, "three/value", result, desc, mode ) );

	// addChild with an empty path fails.
	CHECK( !pWatcher->addChild( "", makeWatcher( &SequenceElement::value ) ) );
}


// -----------------------------------------------------------------------------
// Section: SafeWatcher
// -----------------------------------------------------------------------------

TEST( SafeWatcher_wrapsWatcherWithMutex )
{
	static int value = 3;

	DataWatcher< int > * pDataWatcher = new DataWatcher< int >( value,
		Watcher::WT_READ_WRITE );

	SimpleMutex mutex;
	WatcherPtr pWatcher = new SafeWatcher( pDataWatcher, mutex );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( pWatcher->getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "3" ), result );

	CHECK( pWatcher->setFromString( NULL, "", "4" ) );
	CHECK_EQUAL( 4, value );
}


// -----------------------------------------------------------------------------
// Section: Root watcher
// -----------------------------------------------------------------------------

TEST( Watcher_rootWatcher )
{
	// The root watcher may already exist (created by static initialisers).
	Watcher & root = Watcher::rootWatcherInternal();

	CHECK( Watcher::hasRootWatcherInternal() );

	static int value = 8;
	WatcherPtr pValue = new DataWatcher< int >( value );

	CHECK( root.addChild( "testValue", pValue ) );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( root.getAsString( NULL, "testValue", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "8" ), result );

	CHECK( root.removeChild( "testValue" ) );
}

BW_END_NAMESPACE

// test_watcher.cpp
