#include "pch.hpp"

#include "cstdmf/debug.hpp"
#include "cstdmf/debug_filter.hpp"
#include "cstdmf/memory_stream.hpp"
#include "cstdmf/watcher.hpp"

BW_BEGIN_NAMESPACE

#if ENABLE_MSG_LOGGING

#define ENABLE_CRITICAL_MESSAGES 0

const bool shouldWriteToConsole = false;

TEST( OldMessageMacroFormat )
{
	DebugFilter::shouldWriteToConsole( shouldWriteToConsole );

#if 0

	HACK_MSG[ "DBMgr" ].
		meta( "key1", 1 ).
		meta( "key2", "value" ).
		write( "This was a test\n" );


	DebugFilter::instance().hasDevelopmentAssertions( true );
	printf( "==========\n\n\n\n=============\n" );
	MF_ASSERT_DEV( false );

	// TODO: remove this after testing
	DebugFilter::instance().hasDevelopmentAssertions( false );
	printf( "==========\n\n\n\n=============\n" );
	MF_ASSERT_DEV( false );
	DebugFilter::instance().hasDevelopmentAssertions( true );
	printf( "==========\n\n\n\n=============\n" );
	MF_ASSERT_DEV( false );
#endif


	// Old messages work

#define MSG_MACRO_WRAPPER( SEVERITY )				\
	SEVERITY( "Simple Format String\n" );			\
	SEVERITY( "More format: %s\n", "string" );		\
	SEVERITY( "%d %f %hd\n", 12, 32.f, 5 );			\
	SEVERITY( "blah %f %s\n", 32.f, "a string" );	\

	MSG_MACRO_WRAPPER( TRACE_MSG );
	MSG_MACRO_WRAPPER( DEBUG_MSG );
	MSG_MACRO_WRAPPER( INFO_MSG );
	MSG_MACRO_WRAPPER( NOTICE_MSG );
	MSG_MACRO_WRAPPER( WARNING_MSG );
	MSG_MACRO_WRAPPER( ERROR_MSG );
#if ENABLE_CRITICAL_MESSAGES
	MSG_MACRO_WRAPPER( CRITICAL_MSG );
#endif // ENABLE_CRITICAL_MESSAGES
	MSG_MACRO_WRAPPER( HACK_MSG );
	MSG_MACRO_WRAPPER( ASSET_MSG );

#undef MSG_MACRO_WRAPPER

	DebugFilter::shouldWriteToConsole( false );
}


TEST( NewMessageMacrosWithCategory )
{
	DebugFilter::shouldWriteToConsole( shouldWriteToConsole );

	// New support with categories

#define MSG_MACRO_WRAPPER( SEVERITY )			\
	SEVERITY[ "UnitTestDebugWithCategory" ]( "Simple Format String\n" );	\
	SEVERITY[ "UnitTestDebugWithCategory" ]( "More format: %s\n", "string" ); \
	SEVERITY[ "UnitTestDebugWithCategory" ]( "%d %f %hd\n", 12, 32.f, 5 );

	MSG_MACRO_WRAPPER( TRACE_MSG );
	MSG_MACRO_WRAPPER( DEBUG_MSG );
	MSG_MACRO_WRAPPER( INFO_MSG );
	MSG_MACRO_WRAPPER( NOTICE_MSG );
	MSG_MACRO_WRAPPER( WARNING_MSG );
	MSG_MACRO_WRAPPER( ERROR_MSG );
#if ENABLE_CRITICAL_MESSAGES
	MSG_MACRO_WRAPPER( CRITICAL_MSG );
#endif // ENABLE_CRITICAL_MESSAGES
	MSG_MACRO_WRAPPER( HACK_MSG );
	MSG_MACRO_WRAPPER( ASSET_MSG );


#undef MSG_MACRO_WRAPPER
	DebugFilter::shouldWriteToConsole( false );
}


TEST( NewMessageMacrosWithCategoryUsingMetaData )
{

	DebugFilter::shouldWriteToConsole( shouldWriteToConsole );

#define MSG_MACRO_WRAPPER( SEVERITY )			\
	SEVERITY[ "UnitTestDebugWithMetaData" ].	\
		meta( "int", intValue ).				\
		meta( "long", longValue ).				\
		meta( "float", floatValue ).			\
		meta( "double", doubleValue ).			\
		meta( "str", strValue ).				\
		meta( "unsigned", unsignedValue ).		\
		write( "Simple format string\n" )

	int intValue = -21;
	long long int longValue = 123678213;
	double doubleValue = -12.372;
	float floatValue = -48.24f;
	const char * strValue = "my first string";
	unsigned int unsignedValue = 23;


	MSG_MACRO_WRAPPER( TRACE_MSG );
	MSG_MACRO_WRAPPER( DEBUG_MSG );
	MSG_MACRO_WRAPPER( INFO_MSG );
	MSG_MACRO_WRAPPER( NOTICE_MSG );
	MSG_MACRO_WRAPPER( WARNING_MSG );
	MSG_MACRO_WRAPPER( ERROR_MSG );
#if ENABLE_CRITICAL_MESSAGES
//	MSG_MACRO_WRAPPER( CRITICAL_MSG );
#endif // ENABLE_CRITICAL_MESSAGES
	MSG_MACRO_WRAPPER( HACK_MSG );
	MSG_MACRO_WRAPPER( ASSET_MSG );

#undef MSG_MACRO_WRAPPER

	DebugFilter::shouldWriteToConsole( false );

}


TEST( NewMessageMacrosWithSource )
{
	DebugFilter::shouldWriteToConsole( shouldWriteToConsole );

#define MSG_MACRO_WRAPPER( SEVERITY )			\
	SEVERITY[ "UnitTestDebugWithSource" ].		\
		source( MESSAGE_SOURCE_CPP ).			\
		write( "Simple format string\n" );		\
	SEVERITY[ "UnitTestDebugWithSource" ].		\
		source( MESSAGE_SOURCE_SCRIPT ).		\
		write( "Simple format string\n" );		\

	MSG_MACRO_WRAPPER( TRACE_MSG );
	MSG_MACRO_WRAPPER( DEBUG_MSG );
	MSG_MACRO_WRAPPER( INFO_MSG );
	MSG_MACRO_WRAPPER( NOTICE_MSG );
	MSG_MACRO_WRAPPER( WARNING_MSG );
	MSG_MACRO_WRAPPER( ERROR_MSG );
#if ENABLE_CRITICAL_MESSAGES
	MSG_MACRO_WRAPPER( CRITICAL_MSG );
#endif // ENABLE_CRITICAL_MESSAGES
	MSG_MACRO_WRAPPER( HACK_MSG );
	MSG_MACRO_WRAPPER( ASSET_MSG );

#undef MSG_MACRO_WRAPPER

	DebugFilter::shouldWriteToConsole( false );

}


TEST( CritWithCategory )
{

	DebugFilter::shouldWriteToConsole( shouldWriteToConsole );

#if ENABLE_CRITICAL_MESSAGES
	CRITICAL_MSG_WITH_CATEGORY( "MyCategoryEh" )( "This is my message\n" );
#endif // ENABLE_CRITICAL_MESSAGES

	DebugFilter::shouldWriteToConsole( false );

}


TEST( ImplicitBacktraceMacros )
{

	DebugFilter::shouldWriteToConsole( shouldWriteToConsole );

// TODO: these should
// 1) be removed
// 2) be made to work with the re-write of the messageBackTrace

	TRACE_BACKTRACE();
	DEBUG_BACKTRACE();
	INFO_BACKTRACE();
	NOTICE_BACKTRACE();
	WARNING_BACKTRACE();
	ERROR_BACKTRACE();
#if ENABLE_CRITICAL_MESSAGES
	CRITICAL_BACKTRACE();
#endif // ENABLE_CRITICAL_MESSAGES
	HACK_BACKTRACE();
// TODO: this isn't implemented, should it be?
//	ASSET_BACKTRACE();

#undef MSG_MACRO_WRAPPER

	DebugFilter::shouldWriteToConsole( false );


}

#if ENABLE_WATCHERS

/**
 *	The global filter threshold is writable through the root watcher, so a
 *	running process can be re-leveled at runtime without a restart.
 */
TEST( DebugFilter_filterThresholdWatcher )
{
	DebugFilter::shouldWriteToConsole( shouldWriteToConsole );

	const DebugMessagePriority previous = DebugFilter::instance().filterThreshold();
	DebugFilter::instance().filterThreshold( MESSAGE_PRIORITY_TRACE );

	Watcher & root = Watcher::rootWatcher();
	BW::string desc;
	BW::string value;
	Watcher::Mode mode = Watcher::WT_INVALID;

	CHECK( root.getAsString( NULL, "logger/filterThreshold", value, desc, mode ) );
	CHECK_EQUAL( BW::string( "0" ), value );

	CHECK( root.setFromString( NULL, "logger/filterThreshold", "4" ) );
	CHECK_EQUAL( MESSAGE_PRIORITY_WARNING,
		DebugFilter::instance().filterThreshold() );

	CHECK( root.getAsString( NULL, "logger/filterThreshold", value, desc, mode ) );
	CHECK_EQUAL( BW::string( "4" ), value );

	DebugFilter::instance().filterThreshold( previous );
	DebugFilter::shouldWriteToConsole( false );
}

#endif // ENABLE_WATCHERS

/**
 *	Sampling keeps one message in every N stamps; ratios of 0 and 1 keep
 *	everything. The stamp residue is the whole sampling decision.
 */
TEST( DebugFilter_isSampled )
{
	// disabled ratios keep everything
	CHECK( DebugFilter::isSampled( 0, 0 ) );
	CHECK( DebugFilter::isSampled( 12345, 1 ) );
	CHECK( DebugFilter::isSampled( 999999, 1 ) );

	// one in two
	CHECK( DebugFilter::isSampled( 0, 2 ) );
	CHECK( !DebugFilter::isSampled( 1, 2 ) );
	CHECK( DebugFilter::isSampled( 4, 2 ) );
	CHECK( !DebugFilter::isSampled( 3, 2 ) );

	// one in ten
	CHECK( DebugFilter::isSampled( 10, 10 ) );
	CHECK( !DebugFilter::isSampled( 11, 10 ) );
	CHECK( DebugFilter::isSampled( 90, 10 ) );
	CHECK( !DebugFilter::isSampled( 91, 10 ) );
}

#if ENABLE_WATCHERS

/**
 *	Per-category sampling ratios are writable through the root watcher, and
 *	category level filters still win over sampling.
 */
TEST( DebugFilter_categorySamplingWatcher )
{
	Watcher & root = Watcher::rootWatcher();
	BW::string desc;
	BW::string value;
	Watcher::Mode mode = Watcher::WT_INVALID;

	// A category registers itself on first use, which is what makes its
	// sampling ratio writable: the sampling watcher retunes entries rather
	// than creating them.
	CHECK( DebugFilter::instance().shouldAcceptCategory( "UnitTestSampling",
		MESSAGE_PRIORITY_TRACE ) );

	// a ratio written through the watcher reads back
	CHECK( root.setFromString( NULL, "logger/categorySampling/UnitTestSampling", "3" ) );
	CHECK( root.getAsString( NULL, "logger/categorySampling/UnitTestSampling",
		value, desc, mode ) );
	CHECK_EQUAL( BW::string( "3" ), value );

	// ratio 1 disables sampling: everything passes (the clock dependent
	// residue itself is covered by DebugFilter_isSampled)
	CHECK( root.setFromString( NULL, "logger/categorySampling/UnitTestSampling", "1" ) );
	CHECK( DebugFilter::instance().shouldAcceptCategory( "UnitTestSampling",
		MESSAGE_PRIORITY_TRACE ) );

	// level filters still win: INFO is under the category's WARNING level
	// no matter what the sampler says
	CHECK( root.setFromString( NULL,
		"logger/categorySupression/UnitTestSampling", "4" ) );
	CHECK( !DebugFilter::instance().shouldAcceptCategory( "UnitTestSampling",
		MESSAGE_PRIORITY_INFO ) );

	// restore: sampling off, category level back to trace
	CHECK( root.setFromString( NULL, "logger/categorySampling/UnitTestSampling", "0" ) );
	CHECK( root.setFromString( NULL,
		"logger/categorySupression/UnitTestSampling", "0" ) );
	CHECK( DebugFilter::instance().shouldAcceptCategory( "UnitTestSampling",
		MESSAGE_PRIORITY_INFO ) );
}

#endif // ENABLE_WATCHERS

#endif // ENABLE_MSG_LOGGING

BW_END_NAMESPACE

// test_debug.cpp
