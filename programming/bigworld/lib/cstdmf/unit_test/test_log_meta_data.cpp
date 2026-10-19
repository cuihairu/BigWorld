#include "pch.hpp"

#include "cstdmf/log_meta_data.hpp"

#include "cstdmf/bw_string.hpp"
#include "cstdmf/memory_stream.hpp"
#include "cstdmf/stdmf_minimal.hpp"

#include <float.h>
#include <limits.h>

BW_BEGIN_NAMESPACE


TEST( BasicLogMetaDataConstruction )
{
	LogMetaData logMetaData;
	LogMetaData * pLogMetaData = new LogMetaData;
	delete pLogMetaData;
}

TEST( LogMetaData_NullKey )
{
	LogMetaData lmd;
	int dummyValue = 1;
	lmd.add( NULL, dummyValue );
	MemoryOStream * os = const_cast< MemoryOStream * >( lmd.pData() );

	CHECK( os == NULL );
}

TEST( LogMetaData_EmptyKey )
{
	LogMetaData lmd;
	int dummyValue = 1;
	lmd.add( "", dummyValue );
	MemoryOStream * os = const_cast< MemoryOStream * >( lmd.pData() );

	CHECK( os == NULL );
}


#define LMD_CREATE_TYPE( TYPE, VALUE ) \
	const char * sentKey = "key"; \
	LogMetaData lmd; \
	lmd.add( sentKey, VALUE ); \
	MemoryOStream * os = const_cast< MemoryOStream * >( lmd.pData() ); \
	MemoryIStream is( os->data(), os->size() ); \
	LogMetaData::MessageLoggerValueType keyType;						\
	BW::string recvKey;													\
	LogMetaData::MessageLoggerValueType valueType;						\
	TYPE value;															\
	is >> keyType >> recvKey >> valueType >> value; 					\
	CHECK_EQUAL( LogMetaData::MLVT_STRING, keyType );					\
	CHECK_EQUAL( 0, strcmp( sentKey, recvKey.c_str() ) );				\
	CHECK_EQUAL( expectedType, valueType );



TEST( LogMetaData_AddType_short_int )
{
	LogMetaData::MessageLoggerValueType expectedType = LogMetaData::MLVT_INT;

	{
		short int sendValue = 1;
		LMD_CREATE_TYPE( short int, sendValue )
		CHECK_EQUAL( sendValue, value );
	}
	{
		short int sendValue = -1;
		LMD_CREATE_TYPE( short int, sendValue )
		CHECK_EQUAL( sendValue, value );
	}
	{
		short int sendValue = 0;
		LMD_CREATE_TYPE( short int, sendValue )
		CHECK_EQUAL( sendValue, value );
	}
	{
		LMD_CREATE_TYPE( short int, SHRT_MIN )
		CHECK_EQUAL( SHRT_MIN, value );
	}
	{
		LMD_CREATE_TYPE( short int, SHRT_MAX )
		CHECK_EQUAL( SHRT_MAX, value );
	}
}


TEST( LogMetaData_AddType_int )
{
	LogMetaData::MessageLoggerValueType expectedType = LogMetaData::MLVT_INT;

	{
		int sendValue = 1;
		LMD_CREATE_TYPE( int, sendValue )
		CHECK_EQUAL( sendValue, value );
	}
	{
		int sendValue = -1;
		LMD_CREATE_TYPE( int, sendValue )
		CHECK_EQUAL( sendValue, value );
	}
	{
		int sendValue = 0;
		LMD_CREATE_TYPE( int, sendValue )
		CHECK_EQUAL( sendValue, value );
	}
	{
		LMD_CREATE_TYPE( int, INT_MIN )
		CHECK_EQUAL( INT_MIN, value );
	}
	{
		LMD_CREATE_TYPE( int, INT_MAX )
		CHECK_EQUAL( INT_MAX, value );
	}
}


TEST( LogMetaData_AddType_uint )
{
	LogMetaData::MessageLoggerValueType expectedType = LogMetaData::MLVT_UNSIGNED_INT;

	{
		unsigned int sendValue = 1;
		LMD_CREATE_TYPE( unsigned int, sendValue )
		CHECK_EQUAL( sendValue, value );
	}
	{
		unsigned int sendValue = 0;
		sendValue--;
		LMD_CREATE_TYPE( unsigned int, sendValue )
		CHECK_EQUAL( sendValue, value );
	}
	{
		unsigned int sendValue = 0;
		LMD_CREATE_TYPE( unsigned int, sendValue )
		CHECK_EQUAL( sendValue, value );
	}
	{
		LMD_CREATE_TYPE( unsigned int, UINT_MAX )
		CHECK_EQUAL( UINT_MAX, value );
	}

}


TEST( LogMetaData_AddType_long_long_int )
{
	LogMetaData::MessageLoggerValueType expectedType = LogMetaData::MLVT_LONG_LONG_INT;
	{
		long long sendValue = 1;
		LMD_CREATE_TYPE( long long, sendValue )
		CHECK_EQUAL( sendValue, value );
	}
	{
		long long sendValue = -1;
		LMD_CREATE_TYPE( long long, sendValue )
		CHECK_EQUAL( sendValue, value );
	}
	{
		long long sendValue = 0;
		LMD_CREATE_TYPE( long long, sendValue )
		CHECK_EQUAL( sendValue, value );
	}
	{
		LMD_CREATE_TYPE( long long int, LLONG_MIN )
		CHECK_EQUAL( LLONG_MIN, value );
	}
	{
		LMD_CREATE_TYPE( long long int, LLONG_MAX )
		CHECK_EQUAL( LLONG_MAX, value );
	}
}


/*
TEST( LogMetaData_AddType_unsigned_long_int )
{
	{
		unsigned long int sendValue = 1;
		LMD_CREATE_TYPE( unsigned long int, sendValue )
		CHECK_EQUAL( sendValue, value );
	}
	{
		unsigned long int sendValue = -1;
		LMD_CREATE_TYPE( unsigned long int, sendValue )
		CHECK_EQUAL( sendValue, value );
	}
	{
		unsigned long int sendValue = 0;
		LMD_CREATE_TYPE( unsigned long int, sendValue )
		CHECK_EQUAL( sendValue, value );
	}
	{
		LMD_CREATE_TYPE( unsigned long int, ULONG_MAX )
		CHECK_EQUAL( ULONG_MAX, value );
	}
}


TEST( LogMetaData_AddType_unsigned_long_long_int )
{
	{
		unsigned long long int sendValue = 1;
		LMD_CREATE_TYPE( unsigned long long int, sendValue )
		CHECK_EQUAL( sendValue, value );
	}
	{
		unsigned long long int sendValue = -1;
		LMD_CREATE_TYPE( unsigned long long int, sendValue )
		CHECK_EQUAL( sendValue, value );
	}
	{
		unsigned long long int sendValue = 0;
		LMD_CREATE_TYPE( unsigned long long int, sendValue )
		CHECK_EQUAL( sendValue, value );
	}
	{
		LMD_CREATE_TYPE( unsigned long long int, ULLONG_MAX )
		CHECK_EQUAL( ULLONG_MAX, value );
	}
}
*/


TEST( LogMetaData_AddType_char_ptr )
{
	// An empty string
	{
		const char * sentKey = "key";
		const char * sentValue = "";

		LogMetaData lmd;
		lmd.add( sentKey, sentValue );
		MemoryOStream * os = const_cast< MemoryOStream * >( lmd.pData() );
		MemoryIStream is( os->data(), os->size() );

		LogMetaData::MessageLoggerValueType keyType;
		BW::string recvKey;
		LogMetaData::MessageLoggerValueType valueType;
		BW::string value;

		is >> keyType >> recvKey >> valueType >> value;

		CHECK_EQUAL( LogMetaData::MLVT_STRING, keyType );
		CHECK_EQUAL( 0, strcmp( sentKey, recvKey.c_str() ) );

		CHECK_EQUAL( LogMetaData::MLVT_STRING, valueType );
		CHECK_EQUAL( 0, strcmp( sentValue, value.c_str() ) );
	}

	// A NULL
	{
		const char * sentKey = "key";
		const char * sentValue = NULL;
		const char * expectedValue = "";

		LogMetaData lmd;
		lmd.add( sentKey, sentValue );
		MemoryOStream * os = const_cast< MemoryOStream * >( lmd.pData() );
		MemoryIStream is( os->data(), os->size() );

		LogMetaData::MessageLoggerValueType keyType;
		BW::string recvKey;
		LogMetaData::MessageLoggerValueType valueType;
		BW::string value;

		is >> keyType >> recvKey >> valueType >> value;

		CHECK_EQUAL( LogMetaData::MLVT_STRING, keyType );
		CHECK_EQUAL( 0, strcmp( sentKey, recvKey.c_str() ) );

		CHECK_EQUAL( LogMetaData::MLVT_STRING, valueType );
		CHECK_EQUAL( 0, strcmp( expectedValue, value.c_str() ) );
	}

	// An actual string
	{
		const char * sentKey = "key";
		const char * sentValue = "a string";

		LogMetaData lmd;
		lmd.add( sentKey, sentValue );
		MemoryOStream * os = const_cast< MemoryOStream * >( lmd.pData() );
		MemoryIStream is( os->data(), os->size() );

		LogMetaData::MessageLoggerValueType keyType;
		BW::string recvKey;
		LogMetaData::MessageLoggerValueType valueType;
		BW::string value;

		is >> keyType >> recvKey >> valueType >> value;

		CHECK_EQUAL( LogMetaData::MLVT_STRING, keyType );
		CHECK_EQUAL( 0, strcmp( sentKey, recvKey.c_str() ) );

		CHECK_EQUAL( LogMetaData::MLVT_STRING, valueType );
		CHECK_EQUAL( 0, strcmp( sentValue, value.c_str() ) );
	}


	// A string that looks JSONy
	{
		const char * sentKey = "key";
		const char * sentValue = "{'json': 'string'}";

		LogMetaData lmd;
		lmd.add( sentKey, sentValue );
		MemoryOStream * os = const_cast< MemoryOStream * >( lmd.pData() );
		MemoryIStream is( os->data(), os->size() );

		LogMetaData::MessageLoggerValueType keyType;
		BW::string recvKey;
		LogMetaData::MessageLoggerValueType valueType;
		BW::string value;

		is >> keyType >> recvKey >> valueType >> value;

		CHECK_EQUAL( LogMetaData::MLVT_STRING, keyType );
		CHECK_EQUAL( 0, strcmp( sentKey, recvKey.c_str() ) );

		CHECK_EQUAL( LogMetaData::MLVT_STRING, valueType );
		CHECK_EQUAL( 0, strcmp( sentValue, value.c_str() ) );
	}


	// Broken unicode string
	{
		const char * sentKey = "key";
		const char * sentValue = "Broken unicode \xc3\x28 is broken";

		LogMetaData lmd;
		lmd.add( sentKey, sentValue );
		MemoryOStream * os = const_cast< MemoryOStream * >( lmd.pData() );
		MemoryIStream is( os->data(), os->size() );

		LogMetaData::MessageLoggerValueType keyType;
		BW::string recvKey;
		LogMetaData::MessageLoggerValueType valueType;
		BW::string value;

		is >> keyType >> recvKey >> valueType >> value;

		CHECK_EQUAL( LogMetaData::MLVT_STRING, keyType );
		CHECK_EQUAL( 0, strcmp( sentKey, recvKey.c_str() ) );

		CHECK_EQUAL( LogMetaData::MLVT_STRING, valueType );
		CHECK_EQUAL( 0, strcmp( sentValue, value.c_str() ) );
	}
}


TEST( LogMetaData_AddType_float )
{
	LogMetaData::MessageLoggerValueType expectedType = LogMetaData::MLVT_FLOAT;
	{
		float sendValue = 0.f;
		LMD_CREATE_TYPE( float, sendValue )
		CHECK( isEqual( sendValue, value ) );
	}
	{
		float sendValue = -1.f;
		LMD_CREATE_TYPE( float, sendValue )
		CHECK( isEqual( sendValue, value ) );
	}
	{
		float sendValue = 1.f;
		LMD_CREATE_TYPE( float, sendValue )
		CHECK( isEqual( sendValue, value ) );
	}
	{
		LMD_CREATE_TYPE( float, FLT_MAX )
		CHECK( isEqual( FLT_MAX, value ) );
	}
	{
		LMD_CREATE_TYPE( float, FLT_MIN )
		CHECK( isEqual( FLT_MIN, value ) );
	}
}


TEST( LogMetaData_AddType_double )
{
	LogMetaData::MessageLoggerValueType expectedType = LogMetaData::MLVT_DOUBLE;
	{
		double sendValue = 0.0;
		LMD_CREATE_TYPE( double, sendValue )
		CHECK( isEqual( sendValue, value ) );
	}
	{
		double sendValue = -1.0;
		LMD_CREATE_TYPE( double, sendValue )
		CHECK( isEqual( sendValue, value ) );
	}
	{
		double sendValue = 1.0;
		LMD_CREATE_TYPE( double, sendValue )
		CHECK( isEqual( sendValue, value ) );
	}
	{
		LMD_CREATE_TYPE( double, DBL_MAX )
		CHECK( isEqual( DBL_MAX, value ) );
	}
	{
		LMD_CREATE_TYPE( double, DBL_MIN )
		CHECK( isEqual( DBL_MIN, value ) );
	}
}



TEST( LogMetaData_AddType_json )
{
	// An empty string
	{
		const char * sentValue = "";

		LogMetaData lmd;
		lmd.addJSON( sentValue );
		MemoryOStream * os = const_cast< MemoryOStream * >( lmd.pData() );

		CHECK( os == NULL );
	}

	// A NULL
	{
		const char * sentValue = NULL;

		LogMetaData lmd;
		lmd.addJSON( sentValue );
		MemoryOStream * os = const_cast< MemoryOStream * >( lmd.pData() );

		CHECK( os == NULL );
	}

	// An actual string
	{
		const char * sentValue = "a string";

		LogMetaData lmd;
		lmd.addJSON( sentValue );
		MemoryOStream * os = const_cast< MemoryOStream * >( lmd.pData() );
		MemoryIStream is( os->data(), os->size() );

		LogMetaData::MessageLoggerValueType keyType;
		BW::string recvKey;
		LogMetaData::MessageLoggerValueType valueType;
		BW::string value;

		is >> keyType >> recvKey >> valueType >> value;

		CHECK_EQUAL( LogMetaData::MLVT_STRING, keyType );
		CHECK_EQUAL( 0, strcmp( LogMetaData::key_jsonString, recvKey.c_str() ) );

		CHECK_EQUAL( LogMetaData::MLVT_JSON_STRING, valueType );
		CHECK_EQUAL( 0, strcmp( sentValue, value.c_str() ) );
	}


	// A string that looks JSONy
	{
		const char * sentValue = "{'json': 'string'}";

		LogMetaData lmd;
		lmd.addJSON( sentValue );
		MemoryOStream * os = const_cast< MemoryOStream * >( lmd.pData() );
		MemoryIStream is( os->data(), os->size() );

		LogMetaData::MessageLoggerValueType keyType;
		BW::string recvKey;
		LogMetaData::MessageLoggerValueType valueType;
		BW::string value;

		is >> keyType >> recvKey >> valueType >> value;

		CHECK_EQUAL( LogMetaData::MLVT_STRING, keyType );
		CHECK_EQUAL( 0, strcmp( LogMetaData::key_jsonString, recvKey.c_str() ) );

		CHECK_EQUAL( LogMetaData::MLVT_JSON_STRING, valueType );
		CHECK_EQUAL( 0, strcmp( sentValue, value.c_str() ) );
	}


	// Broken unicode string
	{
		const char * sentValue = "Broken unicode \xc3\x28 is broken";

		LogMetaData lmd;
		lmd.addJSON( sentValue );
		MemoryOStream * os = const_cast< MemoryOStream * >( lmd.pData() );
		MemoryIStream is( os->data(), os->size() );

		LogMetaData::MessageLoggerValueType keyType;
		BW::string recvKey;
		LogMetaData::MessageLoggerValueType valueType;
		BW::string value;

		is >> keyType >> recvKey >> valueType >> value;

		CHECK_EQUAL( LogMetaData::MLVT_STRING, keyType );
		CHECK_EQUAL( 0, strcmp( LogMetaData::key_jsonString, recvKey.c_str() ) );

		CHECK_EQUAL( LogMetaData::MLVT_JSON_STRING, valueType );
		CHECK_EQUAL( 0, strcmp( sentValue, value.c_str() ) );
	}
}


// TODO: Add a mixture of things
// .add( int
// .add( float
// .add( string
//
// from an array of type, value, expected


// -----------------------------------------------------------------------------
// Section: LogCorrelation
// -----------------------------------------------------------------------------

namespace
{
/**
 *	Reads a metadata object back as key/value pairs, assuming every value is
 *	a long long. Asserts on the stream contents belong in the calling TEST
 *	bodies; this helper only unpacks data.
 */
void readCorrelationPairs( const LogMetaData & lmd,
	BW::string keys[], long long values[], int maxPairs, int & numPairs )
{
	numPairs = 0;

	MemoryOStream * pStream = const_cast< MemoryOStream * >( lmd.pData() );
	if (pStream == NULL)
	{
		return;
	}

	MemoryIStream is( pStream->data(), pStream->size() );
	while ((is.remainingLength() > 0) && (numPairs < maxPairs))
	{
		LogMetaData::MessageLoggerValueType keyType;
		LogMetaData::MessageLoggerValueType valueType;
		is >> keyType >> keys[ numPairs ] >> valueType >> values[ numPairs ];

		if ((keyType != LogMetaData::MLVT_STRING) ||
				(valueType != LogMetaData::MLVT_LONG_LONG_INT))
		{
			numPairs = -1;
			return;
		}

		++numPairs;
	}
}

} // anonymous namespace


TEST( LogCorrelation_DefaultsEmpty )
{
	LogCorrelation::clearAll();
	CHECK( !LogCorrelation::hasFields() );

	LogMetaData lmd;
	LogCorrelation::appendTo( lmd );

	CHECK( lmd.pData() == NULL );
}


TEST( LogCorrelation_SetAndAppend )
{
	LogCorrelation::clearAll();

	LogCorrelation::set( "entityID", 42 );
	LogCorrelation::set( "spaceID", 7 );
	CHECK( LogCorrelation::hasFields() );

	LogMetaData lmd;
	LogCorrelation::appendTo( lmd );

	BW::string keys[ 8 ];
	long long values[ 8 ];
	int numPairs = 0;
	readCorrelationPairs( lmd, keys, values, 8, numPairs );

	CHECK_EQUAL( 2, numPairs );
	CHECK_EQUAL( 0, strcmp( "entityID", keys[ 0 ].c_str() ) );
	CHECK_EQUAL( 42, values[ 0 ] );
	CHECK_EQUAL( 0, strcmp( "spaceID", keys[ 1 ].c_str() ) );
	CHECK_EQUAL( 7, values[ 1 ] );

	LogCorrelation::clearAll();
}


TEST( LogCorrelation_ReplaceSameKey )
{
	LogCorrelation::clearAll();

	LogCorrelation::set( "entityID", 1 );
	LogCorrelation::set( "entityID", 2 );

	LogMetaData lmd;
	LogCorrelation::appendTo( lmd );

	BW::string keys[ 8 ];
	long long values[ 8 ];
	int numPairs = 0;
	readCorrelationPairs( lmd, keys, values, 8, numPairs );

	CHECK_EQUAL( 1, numPairs );
	CHECK_EQUAL( 2, values[ 0 ] );

	LogCorrelation::clearAll();
}


TEST( LogCorrelation_ClearAndClearAll )
{
	LogCorrelation::clearAll();

	LogCorrelation::set( "entityID", 1 );
	LogCorrelation::set( "spaceID", 2 );

	// Clearing an absent key is a no-op.
	LogCorrelation::clear( "channel" );
	CHECK( LogCorrelation::hasFields() );

	LogCorrelation::clear( "entityID" );
	CHECK( LogCorrelation::hasFields() );

	LogMetaData lmd;
	LogCorrelation::appendTo( lmd );

	BW::string keys[ 8 ];
	long long values[ 8 ];
	int numPairs = 0;
	readCorrelationPairs( lmd, keys, values, 8, numPairs );

	CHECK_EQUAL( 1, numPairs );
	CHECK_EQUAL( 0, strcmp( "spaceID", keys[ 0 ].c_str() ) );

	LogCorrelation::clearAll();
	CHECK( !LogCorrelation::hasFields() );
}


TEST( LogCorrelation_EmptyKeyIgnored )
{
	LogCorrelation::clearAll();

	LogCorrelation::set( NULL, 1 );
	LogCorrelation::set( "", 1 );
	CHECK( !LogCorrelation::hasFields() );
}


TEST( LogCorrelation_LongKeyTruncated )
{
	LogCorrelation::clearAll();

	const char * pLongKey = "0123456789012345678901234567890123456789";
	LogCorrelation::set( pLongKey, 5 );

	LogMetaData lmd;
	LogCorrelation::appendTo( lmd );

	BW::string keys[ 8 ];
	long long values[ 8 ];
	int numPairs = 0;
	readCorrelationPairs( lmd, keys, values, 8, numPairs );

	CHECK_EQUAL( 1, numPairs );
	CHECK_EQUAL( LogCorrelation::MAX_KEY_LENGTH, keys[ 0 ].length() );
	CHECK_EQUAL( 0, strncmp( pLongKey, keys[ 0 ].c_str(),
		LogCorrelation::MAX_KEY_LENGTH ) );
	CHECK_EQUAL( 5, values[ 0 ] );

	LogCorrelation::clearAll();
}


TEST( LogCorrelation_OverflowDropped )
{
	LogCorrelation::clearAll();

	char key[ 16 ];
	for (int i = 0; i <= LogCorrelation::MAX_FIELDS; ++i)
	{
		bw_snprintf( key, sizeof( key ), "key%d", i );
		LogCorrelation::set( key, i );
	}

	LogMetaData lmd;
	LogCorrelation::appendTo( lmd );

	BW::string keys[ 16 ];
	long long values[ 16 ];
	int numPairs = 0;
	readCorrelationPairs( lmd, keys, values, 16, numPairs );

	CHECK_EQUAL( LogCorrelation::MAX_FIELDS, numPairs );
	// The first fields survive, the excess is dropped.
	CHECK_EQUAL( 0, strcmp( "key0", keys[ 0 ].c_str() ) );
	CHECK_EQUAL( 0, strcmp( "key7", keys[ LogCorrelation::MAX_FIELDS - 1 ].
		c_str() ) );

	LogCorrelation::clearAll();
}


TEST( LogCorrelation_ScopedRestore )
{
	LogCorrelation::clearAll();

	LogCorrelation::set( "entityID", 1 );

	{
		ScopedLogCorrelation scope;

		LogCorrelation::set( "entityID", 2 );
		LogCorrelation::set( "channel", 3 );
		CHECK( LogCorrelation::hasFields() );

		LogMetaData inner;
		LogCorrelation::appendTo( inner );

		BW::string keys[ 8 ];
		long long values[ 8 ];
		int numPairs = 0;
		readCorrelationPairs( inner, keys, values, 8, numPairs );

		CHECK_EQUAL( 2, numPairs );
		CHECK_EQUAL( 2, values[ 0 ] );
		CHECK_EQUAL( 3, values[ 1 ] );
	}

	// The outer context is intact after the scope.
	LogMetaData outer;
	LogCorrelation::appendTo( outer );

	BW::string keys[ 8 ];
	long long values[ 8 ];
	int numPairs = 0;
	readCorrelationPairs( outer, keys, values, 8, numPairs );

	CHECK_EQUAL( 1, numPairs );
	CHECK_EQUAL( 0, strcmp( "entityID", keys[ 0 ].c_str() ) );
	CHECK_EQUAL( 1, values[ 0 ] );

	LogCorrelation::clearAll();
}


BW_END_NAMESPACE

// test_log_meta_data.cpp
