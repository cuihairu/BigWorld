#include "pch.hpp"

#include "cstdmf/base64.h"


BW_BEGIN_NAMESPACE

TEST( Base64_encode )
{
	// RFC 4648 test vectors
	CHECK_EQUAL( "", Base64::encode( "", 0 ) );
	CHECK_EQUAL( "Zg==", Base64::encode( "f", 1 ) );
	CHECK_EQUAL( "Zm8=", Base64::encode( "fo", 2 ) );
	CHECK_EQUAL( "Zm9v", Base64::encode( "foo", 3 ) );
	CHECK_EQUAL( "Zm9vYg==", Base64::encode( "foob", 4 ) );
	CHECK_EQUAL( "Zm9vYmE=", Base64::encode( "fooba", 5 ) );
	CHECK_EQUAL( "Zm9vYmFy", Base64::encode( "foobar", 6 ) );
}


TEST( Base64_encodeStringVariant )
{
	BW::string data( "foobar" );
	CHECK_EQUAL( "Zm9vYmFy", Base64::encode( data ) );

	BW::string empty;
	CHECK_EQUAL( "", Base64::encode( empty ) );
}


TEST( Base64_decodeBufferVariant )
{
	char buf[ 32 ];

	int len = Base64::decode( "Zm9vYmFy", buf, sizeof( buf ) );
	CHECK_EQUAL( 6, len );
	buf[ len ] = '\0';
	CHECK_EQUAL( BW::string( "foobar" ), BW::string( buf ) );

	len = Base64::decode( "Zg==", buf, sizeof( buf ) );
	CHECK_EQUAL( 1, len );
	buf[ len ] = '\0';
	CHECK_EQUAL( BW::string( "f" ), BW::string( buf ) );

	// Destination buffer not large enough fails.
	CHECK_EQUAL( -1, Base64::decode( "Zm9vYmFy", buf, 2 ) );
}


TEST( Base64_decodeStringVariant )
{
	BW::string out;

	CHECK( Base64::decode( "Zm9vYmFy", out ) );
	CHECK_EQUAL( BW::string( "foobar" ), out );

	CHECK( Base64::decode( "Zm9vYg==", out ) );
	CHECK_EQUAL( BW::string( "foob" ), out );

	CHECK( Base64::decode( "", out ) );
	CHECK_EQUAL( BW::string( "" ), out );

	// Invalid padding length should be rejected.
	CHECK( !Base64::decode( "Zm9vYmE", out ) );

	// Invalid characters should be rejected.
	CHECK( !Base64::decode( "Zm9*YmFy", out ) );
}


TEST( Base64_roundTrip )
{
	static const char testData[] =
		"0123456789abcdefghijklmnopqrstuvwxyz"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()_+";

	for (size_t len = 0; len < sizeof( testData ); ++len)
	{
		BW::string encoded = Base64::encode( testData, len );
		BW::string decoded;

		CHECK( Base64::decode( encoded, decoded ) );
		CHECK_EQUAL( len, decoded.size() );

		CHECK_EQUAL( 0, memcmp( testData, decoded.data(), len ) );
	}
}

BW_END_NAMESPACE

// test_base64.cpp
