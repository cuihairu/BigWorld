// test_logger_message_forwarder.cpp
//
// The leg of log correlation that crosses a process boundary: a log record
// that carries metadata has to be handed to a metadata-aware message logger
// as one of two wire variants. Every field the thread-local LogCorrelation
// context holds (channelID set by UDPBundleProcessor::dispatchMessages, plus
// any EntityID/SpaceID a component sets) has to survive that choice, or the
// logger can no longer trace a log line back to the channel or entity that
// produced it.
//
// Neither variant re-serialises the record: both start with a byte-for-byte
// copy of the plain record and the surplus metadata is appended, because
// receivers stream all unparsed trailing bytes as metadata. The BW 2.9
// variant appends the raw JSON string, the 14.4 variant appends the metadata
// argument stream, which is also how a JSON-only record reaches a 14.4 logger
// (LogMetaData::addJSON streams it as a jsonString pair).
//
// The variant construction was inline in LoggerMessageForwarder::parseAndSend,
// where no unit test could reach it; it is now the standalone
// buildLoggerMetadataStreams() helper, which is what these tests exercise.

#include "pch.hpp"

#include "CppUnitLite2/src/CppUnitLite2.h"

#include "unit_test_lib/unit_test.hpp"

#include "network/logger_message_forwarder.hpp"

#include "cstdmf/log_meta_data.hpp"
#include "cstdmf/memory_stream.hpp"


BW_BEGIN_NAMESPACE


#if ENABLE_WATCHERS

namespace
{

const int MAX_PAIRS = 16;


/**
 *	Parses the metadata pairs appended after a plain record. Returns the
 *	number of pairs found, or -1 if the tail is not a well formed metadata
 *	stream. Only long long values are collected; pairs of any other value
 *	type are skipped. Asserting happens in the TEST bodies.
 */
int readLongLongPairs( const MemoryOStream & stream, int prefixLength,
		BW::string keys[], long long values[], int maxPairs )
{
	// The pairs start right after the plain record.
	MemoryIStream is( (const char *)const_cast< MemoryOStream * >(
		&stream )->data() + prefixLength, stream.size() - prefixLength );

	int numPairs = 0;

	while (is.remainingLength() > 0)
	{
		LogMetaData::MessageLoggerValueType keyType;
		LogMetaData::MessageLoggerValueType valueType;
		BW::string key;
		BW::string stringOrJSON;
		long long value;

		is >> keyType >> key >> valueType;

		if (valueType == LogMetaData::MLVT_LONG_LONG_INT)
		{
			is >> value;

			if (numPairs == maxPairs)
			{
				return -1;
			}

			keys[ numPairs ] = key;
			values[ numPairs ] = value;
			++numPairs;
		}
		else
		{
			is >> stringOrJSON;
		}
	}

	return is.remainingLength() > 0 ? -1 : numPairs;
}


/**
 *	A non-metadata record, as it would reach the logger: a message id, some
 *	padded payload and a message body.
 */
void makePlainRecord( MemoryOStream & os )
{
	os << (int)42;
	os << (uint16)7;
	os << BW::string( "record body" );
}


/**
 *	Compares the first prefixLength bytes of two streams.
 */
bool hasSamePrefix( const MemoryOStream & expected,
		const MemoryOStream & actual, int prefixLength )
{
	if (actual.size() < prefixLength)
	{
		return false;
	}

	return memcmp( const_cast< MemoryOStream * >( &expected )->data(),
		const_cast< MemoryOStream * >( &actual )->data(), prefixLength ) == 0;
}


/**
 *	The two correlation fields a component dispatching an entity message
 *	across a channel would hold.
 */
LogMetaData makeCorrelationMetaData()
{
	LogCorrelation::clearAll();
	LogCorrelation::set( "channelID", 77 );
	LogCorrelation::set( "entityID", 5 );

	LogMetaData metaData;
	LogCorrelation::appendTo( metaData );

	LogCorrelation::clearAll();

	return metaData;
}


/**
 *	A log record without metadata is sent to all loggers unmodified: no wire
 *	variant is built at all.
 */
TEST( LoggerMetadataStreams_noMetadataNoVariant )
{
	MemoryOStream os;
	makePlainRecord( os );

	LogMetaData metaData;

	std::unique_ptr< MemoryOStream > pV2_9;
	std::unique_ptr< MemoryOStream > pV14_4;

	CHECK( !buildLoggerMetadataStreams( &metaData, os, pV2_9, pV14_4 ) );
	CHECK( pV2_9 == NULL );
	CHECK( pV14_4 == NULL );
}


/**
 *	Correlation fields without a raw JSON string only reach the stream
 *	variant: the 2.9 variant stays a byte-for-byte copy of the plain record,
 *	the 14.4 variant carries the metadata pairs on top of it.
 */
TEST( LoggerMetadataStreams_correlationFieldsOnStreamVariant )
{
	MemoryOStream os;
	makePlainRecord( os );
	const int recordLength = os.size();

	LogMetaData metaData = makeCorrelationMetaData();

	std::unique_ptr< MemoryOStream > pV2_9;
	std::unique_ptr< MemoryOStream > pV14_4;

	CHECK( buildLoggerMetadataStreams( &metaData, os, pV2_9, pV14_4 ) );
	CHECK( pV2_9 != NULL );
	CHECK( pV14_4 != NULL );

	CHECK_EQUAL( recordLength, pV2_9->size() );
	CHECK( hasSamePrefix( os, *pV2_9, recordLength ) );

	CHECK_EQUAL( recordLength + metaData.pData()->size(), pV14_4->size() );
	CHECK( hasSamePrefix( os, *pV14_4, recordLength ) );

	BW::string keys[ MAX_PAIRS ];
	long long values[ MAX_PAIRS ];

	int numPairs = readLongLongPairs( *pV14_4, recordLength, keys, values,
		MAX_PAIRS );
	CHECK_EQUAL( 2, numPairs );

	CHECK_EQUAL( 0, strcmp( "channelID", keys[ 0 ].c_str() ) );
	CHECK_EQUAL( 77, values[ 0 ] );
	CHECK_EQUAL( 0, strcmp( "entityID", keys[ 1 ].c_str() ) );
	CHECK_EQUAL( 5, values[ 1 ] );
}


/**
 *	A raw JSON string only ever reaches the 2.9 variant: it is appended to a
 *	copy of the plain record, while the 14.4 variant carries the metadata
 *	argument stream instead (which includes a jsonString pair for the same
 *	payload).
 */
TEST( LoggerMetadataStreams_rawJSONOnStringVariant )
{
	MemoryOStream os;
	makePlainRecord( os );
	const int recordLength = os.size();

	const char s_json[] = "{ \"channelID\": 77 }";

	LogMetaData metaData;
	metaData.addJSON( s_json );
	LogCorrelation::set( "channelID", 77 );
	LogCorrelation::appendTo( metaData );
	LogCorrelation::clearAll();

	std::unique_ptr< MemoryOStream > pV2_9;
	std::unique_ptr< MemoryOStream > pV14_4;

	CHECK( buildLoggerMetadataStreams( &metaData, os, pV2_9, pV14_4 ) );
	CHECK( pV2_9 != NULL );
	CHECK( pV14_4 != NULL );

	CHECK( hasSamePrefix( os, *pV2_9, recordLength ) );
	CHECK( pV2_9->size() > recordLength );

	// The JSON arrives as a length-prefixed string right after the record
	// copy, nothing else.
	MemoryIStream is( (const char *)const_cast< MemoryOStream * >(
		pV2_9.get() )->data() + recordLength, pV2_9->size() - recordLength );
	BW::string parsedJSON;
	is >> parsedJSON;
	CHECK_EQUAL( 0, strcmp( s_json, parsedJSON.c_str() ) );
	CHECK_EQUAL( 0, is.remainingLength() );

	CHECK_EQUAL( recordLength + metaData.pData()->size(), pV14_4->size() );
	CHECK( hasSamePrefix( os, *pV14_4, recordLength ) );

	BW::string keys[ MAX_PAIRS ];
	long long values[ MAX_PAIRS ];

	int numPairs = readLongLongPairs( *pV14_4, recordLength, keys, values,
		MAX_PAIRS );
	CHECK_EQUAL( 1, numPairs );
	CHECK_EQUAL( 0, strcmp( "channelID", keys[ 0 ].c_str() ) );
	CHECK_EQUAL( 77, values[ 0 ] );
}

} // end anonymous namespace

#endif // ENABLE_WATCHERS


BW_END_NAMESPACE

// test_logger_message_forwarder.cpp
