#ifndef ENTITY_MESSAGE_LOG_SCOPE_HPP
#define ENTITY_MESSAGE_LOG_SCOPE_HPP

#include "cstdmf/log_meta_data.hpp"
#include "network/basictypes.hpp"


BW_BEGIN_NAMESPACE

/**
 *	This class sets the entity and space this thread is currently handling an
 *	entity message for on the thread-local log correlation context (see
 *	LogCorrelation), so every log message written while that entity message is
 *	being handled carries an "entityID" and, when known, a "spaceID" field.
 *
 *	It completes the correlation started in UDPBundleProcessor, which sets
 *	"channelID" for the channel the message arrived on: the entity (and the
 *	space it lives in) is what the message was for.
 *
 *	The context is restored on destruction, so the fields do not leak into the
 *	log messages of the next entity message handled on this thread.
 *
 *	It must be constructed in place: it owns a ScopedLogCorrelation and is
 *	neither copyable nor movable.
 */
class EntityMessageLogScope
{
public:
	/**
	 *	Constructor.
	 *
	 *	@param entityID The ID of the entity this message is for.
	 */
	explicit EntityMessageLogScope( EntityID entityID );

	/**
	 *	Destructor. Restores the correlation context it was constructed in.
	 */
	~EntityMessageLogScope();

	/**
	 *	Sets the spaceID correlation field.
	 *
	 *	The entity's spaceID is only read when the entity is still alive: a
	 *	destroyed entity is still findable in the population but its space has
	 *	already been released (Entity::destroy clears pSpace_ after
	 *	setDestroyed), and Entity::spaceID dereferences pSpace_ unconditionally.
	 *
	 *	@param spaceID The ID of the space the entity lives in.
	 */
	void setSpaceID( SpaceID spaceID );

private:
	EntityMessageLogScope( const EntityMessageLogScope & );
	EntityMessageLogScope & operator=( const EntityMessageLogScope & );

	ScopedLogCorrelation correlation_;
};


inline EntityMessageLogScope::EntityMessageLogScope( EntityID entityID ) :
	correlation_()
{
	LogCorrelation::set( "entityID", entityID );
}


inline EntityMessageLogScope::~EntityMessageLogScope()
{
}


inline void EntityMessageLogScope::setSpaceID( SpaceID spaceID )
{
	LogCorrelation::set( "spaceID", spaceID );
}


BW_END_NAMESPACE

#endif // ENTITY_MESSAGE_LOG_SCOPE_HPP
