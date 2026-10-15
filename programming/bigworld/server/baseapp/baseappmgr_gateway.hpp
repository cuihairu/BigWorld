#ifndef BASEAPPMGR_GATEWAY
#define BASEAPPMGR_GATEWAY

#include "server/manager_app_gateway.hpp"

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

class BackupHash;
class BaseApp;
class Base;

namespace Mercury
{
	class Address;
	class Bundle;
	class NetworkInterface;
	class ReplyMessageHandler;
} // end namespace Mercury


class BaseAppMgrGateway : public ManagerAppGateway
{
public:
	BaseAppMgrGateway( Mercury::NetworkInterface & interface );

	// TODO:BAR This is a good candidate for pushing up to ManagerAppGateway,
	// and have EntityApp have a pure virtual method e.g.
	// addManagerAppRebirthData().
	void onManagerRebirth( BaseApp & baseApp, const Mercury::Address & addr );

	void add( const Mercury::Address & addrForCells,
		const Mercury::Address & addrForClients, bool isServiceApp,
		Mercury::ReplyMessageHandler * pHandler );

	void useNewBackupHash( const BackupHash & entityToAppHash,
		const BackupHash & newEntityToAppHash );

	void informOfArchiveComplete( const Mercury::Address & deadBaseAppAddr );

	void finishedInit();

	void registerBaseGlobally( const BW::string & pickledKey,
		const EntityMailBoxRef & mailBox, 
		Mercury::ReplyMessageHandler * pHandler );

	void deregisterBaseGlobally( const BW::string & pickledKey );

	void registerServiceFragment( Base * pBaseEntity );
	void deregisterServiceFragment( const BW::string & serviceName );

	// TODO:BAR Remove these eventually
	const Mercury::Address & addr() const 	{ return channel_.addr(); }
	Mercury::Bundle & bundle() 				{ return channel_.bundle(); }
	void send() 							{ channel_.send(); }
};

BW_END_NAMESPACE


#if defined( _WIN32 )
#pragma pop_macro( "interface" )
#endif
#endif // BASEAPPMGR_GATEWAY
