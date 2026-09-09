#include "pch.hpp"

#include "cstdmf/timestamp.hpp"


BW_BEGIN_NAMESPACE

TEST( Timestamp_timestampIsIncreasing )
{
	uint64 last = timestamp();

	for (int i = 0; i < 100; ++i)
	{
		uint64 next = timestamp();
		CHECK( next >= last );
		last = next;
	}
}


TEST( Timestamp_stampsPerSecond )
{
	CHECK( stampsPerSecond() > 0 );
	CHECK( stampsPerSecondD() > 0.0 );

	CHECK( stampsPerSecond_rdtsc() > 0 );
	CHECK( stampsPerSecondD_rdtsc() > 0.0 );

	CHECK( stampsPerSecond_gettimeofday() > 0 );
	CHECK( stampsPerSecondD_gettimeofday() > 0.0 );
}


TEST( Timestamp_stampsToSeconds )
{
	// One second worth of stamps converts to roughly one second.
	uint64 sps = stampsPerSecond();

	CHECK_CLOSE( 1.0, stampsToSeconds( sps ), 0.01 );
	CHECK_CLOSE( 0.5, stampsToSeconds( sps / 2 ), 0.01 );
	CHECK_CLOSE( 2.0, stampsToSeconds( sps * 2 ), 0.02 );
	CHECK_CLOSE( 0.0, stampsToSeconds( 0 ), 0.000001 );
}


TEST( Timestamp_timeStampConversions )
{
	TimeStamp ts( stampsPerSecond() * 3 );

	CHECK_CLOSE( 3.0, ts.inSeconds(), 0.01 );

	TimeStamp fromSecs = TimeStamp::fromSeconds( 1.5 );
	CHECK_CLOSE( 1.5, fromSecs.inSeconds(), 0.01 );

	TimeStamp setSecs;
	setSecs.setInSeconds( 0.25 );
	CHECK_CLOSE( 0.25, setSecs.inSeconds(), 0.01 );
}


TEST( Timestamp_timeStampUtilities )
{
	CHECK_CLOSE( 4.0, TimeStamp::toSeconds( stampsPerSecond() * 4 ), 0.01 );

	// Round trip through seconds.
	TimeStamp original = TimeStamp::fromSeconds( 2.5 );
	CHECK_CLOSE( 2.5, TimeStamp::toSeconds( original ), 0.01 );
}


TEST( Timestamp_timeStampImplicitConversion )
{
	TimeStamp ts( 123 );

	// The class converts to and from uint64.
	uint64 value = ts;
	CHECK_EQUAL( 123u, value );

	TimeStamp other = value + 1;
	CHECK_EQUAL( 124u, other.stamp_ );
}


TEST( Timestamp_ageInStamps )
{
	// A timestamp from the past has a positive age.
	TimeStamp past( timestamp() - stampsPerSecond() );

	CHECK( past.ageInStamps() >= stampsPerSecond() );
	CHECK( past.ageInSeconds() >= 0.9 );
}

BW_END_NAMESPACE

// test_timestamp.cpp
