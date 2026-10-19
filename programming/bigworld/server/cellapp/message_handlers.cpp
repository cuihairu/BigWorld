#include "script/first_include.hpp"

#include "buffered_entity_messages.hpp"
#include "buffered_ghost_message_factory.hpp"
#include "buffered_ghost_messages.hpp"
#include "buffered_input_messages.hpp"
#include "cellapp_interface.hpp"
#include "cell.hpp"
#include "cellapp.hpp"
#include "entity.hpp"
#include "buffered_input_message_handler.hpp"
#include "entity_message_handler.hpp"
#include "entity_message_log_scope.hpp"
#include "entity_message_routing.hpp"
#include "entity_population.hpp"
#include "real_entity.hpp"
#include "space.hpp"

#include "network/channel_sender.hpp"
#include "network/common_message_handlers.hpp"
#include "network/interfaces.hpp"
#include "network/udp_bundle.hpp"

#if defined( _WIN32 )
// MSVC's SDK leaves `#define interface struct` (rpcndr.h via d3d9.h / shlobj /
// shlwapi / d3d10) live for the rest of the TU, which rewrites the
// `interface`-named declarations in this file into syntax errors (C2332/C2027
// in the root superproject build). Off for this file only; the pop below
// restores it for whatever SDK COM header comes later in the TU.
#pragma push_macro( "interface" )
#undef interface
#endif


BW_BEGIN_NAMESPACE

// -----------------------------------------------------------------------------
// Section: BufferedInputMessageHandler
// -----------------------------------------------------------------------------

/**
 *	This method handles this message. It is called from the InputMessageHandler
 *	override and also when replaying buffered messages.
 */
template< class MESSAGE_HANDLER >
void BufferedInputMessageHandler< MESSAGE_HANDLER >::handleMessage(
	const Mercury::Address & srcAddr,
	Mercury::UnpackedMessageHeader & header,
	BinaryIStream & data )
{
	CellApp & app = ServerApp::getApp< CellApp >( header );

	if (app.nextTickPending())
	{
		static GameTime s_lastMessageTime = 0;
		BufferedInputMessages & bufferedInputMessages =
			app.bufferedInputMessages();

		if (s_lastMessageTime != app.time())
		{
			WARNING_MSG( "BufferedInputMessageHandler::handleMessage: No time "
					"left in tick (%u). Buffering '%s' and similar messages this "
					"tick. Num in buffer = %zu.\n",
				app.time(),
				header.msgName(),
				bufferedInputMessages.size() );

			s_lastMessageTime = app.time();
		}

		bufferedInputMessages.add( *this,
				srcAddr, header, data );
	}
	else
	{
		MESSAGE_HANDLER::handleMessage( srcAddr, header, data );
	}
}


// -----------------------------------------------------------------------------
// Section: EntityMessageHandler
// -----------------------------------------------------------------------------

/**
 *	Constructor.
 *
 *	@param reality Whether the message is destined for a real entity, ghost
 *		entity or entity with a witness.
 *	@param shouldBufferIfTickPending If true, messages will be delayed if the
 *		next tick is pending.
 */
EntityMessageHandler::EntityMessageHandler( EntityReality reality,
			bool shouldBufferIfTickPending ) :
		reality_( reality ),
		shouldBufferIfTickPending_( shouldBufferIfTickPending )
{
}


/*
 *	Override from InputMessageHandler.
 */
void EntityMessageHandler::handleMessage( const Mercury::Address & srcAddr,
	Mercury::UnpackedMessageHeader & header,
	BinaryIStream & data )
{
	EntityID entityID;
	data >> entityID;

	this->handleMessage( srcAddr, header, data, entityID );
}


/**
 *	This method handles this message. It is called from the InputMessageHandler
 *	override and from handling of buffered messages.
 */
void EntityMessageHandler::handleMessage( const Mercury::Address & srcAddr,
	Mercury::UnpackedMessageHeader & header,
	BinaryIStream & data,
	EntityID entityID )
{
	CellApp & app = ServerApp::getApp< CellApp >( header );
	Entity * pEntity = app.findEntity( entityID );

	AUTO_SCOPED_ENTITY_PROFILE( pEntity );

	// Every log written while handling this message carries the entity it was
	// for, on top of the channelID set higher up. The space is only read for a
	// live entity: a destroyed entity is still findable in the population but
	// Entity::destroy has already cleared its pSpace_, and Entity::spaceID
	// dereferences it unconditionally.
	EntityMessageLogScope entityLogScope( entityID );
	if (pEntity != NULL && !pEntity->isDestroyed())
	{
		entityLogScope.setSpaceID( pEntity->spaceID() );
	}

	BufferedGhostMessages & bufferedMessages = app.bufferedGhostMessages();

	// The routing decision is data (entity_message_routing.hpp) so the unit
	// tests can pin the ladder down; the side effects stay here.
	EntityMessageRoutingState routingState;
	routingState.reality = reality_;
	routingState.entityExists = (pEntity != NULL);
	routingState.entityIsReal = (pEntity != NULL) && pEntity->isReal();
	routingState.entityIsOffloadingToSender = (pEntity != NULL) &&
		pEntity->isOffloadingTo( srcAddr );
	routingState.entityHasWitness = routingState.entityIsReal &&
		(pEntity->pReal()->pWitness() != NULL);
	routingState.shouldBufferFromSender = (pEntity != NULL) &&
		pEntity->shouldBufferMessagesFrom( srcAddr );
	routingState.delayingForSender =
		bufferedMessages.isDelayingMessagesFor( entityID, srcAddr );
	routingState.hasMessagesForSender =
		bufferedMessages.hasMessagesFor( entityID, srcAddr );
	routingState.isSubsequenceStart =
		BufferedGhostMessages::isSubsequenceStart( header.identifier );
	routingState.shouldBufferIfTickPending = shouldBufferIfTickPending_;
	routingState.nextTickPending = app.nextTickPending();

	switch (classifyEntityMessage( routingState ))
	{
		// Drop GHOST_ONLY messages for destroyed ghost.
		case DISPOSITION_DROP_DESTROYED_GHOST:
			WARNING_MSG( "EntityMessageHandler::handleMessage( %s [id: %d] ): "
					"Dropping ghost message for entity %u from %s, "
					"ghost was destroyed while in-flight.\n",
				header.msgName(), int( header.identifier ),
				entityID, srcAddr.c_str() );

			this->sendFailure( srcAddr, header, data, entityID );
			break;

		// Buffer GHOST_ONLY messages that are out of sender order.
		case DISPOSITION_BUFFER_GHOST_MESSAGE:
		{
			BufferedGhostMessage * pMsg =
				BufferedGhostMessageFactory::createBufferedMessage(
					srcAddr, header, data, entityID, this );

			bufferedMessages.add( entityID, srcAddr, pMsg );

			WARNING_MSG( "EntityMessageHandler::handleMessage( %s [id: %d] ): "
					   "Buffered ghost message for entity %u from %s\n",
					   header.msgName(), int( header.identifier ),
					   entityID, srcAddr.c_str() );
			break;
		}

		// REAL_ONLY messages should be forwarded if we don't have the real.
		// We only try to look up the cached channel for the entity if it
		// doesn't exist, since calling findRealChannel() for ghosts will
		// cause an assertion.
		case DISPOSITION_FORWARD_TO_REAL:
		{
			CellAppChannel * pChannel = pEntity ?
				pEntity->pRealChannel() :
				Entity::population().findRealChannel( entityID );

			if (pChannel)
			{
				Entity::forwardMessageToReal( *pChannel, entityID,
					header.identifier, data, srcAddr, header.replyID );
			}
			else
			{
				ERROR_MSG( "EntityMessageHandler::handleMessage( %s [id: %d] ): "
					"Dropped real message for unknown entity %u\n",
					header.msgName(), int( header.identifier ), entityID );

				this->sendFailure( srcAddr, header, data, entityID );
			}
			break;
		}

		// Drop WITNESS_ONLY message for entities without a witness.
		case DISPOSITION_DROP_NO_WITNESS:
			DEBUG_MSG( "EntityMessageHandler::handleMessage( %s [id: %d] ): "
				"Received witness message for entity %u with no witness "
				"from %s\n",
				header.msgName(), int( header.identifier ),
				entityID, srcAddr.c_str() );

			this->sendFailure( srcAddr, header, data, entityID );
			break;

		case DISPOSITION_BUFFER_IF_TICK_PENDING:
		{
			static GameTime s_lastMessageTime = 0;
			BufferedEntityMessages & bufferedEntityMessages =
				app.bufferedEntityMessages();

			if (s_lastMessageTime != app.time())
			{
				WARNING_MSG( "EntityMessageHandler::handleMessage: No time left in "
						"tick (%u). Buffering '%s' and similar messages this tick. "
						"Num previously in buffer = %zu.\n",
					app.time(),
					header.msgName(),
					bufferedEntityMessages.size() );

				s_lastMessageTime = app.time();
			}

			bufferedEntityMessages.add( *this,
					srcAddr, header, data, entityID );
			break;
		}

		// Message is good, call through to the handler.
		case DISPOSITION_DISPATCH:
			this->callHandler( srcAddr, header, data, pEntity );
			break;
	}
}


/**
 *	This method is called when handling the message has failed.
 */
void EntityMessageHandler::sendFailure( const Mercury::Address & srcAddr,
		Mercury::UnpackedMessageHeader & header,
		BinaryIStream & data,
		EntityID entityID )
{
	data.finish();

	if (header.replyID != Mercury::REPLY_ID_NONE)
	{
		CellApp & app = ServerApp::getApp< CellApp >( header );

		BW::stringstream errorMsg;
		errorMsg << "No such entity " << entityID <<
			" on CellApp " << app.id() << " at " <<
			app.interface().address().c_str();

		MethodDescription::sendReturnValuesError( "BWNoSuchEntityError",
				errorMsg.str().c_str(),	header.replyID, srcAddr,
				app.interface() );
	}
}


// -----------------------------------------------------------------------------
// Section: Other handlers
// -----------------------------------------------------------------------------

/**
 * Objects of this type are used to handle messages destined for an entity.
 */
template <class ARGS_TYPE>
class CellEntityMessageHandler : public EntityMessageHandler
{
public:
	/**
	 *	This type is the function pointer type that handles the incoming
	 *	message.
	 */
	typedef void (Entity::*Handler)( const ARGS_TYPE & args );

	/**
	 *	Constructor.
	 */
	CellEntityMessageHandler( std::pair<Handler, EntityReality> args ) :
		EntityMessageHandler( args.second ),
		handler_( args.first )
	{}

	virtual void callHandler( const Mercury::Address & srcAddr,
		Mercury::UnpackedMessageHeader & header,
		BinaryIStream & data, Entity * pEntity )
	{
		ARGS_TYPE args;
		data >> args;
		(pEntity->*handler_)( args );
	}

protected:
	Handler			handler_;
};


/**
 * Objects of this type are used to handle messages destined for an entity.
 */
template <>
class CellEntityMessageHandler< void > : public EntityMessageHandler
{
public:
	/**
	 *	This type is the function pointer type that handles the incoming
	 *	message.
	 */
	typedef void (Entity::*Handler)();

	/**
	 *	Constructor.
	 */
	CellEntityMessageHandler( std::pair<Handler, EntityReality> args ) :
		EntityMessageHandler( args.second ),
		handler_( args.first )
	{}

	virtual void callHandler( const Mercury::Address & srcAddr,
		Mercury::UnpackedMessageHeader & header,
		BinaryIStream & data, Entity * pEntity )
	{
		(pEntity->*handler_)();
	}

protected:
	Handler			handler_;
};

/**
 * Objects of this type are used to handle messages destined for an entity.
 * This class also redirects failures to CellApp to handle cases where an
 * entity no longer exists but the message needs specific treatment
 */
template <class ARGS_TYPE>
class CellEntityMessageHandlerWithFailure : public CellEntityMessageHandler< ARGS_TYPE >
{
public:
	/**
	 *	This type is the function pointer type that handles the incoming
	 *	message.
	 */
	typedef void (Entity::*Handler)( const ARGS_TYPE & args );

	/**
	 * This type is the CellApp function pointer that handles the failure
	 */
	typedef void (CellApp::*FailureHandler)( EntityID entityID,
			const Mercury::Address &oldBaseAddr,
			const ARGS_TYPE & args );

	/**
	 *	Constructor.
	 */
	CellEntityMessageHandlerWithFailure( const std::pair< FailureHandler,
							std::pair<Handler, EntityReality> > & args ) :
		CellEntityMessageHandler< ARGS_TYPE >( args.second ),
		failureHandler_( args.first )
	{}

	void sendFailure( const Mercury::Address & srcAddr,
			Mercury::UnpackedMessageHeader & header,
			BinaryIStream & data,
			EntityID entityID )
	{
		ARGS_TYPE args;
		data >> args;

		data.finish();

		CellApp & app = ServerApp::getApp< CellApp >( header );
		(app.*failureHandler_)( entityID, srcAddr, args );
	}

protected:
	FailureHandler			failureHandler_;
};


/**
 *	Objects of this type are used to handle variable length messages destined
 *	for an entity.
 */
class EntityVarLenMessageHandler : public EntityMessageHandler
{
public:
	/**
	 *	This type is the function pointer type that handles the incoming
	 *	message.
	 */
	typedef void (Entity::*Handler)( BinaryIStream & stream );

	/**
	 *	Constructor.
	 */
	EntityVarLenMessageHandler( std::pair<Handler, EntityReality> args ) :
		EntityMessageHandler( args.second ),
		handler_( args.first )
	{}

private:
	virtual void callHandler( const Mercury::Address & srcAddr,
		Mercury::UnpackedMessageHeader & header,
		BinaryIStream & data, Entity * pEntity )
	{
		(pEntity->*handler_)( data );
	}

	Handler			handler_;
};

/**
 *	Objects of this type are used to handle variable length messages destined
 *	for an entity. This also passes the source address and header to the
 *	handling function.
 */
class RawEntityVarLenMessageHandler : public EntityMessageHandler
{
public:
	/**
	 *	This type is the function pointer type that handles the incoming
	 *	message.
	 */
	typedef void (Entity::*Handler)( const Mercury::Address & srcAddr,
		const Mercury::UnpackedMessageHeader & header,
		BinaryIStream & stream );

	/**
	 *	Constructor.
	 */
	RawEntityVarLenMessageHandler( std::pair<Handler, EntityReality> args ) :
		EntityMessageHandler( args.second ),
		handler_( args.first )
	{}

private:
	virtual void callHandler( const Mercury::Address & srcAddr,
		Mercury::UnpackedMessageHeader & header,
		BinaryIStream & data, Entity * pEntity )
	{
		(pEntity->*handler_)( srcAddr, header, data );
	}

	Handler			handler_;
};

/**
 *	Objects of this type are used to handle variable length messages destined
 *	for an entity.
 */
class EntityVarLenRequestHandler : public EntityMessageHandler
{
public:
	/**
	 *	This type is the function pointer type that handles the incoming
	 *	message.
	 */
	typedef void (Entity::*Handler)( const Mercury::Address& srcAddr,
			Mercury::UnpackedMessageHeader& header, BinaryIStream & stream );

	/**
	 *	Constructor.
	 */
	EntityVarLenRequestHandler( std::pair<Handler, EntityReality> args ) :
		EntityMessageHandler( args.second ),
		handler_( args.first )
	{}

private:
	virtual void callHandler( const Mercury::Address & srcAddr,
		Mercury::UnpackedMessageHeader & header,
		BinaryIStream & data, Entity * pEntity )
	{
		(pEntity->*handler_)( srcAddr, header, data );
	}

	Handler			handler_;
};


/**
 *	This class sends all our entity positions to an interested source,
 *	such as a CellAppMgr that wants an overview of the world (for debugging)
 */
class EntityPositionSender : public Mercury::InputMessageHandler
{
public:
	EntityPositionSender( void * ) {}

private:
	virtual void handleMessage( const Mercury::Address & srcAddr,
		Mercury::UnpackedMessageHeader & header,
		BinaryIStream & data )
	{
		SpaceID id;
		data >> id;

		CellApp & app = ServerApp::getApp< CellApp >( header );
		Cell * pCell = app.findCell( id );

		if (pCell != NULL)
		{
			Mercury::ChannelSender sender( CellApp::getChannel( srcAddr ) );
			Mercury::Bundle	& bundle = sender.bundle();

			bundle.startReply( header.replyID );
			pCell->sendEntityPositions( bundle );
		}
		else
		{
			ERROR_MSG( "EntityPositionSender::handleMessage: "
					"Do not have cell with id %u\n", id );
		}
	}
};



/**
 *	Objects of this type are used to handle variable length messages destined
 *	for a cell. They simply pass all the mercury parameters to the handler
 *	function.
 */
class CreateEntityNearEntityHandler : public EntityMessageHandler
{
public:
	/**
	 *	This type is the function pointer type that handles the incoming
	 *	message.
	 */
	typedef void (Cell::*Handler)(
		const Mercury::Address & addr,
		const Mercury::UnpackedMessageHeader & header,
		BinaryIStream & stream,
		EntityPtr pNearbyEntity );

	/**
	 *	Constructor.
	 */
	CreateEntityNearEntityHandler( Handler handler ) :
		EntityMessageHandler( REAL_ONLY, /* shouldBufferIfTickPending*/true ),
		handler_( handler )
	{
	}

	virtual void callHandler( const Mercury::Address & srcAddr,
		Mercury::UnpackedMessageHeader & header,
		BinaryIStream & data, Entity * pEntity )
	{
		(pEntity->cell().*handler_)( srcAddr, header, data, pEntity );
	}

private:
	virtual void sendFailure( const Mercury::Address & srcAddr,
			Mercury::UnpackedMessageHeader & header,
			BinaryIStream & data,
			EntityID entityID )
	{
		Mercury::ChannelVersion channelVersion;
		bool isRestore;
		EntityID entityToCreateID;
		EntityTypeID entityTypeID;

		data >> channelVersion >> isRestore >>
			entityToCreateID >> entityTypeID;
		data.finish(); // discard the rest

		DEBUG_MSG( "CreateEntityNearEntityHandler::sendFailure: "
				"for request to create cell entity %u of type %s, "
				"no such entity %u on this CellApp\n",
			entityToCreateID,
			EntityType::getType( entityTypeID )->name(),
			entityID );


		// createEntityNearEntity only comes from a BaseApp, it's
		// likely we have a channel to it somewhere.

		Mercury::UDPChannel & channel =
			CellApp::getChannel( srcAddr );
		Mercury::Bundle & bundle = channel.bundle();
		bundle.startReply( header.replyID );
		bundle << (EntityID)0;

		// TODO: Aggregate these sends and those in
		// Cell::createEntity. At the moment, each create cell
		// request from the BaseApp results in a packet with only
		// one reply message sent back to that BaseApp.
		channel.send();
	}

	Handler handler_;
};


// -----------------------------------------------------------------------------
// Section: Misc
// -----------------------------------------------------------------------------

/**
 *	This works out which space should handle this message by reading from
 *	the start of the input stream.
 */
template <>
class MessageHandlerFinder< Space >
{
public:
	static Space * find( const Mercury::Address & srcAddr,
							const Mercury::UnpackedMessageHeader & header,
							BinaryIStream & data )
	{
		SpaceID id;
		data >> id;

		CellApp & cellApp = ServerApp::getApp< CellApp >( header );
		Space * pResult = cellApp.findSpace( id );

		if (!pResult)
		{
			ERROR_MSG( "MessageHandlerFinder< Space >::find: "
						"Space %u does not exist\n",
					id );
		}

		return pResult;
	}
};


/**
 *	This works out which cell should handle this message by reading from
 *	the start of the input stream.
 */
template <>
class MessageHandlerFinder< Cell >
{
public:
	static Cell * find( const Mercury::Address & srcAddr,
			const Mercury::UnpackedMessageHeader & header,
			BinaryIStream & data )
	{
		SpaceID id;
		data >> id;

		CellApp & cellApp = ServerApp::getApp< CellApp >( header );
		Cell * pResult = cellApp.findCell( id );

		if (!pResult)
		{
			ERROR_MSG( "MessageHandlerFinder< Cell >::find: "
						"No cell for space %u\n",
					id );
		}

		return pResult;
	}
};

BW_END_NAMESPACE

// We serve this interface
#define DEFINE_SERVER_HERE
#include "cellapp_interface.hpp"

// message_handlers.cpp

#if defined( _WIN32 )
#pragma pop_macro( "interface" )
#endif
