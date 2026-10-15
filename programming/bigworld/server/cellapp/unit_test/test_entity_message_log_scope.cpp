// Python.h must come before any standard headers (see lib/script/first_include.hpp).
#include "script/first_include.hpp"

#include "pch.hpp"

#include "../entity_message_log_scope.hpp"

#include "cstdmf/log_meta_data.hpp"

#include <string.h>


BW_BEGIN_NAMESPACE


// Snapshots the thread-local correlation context.
static int snapshotFields( LogCorrelation::Field * pFields )
{
	int count = 0;
	LogCorrelation::save( pFields, count );
	return count;
}


// Looks a field up in a snapshot taken by LogCorrelation::save.
static bool findField( const LogCorrelation::Field * pFields, int count,
		const char * pKey, long long & value )
{
	for (int i = 0; i < count; ++i)
	{
		if (strcmp( pFields[ i ].key, pKey ) == 0)
		{
			value = pFields[ i ].value;
			return true;
		}
	}

	return false;
}


/**
 *	A message for an entity that is not in this cellapp (or has been
 *	destroyed) correlates the logs by entity only.
 */
TEST( EntityMessageLogScope_EntityIDOnly )
{
	LogCorrelation::clearAll();

	{
		EntityMessageLogScope scope( 42 );

		LogCorrelation::Field fields[ LogCorrelation::MAX_FIELDS ];
		const int count = snapshotFields( fields );
		long long value = 0;

		CHECK_EQUAL( 1, count );
		CHECK( findField( fields, count, "entityID", value ) );
		CHECK_EQUAL( 42, value );
		CHECK( !findField( fields, count, "spaceID", value ) );
	}

	CHECK( !LogCorrelation::hasFields() );
}


/**
 *	A message for a live entity correlates the logs by entity and space.
 */
TEST( EntityMessageLogScope_EntityIDAndSpaceID )
{
	LogCorrelation::clearAll();

	{
		EntityMessageLogScope scope( 42 );
		scope.setSpaceID( 7 );

		LogCorrelation::Field fields[ LogCorrelation::MAX_FIELDS ];
		const int count = snapshotFields( fields );
		long long value = 0;

		CHECK_EQUAL( 2, count );
		CHECK( findField( fields, count, "entityID", value ) );
		CHECK_EQUAL( 42, value );
		CHECK( findField( fields, count, "spaceID", value ) );
		CHECK_EQUAL( 7, value );
	}

	CHECK( !LogCorrelation::hasFields() );
}


/**
 *	The scope stacks with the channel correlation set by UDPBundleProcessor
 *	instead of replacing it, and both are restored on destruction.
 */
TEST( EntityMessageLogScope_RestoresOuterContext )
{
	LogCorrelation::clearAll();
	LogCorrelation::set( "channelID", 5 );

	{
		EntityMessageLogScope scope( 42 );
		scope.setSpaceID( 7 );

		LogCorrelation::Field fields[ LogCorrelation::MAX_FIELDS ];
		const int count = snapshotFields( fields );
		long long value = 0;

		CHECK_EQUAL( 3, count );
		CHECK( findField( fields, count, "channelID", value ) );
		CHECK_EQUAL( 5, value );
		CHECK( findField( fields, count, "entityID", value ) );
		CHECK_EQUAL( 42, value );
		CHECK( findField( fields, count, "spaceID", value ) );
		CHECK_EQUAL( 7, value );
	}

	LogCorrelation::Field fields[ LogCorrelation::MAX_FIELDS ];
	const int count = snapshotFields( fields );
	long long value = 0;

	CHECK_EQUAL( 1, count );
	CHECK( findField( fields, count, "channelID", value ) );
	CHECK_EQUAL( 5, value );

	LogCorrelation::clearAll();
}


/**
 *	A nested scope (say, a message forwarded on behalf of another entity)
 *	replaces the entityID it sets and restores the outer fields on
 *	destruction.
 */
TEST( EntityMessageLogScope_NestedScopes )
{
	LogCorrelation::clearAll();

	EntityMessageLogScope outer( 1 );
	outer.setSpaceID( 10 );

	{
		LogCorrelation::Field fields[ LogCorrelation::MAX_FIELDS ];
		long long value = 0;
		const int count = snapshotFields( fields );

		CHECK_EQUAL( 2, count );
		CHECK( findField( fields, count, "entityID", value ) );
		CHECK_EQUAL( 1, value );
		CHECK( findField( fields, count, "spaceID", value ) );
		CHECK_EQUAL( 10, value );
	}

	{
		EntityMessageLogScope inner( 2 );

		LogCorrelation::Field fields[ LogCorrelation::MAX_FIELDS ];
		long long value = 0;
		const int count = snapshotFields( fields );

		CHECK_EQUAL( 2, count );
		CHECK( findField( fields, count, "entityID", value ) );
		CHECK_EQUAL( 2, value );
		CHECK( findField( fields, count, "spaceID", value ) );
		CHECK_EQUAL( 10, value );
	}

	{
		LogCorrelation::Field fields[ LogCorrelation::MAX_FIELDS ];
		long long value = 0;
		const int count = snapshotFields( fields );

		CHECK_EQUAL( 2, count );
		CHECK( findField( fields, count, "entityID", value ) );
		CHECK_EQUAL( 1, value );
		CHECK( findField( fields, count, "spaceID", value ) );
		CHECK_EQUAL( 10, value );
	}

	LogCorrelation::clearAll();
}


// test_entity_message_log_scope.cpp

BW_END_NAMESPACE
