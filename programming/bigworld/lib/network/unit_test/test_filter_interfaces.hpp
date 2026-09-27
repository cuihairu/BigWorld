#ifndef TEST_FILTER_INTERFACES_HPP
#define TEST_FILTER_INTERFACES_HPP

#include "connection/filter_environment.hpp"
#include "connection/filter_helper.hpp"
#include "connection/movement_filter.hpp"


BW_BEGIN_NAMESPACE

/**
 *	A FilterEnvironment that records every call and hands back
 *	identifiable outputs.
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
 *	interface is irrelevant here, only the environment plumbing is.
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


/**
 *	A concrete MovementFilter whose behaviour is entirely fixture
 *	driven, so that copyState()'s three paths can each be provoked.
 */
class TestMovementFilter : public MovementFilter
{
public:
	TestMovementFilter( FilterEnvironment & environment,
			bool tryCopyStateResult = false,
			bool hasLastInput = true ) :
		MovementFilter( environment ),
		tryCopyStateResult_( tryCopyStateResult ),
		hasLastInput_( hasLastInput ),
		resetCalls_( 0 ), inputCalls_( 0 ), tryCopyStateCalls_( 0 )
	{}

	/**
	 *	Stages the sample that getLastInput() hands back.
	 */
	void setLastInput( double time, SpaceID spaceID, EntityID vehicleID,
			const Position3D & position, const Vector3 & positionError,
			const Direction3D & direction )
	{
		cannedTime_ = time;
		cannedSpaceID_ = spaceID;
		cannedVehicleID_ = vehicleID;
		cannedPosition_ = position;
		cannedPositionError_ = positionError;
		cannedDirection_ = direction;
	}

	int resetCalls() const		{ return resetCalls_; }
	int inputCalls() const		{ return inputCalls_; }
	int tryCopyStateCalls() const	{ return tryCopyStateCalls_; }
	double lastTime() const		{ return lastTime_; }
	SpaceID lastSpaceID() const	{ return lastSpaceID_; }
	EntityID lastVehicleID() const	{ return lastVehicleID_; }
	const Position3D & lastPosition() const	{ return lastPosition_; }
	const Vector3 & lastPositionError() const
	{
		return lastPositionError_;
	}
	const Direction3D & lastDirection() const	{ return lastDirection_; }

	/* Override from MovementFilter. */
	virtual void reset( double time )
	{
		++resetCalls_;
	}

	/* Override from MovementFilter. */
	virtual void input( double time, SpaceID spaceID, EntityID vehicleID,
			const Position3D & position, const Vector3 & positionError,
			const Direction3D & direction )
	{
		++inputCalls_;
		lastTime_ = time;
		lastSpaceID_ = spaceID;
		lastVehicleID_ = vehicleID;
		lastPosition_ = position;
		lastPositionError_ = positionError;
		lastDirection_ = direction;
	}

	/* Override from MovementFilter. */
	virtual void output( double time, MovementFilterTarget & target ) {}

	/* Override from MovementFilter. */
	virtual bool getLastInput( double & time, SpaceID & spaceID,
			EntityID & vehicleID, Position3D & position,
			Vector3 & positionError, Direction3D & direction ) const
	{
		if (!hasLastInput_)
		{
			return false;
		}

		time = cannedTime_;
		spaceID = cannedSpaceID_;
		vehicleID = cannedVehicleID_;
		position = cannedPosition_;
		positionError = cannedPositionError_;
		direction = cannedDirection_;
		return true;
	}

private:
	/**
	 *	tryCopyState() is *private* in MovementFilter, but a derived
	 *	class may still override it - access control does not stop
	 *	virtual overriding, and the base's copyState() reaches it
	 *	through the vtable.
	 */
	virtual bool tryCopyState( const MovementFilter & rOtherFilter )
	{
		++tryCopyStateCalls_;
		return tryCopyStateResult_;
	}

	bool tryCopyStateResult_;
	bool hasLastInput_;

	double cannedTime_;
	SpaceID cannedSpaceID_;
	EntityID cannedVehicleID_;
	Position3D cannedPosition_;
	Vector3 cannedPositionError_;
	Direction3D cannedDirection_;

	int resetCalls_;
	int inputCalls_;
	int tryCopyStateCalls_;
	double lastTime_;
	SpaceID lastSpaceID_;
	EntityID lastVehicleID_;
	Position3D lastPosition_;
	Vector3 lastPositionError_;
	Direction3D lastDirection_;
};

BW_END_NAMESPACE

#endif // TEST_FILTER_INTERFACES_HPP
