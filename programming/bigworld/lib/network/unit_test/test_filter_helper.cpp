#include "pch.hpp"

#include "connection/filter_environment.hpp"
#include "connection/filter_helper.hpp"


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

/**
 *	A FilterEnvironment that records every call and hands back
 * identifiable outputs.
 */
class TestFilterEnvironment : public FilterEnvironment
{
public:
	TestFilterEnvironment() :
		dropCalls_( 0 ), groundCalls_( 0 ), intoCalls_( 0 ),
		fromCalls_( 0 ),
		lastDropSpace_( 0 ), lastFall_(), lastGroundNormal_( NULL ),
		lastIntoSpace_( 0 ), lastIntoVehicle_( 0 ),
		lastFromSpace_( 0 ), lastFromVehicle_( 0 )
	{}

	virtual bool filterDropPoint( SpaceID spaceID,
			const Position3D & fall, Position3D & land,
			Vector3 * pGroundNormal )
	{
		++dropCalls_;
		lastDropSpace_ = spaceID;
		lastFall_ = fall;
		lastGroundNormal_ = pGroundNormal;

		land = Position3D( 1.f, 2.f, 3.f );
		if (pGroundNormal != NULL)
		{
			*pGroundNormal = Vector3( 0.f, 1.f, 0.f );
		}
		return true;
	}

	virtual bool resolveOnGroundPosition( Position3D & position,
			bool & isOnGround )
	{
		++groundCalls_;
		position = Position3D( 9.f, 8.f, 7.f );
		isOnGround = !isOnGround;
		return false;
	}

	virtual void transformIntoCommon( SpaceID spaceID, EntityID vehicleID,
			Position3D & position, Direction3D & direction )
	{
		++intoCalls_;
		lastIntoSpace_ = spaceID;
		lastIntoVehicle_ = vehicleID;
		position = Position3D( 4.f, 5.f, 6.f );
	}

	virtual void transformFromCommon( SpaceID spaceID, EntityID vehicleID,
			Position3D & position, Direction3D & direction )
	{
		++fromCalls_;
		lastFromSpace_ = spaceID;
		lastFromVehicle_ = vehicleID;
		direction = Direction3D( Vector3( 0.1f, 0.2f, 0.3f ) );
	}

	int dropCalls() const		{ return dropCalls_; }
	int groundCalls() const		{ return groundCalls_; }
	int intoCalls() const		{ return intoCalls_; }
	int fromCalls() const		{ return fromCalls_; }
	SpaceID lastDropSpace() const	{ return lastDropSpace_; }
	const Position3D & lastFall() const	{ return lastFall_; }
	const Vector3 * lastGroundNormal() const	{ return lastGroundNormal_; }
	SpaceID lastIntoSpace() const	{ return lastIntoSpace_; }
	EntityID lastIntoVehicle() const	{ return lastIntoVehicle_; }
	SpaceID lastFromSpace() const	{ return lastFromSpace_; }
	EntityID lastFromVehicle() const	{ return lastFromVehicle_; }

private:
	int dropCalls_;
	int groundCalls_;
	int intoCalls_;
	int fromCalls_;
	SpaceID lastDropSpace_;
	Position3D lastFall_;
	Vector3 * lastGroundNormal_;
	SpaceID lastIntoSpace_;
	EntityID lastIntoVehicle_;
	SpaceID lastFromSpace_;
	EntityID lastFromVehicle_;
};


/**
 *	The smallest concrete FilterHelper: the pure virtual filter
 * interface is irrelevant here, only the environment plumbing is.
 */
class TestFilter : public FilterHelper
{
public:
	TestFilter( FilterEnvironment & environment ) :
		FilterHelper( environment )
	{}

	virtual void reset( double time ) {}

	virtual void input( double time, SpaceID spaceID, EntityID vehicleID,
			const Position3D & position, const Vector3 & positionError,
			const Direction3D & dir )
	{}

	virtual double output( double time, SpaceID & rSpaceID,
			EntityID & rVehicleID, Position3D & rPosition,
			Vector3 & rVelocity, Direction3D & rDirection )
	{
		return 0.0;
	}

	// The environment helpers are protected - real filters call them
	// from inside input()/output(); these wrappers do the same for the
	// test.
	bool dropPoint( SpaceID spaceID, const Position3D & fall,
			Position3D & land, Vector3 * pGroundNormal = NULL )
	{
		return this->filterDropPoint( spaceID, fall, land,
			pGroundNormal );
	}

	bool onGround( Position3D & position, bool & isOnGround )
	{
		return this->resolveOnGroundPosition( position, isOnGround );
	}

	void into( SpaceID spaceID, EntityID vehicleID, Position3D & position,
			Direction3D & direction )
	{
		this->transformIntoCommon( spaceID, vehicleID, position,
			direction );
	}

	void from( SpaceID spaceID, EntityID vehicleID, Position3D & position,
			Direction3D & direction )
	{
		this->transformFromCommon( spaceID, vehicleID, position,
			direction );
	}
};

} // end namespace (anonymous)


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
