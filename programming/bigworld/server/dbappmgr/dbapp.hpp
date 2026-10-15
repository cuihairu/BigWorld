#ifndef DB_APP_HPP
#define DB_APP_HPP

#include "cstdmf/smartpointer.hpp"
#include "cstdmf/watcher.hpp"

#include "db/dbapp_interface.hpp"

#include "network/channel_owner.hpp"

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

class DBApp;
typedef SmartPointer< DBApp > DBAppPtr;

/**
 *	This class represents the DBAppMgr's view of a DBApp.
 */
class DBApp: public ReferenceCount
{
public:
	DBApp( Mercury::NetworkInterface & interface, 
		const Mercury::Address & addr, DBAppID id,
		Mercury::ReplyID addReplyID );

	DBAppID id() const	{ return id_; }
	void id( int id )	{ id_ = id; }

	const Mercury::Address & address() const { return channelOwner_.addr(); }
	Mercury::ChannelOwner & channelOwner() { return channelOwner_; }

	template< typename HASH >
	void updateDBAppHash( const HASH & dbApps,
		bool shouldAlphaResetGameServerState );

	void controlledShutDown( ShutDownStage stage = SHUTDOWN_REQUEST );

	static WatcherPtr pWatcher();

	// Message handlers
	void retireApp();

private:
	DBAppID					id_;
	Mercury::ChannelOwner	channelOwner_;
	Mercury::ReplyID 		pendingReplyID_;
};


/**
 *	This method sends an update for the given DBApp hash to this DBApp.
 *
 *	@param hash 		The hash to send.
 *	@param shouldAlphaResetGameServerState
 *						For the DBApp-Alpha, if this is true, then it should
 *						reset the game server state, otherwise it has no
 *						effect if false or for non-Alpha DBApps.
 */
template< typename HASH >
void DBApp::updateDBAppHash( const HASH & hash,
		bool shouldAlphaResetGameServerState )
{
	Mercury::Bundle & bundle = channelOwner_.channel().bundle();

	if (pendingReplyID_ != Mercury::REPLY_ID_NONE)
	{
		bundle.startReply( pendingReplyID_ );
		pendingReplyID_ = Mercury::REPLY_ID_NONE;
		bundle << id_;
	}
	else
	{
		bundle.startMessage( DBAppInterface::updateDBAppHash );
	}

	bundle << uint8( shouldAlphaResetGameServerState );
	bundle << hash;

	channelOwner_.channel().send();
}


BW_END_NAMESPACE


#if defined( _WIN32 )
#pragma pop_macro( "interface" )
#endif
#endif // DB_APP_HPP
