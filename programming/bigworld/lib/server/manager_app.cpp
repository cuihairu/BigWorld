#include "manager_app.hpp"

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
// Section: ManagerApp
// -----------------------------------------------------------------------------

/**
 *	Constructor.
 */
ManagerApp::ManagerApp( Mercury::EventDispatcher & mainDispatcher,
			Mercury::NetworkInterface & interface ) :
	ServerApp( mainDispatcher, interface )
{
}

/**
 *	This method adds the watchers associated with this class.
 */
void ManagerApp::addWatchers( Watcher & watcher )
{
	ServerApp::addWatchers( watcher );
}

BW_END_NAMESPACE

// manager_app.hpp

#if defined( _WIN32 )
#pragma pop_macro( "interface" )
#endif
