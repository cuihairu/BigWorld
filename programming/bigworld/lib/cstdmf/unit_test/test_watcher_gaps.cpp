#include "pch.hpp"

#include "cstdmf/watcher.hpp"
#include "cstdmf/memory_stream.hpp"
#include "cstdmf/bw_map.hpp"
#include "cstdmf/bw_vector.hpp"
#include "cstdmf/stdmf.hpp"

BW_BEGIN_NAMESPACE

namespace
{

class GapObject
{
public:
	GapObject() : value_( 0 ) {}

	int value() const { return value_; }
	void setValue( int v ) { value_ = v; }

	const BW::string & name() const { return name_; }
	void setName( const BW::string & n ) { name_ = n; }

private:
	int value_;
	BW::string name_;
};


struct GapElement
{
	int value;
};


struct GapIntValue
{
	int value;
};


struct GapUintValue
{
	unsigned int value;
};


BW::string gapIndexToString( const void * /*base*/, int index )
{
	char buf[ 32 ];
	snprintf( buf, sizeof( buf ), "idx%d", index );
	return BW::string( buf );
}


int gapStringToIndex( const void * /*base*/, const BW::string & name )
{
	if (name.size() < 4 || name.compare( 0, 3, "idx" ) != 0)
	{
		return -1;
	}
	char * end = NULL;
	long v = strtol( name.c_str() + 3, &end, 10 );
	if (end == NULL || *end != '\0' || v < 0)
	{
		return -1;
	}
	return (int)v;
}


class IntKeyConverter : public IMapKeyStringConverter< int >
{
public:
	virtual bool stringToValue( const BW::string & s, int & k )
	{
		if (s.size() < 2 || s[ 0 ] != 'k')
		{
			return false;
		}
		char * end = NULL;
		long v = strtol( s.c_str() + 1, &end, 10 );
		if (end == NULL || *end != '\0')
		{
			return false;
		}
		k = (int)v;
		return true;
	}

	virtual const BW::string valueToString( const int & k )
	{
		char buf[ 32 ];
		snprintf( buf, sizeof( buf ), "k%d", k );
		return BW::string( buf );
	}
};


int g_gapFuncInt = 5;

int gapGetInt()
{
	return g_gapFuncInt;
}


void gapSetInt( int v )
{
	g_gapFuncInt = v;
}


BW::string g_gapFuncStr = "gap";

BW::string gapGetStr()
{
	return g_gapFuncStr;
}


void gapSetStr( BW::string v )
{
	g_gapFuncStr = v;
}


class GapVisitor : public WatcherVisitor
{
public:
	GapVisitor() : count_( 0 ) {}

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


// Minimal concrete watcher to hit Watcher base-class defaults.
class BareWatcher : public Watcher
{
public:
	virtual bool getAsString( const void * /*base*/, const char * /*path*/,
		BW::string & /*result*/, BW::string & /*desc*/,
		Mode & /*mode*/ ) const
	{
		return false;
	}

	virtual bool setFromString( void * /*base*/, const char * /*path*/,
		const char * /*valueStr*/ )
	{
		return false;
	}

	virtual bool setFromStream( void * /*base*/, const char * /*path*/,
		WatcherPathRequestV2 & /*pathRequest*/ )
	{
		return false;
	}

	virtual bool getAsStream( const void * /*base*/, const char * /*path*/,
		WatcherPathRequestV2 & /*pathRequest*/ ) const
	{
		return false;
	}
};


class GapSimpleCallable : public SimpleCallableWatcher
{
public:
	GapSimpleCallable() : SimpleCallableWatcher( LOCAL_ONLY, "gap" ) {}

private:
	virtual bool onCall( BW::string & output, BW::string & value,
		int /*parameterCount*/, BinaryIStream & /*parameters*/ )
	{
		output = "";
		value = "gap-result";
		return true;
	}
};


WatcherPtr makeGapElementDir()
{
	DirectoryWatcher * pDir = new DirectoryWatcher();
	pDir->addChild( "value",
		makeWatcher( &GapElement::value, Watcher::WT_READ_WRITE ) );
	return pDir;
}

} // namespace


// -----------------------------------------------------------------------------
// Section: watcherStringToValue / watcherValueToString leftovers
// -----------------------------------------------------------------------------

TEST( WatcherGaps_stringToValue_voidPointerFails )
{
	// The pointer overload exists so pointer-typed watcher values fail
	// conversion instead of going through the stringstream path.
	void * ptr = NULL;
	CHECK( !watcherStringToValue( "anything", ptr ) );

	unsigned int u = 0;
	CHECK( watcherStringToValue( "77", u ) );
	CHECK_EQUAL( 77u, u );
	CHECK( !watcherStringToValue( "not_a_number", u ) );

	double d = 0.0;
	CHECK( watcherStringToValue( "2.5", d ) );
	CHECK( d > 2.49 && d < 2.51 );
	CHECK( !watcherStringToValue( "not_a_number", d ) );
}


// -----------------------------------------------------------------------------
// Section: watcherStreamToValue / watcherValueToStream error arms
// -----------------------------------------------------------------------------

TEST( WatcherGaps_stream_emptyStreamFailsPerOverload )
{
	MemoryOStream os;

	// One hit per watcherStreamToValue overload (int is covered by the
	// existing empty-stream test).
	{
		MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );
		bool b = false;
		CHECK( !watcherStreamToValue( is, b ) );
	}
	{
		MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );
		unsigned int u = 0;
		CHECK( !watcherStreamToValue( is, u ) );
	}
	{
		MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );
		float f = 0.f;
		CHECK( !watcherStreamToValue( is, f ) );
	}
	{
		MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );
		double d = 0.0;
		CHECK( !watcherStreamToValue( is, d ) );
	}
	{
		MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );
		BW::string s;
		CHECK( !watcherStreamToValue( is, s ) );
	}
	{
		MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );
		long l = 0;
		CHECK( !watcherStreamToValue( is, l ) );
	}
	{
		MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );
		uint64 u64 = 0;
		CHECK( !watcherStreamToValue( is, u64 ) );
	}
}


TEST( WatcherGaps_stream_truncatedModeByteFails )
{
	// Type byte present but the mode byte missing: watcherStreamSkipMode
	// fails and the read is rejected.
	MemoryOStream os;
	os << (uchar)WATCHER_TYPE_INT;

	MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );
	int v = 0;
	CHECK( !watcherStreamToValue( is, v ) );
}


TEST( WatcherGaps_stream_int64UpcastDowncast )
{
	Watcher::Mode mode = Watcher::WT_READ_WRITE;

	// int32 on the stream upcasts into an int64.
	{
		MemoryOStream os;
		int out = -12345;
		watcherValueToStream( os, out, mode );

		MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );
		int64 in = 0;
		CHECK( watcherStreamToValue( is, in ) );
		CHECK_EQUAL( (int64)-12345, in );
	}

	// int64 on the stream downcasts into an int32.
	{
		MemoryOStream os;
		int64 out = 987654321LL;
		watcherValueToStream( os, out, mode );

		MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );
		int in = 0;
		CHECK( watcherStreamToValue( is, in ) );
		CHECK_EQUAL( (int)987654321LL, in );
	}
}


TEST( WatcherGaps_stream_uint32ToUint64Fails )
{
	// uint32 on the stream read as uint64: the upcast block fills value
	// from tmpVal but then falls through to a second 8-byte extraction
	// past the end of the stream, so the read fails. This documents the
	// current behaviour (missing else after the upcast block).
	MemoryOStream os;
	unsigned int out = 3000000000u;
	Watcher::Mode mode = Watcher::WT_READ_ONLY;
	watcherValueToStream( os, out, mode );

	MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );
	uint64 in = 0;
	CHECK( !watcherStreamToValue( is, in ) );
}


TEST( WatcherGaps_stream_uint64IntTypedRoundTrip )
{
	// An 8-byte INT payload reads cleanly into a uint64 through the
	// generic template path.
	MemoryOStream os;
	os << (uchar)WATCHER_TYPE_INT;
	os << (uchar)Watcher::WT_READ_WRITE;
	os.writeStringLength( sizeof( uint64 ) );
	uint64 out = 0x1122334455667788ULL;
	os << out;

	MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );
	uint64 in = 0;
	CHECK( watcherStreamToValue( is, in ) );
	CHECK_EQUAL( out, in );
}


TEST( WatcherGaps_stream_intBadSizeFails )
{
	// INT payload that is neither 4 nor 8 bytes is consumed and rejected.
	MemoryOStream os;
	os << (uchar)WATCHER_TYPE_INT;
	os << (uchar)Watcher::WT_READ_ONLY;
	os.writeStringLength( 3 );
	os << (uchar)1 << (uchar)2 << (uchar)3;

	MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );
	int v = 0;
	CHECK( !watcherStreamToValue( is, v ) );
}


TEST( WatcherGaps_stream_boolWrongSizeFails )
{
	// BOOL payload whose size is not sizeof(bool) is consumed and rejected.
	MemoryOStream os;
	os << (uchar)WATCHER_TYPE_BOOL;
	os << (uchar)Watcher::WT_READ_ONLY;
	os.writeStringLength( sizeof( int ) );
	int filler = 0;
	os << filler;

	MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );
	bool b = false;
	CHECK( !watcherStreamToValue( is, b ) );
}


TEST( WatcherGaps_stream_stringFallbackFails )
{
	// STRING payload that does not parse as the target type fails.
	MemoryOStream os;
	BW::string out( "abc" );
	Watcher::Mode mode = Watcher::WT_READ_WRITE;
	watcherValueToStream( os, out, mode );

	MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );
	int v = 0;
	CHECK( !watcherStreamToValue( is, v ) );
}


TEST( WatcherGaps_stream_longAndGenericValueRoundTrip )
{
	// long has no watcherValueToStream overload, so it goes through the
	// generic template (STRING typed); the long watcherStreamToValue
	// overload reads it back via the string fallback.
	Watcher::Mode mode = Watcher::WT_READ_WRITE;
	{
		MemoryOStream os;
		long out = -987654L;
		watcherValueToStream( os, out, mode );

		MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );
		long in = 0;
		CHECK( watcherStreamToValue( is, in ) );
		CHECK_EQUAL( out, in );
	}

	// uint64 likewise streams through the generic template.
	{
		MemoryOStream os;
		uint64 out = 123456789ULL;
		watcherValueToStream( os, out, mode );

		MemoryIStream is( os.retrieve( os.size() ), (int)os.size() );
		uint64 in = 0;
		CHECK( watcherStreamToValue( is, in ) );
		CHECK_EQUAL( out, in );
	}

	// The WatcherDataType overload writes a TYPE typed entry.
	{
		MemoryOStream os;
		watcherValueToStream( os, WATCHER_TYPE_INT, mode );
		CHECK( os.size() > 0 );
	}
}


// -----------------------------------------------------------------------------
// Section: Watcher base-class defaults
// -----------------------------------------------------------------------------

TEST( WatcherGaps_baseDefaults )
{
	BareWatcher bare;

	// addChild / removeChild / getChild defaults.
	CHECK( !bare.addChild( "child", NULL ) );
	CHECK( !bare.removeChild( "child" ) );
	CHECK( bare.getChild( "child" ) == NULL );

	// visitChildren default returns false.
	WatcherPathRequest req;
	CHECK( !bare.visitChildren( NULL, "", req ) );

	// Comment accessors.
	bare.setComment( "gap comment" );
	CHECK_EQUAL( BW::string( "gap comment" ), bare.getComment() );

	// The 4-arg getAsString delegates to the 5-arg overload.
	int value = 7;
	WatcherPtr pData = new DataWatcher< int >( value );
	BW::string result;
	Watcher::Mode getMode = Watcher::WT_INVALID;
	CHECK( pData->getAsString( NULL, "", result, getMode ) );
	CHECK_EQUAL( BW::string( "7" ), result );
	CHECK_EQUAL( Watcher::WT_READ_ONLY, getMode );
}


TEST( WatcherGaps_visitorThreeArgDelegates )
{
	struct LocalVisitor : public WatcherVisitor
	{
		using WatcherVisitor::visit;
		int count_;
		LocalVisitor() : count_( 0 ) {}
		virtual bool visit( Watcher::Mode /*mode*/,
			const BW::string & /*label*/,
			const BW::string & /*desc*/,
			const BW::string & /*valueStr*/ )
		{
			++count_;
			return true;
		}
	};

	LocalVisitor v;
	CHECK( v.visit( Watcher::WT_READ_ONLY, "label", "value" ) );
	CHECK_EQUAL( 1, v.count_ );
}


// -----------------------------------------------------------------------------
// Section: SequenceWatcher gaps
// -----------------------------------------------------------------------------

TEST( WatcherGaps_Seq_labelsShorterThanVector )
{
	typedef BW::vector< GapElement > GapVec;
	static const char * labels[] = { "first", NULL };

	GapVec values;
	GapElement e;
	e.value = 10; values.push_back( e );
	e.value = 20; values.push_back( e );
	e.value = 30; values.push_back( e );

	WatcherPtr pWatcher = new SequenceWatcher< GapVec >( values, labels );
	pWatcher->addChild( "*", makeGapElementDir() );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	// Label hit for the first element.
	CHECK( pWatcher->getAsString( NULL, "first/value", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "10" ), result );

	// Beyond the label table: numeric index fallback.
	CHECK( pWatcher->getAsString( NULL, "2/value", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "30" ), result );

	// Out of range fails.
	CHECK( !pWatcher->getAsString( NULL, "9/value", result, desc, mode ) );
}


TEST( WatcherGaps_Seq_labelSubPath )
{
	typedef BW::vector< GapElement > GapVec;

	GapVec values;
	GapElement e;
	e.value = 10; values.push_back( e );
	e.value = 20; values.push_back( e );

	WatcherPtr pWatcher = new SequenceWatcher< GapVec >( values );
	SequenceWatcher< GapVec > * pSeq =
		static_cast< SequenceWatcher< GapVec > * >( pWatcher.get() );
	pSeq->addChild( "*", makeGapElementDir() );
	pSeq->setLabelSubPath( "value" );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	// Lookup by the label sub-path value instead of the index.
	CHECK( pWatcher->getAsString( NULL, "20/value", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "20" ), result );

	// visitChildren labels come from the sub-path too.
	GapVisitor visitor;
	CHECK( pWatcher->visitChildren( NULL, "", visitor ) );
	CHECK_EQUAL( 2, visitor.count_ );
	CHECK_EQUAL( BW::string( "10" ), visitor.labels_[ 0 ] );
	CHECK_EQUAL( BW::string( "20" ), visitor.labels_[ 1 ] );
}


TEST( WatcherGaps_Seq_customIndexConverter )
{
	typedef BW::vector< GapElement > GapVec;

	GapVec values;
	GapElement e;
	e.value = 10; values.push_back( e );
	e.value = 20; values.push_back( e );

	WatcherPtr pWatcher = new SequenceWatcher< GapVec >( values );
	SequenceWatcher< GapVec > * pSeq =
		static_cast< SequenceWatcher< GapVec > * >( pWatcher.get() );
	pSeq->addChild( "*", makeGapElementDir() );
	pSeq->setStringIndexConverter( gapStringToIndex, gapIndexToString );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	// Lookup through the custom converter.
	CHECK( pWatcher->getAsString( NULL, "idx1/value", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "20" ), result );

	// Converter miss (-1) throws NoSuchChild and fails.
	CHECK( !pWatcher->getAsString( NULL, "bogus/value", result, desc, mode ) );

	// visitChildren labels come from the index converter.
	GapVisitor visitor;
	CHECK( pWatcher->visitChildren( NULL, "", visitor ) );
	CHECK_EQUAL( 2, visitor.count_ );
	CHECK_EQUAL( BW::string( "idx0" ), visitor.labels_[ 0 ] );
	CHECK_EQUAL( BW::string( "idx1" ), visitor.labels_[ 1 ] );

	// Nested visit delegates into the child; miss fails.
	GapVisitor nested;
	CHECK( pWatcher->visitChildren( NULL, "idx0", nested ) );
	CHECK( nested.count_ > 0 );
	CHECK( !pWatcher->visitChildren( NULL, "bogus", nested ) );
}


TEST( WatcherGaps_Seq_streamDocNestedMiss )
{
	typedef BW::vector< GapElement > GapVec;

	GapVec values;
	GapElement e;
	e.value = 10; values.push_back( e );
	e.value = 20; values.push_back( e );

	WatcherPtr pWatcher = new SequenceWatcher< GapVec >( values );
	pWatcher->addChild( "*", makeGapElementDir() );

	// Empty path streams <DIR>.
	{
		WatcherPathRequestV2 req( "" );
		CHECK( pWatcher->getAsStream( NULL, "", req ) );
		CHECK( req.getDataSize() > 0 );
	}

	// __doc__ path streams the comment.
	{
		WatcherPathRequestV2 req( "__doc__" );
		CHECK( pWatcher->getAsStream( NULL, "__doc__", req ) );
	}

	// Nested get/set stream through the child.
	{
		WatcherPathRequestV2 req( "1/value" );
		CHECK( pWatcher->getAsStream( NULL, "1/value", req ) );
		CHECK( req.getDataSize() > 0 );
	}
	{
		MemoryOStream os;
		watcherValueToStream( os, 25, Watcher::WT_READ_WRITE );
		WatcherPathRequestV2 req( "1/value" );
		CHECK( req.setPacketData( os ) );
		CHECK( pWatcher->setFromStream( NULL, "1/value", req ) );
		CHECK_EQUAL( 25, values[ 1 ].value );
	}

	// Misses fail on every verb.
	{
		WatcherPathRequestV2 req( "99/value" );
		CHECK( !pWatcher->getAsStream( NULL, "99/value", req ) );
	}
	{
		MemoryOStream os;
		watcherValueToStream( os, 1, Watcher::WT_READ_WRITE );
		WatcherPathRequestV2 req( "99/value" );
		CHECK( req.setPacketData( os ) );
		CHECK( !pWatcher->setFromStream( NULL, "99/value", req ) );
	}
	BW::string result;
	BW::string desc;
	CHECK( !pWatcher->setFromString( NULL, "99/value", "1" ) );
}


TEST( WatcherGaps_Seq_addChildSlashAndSecondFails )
{
	typedef BW::vector< GapElement > GapVec;
	GapVec values;

	WatcherPtr pWatcher = new SequenceWatcher< GapVec >( values );
	CHECK( pWatcher->addChild( "*", makeGapElementDir() ) );

	// A path with a separator delegates into the existing child.
	int extra = 0;
	CHECK( pWatcher->addChild( "*/extra",
		new DataWatcher< int >( extra, Watcher::WT_READ_WRITE ) ) );

	// A second single-component child is rejected (one-child family).
	CHECK( !pWatcher->addChild( "other",
		new DataWatcher< int >( extra ) ) );

	// tail() helper delegates to DirectoryWatcher::tail.
	CHECK( SequenceWatcher< GapVec >::tail( "a/b" ) != NULL );
	CHECK_EQUAL( 0, strcmp( "b",
		SequenceWatcher< GapVec >::tail( "a/b" ) ) );
}


// -----------------------------------------------------------------------------
// Section: MapWatcher gaps
// -----------------------------------------------------------------------------

TEST( WatcherGaps_Map_customKeyConverter )
{
	typedef BW::map< int, GapElement > GapIntMap;

	GapIntMap values;
	values[ 1 ].value = 100;
	values[ 2 ].value = 200;

	IntKeyConverter converter;
	WatcherPtr pWatcher = new MapWatcher< GapIntMap >( values, &converter );
	pWatcher->addChild( "*", makeGapElementDir() );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	// Lookup through the converter ("k1" -> key 1).
	CHECK( pWatcher->getAsString( NULL, "k1/value", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "100" ), result );

	// Converter rejection misses.
	CHECK( !pWatcher->getAsString( NULL, "bogus/value", result, desc, mode ) );

	// Unknown-but-well-formed key misses.
	CHECK( !pWatcher->getAsString( NULL, "k9/value", result, desc, mode ) );

	// visitChildren labels come from the converter.
	GapVisitor visitor;
	CHECK( pWatcher->visitChildren( NULL, "", visitor ) );
	CHECK_EQUAL( 2, visitor.count_ );
	CHECK_EQUAL( BW::string( "k1" ), visitor.labels_[ 0 ] );
	CHECK_EQUAL( BW::string( "k2" ), visitor.labels_[ 1 ] );
}


TEST( WatcherGaps_Map_streamDocNestedMiss )
{
	typedef BW::map< BW::string, GapElement > GapStrMap;

	GapStrMap values;
	values[ "one" ].value = 1;
	values[ "two" ].value = 2;

	WatcherPtr pWatcher = new MapWatcher< GapStrMap >( values );
	pWatcher->addChild( "*", makeGapElementDir() );

	{
		WatcherPathRequestV2 req( "" );
		CHECK( pWatcher->getAsStream( NULL, "", req ) );
		CHECK( req.getDataSize() > 0 );
	}
	{
		WatcherPathRequestV2 req( "__doc__" );
		CHECK( pWatcher->getAsStream( NULL, "__doc__", req ) );
	}
	{
		WatcherPathRequestV2 req( "one/value" );
		CHECK( pWatcher->getAsStream( NULL, "one/value", req ) );
		CHECK( req.getDataSize() > 0 );
	}
	{
		MemoryOStream os;
		watcherValueToStream( os, 11, Watcher::WT_READ_WRITE );
		WatcherPathRequestV2 req( "one/value" );
		CHECK( req.setPacketData( os ) );
		CHECK( pWatcher->setFromStream( NULL, "one/value", req ) );
		CHECK_EQUAL( 11, values[ "one" ].value );
	}
	{
		WatcherPathRequestV2 req( "three/value" );
		CHECK( !pWatcher->getAsStream( NULL, "three/value", req ) );
	}
	{
		MemoryOStream os;
		watcherValueToStream( os, 1, Watcher::WT_READ_WRITE );
		WatcherPathRequestV2 req( "three/value" );
		CHECK( req.setPacketData( os ) );
		CHECK( !pWatcher->setFromStream( NULL, "three/value", req ) );
	}

	// Nested visit delegates into the child; miss fails.
	GapVisitor nested;
	CHECK( pWatcher->visitChildren( NULL, "one", nested ) );
	CHECK( nested.count_ > 0 );
	CHECK( !pWatcher->visitChildren( NULL, "three", nested ) );

	// Empty-path visit enumerates both keys.
	GapVisitor visitor;
	CHECK( pWatcher->visitChildren( NULL, "", visitor ) );
	CHECK_EQUAL( 2, visitor.count_ );
}


TEST( WatcherGaps_Map_addChildSlashAndSecondFails )
{
	typedef BW::map< BW::string, GapElement > GapStrMap;
	GapStrMap values;

	WatcherPtr pWatcher = new MapWatcher< GapStrMap >( values );
	CHECK( pWatcher->addChild( "*", makeGapElementDir() ) );

	int extra = 0;
	CHECK( pWatcher->addChild( "*/extra",
		new DataWatcher< int >( extra, Watcher::WT_READ_WRITE ) ) );

	CHECK( !pWatcher->addChild( "other",
		new DataWatcher< int >( extra ) ) );

	CHECK( MapWatcher< GapStrMap >::tail( "a/b" ) != NULL );
}


// -----------------------------------------------------------------------------
// Section: DataWatcher gaps
// -----------------------------------------------------------------------------

TEST( WatcherGaps_Data_nullAndBadPaths )
{
	int value = 7;
	WatcherPtr pWatcher = new DataWatcher< int >( value );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	// NULL path counts as empty.
	CHECK( pWatcher->getAsString( NULL, NULL, result, desc, mode ) );
	CHECK_EQUAL( BW::string( "7" ), result );

	// Non-empty paths fail on a leaf watcher.
	CHECK( !pWatcher->getAsString( NULL, "sub", result, desc, mode ) );

	// Read-only leaf rejects sets; tested for the failure arm of the
	// access check.
	CHECK( !pWatcher->setFromString( NULL, "", "8" ) );
	CHECK_EQUAL( 7, value );
	CHECK( !pWatcher->setFromString( NULL, "sub", "8" ) );

	// A read/write leaf rejects unparseable input.
	int rwValue = 0;
	WatcherPtr pRW = new DataWatcher< int >( rwValue, Watcher::WT_READ_WRITE );
	CHECK( !pRW->setFromString( NULL, "", "not_a_number" ) );
	CHECK_EQUAL( 0, rwValue );
}


TEST( WatcherGaps_Data_setFromStreamNoPacketFails )
{
	int value = 0;
	WatcherPtr pWatcher =
		new DataWatcher< int >( value, Watcher::WT_READ_WRITE );

	// No packet data: getValueStream() is NULL and the set fails.
	WatcherPathRequestV2 req( "value" );
	CHECK( !pWatcher->setFromStream( NULL, "", req ) );
	CHECK_EQUAL( 0, value );
}


TEST( WatcherGaps_Data_uint64SetFromStream )
{
	// Exercises the generic watcherStreamToValue template through a
	// DataWatcher: the 8-byte INT payload path and the STRING fallback.
	uint64 value = 0;
	WatcherPtr pWatcher =
		new DataWatcher< uint64 >( value, Watcher::WT_READ_WRITE );

	{
		MemoryOStream os;
		os << (uchar)WATCHER_TYPE_INT;
		os << (uchar)Watcher::WT_READ_WRITE;
		os.writeStringLength( sizeof( uint64 ) );
		uint64 out = 0x0102030405060708ULL;
		os << out;

		WatcherPathRequestV2 req( "value" );
		CHECK( req.setPacketData( os ) );
		CHECK( pWatcher->setFromStream( NULL, "", req ) );
		CHECK_EQUAL( out, value );
	}

	{
		MemoryOStream os;
		watcherValueToStream( os, BW::string( "999" ),
			Watcher::WT_READ_WRITE );

		WatcherPathRequestV2 req( "value" );
		CHECK( req.setPacketData( os ) );
		CHECK( pWatcher->setFromStream( NULL, "", req ) );
		CHECK_EQUAL( (uint64)999, value );
	}
}


// -----------------------------------------------------------------------------
// Section: ReadOnlyMemberWatcher / MemberWatcher gaps
// -----------------------------------------------------------------------------

TEST( WatcherGaps_ReadOnly_nullGetter )
{
	// NULL getter: string and stream reads return empty, sets always fail.
	ReadOnlyMemberWatcher< int, GapObject > * pNull =
		new ReadOnlyMemberWatcher< int, GapObject >(
			(int (GapObject::*)() const)NULL );
	WatcherPtr pWatcher = pNull;

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( pWatcher->getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "" ), result );
	CHECK_EQUAL( Watcher::WT_READ_ONLY, mode );
	CHECK( !pWatcher->getAsString( NULL, "sub", result, desc, mode ) );

	{
		WatcherPathRequestV2 req( "" );
		CHECK( pWatcher->getAsStream( NULL, "", req ) );
	}
	{
		WatcherPathRequestV2 req( "__doc__" );
		CHECK( pWatcher->getAsStream( NULL, "__doc__", req ) );
	}
	{
		WatcherPathRequestV2 req( "sub" );
		CHECK( !pWatcher->getAsStream( NULL, "sub", req ) );
	}

	CHECK( !pWatcher->setFromString( NULL, "", "1" ) );
	{
		WatcherPathRequestV2 req( "value" );
		CHECK( !pWatcher->setFromStream( NULL, "", req ) );
	}
}


TEST( WatcherGaps_ReadOnly_withObjectStreams )
{
	GapObject obj;
	obj.setValue( 33 );

	WatcherPtr pWatcher =
		new ReadOnlyMemberWatcher< int, GapObject >( obj, &GapObject::value );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( pWatcher->getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "33" ), result );
	CHECK( !pWatcher->getAsString( NULL, "sub", result, desc, mode ) );

	{
		WatcherPathRequestV2 req( "" );
		CHECK( pWatcher->getAsStream( NULL, "", req ) );
		CHECK( req.getDataSize() > 0 );
	}
	{
		WatcherPathRequestV2 req( "__doc__" );
		CHECK( pWatcher->getAsStream( NULL, "__doc__", req ) );
	}

	CHECK( !pWatcher->setFromString( NULL, "", "1" ) );
	CHECK_EQUAL( 33, obj.value() );
}


TEST( WatcherGaps_Member_nullGetterWithSetter )
{
	// Set-only member watcher via MF_WRITE_ACCESSOR: reads return empty.
	GapObject obj;
	WatcherPtr pWatcher = new MemberWatcher< int, GapObject >(
		MF_WRITE_ACCESSOR( int, GapObject, setValue ) );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( pWatcher->getAsString( &obj, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "" ), result );

	{
		WatcherPathRequestV2 req( "" );
		CHECK( pWatcher->getAsStream( &obj, "", req ) );
	}

	CHECK( pWatcher->setFromString( &obj, "", "42" ) );
	CHECK_EQUAL( 42, obj.value() );
}


TEST( WatcherGaps_Member_setBadStringAndStreams )
{
	GapObject obj;
	WatcherPtr pWatcher = makeNonRefWatcher( obj,
		&GapObject::value, &GapObject::setValue );

	// Unparseable input is rejected without touching the object.
	CHECK( !pWatcher->setFromString( NULL, "", "not_a_number" ) );
	CHECK_EQUAL( 0, obj.value() );

	// Non-empty paths fail on every verb.
	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;
	CHECK( !pWatcher->getAsString( NULL, "sub", result, desc, mode ) );
	CHECK( !pWatcher->setFromString( NULL, "sub", "1" ) );
	{
		WatcherPathRequestV2 req( "sub" );
		CHECK( !pWatcher->getAsStream( NULL, "sub", req ) );
		CHECK( !pWatcher->setFromStream( NULL, "sub", req ) );
	}

	// Doc path streams the comment.
	pWatcher->setComment( "member comment" );
	{
		WatcherPathRequestV2 req( "__doc__" );
		CHECK( pWatcher->getAsStream( NULL, "__doc__", req ) );
	}

	// Stream set without packet data fails.
	{
		WatcherPathRequestV2 req( "value" );
		CHECK( !pWatcher->setFromStream( NULL, "", req ) );
		CHECK_EQUAL( 0, obj.value() );
	}

	// Stream set with a value round-trips.
	{
		MemoryOStream os;
		watcherValueToStream( os, 19, Watcher::WT_READ_WRITE );
		WatcherPathRequestV2 req( "value" );
		CHECK( req.setPacketData( os ) );
		CHECK( pWatcher->setFromStream( NULL, "", req ) );
		CHECK_EQUAL( 19, obj.value() );
	}
}


// -----------------------------------------------------------------------------
// Section: FunctionWatcher gaps (string specialization + streams)
// -----------------------------------------------------------------------------

TEST( WatcherGaps_Func_stringGetSetStreams )
{
	g_gapFuncStr = "gap";

	WatcherPtr pWatcher = new FunctionWatcher< BW::string >(
		gapGetStr, gapSetStr );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( pWatcher->getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "gap" ), result );
	CHECK_EQUAL( Watcher::WT_READ_WRITE, mode );
	CHECK( !pWatcher->getAsString( NULL, "sub", result, desc, mode ) );

	CHECK( pWatcher->setFromString( NULL, "", "via-watcher" ) );
	CHECK_EQUAL( BW::string( "via-watcher" ), g_gapFuncStr );

	{
		WatcherPathRequestV2 req( "" );
		CHECK( pWatcher->getAsStream( NULL, "", req ) );
		CHECK( req.getDataSize() > 0 );
	}
	{
		WatcherPathRequestV2 req( "__doc__" );
		CHECK( pWatcher->getAsStream( NULL, "__doc__", req ) );
	}
	{
		WatcherPathRequestV2 req( "sub" );
		CHECK( !pWatcher->getAsStream( NULL, "sub", req ) );
		CHECK( !pWatcher->setFromStream( NULL, "sub", req ) );
	}
	{
		WatcherPathRequestV2 req( "value" );
		CHECK( !pWatcher->setFromStream( NULL, "", req ) );
		CHECK_EQUAL( BW::string( "via-watcher" ), g_gapFuncStr );
	}
	{
		MemoryOStream os;
		watcherValueToStream( os, BW::string( "via-stream" ),
			Watcher::WT_READ_WRITE );
		WatcherPathRequestV2 req( "value" );
		CHECK( req.setPacketData( os ) );
		CHECK( pWatcher->setFromStream( NULL, "", req ) );
		CHECK_EQUAL( BW::string( "via-stream" ), g_gapFuncStr );
	}

	g_gapFuncStr = "gap";
}


TEST( WatcherGaps_Func_intReadOnlyStreams )
{
	g_gapFuncInt = 5;

	WatcherPtr pWatcher = new FunctionWatcher< int >( gapGetInt );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( pWatcher->getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "5" ), result );

	{
		WatcherPathRequestV2 req( "" );
		CHECK( pWatcher->getAsStream( NULL, "", req ) );
	}
	{
		WatcherPathRequestV2 req( "__doc__" );
		CHECK( pWatcher->getAsStream( NULL, "__doc__", req ) );
	}
	{
		MemoryOStream os;
		watcherValueToStream( os, 6, Watcher::WT_READ_WRITE );
		WatcherPathRequestV2 req( "value" );
		CHECK( req.setPacketData( os ) );
		CHECK( !pWatcher->setFromStream( NULL, "", req ) );
		CHECK_EQUAL( 5, g_gapFuncInt );
	}
}


// -----------------------------------------------------------------------------
// Section: DereferenceWatcher family / AbsoluteWatcher
// -----------------------------------------------------------------------------

TEST( WatcherGaps_BaseDeref_okAndNull )
{
	int target = 123;
	void * slot = &target;

	WatcherPtr pChild =
		makeWatcher( &GapIntValue::value, Watcher::WT_READ_WRITE );
	WatcherPtr pWatcher = new BaseDereferenceWatcher( pChild );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( pWatcher->getAsString( &slot, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "123" ), result );

	CHECK( pWatcher->setFromString( &slot, "", "456" ) );
	CHECK_EQUAL( 456, target );

	// NULL base and NULL-slot both fail.
	CHECK( !pWatcher->getAsString( NULL, "", result, desc, mode ) );
	void * nullSlot = NULL;
	CHECK( !pWatcher->getAsString( &nullSlot, "", result, desc, mode ) );

	// Stream verbs pass through the dereference too.
	{
		WatcherPathRequestV2 req( "" );
		CHECK( pWatcher->getAsStream( &slot, "", req ) );
		CHECK( req.getDataSize() > 0 );
	}
	{
		MemoryOStream os;
		watcherValueToStream( os, 789, Watcher::WT_READ_WRITE );
		WatcherPathRequestV2 req( "value" );
		CHECK( req.setPacketData( os ) );
		CHECK( pWatcher->setFromStream( &slot, "", req ) );
		CHECK_EQUAL( 789, target );
	}
}


// Layout-compatible stand-in for SmartPointer< uint >: that type cannot be
// constructed in a unit test (uint has no incRef/decRef, so any non-null
// SmartPointer< uint > fails to compile), but ConstSmartPointer has no
// vtable and a single data member (const Object *object_), so reading it
// back through get() is layout-safe. Production relies on the same
// coincidence when SmartPointer-like bases flow through
// SmartPointerDereferenceWatcher::dereference().
struct FakeSmartUintHolder
{
	const unsigned int * p_;
};


TEST( WatcherGaps_SmartPtrDeref_holderPaths )
{
	unsigned int raw = 42;
	FakeSmartUintHolder holder = { &raw };
	FakeSmartUintHolder nullHolder = { NULL };

	WatcherPtr pChild =
		makeWatcher( &GapUintValue::value, Watcher::WT_READ_WRITE );
	WatcherPtr pWatcher = new SmartPointerDereferenceWatcher( pChild );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	// Non-null holder passes the raw pointer through to the child watcher.
	CHECK( pWatcher->getAsString( &holder, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "42" ), result );

	CHECK( pWatcher->setFromString( &holder, "", "43" ) );
	CHECK_EQUAL( 43u, raw );

	// Null holder dereferences to NULL, so the read fails - this executes
	// the SmartPointerDereferenceWatcher dereference body in watcher.hpp.
	CHECK( !pWatcher->getAsString( &nullHolder, "", result, desc, mode ) );
	CHECK( !pWatcher->getAsString( NULL, "", result, desc, mode ) );
}


TEST( WatcherGaps_ContainerBounce_changeContainer )
{
	BW::map< int, int > map1;
	map1[ 1 ] = 10;
	map1[ 2 ] = 20;
	BW::map< int, int > map2;
	map2[ 1 ] = 100;

	WatcherPtr pChild =
		makeWatcher( &GapIntValue::value, Watcher::WT_READ_WRITE );
	ContainerBounceWatcher< BW::map< int, int >, int > * pBounce =
		new ContainerBounceWatcher< BW::map< int, int >, int >(
			pChild, &map1 );
	WatcherPtr pWatcher = pBounce;

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	int key = 1;
	CHECK( pWatcher->getAsString( &key, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "10" ), result );

	CHECK( pWatcher->setFromString( &key, "", "15" ) );
	CHECK_EQUAL( 15, map1[ 1 ] );

	// Switch containers: reads now come from map2.
	pBounce->changeContainer( &map2 );
	CHECK( pWatcher->getAsString( &key, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "100" ), result );
}


TEST( WatcherGaps_Absolute_passthrough )
{
	int target = 11;
	WatcherPtr pInner =
		makeWatcher( &GapIntValue::value, Watcher::WT_READ_WRITE );
	WatcherPtr pWatcher = new AbsoluteWatcher( pInner, &target );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	int dummy = 0;
	CHECK( pWatcher->getAsString( &dummy, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "11" ), result );

	CHECK( pWatcher->setFromString( &dummy, "", "22" ) );
	CHECK_EQUAL( 22, target );

	// NULL base fails without touching the inner watcher.
	CHECK( !pWatcher->getAsString( NULL, "", result, desc, mode ) );

	{
		WatcherPathRequestV2 req( "" );
		CHECK( pWatcher->getAsStream( &dummy, "", req ) );
		CHECK( req.getDataSize() > 0 );
	}
}


// -----------------------------------------------------------------------------
// Section: makeWatcher factory overloads
// -----------------------------------------------------------------------------

TEST( WatcherGaps_Factories_constRefAndBare )
{
	GapObject obj;
	obj.setValue( 8 );
	obj.setName( "eight" );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	// With-object const-ref getter.
	{
		WatcherPtr p = makeWatcher( obj, &GapObject::name );
		CHECK( p->getAsString( NULL, "", result, desc, mode ) );
		CHECK_EQUAL( BW::string( "eight" ), result );
		CHECK_EQUAL( Watcher::WT_READ_ONLY, mode );
		CHECK( !p->setFromString( NULL, "", "x" ) );
	}

	// With-object const-ref getter + setter.
	{
		WatcherPtr p = makeWatcher( obj,
			&GapObject::name, &GapObject::setName );
		CHECK( p->setFromString( NULL, "", "nine" ) );
		CHECK_EQUAL( BW::string( "nine" ), obj.name() );
	}

	// Without-object value getter + setter, object via base.
	{
		WatcherPtr p = makeWatcher(
			(int (GapObject::*)() const)( &GapObject::value ),
			(void (GapObject::*)( int ))( &GapObject::setValue ) );
		CHECK( p->getAsString( &obj, "", result, desc, mode ) );
		CHECK_EQUAL( BW::string( "8" ), result );
		CHECK( p->setFromString( &obj, "", "80" ) );
		CHECK_EQUAL( 80, obj.value() );
	}

	// Without-object const-ref getter, object via base.
	{
		WatcherPtr p = makeWatcher(
			(const BW::string & (GapObject::*)() const)( &GapObject::name ) );
		obj.setName( "via-base" );
		CHECK( p->getAsString( &obj, "", result, desc, mode ) );
		CHECK_EQUAL( BW::string( "via-base" ), result );
	}

	// Without-object const-ref getter + setter, object via base.
	{
		WatcherPtr p = makeWatcher(
			(const BW::string & (GapObject::*)() const)( &GapObject::name ),
			(void (GapObject::*)( const BW::string & ))( &GapObject::setName ) );
		CHECK( p->setFromString( &obj, "", "via-base-2" ) );
		CHECK_EQUAL( BW::string( "via-base-2" ), obj.name() );
	}

	// makeNonRefWatcher without object, object via base.
	{
		WatcherPtr p = makeNonRefWatcher(
			(int (GapObject::*)() const)( &GapObject::value ),
			(void (GapObject::*)( int ))( &GapObject::setValue ) );
		CHECK( p->setFromString( &obj, "", "81" ) );
		CHECK_EQUAL( 81, obj.value() );
	}
}


// -----------------------------------------------------------------------------
// Section: addWatcher / addReferenceWatcher / getWatcher / FreezeWatcher
// -----------------------------------------------------------------------------

TEST( WatcherGaps_FreezeWatcher_cycle )
{
	{
		FreezeWatcher< int > fw( "GapBatch22/frozen", "frozen comment" );
		WatcherPtr pFrozen = getWatcher( "GapBatch22/frozen" );
		CHECK( pFrozen != NULL );

		int v = 5;
		fw.freezeValue( v );
		CHECK_EQUAL( 5, v );

		// Freeze via the watcher, record the value.
		CHECK( pFrozen->setFromString( NULL, "", "true" ) );
		fw.freezeValue( v );
		v = 7;
		// Still frozen: latest value overridden with the stored one.
		fw.freezeValue( v );
		CHECK_EQUAL( 5, v );

		CHECK( pFrozen->setFromString( NULL, "", "false" ) );
		v = 9;
		fw.freezeValue( v );
		CHECK_EQUAL( 9, v );
	}

	// Destructor removed the watcher from the root.
	CHECK( getWatcher( "GapBatch22/frozen" ) == NULL );
}


TEST( WatcherGaps_AddGetWatcher_globalRoot )
{
	GapObject obj;
	obj.setValue( 3 );
	obj.setName( "rooted" );
	int plain = 11;
	g_gapFuncInt = 5;

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	// Read-only member watcher, no comment.
	WatcherPtr pRO = addWatcher( "GapBatch22/roValue", obj, &GapObject::value );
	CHECK( pRO != NULL );
	// Duplicate add fails and yields NULL.
	CHECK( addWatcher( "GapBatch22/roValue",
		obj, &GapObject::value ) == NULL );

	// Read/write member watcher with comment.
	WatcherPtr pRW = addWatcher( "GapBatch22/rwValue", obj,
		&GapObject::value, &GapObject::setValue, "rw comment" );
	CHECK( pRW != NULL );

	// Function watcher with getter + setter.
	WatcherPtr pFn = addWatcher( "GapBatch22/fnValue",
		gapGetInt, gapSetInt, "fn comment" );
	CHECK( pFn != NULL );

	// Read-only function watcher (delegating overload).
	WatcherPtr pFnRO = addWatcher( "GapBatch22/fnROValue",
		gapGetInt, "fn ro comment" );
	CHECK( pFnRO != NULL );

	// Plain value watcher.
	WatcherPtr pVal = addWatcher( "GapBatch22/plainValue", plain,
		Watcher::WT_READ_WRITE, "plain comment" );
	CHECK( pVal != NULL );

	// Reference-style watchers.
	WatcherPtr pRefRO = addReferenceWatcher( "GapBatch22/refROValue", obj,
		&GapObject::name );
	CHECK( pRefRO != NULL );
	WatcherPtr pRefRW = addReferenceWatcher( "GapBatch22/refRWValue", obj,
		&GapObject::name, &GapObject::setName, "ref comment" );
	CHECK( pRefRW != NULL );

	// getWatcher resolves through the root; misses yield NULL.
	CHECK( getWatcher( "GapBatch22/rwValue" ) == pRW );
	CHECK( getWatcher( "GapBatch22/noSuch" ) == NULL );

	// Values are live through the root.
	CHECK( Watcher::rootWatcher().getAsString( NULL, "GapBatch22/rwValue",
		result, desc, mode ) );
	CHECK_EQUAL( BW::string( "3" ), result );
	CHECK( Watcher::rootWatcher().setFromString( NULL,
		"GapBatch22/rwValue", "30" ) );
	CHECK_EQUAL( 30, obj.value() );

	CHECK( Watcher::rootWatcher().setFromString( NULL,
		"GapBatch22/plainValue", "12" ) );
	CHECK_EQUAL( 12, plain );

	// Read-only entries reject sets through the root.
	CHECK( !Watcher::rootWatcher().setFromString( NULL,
		"GapBatch22/roValue", "99" ) );
	CHECK_EQUAL( 30, obj.value() );

	CHECK( Watcher::rootWatcher().setFromString( NULL,
		"GapBatch22/fnValue", "50" ) );
	CHECK_EQUAL( 50, g_gapFuncInt );

	// Callable helper + comment-only watcher object.
	WatcherPtr pCallable( new GapSimpleCallable() );
	CHECK( pCallable != NULL );

	// Single cleanup of the whole subtree.
	CHECK( Watcher::rootWatcher().removeChild( "GapBatch22" ) );
	CHECK( getWatcher( "GapBatch22/rwValue" ) == NULL );

	g_gapFuncInt = 5;
}

// -----------------------------------------------------------------------------
// Section: leaf rejection arms / SafeWatcher / MemberWatcher formats
// -----------------------------------------------------------------------------

TEST( WatcherGaps_base_nonEmptyPathRejection )
{
	// DataWatcher::getAsString is protected: drive it through the public
	// WatcherPtr interface, which is what every real caller does anyway.
	int readOnlyValue = 42;
	int rwValue = 0;
	WatcherPtr pRO = new DataWatcher< int >( readOnlyValue );
	WatcherPtr pRW = new DataWatcher< int >( rwValue, Watcher::WT_READ_WRITE );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	// Non-empty path on a leaf watcher: every arm returns false and leaves
	// the caller's mode/result untouched
	CHECK( !pRO->getAsString( NULL, "sub", result, desc, mode ) );
	CHECK_EQUAL( Watcher::WT_INVALID, mode );
	CHECK( result.empty() );

	// setFromString with a non-empty path fails even in read/write mode
	CHECK( !pRW->setFromString( NULL, "sub", "10" ) );
	CHECK_EQUAL( 0, rwValue );

	// The same failures hold for the stream arms
	{
		WatcherPathRequestV2 req( "sub" );
		CHECK( !pRW->getAsStream( NULL, "sub", req ) );
	}

	// Doc path still works on a read/write leaf, and the empty path works
	{
		WatcherPathRequestV2 req( "__doc__" );
		CHECK( pRW->getAsStream( NULL, "__doc__", req ) );
	}
	CHECK( pRW->getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( Watcher::WT_READ_WRITE, mode );
	CHECK_EQUAL( BW::string( "0" ), result );
}

TEST( WatcherGaps_SafeWatcher_grabGivePassthrough )
{
	using namespace BW;

	int value = 123;
	DataWatcher< int > * pDataWatcher = new DataWatcher< int >( value,
		Watcher::WT_READ_WRITE );
	SimpleMutex mutex;
	WatcherPtr pWatcher = new SafeWatcher( pDataWatcher, mutex );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	// Direct-ctor DataWatcher binds rValue_ to the real object, so the
	// base pointer must stay NULL (non-NULL base is offset arithmetic
	// on top of &rValue_). SafeWatcher grabs/gives around each call and
	// passes the base through untouched.
	CHECK( pWatcher->getAsString( NULL, "", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "123" ), result );

	CHECK( pWatcher->setFromString( NULL, "", "456" ) );
	CHECK_EQUAL( 456, value );
}

TEST( WatcherGaps_Member_setFromString_variousFormats )
{
	using namespace BW;

	GapObject obj;
	obj.setValue( 5 );
	obj.setName( "test" );

	WatcherPtr pWatcher = makeNonRefWatcher( obj,
		&GapObject::value, &GapObject::setValue );

	// Valid integer string
	CHECK( pWatcher->setFromString( NULL, "", "42" ) );
	CHECK_EQUAL( 42, obj.value() );

	// Floating point string parses up to the dot (stringstream semantics).
	obj.setValue( 5 );
	CHECK( pWatcher->setFromString( NULL, "", "3.14" ) );
	CHECK_EQUAL( 3, obj.value() );

	// Invalid string rejected
	obj.setValue( 5 );
	CHECK( !pWatcher->setFromString( NULL, "", "not_a_number" ) );
	CHECK_EQUAL( 5, obj.value() );

	// Empty string fails on a numeric leaf
	obj.setValue( 5 );
	CHECK( !pWatcher->setFromString( NULL, "", "" ) );
	CHECK_EQUAL( 5, obj.value() );
}

BW_END_NAMESPACE

// test_watcher_gaps.cpp
