#ifndef DBAPPMGR_GATEWAY_HPP
#define DBAPPMGR_GATEWAY_HPP

#include "cstdmf/stdmf.hpp"
#include "cstdmf/watcher.hpp"
#include "network/basictypes.hpp"
#include "network/channel_owner.hpp"
#include "network/misc.hpp"

#include "server/common.hpp"
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

typedef uint8 SharedDataType;

class DBApp;

/**
 * 	This is a simple helper class that is used to represent the remote db
 * 	manager.
 */
class DBAppMgrGateway : public ManagerAppGateway
{
public:
	DBAppMgrGateway( Mercury::NetworkInterface & interface );
	const Mercury::Address & address() const 
	{
		return this->ManagerAppGateway::address();
	}

	void address( const Mercury::Address & address );

	void addDBApp( Mercury::ReplyMessageHandler * pReplyHandler );
	void notifyServerStartupComplete();
	void recoverDBApp( DBAppID id );

	void requestControlledShutDown();
};

BW_END_NAMESPACE


#if defined( _WIN32 )
#pragma pop_macro( "interface" )
#endif
#endif // DBAPPMGR_GATEWAY_HPP
