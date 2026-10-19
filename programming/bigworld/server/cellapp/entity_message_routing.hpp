// entity_message_routing.hpp
//
// The EntityMessageHandler::handleMessage disposition ladder, as data.
//
// EntityMessageHandler::handleMessage (message_handlers.cpp) decides what
// happens to an incoming entity message through a ladder of branch conditions
// over the entity's reality (GHOST_ONLY / REAL_ONLY / WITNESS_ONLY, from
// cellapp_interface.hpp), whether the entity is here and alive, and the ghost
// buffering bookkeeping. That ladder is the whole of the routing policy for
// entity messages, and it is only reachable in the cellapp binary - the unit
// test harness does not link message_handlers.cpp (it needs a live CellApp and
// network interface, see the C-tier test-carrier gap in todo.md). The ladder
// therefore lives here as a pure classifier over a plain state struct, and
// handleMessage drives its side effects (sendFailure, buffering, forwarding)
// from the returned disposition.
//
// Two notes on fidelity to the original ladder:
//
//   * the original evaluates isOffloadingTo/hasMessagesFor lazily inside
//     GHOST_ONLY branches; the caller fills them eagerly. Both are const
//     lookups (entity.cpp:1952/:7732, buffered_ghost_messages.cpp:64), so the
//     disposition is unchanged - only the evaluation order of pure reads.
//   * the FORWARD_TO_REAL disposition covers both forward outcomes: the
//     channel lookup (pEntity ? pEntity->pRealChannel() :
//     Entity::population().findRealChannel(entityID)) stays at the call site
//     because it touches the entity population, and its NULL result is what
//     turns the forward into the unknown-entity failure.

#ifndef ENTITY_MESSAGE_ROUTING_HPP
#define ENTITY_MESSAGE_ROUTING_HPP

#include "cellapp_interface.hpp"

BW_BEGIN_NAMESPACE

/// What EntityMessageHandler::handleMessage does with a message.
enum EntityMessageDisposition
{
	/// GHOST_ONLY: the ghost was destroyed while the message was in flight.
	DISPOSITION_DROP_DESTROYED_GHOST,
	/// GHOST_ONLY: out of sender order - buffer until the sender catches up.
	DISPOSITION_BUFFER_GHOST_MESSAGE,
	/// REAL_ONLY/WITNESS_ONLY: not holding the real - attempt a forward.
	DISPOSITION_FORWARD_TO_REAL,
	/// WITNESS_ONLY: the entity has no witness to receive it.
	DISPOSITION_DROP_NO_WITNESS,
	/// No tick budget left this tick - defer to the next one.
	DISPOSITION_BUFFER_IF_TICK_PENDING,
	/// None of the above: call through to the message's handler.
	DISPOSITION_DISPATCH,

	DISPOSITION_COUNT
};

/// Everything the disposition ladder branches on.
struct EntityMessageRoutingState
{
	EntityReality reality;

	bool entityExists;
	bool entityIsReal;
	bool entityIsOffloadingToSender;

	/// Only meaningful when the entity exists and is real; the caller fills
	/// it through Entity::pReal()->pWitness() under that same guard.
	bool entityHasWitness;

	/// Entity::shouldBufferMessagesFrom( srcAddr ).
	bool shouldBufferFromSender;
	/// BufferedGhostMessages::isDelayingMessagesFor( entityID, srcAddr ).
	bool delayingForSender;
	/// BufferedGhostMessages::hasMessagesFor( entityID, srcAddr ).
	bool hasMessagesForSender;
	/// BufferedGhostMessages::isSubsequenceStart( header.identifier ).
	bool isSubsequenceStart;

	/// The handler's deferral flag and the cellapp's tick state.
	bool shouldBufferIfTickPending;
	bool nextTickPending;
};

/**
 *	The EntityMessageHandler::handleMessage disposition ladder
 *	(message_handlers.cpp), as a pure function.
 */
inline EntityMessageDisposition classifyEntityMessage(
	const EntityMessageRoutingState & s )
{
	bool shouldBufferGhostMessage = !s.entityExists ||
		s.shouldBufferFromSender || s.delayingForSender;

	bool isForDestroyedGhost = false;

	// Message is for a destroyed ghost if it is out of subsequence order.
	if (s.reality == GHOST_ONLY)
	{
		// Destroyed by restored real.
		if (s.entityExists && s.entityIsReal)
		{
			isForDestroyedGhost = true;
		}

		// Destroyed by restored real, then recreated when offload to sender.
		else if (s.entityExists && s.entityIsOffloadingToSender &&
				!s.isSubsequenceStart)
		{
			isForDestroyedGhost = true;
		}

		// Destroyed when sender's death was handled. A subsequence start has
		// no predecessor to reference its order, so it is never counted as
		// destroyed-ghost (buffering/handling one redundantly is OK if the
		// message is idempotent).
		else if (shouldBufferGhostMessage && !s.hasMessagesForSender &&
				!s.isSubsequenceStart)
		{
			isForDestroyedGhost = true;
		}
	}

	// Drop GHOST_ONLY messages for destroyed ghost.
	if (isForDestroyedGhost)
	{
		return DISPOSITION_DROP_DESTROYED_GHOST;
	}

	// Buffer GHOST_ONLY messages that are out of sender order.
	if (s.reality == GHOST_ONLY && shouldBufferGhostMessage)
	{
		return DISPOSITION_BUFFER_GHOST_MESSAGE;
	}

	// REAL_ONLY messages should be forwarded if we don't have the real.
	// WITNESS_ONLY is above REAL_ONLY in EntityReality, so this arm also
	// catches witness messages for entities we do not hold as real - which is
	// what keeps the no-witness arm below from ever seeing a missing entity.
	if (s.reality >= REAL_ONLY && (!s.entityExists || !s.entityIsReal))
	{
		return DISPOSITION_FORWARD_TO_REAL;
	}

	// Drop WITNESS_ONLY messages for entities without a witness.
	if (s.reality == WITNESS_ONLY && s.entityExists && !s.entityHasWitness)
	{
		return DISPOSITION_DROP_NO_WITNESS;
	}

	if (s.shouldBufferIfTickPending && s.nextTickPending)
	{
		return DISPOSITION_BUFFER_IF_TICK_PENDING;
	}

	return DISPOSITION_DISPATCH;
}

BW_END_NAMESPACE

#endif // ENTITY_MESSAGE_ROUTING_HPP
