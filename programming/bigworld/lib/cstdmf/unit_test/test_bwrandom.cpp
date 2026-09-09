#include "pch.hpp"

#include "cstdmf/bwrandom.hpp"
#include "cstdmf/bw_vector.hpp"


BW_BEGIN_NAMESPACE

TEST( BWRandom_seededGeneratorsAreDeterministic )
{
	const uint32 seed = 1234;

	BWRandom r1( seed );
	BWRandom r2( seed );

	for (int i = 0; i < 1000; ++i)
	{
		uint32 v1 = r1();
		uint32 v2 = r2();
		CHECK_EQUAL( v1, v2 );
	}
}


TEST( BWRandom_differentSeedsDiffer )
{
	BWRandom r1( 1 );
	BWRandom r2( 2 );

	bool anyDifferent = false;
	for (int i = 0; i < 100; ++i)
	{
		if (r1() != r2())
		{
			anyDifferent = true;
			break;
		}
	}

	CHECK( anyDifferent );
}


TEST( BWRandom_arraySeedInit )
{
	const uint32 seeds[] = { 1u, 2u, 3u, 4u, 5u };

	BWRandom r1( seeds, sizeof( seeds ) / sizeof( seeds[0] ) );
	BWRandom r2( seeds, sizeof( seeds ) / sizeof( seeds[0] ) );

	for (int i = 0; i < 1000; ++i)
	{
		CHECK_EQUAL( r1(), r2() );
	}

	// Exercise a seed array larger than the internal state.
	BW::vector< uint32 > bigSeeds;
	for (uint32 i = 0; i < 1000; ++i)
	{
		bigSeeds.push_back( i * 2654435761u );
	}

	BWRandom r3( &bigSeeds.front(), bigSeeds.size() );
	BWRandom r4( &bigSeeds.front(), bigSeeds.size() );

	for (int i = 0; i < 1000; ++i)
	{
		CHECK_EQUAL( r3(), r4() );
	}
}


TEST( BWRandom_intRange )
{
	BWRandom r( 42 );

	for (int i = 0; i < 1000; ++i)
	{
		int v = r( 10, 20 );
		CHECK( v >= 10 );
		CHECK( v < 20 );
	}
}


TEST( BWRandom_intRangeReversedBounds )
{
	BWRandom r( 42 );

	for (int i = 0; i < 100; ++i)
	{
		int v = r( 20, 10 );
		CHECK( v >= 10 );
		CHECK( v < 20 );
	}
}


TEST( BWRandom_intRangeEqualBounds )
{
	BWRandom r( 42 );

	CHECK_EQUAL( 5, r( 5, 5 ) );
}


TEST( BWRandom_floatRange )
{
	BWRandom r( 42 );

	for (int i = 0; i < 1000; ++i)
	{
		float v = r( 1.5f, 2.5f );
		CHECK( v >= 1.5f );
		CHECK( v < 2.5f );
	}
}


TEST( BWRandom_floatRangeReversedBounds )
{
	BWRandom r( 42 );

	for (int i = 0; i < 100; ++i)
	{
		float v = r( 2.5f, 1.5f );
		CHECK( v >= 1.5f );
		CHECK( v < 2.5f );
	}
}


TEST( BWRandom_floatRangeEqualBounds )
{
	BWRandom r( 42 );

	CHECK_CLOSE( 3.25f, r( 3.25f, 3.25f ), 0.000001f );
}


TEST( BWRandom_defaultConstructedAndSingleton )
{
	// The default constructor seeds from the current time.
	BWRandom r;

	uint32 v = r();
	(void)v;

	// The shared instance is usable.
	uint32 shared = bw_random( 0, 100 );
	CHECK( (int)shared >= 0 );
	CHECK( (int)shared < 100 );
}


TEST( BWRandom_spread )
{
	// Basic sanity check that values cover both halves of the range.
	BWRandom r( 7 );

	int lowHalf = 0;
	int highHalf = 0;

	for (int i = 0; i < 1000; ++i)
	{
		uint32 v = r();
		if (v < 0x80000000u)
		{
			++lowHalf;
		}
		else
		{
			++highHalf;
		}
	}

	CHECK( lowHalf > 400 );
	CHECK( highHalf > 400 );
}

BW_END_NAMESPACE

// test_bwrandom.cpp
