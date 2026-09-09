#include "pch.hpp"

#include "cstdmf/unique_id.hpp"
#include "cstdmf/bw_map.hpp"


BW_BEGIN_NAMESPACE

TEST( UniqueID_defaultAndZero )
{
	UniqueID id;

	CHECK_EQUAL( 0u, id.getA() );
	CHECK_EQUAL( 0u, id.getB() );
	CHECK_EQUAL( 0u, id.getC() );
	CHECK_EQUAL( 0u, id.getD() );

	CHECK( id == UniqueID::zero() );
	CHECK_EQUAL( BW::string( "00000000.00000000.00000000.00000000" ),
		UniqueID::zero().toString() );
}


TEST( UniqueID_stringRoundTrip )
{
	const BW::string input = "81A9D1BF.4B8B622E.6F7081B3.0698330E";

	UniqueID id( input );

	CHECK_EQUAL( 0x81A9D1BFu, id.getA() );
	CHECK_EQUAL( 0x4B8B622Eu, id.getB() );
	CHECK_EQUAL( 0x6F7081B3u, id.getC() );
	CHECK_EQUAL( 0x0698330Eu, id.getD() );

	CHECK_EQUAL( input, id.toString() );
	CHECK_EQUAL( input, static_cast< BW::string >( id ) );
}


TEST( UniqueID_fromComponentValues )
{
	UniqueID id( 0x11223344u, 0x55667788u, 0x99AABBCCu, 0xDDEEFF00u );

	CHECK_EQUAL( BW::string( "11223344.55667788.99AABBCC.DDEEFF00" ),
		id.toString() );
}


TEST( UniqueID_emptyString )
{
	UniqueID id( BW::string( "" ) );

	// An empty string leaves the id at zero.
	CHECK( id == UniqueID::zero() );
}


TEST( UniqueID_invalidString )
{
	// Invalid parse falls back to zero and reports an error message.
	UniqueID id( BW::string( "not-a-unique-id" ) );

	CHECK( id == UniqueID::zero() );
}


TEST( UniqueID_isUniqueID )
{
	CHECK( UniqueID::isUniqueID( "81A9D1BF.4B8B622E.6F7081B3.0698330E" ) );
	CHECK( UniqueID::isUniqueID( "1.2.3.4" ) );

	CHECK( !UniqueID::isUniqueID( "" ) );
	CHECK( !UniqueID::isUniqueID( "zzzz.zzzz.zzzz.zzzz" ) );
}


TEST( UniqueID_equality )
{
	UniqueID a( 1, 2, 3, 4 );
	UniqueID b( 1, 2, 3, 4 );
	UniqueID c( 1, 2, 3, 5 );

	CHECK( a == b );
	CHECK( a != c );
	CHECK( !(a == c) );
	CHECK( !(a != b) );
}


TEST( UniqueID_ordering )
{
	UniqueID a( 1, 2, 3, 4 );
	UniqueID sameAsA( 1, 2, 3, 4 );
	UniqueID higherA( 2, 2, 3, 4 );
	UniqueID higherB( 1, 3, 3, 4 );
	UniqueID higherC( 1, 2, 4, 4 );
	UniqueID higherD( 1, 2, 3, 5 );

	CHECK( a < higherA );
	CHECK( a < higherB );
	CHECK( a < higherC );
	CHECK( a < higherD );

	CHECK( !(a < sameAsA) );
	CHECK( !(higherA < a) );
}


TEST( UniqueID_usableAsMapKey )
{
	BW::map< UniqueID, int > m;

	UniqueID k1( 1, 2, 3, 4 );
	UniqueID k2( 1, 2, 3, 5 );

	m[ k1 ] = 10;
	m[ k2 ] = 20;

	CHECK_EQUAL( 10, m[ k1 ] );
	CHECK_EQUAL( 20, m[ k2 ] );
	CHECK_EQUAL( 2u, m.size() );
}

BW_END_NAMESPACE

// test_unique_id.cpp
