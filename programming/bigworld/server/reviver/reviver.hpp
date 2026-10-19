#ifndef REVIVER_HPP
#define REVIVER_HPP

#include "cstdmf/singleton.hpp"
#include "network/machine_guard.hpp"
#include "server/server_app.hpp"

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

class ReviverConfig;

namespace Mercury
{
class EventDispatcher;
class NetworkInterface;
}

class ComponentReviver;
typedef BW::vector< ComponentReviver * > ComponentRevivers;

extern ComponentRevivers * g_pComponentRevivers;

/**
 *	This class is used to represent the reviver process. It monitors for the
 *	unexpected death of processes and starts new ones.
 */
class Reviver : public ServerApp, public TimerHandler,
	public Singleton< Reviver >
{
public:
	SERVER_APP_HEADER( Reviver, reviver )

	typedef ReviverConfig Config;

	Reviver( Mercury::EventDispatcher & mainDispatcher,
			Mercury::NetworkInterface & interface );
	virtual ~Reviver();

	bool queryMachinedSettings();

	void shutDown();

	void revive( const char * createComponent );

	bool hasEnabledComponents() const;

	// Overrides from TimerHandler
	virtual void handleTimeout( TimerHandle handle, void * arg );

	void markAsDirty()			{ isDirty_ = true; }

	/**
	 *	This class is used to handle a reply from BWMachined telling us the tags
	 *	associated with this machine.
	 */
	class TagsHandler : public MachineGuardMessage::ReplyHandler
	{
	public:
		TagsHandler( Reviver &reviver ) : reviver_( reviver ) {}
		virtual bool onTagsMessage( TagsMessage &tm, uint32 addr );

	private:
		Reviver &reviver_;
	};

private:
	virtual bool init( int argc, char * argv[] );
	virtual bool run();

	enum TimeoutType
	{
		TIMEOUT_REATTACH,
		TIMEOUT_TICK
	};

	TimerHandle						timerHandle_;
	TimerHandle						tickTimer_;

	ComponentRevivers	components_;

	bool				shuttingDown_;
	bool				isDirty_; // For output
};

BW_END_NAMESPACE


#if defined( _WIN32 )
#pragma pop_macro( "interface" )
#endif
#endif // REVIVER_HPP
