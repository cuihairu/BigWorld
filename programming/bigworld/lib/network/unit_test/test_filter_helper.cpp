#include "pch.hpp"

#include "test_filter_interfaces.hpp"


BW_BEGIN_NAMESPACE


// Every FilterHelper operation forwards to the environment it was
// constructed with - arguments in order, output parameters passed
// through both ways, return values preserved.
TEST( FilterHelper_forwardsToEnvironment )
{
	TestFilterEnvironment environment;
	TestFilter filter( environment );

	// filterDropPoint: the fall position and space ID go in, the landing
	// point and the optional ground normal come back.
	Position3D land;
	Vector3 normal;
	CHECK( filter.dropPoint( 42, Position3D( 4.f, 5.f, 6.f ), land,
		&normal ) );
	CHECK_EQUAL( 1, environment.dropCalls() );
	CHECK_EQUAL( 42, environment.lastDropSpace() );
	CHECK_EQUAL( 4.f, environment.lastFall().x );
	CHECK_EQUAL( 5.f, environment.lastFall().y );
	CHECK_EQUAL( 6.f, environment.lastFall().z );
	CHECK_EQUAL( 1.f, land.x );
	CHECK_EQUAL( 2.f, land.y );
	CHECK_EQUAL( 3.f, land.z );
	CHECK_EQUAL( 0.f, normal.x );
	CHECK_EQUAL( 1.f, normal.y );

	// Without a ground normal pointer the call still reaches the
	// environment with NULL.
	CHECK( filter.dropPoint( 43, Position3D(), land ) );
	CHECK_EQUAL( 2, environment.dropCalls() );
	CHECK_EQUAL( 43, environment.lastDropSpace() );
	CHECK( environment.lastGroundNormal() == NULL );

	// resolveOnGroundPosition: position and the on-ground flag are
	// updated through the environment; its return value comes back.
	Position3D position;
	bool isOnGround = true;
	CHECK( !filter.onGround( position, isOnGround ) );
	CHECK_EQUAL( 1, environment.groundCalls() );
	CHECK_EQUAL( 9.f, position.x );
	CHECK_EQUAL( 8.f, position.y );
	CHECK_EQUAL( 7.f, position.z );
	CHECK( !isOnGround );

	// transformIntoCommon / transformFromCommon carry both ids through
	// and let the environment mutate the by-reference arguments.
	Position3D moved;
	Direction3D rotated;
	filter.into( 100, 200, moved, rotated );
	CHECK_EQUAL( 1, environment.intoCalls() );
	CHECK_EQUAL( 100, environment.lastIntoSpace() );
	CHECK_EQUAL( 200, environment.lastIntoVehicle() );
	CHECK_EQUAL( 6.f, moved.z );

	filter.from( 101, 201, moved, rotated );
	CHECK_EQUAL( 1, environment.fromCalls() );
	CHECK_EQUAL( 101, environment.lastFromSpace() );
	CHECK_EQUAL( 201, environment.lastFromVehicle() );
	CHECK_CLOSE( 0.3f, rotated.yaw, 0.0001f );
}

BW_END_NAMESPACE

// test_filter_helper.cpp
