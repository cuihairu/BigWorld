#include "pch.hpp"

#include "cstdmf/string_builder.hpp"
#include "cstdmf/bw_string_ref.hpp"


BW_BEGIN_NAMESPACE

TEST( StringBuilder_externalBuffer )
{
	char buffer[ 16 ];

	StringBuilder sb( buffer, sizeof( buffer ) );

	CHECK_EQUAL( 0u, sb.length() );
	CHECK_EQUAL( BW::string( "" ), BW::string( sb.string() ) );

	sb.append( "hello" );

	CHECK_EQUAL( 5u, sb.length() );
	CHECK_EQUAL( BW::string( "hello" ), BW::string( sb.string() ) );
}


TEST( StringBuilder_internalBuffer )
{
	StringBuilder sb( 32 );

	sb.append( "abc" );

	CHECK_EQUAL( 3u, sb.length() );
	CHECK_EQUAL( BW::string( "abc" ), BW::string( sb.string() ) );
}


TEST( StringBuilder_appendChar )
{
	StringBuilder sb( 16 );

	sb.append( 'a' );
	sb.append( 'b' );
	sb.append( 'c' );

	CHECK_EQUAL( 3u, sb.length() );
	CHECK_EQUAL( BW::string( "abc" ), BW::string( sb.string() ) );

	// Non const and const indexing.
	CHECK_EQUAL( 'b', sb[ 1 ] );
	sb[ 1 ] = 'B';
	CHECK_EQUAL( 'B', sb[ 1 ] );
	CHECK_EQUAL( BW::string( "aBc" ), BW::string( sb.string() ) );
}


TEST( StringBuilder_appendInt )
{
	StringBuilder sb( 32 );

	sb.append( 0 );
	sb.append( -123 );
	sb.append( 456 );

	CHECK_EQUAL( BW::string( "0-123456" ), BW::string( sb.string() ) );
}


TEST( StringBuilder_appendf )
{
	StringBuilder sb( 64 );

	sb.appendf( "%s has %d %s", "fred", 3, "apples" );

	CHECK_EQUAL( BW::string( "fred has 3 apples" ), BW::string( sb.string() ) );
}


TEST( StringBuilder_vappendf )
{
	StringBuilder sb( 64 );

	sb.append( "x" );
	sb.appendf( "y%dz", 7 );

	CHECK_EQUAL( BW::string( "xy7z" ), BW::string( sb.string() ) );
}


TEST( StringBuilder_appendStringRef )
{
	BW::string str( "StringRef content" );
	StringRef ref( str );

	StringBuilder sb( 64 );

	sb.append( ref );

	CHECK_EQUAL( str, BW::string( sb.string() ) );
}


TEST( StringBuilder_truncation )
{
	char buffer[ 8 ];

	StringBuilder sb( buffer, sizeof( buffer ) );

	sb.append( "0123456789" ); // Only 7 characters fit.

	CHECK_EQUAL( 7u, sb.length() );
	CHECK_EQUAL( BW::string( "0123456" ), BW::string( sb.string() ) );

	// Appending to a full buffer does nothing.
	sb.append( 'x' );
	CHECK_EQUAL( 7u, sb.length() );

	sb.append( "more" );
	CHECK_EQUAL( 7u, sb.length() );

	CHECK( sb.isFull() );
	CHECK_EQUAL( 0u, sb.numFree() );
}


TEST( StringBuilder_appendIntTruncation )
{
	StringBuilder sb( 5 );

	sb.append( 123456 ); // Only 4 digits fit.

	CHECK_EQUAL( 4u, sb.length() );
	CHECK_EQUAL( BW::string( "1234" ), BW::string( sb.string() ) );
}


TEST( StringBuilder_clear )
{
	StringBuilder sb( 16 );

	sb.append( "temporary" );
	CHECK_EQUAL( 9u, sb.length() );

	sb.clear();

	CHECK_EQUAL( 0u, sb.length() );
	CHECK_EQUAL( BW::string( "" ), BW::string( sb.string() ) );

	sb.append( "new" );
	CHECK_EQUAL( BW::string( "new" ), BW::string( sb.string() ) );
}


TEST( StringBuilder_replace )
{
	StringBuilder sb( 32 );

	sb.append( "a/b/c" );
	sb.replace( '/', '\\' );

	CHECK_EQUAL( BW::string( "a\\b\\c" ), BW::string( sb.string() ) );
}


TEST( StringBuilder_copyTo )
{
	StringBuilder sb( 32 );

	sb.append( "copy me" );

	char dst[ 16 ];
	sb.copyTo( dst, sizeof( dst ) );

	CHECK_EQUAL( BW::string( "copy me" ), BW::string( dst ) );

	// Truncating copy.
	char small[ 4 ];
	sb.copyTo( small, sizeof( small ) );

	CHECK_EQUAL( BW::string( "cop" ), BW::string( small ) );
}


TEST( StringBuilder_numFree )
{
	StringBuilder sb( 11 );

	CHECK_EQUAL( 10u, sb.numFree() );

	sb.append( "abc" );

	CHECK_EQUAL( 7u, sb.numFree() );
	CHECK_EQUAL( 3u, sb.length() );
	CHECK( !sb.isFull() );
}

BW_END_NAMESPACE

// test_string_builder.cpp
