#include "pch.hpp"

#include "cstdmf/debug_message_source.hpp"

#include <string.h>


BW_BEGIN_NAMESPACE

// messageSourceAsCString() maps a source index onto a display name and
// answers "(Unknown)" for anything outside the table. Note that
// CHECK_EQUAL would compare the returned pointers, not the strings, so
// these comparisons go through strcmp.
TEST( DebugMessageSource_knownSources )
{
	CHECK( strcmp( "C++", messageSourceAsCString( MESSAGE_SOURCE_CPP ) )
		== 0 );
	CHECK( strcmp( "Script",
		messageSourceAsCString( MESSAGE_SOURCE_SCRIPT ) ) == 0 );
}


// Everything past the last enum value is rejected, including the enum's
// own count and a negative index.
TEST( DebugMessageSource_outOfRangeIsUnknown )
{
	CHECK( strcmp( "(Unknown)",
		messageSourceAsCString( NUM_MESSAGE_SOURCE ) ) == 0 );
	CHECK( strcmp( "(Unknown)",
		messageSourceAsCString( DebugMessageSource( 12345 ) ) ) == 0 );
	CHECK( strcmp( "(Unknown)",
		messageSourceAsCString( DebugMessageSource( -1 ) ) ) == 0 );
}

BW_END_NAMESPACE

// test_debug_message_source.cpp
