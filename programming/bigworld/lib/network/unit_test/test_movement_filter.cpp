#include "pch.hpp"

#include "test_filter_interfaces.hpp"


BW_BEGIN_NAMESPACE


// copyState() gives the subclass its chance first: a tryCopyState() that
// returns true stops the copy dead, with no fallback input replayed.
TEST( MovementFilter_copyStateUsesTryCopyState )
{
	TestFilterEnvironment environment;

	TestMovementFilter source( environment );
	source.setLastInput( 1.5, 10, 20, Position3D( 1.f, 2.f, 3.f ),
		Vector3( 0.5f, 0.f, 0.f ),
		Direction3D( Vector3( 0.f, 0.f, 1.f ) ) );

	// This one claims the copy.
	TestMovementFilter target( environment, /* tryCopyState */ true );

	target.copyState( source );

	// tryCopyState was consulted exactly once, and because it said yes
	// the fallback never ran - no input was replayed.
	CHECK_EQUAL( 1, target.tryCopyStateCalls() );
	CHECK_EQUAL( 0, target.inputCalls() );
}


// The documented fallback: when the subclass declines, the last input
// of the source filter is replayed as this filter's first input, with
// every argument carried across unchanged.
TEST( MovementFilter_copyStateReplaysLastInput )
{
	TestFilterEnvironment environment;

	TestMovementFilter source( environment );
	source.setLastInput( 12.25, 111, 222, Position3D( 7.f, 8.f, 9.f ),
		Vector3( 0.25f, 0.5f, 0.75f ),
		Direction3D( Vector3( 0.1f, 0.2f, 0.3f ) ) );

	TestMovementFilter target( environment, /* tryCopyState */ false );

	target.copyState( source );

	// The subclass was asked, declined, and the fallback ran.
	CHECK_EQUAL( 1, target.tryCopyStateCalls() );
	CHECK_EQUAL( 1, target.inputCalls() );

	CHECK_EQUAL( 12.25, target.lastTime() );
	CHECK_EQUAL( 111, int( target.lastSpaceID() ) );
	CHECK_EQUAL( 222, int( target.lastVehicleID() ) );
	CHECK_EQUAL( 7.f, target.lastPosition().x );
	CHECK_EQUAL( 8.f, target.lastPosition().y );
	CHECK_EQUAL( 9.f, target.lastPosition().z );
	CHECK_EQUAL( 0.25f, target.lastPositionError().x );
	CHECK_EQUAL( 0.5f, target.lastPositionError().y );
	CHECK_EQUAL( 0.75f, target.lastPositionError().z );
	CHECK_CLOSE( 0.3f, target.lastDirection().yaw, 0.0001f );
}


// A source with nothing to give makes the fallback a no-op: the
// subclass was still consulted, but there is no sample to replay, so
// no input reaches this filter and its own state is left alone.
TEST( MovementFilter_copyStateWithNoLastInput )
{
	TestFilterEnvironment environment;

	TestMovementFilter source( environment, /* tryCopyState */ false,
		/* hasLastInput */ false );

	TestMovementFilter target( environment, /* tryCopyState */ false );

	target.copyState( source );

	CHECK_EQUAL( 1, target.tryCopyStateCalls() );
	CHECK_EQUAL( 0, target.inputCalls() );
}


// All four FilterEnvironment delegations on MovementFilter forward
// verbatim, and they are public here (unlike FilterHelper's, which are
// protected).
TEST( MovementFilter_forwardsToEnvironment )
{
	TestFilterEnvironment environment;
	TestMovementFilter filter( environment );

	// filterDropPoint.
	Position3D land;
	Vector3 normal;
	CHECK( filter.filterDropPoint( 42, Position3D( 4.f, 5.f, 6.f ), land,
		&normal ) );
	CHECK_EQUAL( 1, environment.dropCalls() );
	CHECK_EQUAL( 42, environment.lastDropSpace() );
	CHECK_EQUAL( 5.f, environment.lastFall().y );
	CHECK_EQUAL( 1.f, land.x );
	CHECK_EQUAL( 2.f, land.y );
	CHECK_EQUAL( 3.f, land.z );
	CHECK_EQUAL( 0.f, normal.x );
	CHECK_EQUAL( 1.f, normal.y );

	// The ground normal is optional.
	CHECK( filter.filterDropPoint( 43, Position3D(), land ) );
	CHECK_EQUAL( 2, environment.dropCalls() );
	CHECK( environment.lastGroundNormal() == NULL );

	// resolveOnGroundPosition: the environment's return value and its
	// out-parameters both come back.
	Position3D position;
	bool isOnGround = true;
	CHECK( !filter.resolveOnGroundPosition( position, isOnGround ) );
	CHECK_EQUAL( 1, environment.groundCalls() );
	CHECK_EQUAL( 9.f, position.x );
	CHECK_EQUAL( 7.f, position.z );
	CHECK( !isOnGround );

	// transformIntoCommon / transformFromCommon.
	Position3D moved;
	Direction3D rotated;
	filter.transformIntoCommon( 100, 200, moved, rotated );
	CHECK_EQUAL( 1, environment.intoCalls() );
	CHECK_EQUAL( 100, environment.lastIntoSpace() );
	CHECK_EQUAL( 200, environment.lastIntoVehicle() );
	CHECK_EQUAL( 4.f, moved.x );
	CHECK_EQUAL( 6.f, moved.z );

	filter.transformFromCommon( 101, 201, moved, rotated );
	CHECK_EQUAL( 1, environment.fromCalls() );
	CHECK_EQUAL( 101, environment.lastFromSpace() );
	CHECK_EQUAL( 201, environment.lastFromVehicle() );
	CHECK_CLOSE( 0.3f, rotated.yaw, 0.0001f );
}

BW_END_NAMESPACE

// test_movement_filter.cpp
