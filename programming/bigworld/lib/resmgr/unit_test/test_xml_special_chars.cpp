#include "pch.hpp"

#include "cstdmf/cstdmf_init.hpp"
#include "cstdmf/bw_string.hpp"
#include "resmgr/xml_special_chars.hpp"
#include "test_harness.hpp"

#include <string.h>


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

/**
 *	XmlSpecialChars has no instance state, so the fixture only brings up a
 *	CStdMf (the warning messages in reduce() need a debug filter, exactly
 *	like the other resmgr tests).
 */
class Fixture : public ResMgrUnitTestHarness
{
public:
	Fixture() : ResMgrUnitTestHarness() { new CStdMf; }
	~Fixture() { delete CStdMf::pInstance(); }
};


/**
 *	reduce() edits its buffer in place, so it gets a writable copy.
 */
BW::string reduced( const char * text )
{
	char buf[ 512 ];
	strncpy( buf, text, sizeof( buf ) - 1 );
	buf[ sizeof( buf ) - 1 ] = 0;
	XmlSpecialChars::reduce( buf );
	return BW::string( buf );
}

} // end namespace (anonymous)


// expand() replaces the five XML meta characters and leaves everything
// else alone, so escaping is idempotent-safe: only a bare '&' becomes an
// entity, an already escaped sequence has its ';' escaped too.
TEST_F( Fixture, XmlSpecialChars_expandReplacesMetaCharacters )
{
	CHECK_EQUAL( BW::string( "plain text" ),
		XmlSpecialChars::expand( "plain text" ) );
	CHECK_EQUAL( BW::string( "a &amp; b" ), XmlSpecialChars::expand( "a & b" ) );
	CHECK_EQUAL( BW::string( "&lt;a&gt;" ), XmlSpecialChars::expand( "<a>" ) );
	CHECK_EQUAL( BW::string( "&quot;q&quot;" ),
		XmlSpecialChars::expand( "\"q\"" ) );
	CHECK_EQUAL( BW::string( "&apos;" ), XmlSpecialChars::expand( "'" ) );

	CHECK_EQUAL( BW::string( "&lt;a href=&quot;x&quot;&gt;Tom &amp; "
		"Jerry&lt;/a&gt;" ),
		XmlSpecialChars::expand( "<a href=\"x\">Tom & Jerry</a>" ) );

	// An already escaped ampersand is escaped again - expand() is a pure
	// character translation and knows nothing about existing entities.
	CHECK_EQUAL( BW::string( "&amp;lt;" ), XmlSpecialChars::expand( "&lt;" ) );
}


// Bytes outside the ascii range become decimal character code entities.
TEST_F( Fixture, XmlSpecialChars_expandEncodesHighBytes )
{
	CHECK_EQUAL( BW::string( "caf&#233;" ),
		XmlSpecialChars::expand( "caf\351" ) );			// 0xE9
	CHECK_EQUAL( BW::string( "&#128;&#255;" ),
		XmlSpecialChars::expand( "\200\377" ) );			// 0x80, 0xFF
	// Signed char arithmetic, not ASCII order: 0xFF must come out as 255.
	CHECK_EQUAL( BW::string( "&#255;" ), XmlSpecialChars::expand( "\377" ) );
	// 0x7F is the last ascii-ish byte and is left alone.
	CHECK_EQUAL( BW::string( "\177" ), XmlSpecialChars::expand( "\177" ) );
}


// The five named entities, mixed with ordinary text, in any order.
TEST_F( Fixture, XmlSpecialChars_reduceNamedEntities )
{
	CHECK_EQUAL( BW::string( "<a> & \"q'" ),
		reduced( "&lt;a&gt; &amp; &quot;q&apos;" ) );
	CHECK_EQUAL( BW::string( "no entities here" ),
		reduced( "no entities here" ) );
	CHECK_EQUAL( BW::string( "a<b>c" ), reduced( "a&lt;b&gt;c" ) );

	// Entities are only recognised up to the first ';' - text after one
	// keeps its literal form.
	CHECK_EQUAL( BW::string( "<a> & " ), reduced( "&lt;a&gt; & " ) );
}


// Character code entities in both notations. Note the parser takes the
// digits greedily up to the ';' and does not validate the result.
TEST_F( Fixture, XmlSpecialChars_reduceNumericEntities )
{
	CHECK_EQUAL( BW::string( "AB" ), reduced( "&#65;&#66;" ) );
	CHECK_EQUAL( BW::string( "AB" ), reduced( "&#x41;&#x42;" ) );
	CHECK_EQUAL( BW::string( "a b" ), reduced( "a&#32;b" ) );
	// The recognised range is 1..255; the boundary values still apply.
	CHECK_EQUAL( BW::string( "\1\377" ), reduced( "&#1;&#255;" ) );
	// Hex digits may be either case, but the marker may not: only a lower
	// case 'x' selects the hex branch, so "&#X42;" falls through to atoi(),
	// reads no digits at all and lands on the NUL code of the range check.
	CHECK_EQUAL( BW::string( "B" ), reduced( "&#x42;" ) );
	CHECK_EQUAL( BW::string( "J" ), reduced( "&#x4a;" ) );
	CHECK_EQUAL( BW::string( "A" ), reduced( "&#x41;&#X42;" ) );
}


// Out of range character codes are warned about but still substituted -
// and because 0 and 256 both truncate to a NUL byte in the target char,
// the buffer is cut short there. Pinned as the current behaviour: the
// warning is swallowed by the test DebugFilter, so the truncated string is
// all that is observable.
TEST_F( Fixture, XmlSpecialChars_reduceOutOfRangeCodesTruncate )
{
	CHECK_EQUAL( BW::string( "" ), reduced( "&#0;" ) );
	CHECK_EQUAL( BW::string( "A" ), reduced( "A&#0;B" ) );
	CHECK_EQUAL( BW::string( "A" ), reduced( "A&#256;B" ) );
	CHECK_EQUAL( BW::string( "A" ), reduced( "A&#xZZ;B" ) );
}


// Anything the parser does not recognise is left in place, untouched.
TEST_F( Fixture, XmlSpecialChars_reduceLeavesInvalidSequences )
{
	// A lone ampersand, and an ampersand immediately closed.
	CHECK_EQUAL( BW::string( "a & b" ), reduced( "a & b" ) );
	CHECK_EQUAL( BW::string( "&;" ), reduced( "&;" ) );
	// Unknown entity name.
	CHECK_EQUAL( BW::string( "&foo;" ), reduced( "&foo;" ) );
	// A recognised entity next to an unrecognised one: only the recognised
	// one is folded, the invalid run is left exactly as it was.
	CHECK_EQUAL( BW::string( "<a> &foo; " ), reduced( "&lt;a&gt; &foo; " ) );
	// A named entity with no trailing semicolon is still folded: the scan
	// stops at the end of the string rather than rejecting the sequence.
	CHECK_EQUAL( BW::string( "<" ), reduced( "&lt" ) );

	// An unterminated sequence of more than 31 characters is rejected
	// wholesale, so the whole run survives.
	BW::string longRun = "&";
	for (int i = 0; i < 40; ++i)
	{
		longRun += "a";
	}
	CHECK_EQUAL( longRun, reduced( longRun.c_str() ) );
}


// expand() and reduce() are inverse on every character expand() produces.
TEST_F( Fixture, XmlSpecialChars_expandReduceRoundTrip )
{
	static const char * samples[] =
	{
		"plain",
		"<tag attr='value'>body & more</tag>",
		"caf\351 au lait",
		"a &amp; b",
		"\"\351\"",
		"line1\nline2\ttabbed",
	};

	for (size_t i = 0; i < sizeof( samples ) / sizeof( samples[ 0 ] ); ++i)
	{
		const BW::string original( samples[ i ] );
		const BW::string escaped = XmlSpecialChars::expand( original.c_str() );
		CHECK_EQUAL( original, reduced( escaped.c_str() ) );
		// Escaping twice is not idempotent (the '&' of the first entity
		// is escaped again), but reducing the double escape returns the
		// single escape - the escaping is a lossless bijection.
		const BW::string doubleEscaped =
			XmlSpecialChars::expand( escaped.c_str() );
		CHECK_EQUAL( escaped, reduced( doubleEscaped.c_str() ) );
	}
}

BW_END_NAMESPACE

// test_xml_special_chars.cpp
