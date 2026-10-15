// test_entity_message_routing.cpp
//
// The EntityMessageHandler::handleMessage disposition ladder, as testable data
// (entity_message_routing.hpp). The ladder is the whole routing policy for
// entity messages, but driving the real handleMessage needs a live CellApp and
// network interface - message_handlers.cpp is not in the cellapp_test link set
// (the C-tier test-carrier gap in todo.md). classifyEntityMessage() carries
// the branch policy as a pure function, so the ladder's order and its escape
// hatches are pinned here:
//
//   * the destroyed-ghost detection has one escape hatch: a subsequence start
//     has no predecessor, so it is never counted as destroyed;
//   * the drop-destroyed-ghost decision outranks buffering;
//   * the forward-to-real arm precedes the no-witness arm, which is what
//     keeps the no-witness arm from ever dereferencing a missing entity;
//   * deferral to the next tick only applies when the handler opted in.

#include "pch.hpp"

#include "../entity_message_routing.hpp"


BW_BEGIN_NAMESPACE


namespace
{

// A state with every input at its "quiet" default: no entity, nothing
// buffered, nothing deferred. Tests turn on the inputs they exercise.
EntityMessageRoutingState makeState( EntityReality reality )
{
	EntityMessageRoutingState state;
	state.reality = reality;
	state.entityExists = false;
	state.entityIsReal = false;
	state.entityIsOffloadingToSender = false;
	state.entityHasWitness = false;
	state.shouldBufferFromSender = false;
	state.delayingForSender = false;
	state.hasMessagesForSender = false;
	state.isSubsequenceStart = false;
	state.shouldBufferIfTickPending = false;
	state.nextTickPending = false;
	return state;
}


// The state for a message to a live ghost held by this cellapp.
EntityMessageRoutingState liveGhostState()
{
	EntityMessageRoutingState state = makeState( GHOST_ONLY );
	state.entityExists = true;
	state.entityIsReal = false;
	return state;
}


} // end anonymous namespace


/**
 *	A ghost message for an entity that has been restored to a real here is
 *	for a destroyed ghost: the real replaced it while the message was in
 *	flight.
 */
TEST( EntityMessageRouting_GhostForRestoredRealDrops )
{
	EntityMessageRoutingState state = makeState( GHOST_ONLY );
	state.entityExists = true;
	state.entityIsReal = true;

	CHECK_EQUAL( DISPOSITION_DROP_DESTROYED_GHOST,
		classifyEntityMessage( state ) );
}


/**
 *	An ordinary ghost message to a ghost we hold, in order and with nothing
 *	buffered, dispatches.
 */
TEST( EntityMessageRouting_GhostInOrderDispatches )
{
	CHECK_EQUAL( DISPOSITION_DISPATCH, classifyEntityMessage( liveGhostState() ) );
}


/**
 *	A ghost message out of sender order buffers until the sender catches up.
 * With messages already queued for the pair this is the buffering path; the
 * same out-of-order state with an empty queue reads as a destroyed ghost
 * (the next test).
 */
TEST( EntityMessageRouting_GhostOutOfOrderBuffers )
{
	EntityMessageRoutingState state = liveGhostState();
	state.shouldBufferFromSender = true;
	state.hasMessagesForSender = true;

	CHECK_EQUAL( DISPOSITION_BUFFER_GHOST_MESSAGE,
		classifyEntityMessage( state ) );

	// The delay bookkeeping counts as out-of-order too. While a subsequence
	// is held back there are queued messages, which is what keeps the
	// destroyed-ghost detection from firing on this state.
	EntityMessageRoutingState delayed = liveGhostState();
	delayed.delayingForSender = true;
	delayed.hasMessagesForSender = true;

	CHECK_EQUAL( DISPOSITION_BUFFER_GHOST_MESSAGE,
		classifyEntityMessage( delayed ) );

	// The same delay flag without queued messages reads as a destroyed ghost:
	// the sender's stream is no longer being held back by anything.
	EntityMessageRoutingState staleDelay = liveGhostState();
	staleDelay.delayingForSender = true;

	CHECK_EQUAL( DISPOSITION_DROP_DESTROYED_GHOST,
		classifyEntityMessage( staleDelay ) );
}


/**
 *	A ghost message for an entity we do not have at all is a destroyed ghost
 *	when nothing is queued for it and it is not a subsequence start; with
 *	messages queued the drop cannot be told apart from a missing first
 *	message, so it buffers.
 */
TEST( EntityMessageRouting_GhostForMissingEntity )
{
	// No entity, nothing queued, not a subsequence start: destroyed ghost.
	EntityMessageRoutingState state = makeState( GHOST_ONLY );
	CHECK_EQUAL( DISPOSITION_DROP_DESTROYED_GHOST,
		classifyEntityMessage( state ) );

	// Messages queued for the destroyed ghost: buffer instead.
	state.hasMessagesForSender = true;

	CHECK_EQUAL( DISPOSITION_BUFFER_GHOST_MESSAGE,
		classifyEntityMessage( state ) );
}


/**
 *	The subsequence-start escape hatch: a subsequence start has no predecessor
 *	to reference its order, so it is never counted as destroyed-ghost and
 *	falls through to buffering.
 */
TEST( EntityMessageRouting_SubsequenceStartNeverCountedDestroyed )
{
	// Would be destroyed-ghost (no entity, messages queued) if not a start.
	EntityMessageRoutingState state = makeState( GHOST_ONLY );
	state.hasMessagesForSender = true;
	state.isSubsequenceStart = true;

	CHECK_EQUAL( DISPOSITION_BUFFER_GHOST_MESSAGE,
		classifyEntityMessage( state ) );

	// Would be destroyed-ghost (recreated offloading to sender) if not a
	// start; with the escape hatch it falls through to dispatch.
	EntityMessageRoutingState offloading = liveGhostState();
	offloading.entityIsOffloadingToSender = true;
	offloading.isSubsequenceStart = true;

	CHECK_EQUAL( DISPOSITION_DISPATCH, classifyEntityMessage( offloading ) );

	// Without the escape hatch the same state is a destroyed ghost.
	offloading.isSubsequenceStart = false;

	CHECK_EQUAL( DISPOSITION_DROP_DESTROYED_GHOST,
		classifyEntityMessage( offloading ) );
}


/**
 *	REAL_ONLY messages forward when the real is not here - whether the entity
 *	is entirely missing or we only hold it as a ghost. The channel lookup
 *	behind the forward is the call site's, not the classifier's.
 */
TEST( EntityMessageRouting_RealOnlyForwardsWithoutReal )
{
	EntityMessageRoutingState missing = makeState( REAL_ONLY );
	CHECK_EQUAL( DISPOSITION_FORWARD_TO_REAL,
		classifyEntityMessage( missing ) );

	EntityMessageRoutingState ghost = makeState( REAL_ONLY );
	ghost.entityExists = true;
	ghost.entityIsReal = false;

	CHECK_EQUAL( DISPOSITION_FORWARD_TO_REAL, classifyEntityMessage( ghost ) );

	ghost.entityIsReal = true;

	CHECK_EQUAL( DISPOSITION_DISPATCH, classifyEntityMessage( ghost ) );
}


/**
 *	The no-witness arm sits behind the forward arm: a witness message for an
 *	entity that is not held as real forwards, it never reaches the witness
 *	check (the original ladder dereferences the entity there).
 */
TEST( EntityMessageRouting_WitnessOnlyWithoutWitnessDrops )
{
	EntityMessageRoutingState state = makeState( WITNESS_ONLY );
	state.entityExists = true;
	state.entityIsReal = true;

	CHECK_EQUAL( DISPOSITION_DROP_NO_WITNESS,
		classifyEntityMessage( state ) );

	state.entityHasWitness = true;

	CHECK_EQUAL( DISPOSITION_DISPATCH, classifyEntityMessage( state ) );

	// Missing entity: the forward arm catches it first.
	EntityMessageRoutingState missing = makeState( WITNESS_ONLY );
	missing.entityHasWitness = false;

	CHECK_EQUAL( DISPOSITION_FORWARD_TO_REAL,
		classifyEntityMessage( missing ) );

	// Ghost entity: also the forward arm.
	EntityMessageRoutingState ghost = makeState( WITNESS_ONLY );
	ghost.entityExists = true;
	ghost.entityIsReal = false;

	CHECK_EQUAL( DISPOSITION_FORWARD_TO_REAL, classifyEntityMessage( ghost ) );
}


/**
 *	Deferral to the next tick only fires when the handler opted in and the
 *	cellapp actually has a next tick pending; either alone leaves the message
 *	to dispatch.
 */
TEST( EntityMessageRouting_TickPendingDeferral )
{
	EntityMessageRoutingState state = liveGhostState();
	state.shouldBufferIfTickPending = true;
	state.nextTickPending = true;

	CHECK_EQUAL( DISPOSITION_BUFFER_IF_TICK_PENDING,
		classifyEntityMessage( state ) );

	state.shouldBufferIfTickPending = false;

	CHECK_EQUAL( DISPOSITION_DISPATCH, classifyEntityMessage( state ) );

	state.shouldBufferIfTickPending = true;
	state.nextTickPending = false;

	CHECK_EQUAL( DISPOSITION_DISPATCH, classifyEntityMessage( state ) );
}


// test_entity_message_routing.cpp

BW_END_NAMESPACE
