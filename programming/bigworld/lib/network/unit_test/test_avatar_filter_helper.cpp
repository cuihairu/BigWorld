#include "pch.hpp"

#include "connection/avatar_filter_helper.hpp"
#include "connection/filter_environment.hpp"

#include <limits>


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

/**
 *	A FilterEnvironment that is the identity transform everywhere, so
 *	that the helper's waypoint arithmetic can be asserted in plain world
 *	coordinates. Calls are counted, since the waypoint machinery routes
 *	coordinate changes through here.
 */
class IdentityFilterEnvironment : public FilterEnvironment
{
public:
	IdentityFilterEnvironment() :
		groundCalls_( 0 ), intoCalls_( 0 ), fromCalls_( 0 )
	{}

	virtual bool filterDropPoint( SpaceID spaceID,
			const Position3D & fall, Position3D & land,
			Vector3 * pGroundNormal )
	{
		return false;
	}

	virtual bool resolveOnGroundPosition( Position3D & position,
			bool & isOnGround )
	{
		++groundCalls_;
		isOnGround = false;
		return false;
	}

	virtual void transformIntoCommon( SpaceID spaceID, EntityID vehicleID,
			Position3D & position, Direction3D & direction )
	{
		++intoCalls_;
	}

	virtual void transformFromCommon( SpaceID spaceID, EntityID vehicleID,
			Position3D & position, Direction3D & direction )
	{
		++fromCalls_;
	}

	int groundCalls() const	{ return groundCalls_; }
	int intoCalls() const	{ return intoCalls_; }
	int fromCalls() const	{ return fromCalls_; }

private:
	int groundCalls_;
	int intoCalls_;
	int fromCalls_;
};


/**
 *	Applies a full set of AvatarFilterSettings on construction and puts
 *	the previous values back on destruction, so that tests that depend
 *	on the controller constants are order independent.
 */
class ScopedFilterSettings
{
public:
	ScopedFilterSettings( float latencyVelocity, float latencyMinimum,
			float latencyFrames, float latencyCurvePower ) :
		prevVelocity_( AvatarFilterSettings::s_latencyVelocity_ ),
		prevMinimum_( AvatarFilterSettings::s_latencyMinimum_ ),
		prevFrames_( AvatarFilterSettings::s_latencyFrames_ ),
		prevCurvePower_( AvatarFilterSettings::s_latencyCurvePower_ )
	{
		AvatarFilterSettings::s_latencyVelocity_ = latencyVelocity;
		AvatarFilterSettings::s_latencyMinimum_ = latencyMinimum;
		AvatarFilterSettings::s_latencyFrames_ = latencyFrames;
		AvatarFilterSettings::s_latencyCurvePower_ = latencyCurvePower;
	}

	~ScopedFilterSettings()
	{
		AvatarFilterSettings::s_latencyVelocity_ = prevVelocity_;
		AvatarFilterSettings::s_latencyMinimum_ = prevMinimum_;
		AvatarFilterSettings::s_latencyFrames_ = prevFrames_;
		AvatarFilterSettings::s_latencyCurvePower_ = prevCurvePower_;
	}

private:
	float prevVelocity_;
	float prevMinimum_;
	float prevFrames_;
	float prevCurvePower_;
};


/**
 *	Read-only access to a stored input. Only the const overload of
 *	getStoredInput() is public, so inspections go through a const
 *	reference to pick it.
 */
const AvatarFilterHelper::StoredInput & storedInputAt(
		const AvatarFilterHelper & helper, uint index )
{
	return helper.getStoredInput( index );
}


/**
 *	Feeds one server input: one unit per second along x, yaw growing at
 *	half a radian per unit, a nonzero roll that the filter must ignore.
 */
void addLineInput( AvatarFilterHelper & helper, double time )
{
	helper.input( time, 5, 9, Position3D( float( time - 100 ), 0.f, 0.f ),
		Vector3::zero(),
		Direction3D( Vector3( 0.9f, 0.f, 0.5f * float( time - 100 ) ) ) );
}


/**
 *	Runs the stored-input comparisons shared by the copy tests: both
 *	histories must agree on every slot. Reports the first mismatch, so
 *	that a single CHECK() in the test body can carry the detail (the
 *	check macros only expand inside a test object).
 */
BW::string firstHistoryMismatch( const AvatarFilterHelper & lhs,
		const AvatarFilterHelper & rhs )
{
	for (uint i = 0; i < AvatarFilterHelper::NUM_STORED_INPUTS; ++i)
	{
		const AvatarFilterHelper::StoredInput & left =
			lhs.getStoredInput( i );
		const AvatarFilterHelper::StoredInput & right =
			rhs.getStoredInput( i );

		if (fabs( left.time_ - right.time_ ) > 1e-9)
		{
			return BW::string( "time differs at slot " ) +
				BW::string::value_type( char( '0' + i ) );
		}
		if (fabsf( left.position_.x - right.position_.x ) > 1e-4f ||
				fabsf( left.position_.y - right.position_.y ) > 1e-4f ||
				fabsf( left.position_.z - right.position_.z ) > 1e-4f)
		{
			return BW::string( "position differs at slot " ) +
				BW::string::value_type( char( '0' + i ) );
		}
		if (fabsf( left.direction_.yaw - right.direction_.yaw ) > 1e-6f)
		{
			return BW::string( "yaw differs at slot " ) +
				BW::string::value_type( char( '0' + i ) );
		}
	}

	return BW::string();
}

} // end namespace (anonymous)


// -----------------------------------------------------------------------------
// Section: AvatarFilterHelper
// -----------------------------------------------------------------------------

// A freshly constructed helper has no last input, its latency is the
// configured frame count scaled by the seed time step, and its history is
// the -2000 sentinel seed.
TEST( AvatarFilterHelper_initialState )
{
	IdentityFilterEnvironment environment;
	AvatarFilterHelper helper( environment );

	double time = 1234.0;
	SpaceID spaceID = 77;
	EntityID vehicleID = 88;
	Position3D position( 1.f, 2.f, 3.f );
	Vector3 positionError( 4.f, 5.f, 6.f );
	Direction3D direction( Vector3( 1.f, 1.f, 1.f ) );

	CHECK( !helper.getLastInput( time, spaceID, vehicleID, position,
		positionError, direction ) );
	// Out parameters are untouched when there is no input.
	CHECK_EQUAL( 1234.0, time );
	CHECK_EQUAL( 77, spaceID );
	CHECK_EQUAL( 88, vehicleID );
	CHECK_EQUAL( 1.f, position.x );

	// The constructor seeds the history at -2000 with the default step.
	CHECK_CLOSE( -2000.0, storedInputAt( helper, 0 ).time_, 1e-9 );
	CHECK_CLOSE( -2000.07, storedInputAt( helper, 7 ).time_, 1e-9 );

	// Default settings: two latency frames at 0.01s seed spacing.
	ScopedFilterSettings settings( 1.f, 0.1f, 2.f, 2.f );
	AvatarFilterHelper another( environment );
	CHECK_CLOSE( 0.02f, another.latency(), 1e-6 );

	// The static active flag round trips (and is put back).
	const bool wasActive = AvatarFilterHelper::isActive();
	AvatarFilterHelper::isActive( !wasActive );
	CHECK( AvatarFilterHelper::isActive() != wasActive );
	AvatarFilterHelper::isActive( wasActive );
	CHECK_EQUAL( wasActive, AvatarFilterHelper::isActive() );
}


// The first input after construction does not append - it seeds the whole
// history with itself, spaced 0.01s apart, and arms getLastInput().
TEST( AvatarFilterHelper_firstInputSeedsHistory )
{
	IdentityFilterEnvironment environment;
	AvatarFilterHelper helper( environment );

	helper.input( 100.0, 5, 9, Position3D( 3.f, 4.f, 5.f ),
		Vector3( 0.1f, 0.2f, 0.3f ),
		Direction3D( Vector3( 0.f, 0.5f, 1.f ) ) );

	double time = 0.0;
	SpaceID spaceID = 0;
	EntityID vehicleID = 0;
	Position3D position;
	Vector3 positionError;
	Direction3D direction;

	CHECK( helper.getLastInput( time, spaceID, vehicleID, position,
		positionError, direction ) );
	CHECK_CLOSE( 100.0, time, 1e-9 );
	CHECK_EQUAL( 5, spaceID );
	CHECK_EQUAL( 9, vehicleID );
	CHECK_EQUAL( 3.f, position.x );
	CHECK_EQUAL( 4.f, position.y );
	CHECK_EQUAL( 5.f, position.z );
	CHECK_EQUAL( 0.1f, positionError.x );
	CHECK_EQUAL( 1.f, direction.yaw );

	// Every slot holds the same sample, marching 0.01s back in time; the
	// environment resolved the position (identity here) for each of them.
	for (uint i = 0; i < AvatarFilterHelper::NUM_STORED_INPUTS; ++i)
	{
		const AvatarFilterHelper::StoredInput & stored =
			storedInputAt( helper, i );

		// The seed step is a float constant in production, so the times
		// carry float rounding - 1e-5 tolerates it.
		CHECK_CLOSE( 100.0 - 0.01 * i, stored.time_, 1e-5 );
		CHECK_EQUAL( 3.f, stored.position_.x );
		CHECK( !stored.onGround_ );
	}
}


// Inputs at or behind the newest stored time are dropped; strictly newer
// inputs stack onto the front of the ring.
TEST( AvatarFilterHelper_staleInputIgnored )
{
	IdentityFilterEnvironment environment;
	AvatarFilterHelper helper( environment );

	helper.input( 100.0, 5, 9, Position3D( 0.f, 0.f, 0.f ),
		Vector3::zero(), Direction3D( Vector3::zero() ) );
	helper.input( 100.5, 5, 9, Position3D( 99.f, 0.f, 0.f ),
		Vector3::zero(), Direction3D( Vector3::zero() ) );

	// Older than the newest input: ignored outright.
	helper.input( 100.2, 5, 9, Position3D( 55.f, 0.f, 0.f ),
		Vector3::zero(), Direction3D( Vector3::zero() ) );
	CHECK_CLOSE( 100.5, storedInputAt( helper, 0 ).time_, 1e-9 );
	CHECK_EQUAL( 99.f, storedInputAt( helper, 0 ).position_.x );

	// Equal to the newest input: also ignored (the check is <=).
	helper.input( 100.5, 5, 9, Position3D( 77.f, 0.f, 0.f ),
		Vector3::zero(), Direction3D( Vector3::zero() ) );
	CHECK_EQUAL( 99.f, storedInputAt( helper, 0 ).position_.x );

	// Strictly newer inputs stack in front.
	helper.input( 100.75, 5, 9, Position3D( 33.f, 0.f, 0.f ),
		Vector3::zero(), Direction3D( Vector3::zero() ) );
	CHECK_CLOSE( 100.75, storedInputAt( helper, 0 ).time_, 1e-9 );
	CHECK_EQUAL( 33.f, storedInputAt( helper, 0 ).position_.x );
	CHECK_CLOSE( 100.5, storedInputAt( helper, 1 ).time_, 1e-9 );
	CHECK_EQUAL( 99.f, storedInputAt( helper, 1 ).position_.x );
	CHECK_CLOSE( 100.0, storedInputAt( helper, 2 ).time_, 1e-9 );
	CHECK_EQUAL( 0.f, storedInputAt( helper, 2 ).position_.x );
}


// reset() disarms getLastInput() and makes the next input reseed the whole
// history from scratch, discarding the samples collected so far.
TEST( AvatarFilterHelper_resetDiscardsHistory )
{
	IdentityFilterEnvironment environment;
	AvatarFilterHelper helper( environment );

	helper.input( 100.0, 5, 9, Position3D( 1.f, 0.f, 0.f ),
		Vector3::zero(), Direction3D( Vector3::zero() ) );

	double time = 0.0;
	SpaceID spaceID = 0;
	EntityID vehicleID = 0;
	Position3D position;
	Vector3 positionError;
	Direction3D direction;
	CHECK( helper.getLastInput( time, spaceID, vehicleID, position,
		positionError, direction ) );

	helper.reset( 50.0 );
	CHECK( !helper.getLastInput( time, spaceID, vehicleID, position,
		positionError, direction ) );

	// The next input reseeds rather than appends.
	helper.input( 100.5, 5, 9, Position3D( 9.f, 0.f, 0.f ),
		Vector3::zero(), Direction3D( Vector3::zero() ) );

	CHECK( helper.getLastInput( time, spaceID, vehicleID, position,
		positionError, direction ) );
	CHECK_CLOSE( 100.5, time, 1e-9 );
	CHECK_EQUAL( 9.f, position.x );

	// The second slot is the reseed step behind the first, not a survivor
	// of the pre-reset history (which held a sample at 100.0).
	CHECK_CLOSE( 100.49, storedInputAt( helper, 1 ).time_, 1e-9 );
	CHECK_EQUAL( 9.f, storedInputAt( helper, 1 ).position_.x );
}


// With a collinear input history, extract() walks the line: once the
// waypoints have advanced onto real inputs the position is the exact
// interpolation, the velocity is the input speed, the yaw is blended and
// the roll is always forced to zero.
TEST( AvatarFilterHelper_extractWalksInputLine )
{
	IdentityFilterEnvironment environment;
	AvatarFilterHelper helper( environment );

	for (double t = 100.0; t <= 107.0; t += 1.0)
	{
		addLineInput( helper, t );
	}

	SpaceID spaceID = 0;
	EntityID vehicleID = 0;
	Position3D position;
	Vector3 velocity;
	Direction3D direction;

	// The first call jumps the waypoints from their seed times; from here
	// on they sit on actual inputs and the interpolation is exact.
	helper.extract( 100.5, spaceID, vehicleID, position, velocity,
		direction );

	helper.extract( 101.0, spaceID, vehicleID, position, velocity,
		direction );
	CHECK_CLOSE( 1.0, position.x, 1e-3 );

	helper.extract( 101.5, spaceID, vehicleID, position, velocity,
		direction );
	CHECK_CLOSE( 1.5, position.x, 1e-3 );
	// Halfway between the yaw at t=101 (0.5 rad) and t=102 (1.0 rad).
	CHECK_CLOSE( 0.75f, direction.yaw, 1e-3 );
	// Roll is never forwarded, whatever the inputs carry.
	CHECK_EQUAL( 0.f, direction.roll );
	CHECK_EQUAL( 5, spaceID );
	CHECK_EQUAL( 9, vehicleID );

	helper.extract( 102.0, spaceID, vehicleID, position, velocity,
		direction );
	CHECK_CLOSE( 2.0, position.x, 1e-3 );
	CHECK_CLOSE( 1.0, velocity.x, 1e-3 );

	helper.extract( 102.5, spaceID, vehicleID, position, velocity,
		direction );
	CHECK_CLOSE( 2.5, position.x, 1e-3 );
	CHECK_CLOSE( 1.0, velocity.x, 1e-3 );
}


// Once the requested time passes the newest input the filter stands still:
// the position freezes at the newest sample and the velocity collapses
// instead of extrapolating onwards.
TEST( AvatarFilterHelper_extrapolationStandsStill )
{
	IdentityFilterEnvironment environment;
	AvatarFilterHelper helper( environment );

	for (double t = 100.0; t <= 103.0; t += 1.0)
	{
		addLineInput( helper, t );
	}

	SpaceID spaceID = 0;
	EntityID vehicleID = 0;
	Position3D position;
	Vector3 velocity;
	Direction3D direction;

	// Walk the waypoints up onto the newest input first.
	for (double t = 100.5; t <= 103.0; t += 0.5)
	{
		helper.extract( t, spaceID, vehicleID, position, velocity,
			direction );
	}
	CHECK_CLOSE( 3.0, position.x, 1e-3 );

	helper.extract( 103.5, spaceID, vehicleID, position, velocity,
		direction );
	CHECK_CLOSE( 3.0, position.x, 1e-3 );
	CHECK_CLOSE( 0.0, velocity.x, 1e-4 );
	CHECK_CLOSE( 0.0, velocity.lengthSquared(), 1e-6 );

	// Still standing still on further calls, not creeping forwards.
	helper.extract( 103.6, spaceID, vehicleID, position, velocity,
		direction );
	CHECK_CLOSE( 3.0, position.x, 1e-3 );
	CHECK_CLOSE( 0.0, velocity.x, 1e-4 );
}


// With the filter marked inactive it behaves like the DumbFilter: any
// requested time returns the most recent input verbatim with zero velocity.
TEST( AvatarFilterHelper_inactivePassthrough )
{
	const bool wasActive = AvatarFilterHelper::isActive();
	AvatarFilterHelper::isActive( false );

	IdentityFilterEnvironment environment;
	AvatarFilterHelper helper( environment );

	for (double t = 100.0; t <= 103.0; t += 1.0)
	{
		addLineInput( helper, t );
	}

	SpaceID spaceID = 0;
	EntityID vehicleID = 0;
	Position3D position;
	Vector3 velocity( 42.f, 42.f, 42.f );
	Direction3D direction;

	helper.extract( 999.0, spaceID, vehicleID, position, velocity,
		direction );

	CHECK_EQUAL( 3.f, position.x );
	CHECK_EQUAL( 5, spaceID );
	CHECK_EQUAL( 9, vehicleID );
	CHECK_CLOSE( 1.5f, direction.yaw, 1e-5 );
	CHECK_EQUAL( 0.f, velocity.x );
	CHECK_CLOSE( 0.f, velocity.lengthSquared(), 1e-6 );

	AvatarFilterHelper::isActive( wasActive );
}


// output() runs the latency controller: with default settings and a steady
// one-second input stream the ideal latency lands on two frames, and the
// first output call climbs straight to it, extracting at time - latency.
// Once converged, latency holds still instead of overshooting.
TEST( AvatarFilterHelper_outputLatencyController )
{
	ScopedFilterSettings settings( 1.f /* velocity */,
		0.1f /* minimum */, 2.f /* frames */, 2.f /* curve power */ );

	IdentityFilterEnvironment environment;
	AvatarFilterHelper helper( environment );

	for (double t = 100.0; t <= 107.0; t += 1.0)
	{
		addLineInput( helper, t );
	}

	SpaceID spaceID = 0;
	EntityID vehicleID = 0;
	Position3D position;
	Vector3 velocity;
	Direction3D direction;

	// Ideal latency anchors the interpolation five sevenths of the way
	// from the oldest surviving input (101) to the newest (107), i.e. at
	// t=105, so at output time 107.5 the ideal is 2.5s. The whole climb
	// happens on this first call (dTime is large), clamped to the ideal.
	const double outputTime = helper.output( 107.5, spaceID, vehicleID,
		position, velocity, direction );

	CHECK_CLOSE( 2.5, helper.latency(), 1e-5 );
	CHECK_CLOSE( 105.0, outputTime, 1e-9 );
	// extract(105.0) walks the input line (allowing for the seed-step
	// residue on the trailing waypoint).
	CHECK_CLOSE( 5.0, position.x, 0.01 );
	CHECK_CLOSE( 1.0, velocity.x, 0.01 );

	// Converged: a further call holds latency at the ideal and slides the
	// extraction point forward one to one.
	const double outputTime2 = helper.output( 107.6, spaceID, vehicleID,
		position, velocity, direction );

	CHECK_CLOSE( 2.5, helper.latency(), 1e-5 );
	CHECK_CLOSE( 105.1, outputTime2, 1e-9 );
	CHECK_CLOSE( 5.1, position.x, 0.01 );
}


// Pinned quirk (not fixed here): reset() clears latency_ but not
// idealLatency_, and the controller only recomputes the ideal once two
// real inputs are on file - so after a reset the first output still pulls
// the latency towards the stale pre-reset ideal instead of the configured
// minimum. Fixing it changes client movement feel after teleports and
// needs its own evaluation, so this test just documents the behaviour.
TEST( AvatarFilterHelper_resetKeepsStaleIdealLatency )
{
	ScopedFilterSettings settings( 1.f, 0.1f, 2.f, 2.f );

	IdentityFilterEnvironment environment;
	AvatarFilterHelper helper( environment );

	for (double t = 100.0; t <= 107.0; t += 1.0)
	{
		addLineInput( helper, t );
	}

	SpaceID spaceID = 0;
	EntityID vehicleID = 0;
	Position3D position;
	Vector3 velocity;
	Direction3D direction;
	helper.output( 107.5, spaceID, vehicleID, position, velocity,
		direction );
	CHECK_CLOSE( 2.5, helper.latency(), 1e-5 );

	helper.reset( 0.0 );
	helper.input( 200.0, 5, 9, Position3D( 100.f, 0.f, 0.f ),
		Vector3::zero(), Direction3D( Vector3::zero() ) );

	// latency_ was reset, but the ideal is still the stale 2.5s, so this
	// output climbs to 0.52s (0.02s seed + one full dLatency step) rather
	// than settling at the 0.1s minimum.
	const double outputTime = helper.output( 200.5, spaceID, vehicleID,
		position, velocity, direction );

	CHECK_CLOSE( 0.52, helper.latency(), 1e-4 );
	// latency_ is a float, so the returned time differs from the pure
	// double arithmetic by ~2e-8.
	CHECK_CLOSE( 200.5 - 0.52, outputTime, 1e-6 );
}


// Copies (constructor and assignment) duplicate the whole input history and
// then run independently of their source.
TEST( AvatarFilterHelper_copyIsIndependent )
{
	IdentityFilterEnvironment environment;
	AvatarFilterHelper source( environment );

	for (double t = 100.0; t <= 101.0; t += 0.5)
	{
		addLineInput( source, t );
	}

	AvatarFilterHelper copied( source );
	CHECK( firstHistoryMismatch( source, copied ).empty() );
	CHECK_CLOSE( source.latency(), copied.latency(), 1e-6 );

	AvatarFilterHelper assigned( environment );
	assigned = source;
	CHECK( firstHistoryMismatch( source, assigned ).empty() );

	// New input on the copy does not disturb the source's history.
	copied.input( 102.0, 5, 9, Position3D( 50.f, 0.f, 0.f ),
		Vector3::zero(), Direction3D( Vector3::zero() ) );

	CHECK_CLOSE( 102.0, storedInputAt( copied, 0 ).time_, 1e-9 );
	CHECK_EQUAL( 50.f, storedInputAt( copied, 0 ).position_.x );
	CHECK_CLOSE( 101.0, storedInputAt( source, 0 ).time_, 1e-9 );
	CHECK_CLOSE( 1.0, storedInputAt( source, 0 ).position_.x, 1e-6 );

	// Self-assignment is a no-op.
	source = source;
	CHECK( firstHistoryMismatch( source, source ).empty() );
	CHECK_CLOSE( 101.0, storedInputAt( source, 0 ).time_, 1e-9 );
}


BW_END_NAMESPACE

// test_avatar_filter_helper.cpp
