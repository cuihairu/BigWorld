#ifndef MANAGER_APP_HPP
#define MANAGER_APP_HPP

#include "server_app.hpp"

#if defined( _WIN32 )
// MSVC's SDK leaves `#define interface struct` (rpcndr.h via d3d9.h / shlobj /
// shlwapi / d3d10) live for the rest of the TU, which rewrites the
// `interface`-named declarations in this file into syntax errors (C2332/C2027
// in the root superproject build). Off for this file only; the pop below
// restores it for whatever SDK COM header comes later in the TU.
#pragma push_macro( "interface" )
#undef interface
#endif

#define MANAGER_APP_HEADER SERVER_APP_HEADER


BW_BEGIN_NAMESPACE

/**
 *	This class is a common base class for BaseAppMgr and CellAppMgr.
 */
class ManagerApp : public ServerApp
{
public:
	ManagerApp( Mercury::EventDispatcher & mainDispatcher,
			Mercury::NetworkInterface & interface );

protected:
	void addWatchers( Watcher & watcher );

private:
};

BW_END_NAMESPACE


#if defined( _WIN32 )
#pragma pop_macro( "interface" )
#endif
#endif // MANAGER_APP_HPP
