#include "pch.hpp"

#include "cstdmf/log_msg.hpp"

#include "cstdmf/debug.hpp"
#include "cstdmf/debug_filter.hpp"
#include "cstdmf/debug_message_callbacks.hpp"
#include "cstdmf/debug_message_priority.hpp"

#include "cstdmf/memory_stream.hpp"

//#include "cstdmf/bw_string.hpp"
//#include "cstdmf/stdmf_minimal.hpp"
//
//#include <float.h>
//#include <limits.h>

BW_BEGIN_NAMESPACE


#define CONSTRUCT_LOG_MSG( PRIORITY ) 			\
{												\
	LogMsg logMsg( PRIORITY );					\
	LogMsg * pLogMsg = new LogMsg( PRIORITY );	\
	delete pLogMsg;								\
}


TEST( BasicLogMsgConstruction )
{
		CONSTRUCT_LOG_MSG( MESSAGE_PRIORITY_TRACE );
		CONSTRUCT_LOG_MSG( MESSAGE_PRIORITY_DEBUG );
		CONSTRUCT_LOG_MSG( MESSAGE_PRIORITY_INFO );
		CONSTRUCT_LOG_MSG( MESSAGE_PRIORITY_NOTICE );
		CONSTRUCT_LOG_MSG( MESSAGE_PRIORITY_WARNING );
		CONSTRUCT_LOG_MSG( MESSAGE_PRIORITY_ERROR );
		CONSTRUCT_LOG_MSG( MESSAGE_PRIORITY_CRITICAL );
		CONSTRUCT_LOG_MSG( MESSAGE_PRIORITY_HACK );
		CONSTRUCT_LOG_MSG( MESSAGE_PRIORITY_ASSET );
}


#if ENABLE_MSG_LOGGING

namespace
{
static const char s_correlationMarker[] = "correlation test marker";

/**
 *	Captures the metadata of marker messages so tests can assert what the
 *	message callbacks received.
 */
class CorrelationCaptureCallback : public DebugMessageCallback
{
public:
	CorrelationCaptureCallback() : sawMetaData_( false ), numFields_( 0 ) {}

	virtual bool handleMessage( DebugMessagePriority /*messagePriority*/,
			const char * /*pCategory*/, DebugMessageSource /*messageSource*/,
			const LogMetaData & metaData, const char * pFormat,
			va_list /*argPtr*/ )
	{
		if (strstr( pFormat, s_correlationMarker ) == NULL)
		{
			return false;
		}

		MemoryOStream * pStream =
			const_cast< MemoryOStream * >( metaData.pData() );
		if (pStream == NULL)
		{
			return true;
		}

		MemoryIStream is( pStream->data(), pStream->size() );
		while ((is.remainingLength() > 0) && (numFields_ < MAX_FIELDS))
		{
			LogMetaData::MessageLoggerValueType keyType;
			LogMetaData::MessageLoggerValueType valueType;
			is >> keyType >> keys_[ numFields_ ] >> valueType >>
				values_[ numFields_ ];

			if ((keyType != LogMetaData::MLVT_STRING) ||
					(valueType != LogMetaData::MLVT_LONG_LONG_INT))
			{
				return true;
			}

			++numFields_;
		}

		sawMetaData_ = true;
		return true;
	}

	bool sawMetaData() const			{ return sawMetaData_; }
	int numFields() const				{ return numFields_; }
	const BW::string & key( int i ) const	{ return keys_[ i ]; }
	long long value( int i ) const		{ return values_[ i ]; }

private:
	static const int MAX_FIELDS = 8;

	bool sawMetaData_;
	int numFields_;
	BW::string keys_[ MAX_FIELDS ];
	long long values_[ MAX_FIELDS ];
};

} // anonymous namespace


/**
 *	The thread's correlation context is attached to every message written
 *	through the logging macros.
 */
TEST( LogMsg_AttachesCorrelationContext )
{
	LogCorrelation::clearAll();

	CorrelationCaptureCallback capture;
	DebugFilter::instance().addMessageCallback( &capture );

	LogCorrelation::set( "entityID", 1234 );
	LogCorrelation::set( "spaceID", 99 );

	INFO_MSG( s_correlationMarker );

	DebugFilter::instance().deleteMessageCallback( &capture );
	LogCorrelation::clearAll();

	CHECK( capture.sawMetaData() );
	CHECK_EQUAL( 2, capture.numFields() );
	CHECK_EQUAL( 0, strcmp( "entityID", capture.key( 0 ).c_str() ) );
	CHECK_EQUAL( 1234, capture.value( 0 ) );
	CHECK_EQUAL( 0, strcmp( "spaceID", capture.key( 1 ).c_str() ) );
	CHECK_EQUAL( 99, capture.value( 1 ) );
}


/**
 *	With no correlation context set, messages carry no metadata at all.
 */
TEST( LogMsg_NoCorrelationNoMetaData )
{
	LogCorrelation::clearAll();

	CorrelationCaptureCallback capture;
	DebugFilter::instance().addMessageCallback( &capture );

	INFO_MSG( s_correlationMarker );

	DebugFilter::instance().deleteMessageCallback( &capture );

	CHECK( !capture.sawMetaData() );
}

#endif // ENABLE_MSG_LOGGING

BW_END_NAMESPACE

// test_log_msg.cpp
