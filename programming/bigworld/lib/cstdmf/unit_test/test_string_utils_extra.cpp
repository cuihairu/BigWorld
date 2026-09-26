#include "pch.hpp"

#include "cstdmf/string_utils.hpp"


BW_BEGIN_NAMESPACE

TEST( StringUtils_bw_hextodec )
{
	CHECK_EQUAL( 0, bw_hextodec( '0' ) );
	CHECK_EQUAL( 9, bw_hextodec( '9' ) );
	CHECK_EQUAL( 10, bw_hextodec( 'a' ) );
	CHECK_EQUAL( 15, bw_hextodec( 'f' ) );
	CHECK_EQUAL( 10, bw_hextodec( 'A' ) );
	CHECK_EQUAL( 15, bw_hextodec( 'F' ) );

	// Invalid characters return -1.
	CHECK_EQUAL( -1, bw_hextodec( 'g' ) );
	CHECK_EQUAL( -1, bw_hextodec( ' ' ) );
	CHECK_EQUAL( -1, bw_hextodec( '0' - 1 ) );
}


TEST( StringUtils_utf8ParseLeadByte )
{
	// ASCII characters are a single byte long.
	CHECK_EQUAL( 1u, utf8ParseLeadByte( 'a' ) );

	// Lead bytes of multibyte sequences report the sequence length.
	CHECK_EQUAL( 2u, utf8ParseLeadByte( (char)0xC2 ) );
	CHECK_EQUAL( 2u, utf8ParseLeadByte( (char)0xDF ) );
	CHECK_EQUAL( 3u, utf8ParseLeadByte( (char)0xE1 ) );
	CHECK_EQUAL( 3u, utf8ParseLeadByte( (char)0xEF ) );
	CHECK_EQUAL( 4u, utf8ParseLeadByte( (char)0xF1 ) );

	// Continuation bytes and invalid values return 0.
	CHECK_EQUAL( 0u, utf8ParseLeadByte( (char)0x80 ) );
	CHECK_EQUAL( 0u, utf8ParseLeadByte( (char)0xBF ) );
	CHECK_EQUAL( 0u, utf8ParseLeadByte( (char)0xFE ) );
	CHECK_EQUAL( 0u, utf8ParseLeadByte( (char)0xFF ) );
}


TEST( StringUtils_utf8IsContinuationByte )
{
	CHECK( !utf8IsContinuationByte( 'a' ) );
	CHECK( utf8IsContinuationByte( (char)0x80 ) );
	CHECK( utf8IsContinuationByte( (char)0xBF ) );
}


TEST( StringUtils_utf8StringLength )
{
	// Pure ASCII counts one per character.
	CHECK_EQUAL( 5u, utf8StringLength( "hello" ) );
	CHECK_EQUAL( 0u, utf8StringLength( "" ) );

	// A two byte sequence counts as a single character.
	CHECK_EQUAL( 2u, utf8StringLength( "a\xC3\xA9" ) );
}


TEST( StringUtils_findNextNonUtf8Char )
{
	// NULL means "scanned the whole string without finding an invalid byte".
	// That is the contract isValidUtf8Str() is built on, so it is what a
	// well-formed string must return.
	CHECK( findNextNonUtf8Char( "" ) == NULL );
	CHECK( findNextNonUtf8Char( "hello" ) == NULL );

	// A valid two byte sequence (e-acute) is still fully valid.
	CHECK( findNextNonUtf8Char( "caf\xC3\xA9" ) == NULL );

	// Otherwise a pointer to the first offending byte is returned.
	const char * bare = "\x80";				// continuation byte on its own
	CHECK( findNextNonUtf8Char( bare ) == bare );

	const char * truncated = "\xC3";		// lead byte, no continuation byte
	CHECK( findNextNonUtf8Char( truncated ) == truncated );

	// Only the first occurrence, and the offset has to be exact. The literal
	// is split so that 'c' is not swallowed into the \x escape.
	const char * mixed = "ab\x80" "cd";
	CHECK( findNextNonUtf8Char( mixed ) == mixed + 2 );

	// A multi-byte lead byte followed by a non-continuation byte is reported
	// at the lead byte, not at the byte that failed the check.
	const char * badCont = "\xE2\x41";
	CHECK( findNextNonUtf8Char( badCont ) == badCont );
}


TEST( StringUtils_isValidUtf8Str )
{
	CHECK( isValidUtf8Str( "" ) );
	CHECK( isValidUtf8Str( "plain ascii" ) );

	// A valid two byte sequence (e-acute).
	CHECK( isValidUtf8Str( "caf\xC3\xA9" ) );

	// Invalid: a continuation byte on its own.
	CHECK( !isValidUtf8Str( "\x80" ) );

	// Invalid: a lead byte without continuation bytes.
	CHECK( !isValidUtf8Str( "\xC3" ) );

	// Invalid: truncated three byte sequence.
	CHECK( !isValidUtf8Str( "\xE2\x82" ) );
}


TEST( StringUtils_toValidUtf8Str )
{
	// toValidUtf8Str() appends to the destination, it does not replace it.
	BW::string result;

	// Already valid strings are appended unchanged.
	toValidUtf8Str( "hello", result );
	CHECK_EQUAL( BW::string( "hello" ), result );

	// Invalid sequences are replaced. Each offending byte becomes its
	// "0xNN" hex form (the byteToString table in string_utils.cpp), not a
	// '?' placeholder.
	toValidUtf8Str( "\x80\x80", result );
	CHECK( isValidUtf8Str( result.c_str() ) );
	CHECK_EQUAL( BW::string( "hello0x800x80" ), result );

	// Truncated sequences are replaced the same way.
	BW::string result2;
	toValidUtf8Str( "abc\xC3", result2 );
	CHECK( isValidUtf8Str( result2.c_str() ) );
	CHECK_EQUAL( BW::string( "abc0xC3" ), result2 );

	// An empty source leaves the destination alone.
	BW::string result3( "keep" );
	toValidUtf8Str( "", result3 );
	CHECK_EQUAL( BW::string( "keep" ), result3 );
}


TEST( StringUtils_bw_format )
{
	CHECK_EQUAL( BW::string( "hello" ), bw_format( "hello" ) );
	CHECK_EQUAL( BW::string( "value: 42" ), bw_format( "value: %d", 42 ) );
	CHECK_EQUAL( BW::string( "a, b, c" ), bw_format( "%s, %s, %c", "a", "b", 'c' ) );
	CHECK_EQUAL( BW::string( "x=1.5" ), bw_format( "x=%.1f", 1.5 ) );
}


TEST( StringUtils_utf8WideRoundTrip )
{
	BW::wstring wide;

	CHECK( bw_utf8tow( "hello", wide ) );
	CHECK_EQUAL( 5u, wide.size() );
	CHECK( wide == BW::wstring( L"hello" ) );

	BW::string narrow;
	CHECK( bw_wtoutf8( wide.c_str(), narrow ) );
	CHECK_EQUAL( BW::string( "hello" ), narrow );

	// Non ASCII round trip.
	CHECK( bw_utf8tow( "caf\xC3\xA9", wide ) );
	CHECK_EQUAL( 4u, wide.size() );

	CHECK( bw_wtoutf8( wide.c_str(), narrow ) );
	CHECK_EQUAL( BW::string( "caf\xC3\xA9" ), narrow );
}


TEST( StringUtils_utf8towBufferOverloads )
{
	// Note the units on this platform: both size arguments are in *bytes*
	// (the destination is handed to iconv as a char buffer), and the output is
	// not NUL terminated - the caller supplies the exact source length.
	wchar_t dst[ 8 ];

	// Three ASCII bytes become three wide characters, i.e. 12 bytes, which
	// fits in dst's 8 * wchar_t.
	CHECK( bw_utf8tow( "abc", 3, dst, sizeof( dst ) ) );
	CHECK( dst[ 0 ] == L'a' );
	CHECK( dst[ 2 ] == L'c' );

	// A destination that is too small in bytes must fail rather than
	// silently truncate: three wide characters need 12 bytes.
	wchar_t tooSmall[ 2 ];
	CHECK( !bw_utf8tow( "abc", 3, tooSmall, sizeof( tooSmall ) ) );

	// And back to UTF-8. The wide source length is in bytes too (iconv counts
	// both sides in bytes), so three wide characters are 3 * sizeof(wchar_t),
	// and no terminator is written, so the length is explicit.
	char back[ 8 ];
	CHECK( bw_wtoutf8( dst, 3 * sizeof( wchar_t ), back, sizeof( back ) ) );
	CHECK_EQUAL( BW::string( "abc" ), BW::string( back, 3 ) );
}


TEST( StringUtils_ansiWideConversion )
{
	BW::wstring wide;

	bw_ansitow( "text", wide );
	CHECK( wide == BW::wstring( L"text" ) );
}


TEST( StringUtils_bw_MW_strcmp )
{
	CHECK_EQUAL( 0, bw_MW_strcmp( L"same", "same" ) );
	CHECK( bw_MW_strcmp( L"a", "b" ) < 0 );
	CHECK( bw_MW_strcmp( L"b", "a" ) > 0 );

	CHECK_EQUAL( 0, bw_MW_strcmp( "same", L"same" ) );
	CHECK( bw_MW_strcmp( "a", L"b" ) < 0 );
	CHECK( bw_MW_strcmp( "b", L"a" ) > 0 );

	CHECK_EQUAL( 0, bw_MW_stricmp( "Case", L"case" ) );
	CHECK( bw_MW_stricmp( "a", L"B" ) < 0 );
}

BW_END_NAMESPACE

// test_string_utils_extra.cpp
