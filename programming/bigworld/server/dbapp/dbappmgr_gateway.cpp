#include "dbappmgr_gateway.hpp"

#include "db/dbappmgr_interface.hpp"

#include "network/bundle.hpp"
#include "network/machined_utils.hpp"

#if defined( _WIN32 )
// MSVC's SDK leaves `#define interface struct` (rpcndr.h via d3d9.h / shlobj /
// shlwapi / d3d10) live for the rest of the TU, which rewrites the
// `interface`-named declarations in this file into syntax errors (C2332/C2027
// in the root superproject build). Off for this file only; the pop below
// restores it for whatever SDK COM header comes later in the TU.
#pragma push_macro( "interface" )
#undef interface
#endif

DECLARE_DEBUG_COMPONENT( 0 )


BW_BEGIN_NAMESPACE

// -----------------------------------------------------------------------------
// Section: DB App Manager
// -----------------------------------------------------------------------------


/**
 *	The constructor for DBAppMgrGateway.
 */
DBAppMgrGateway::DBAppMgrGateway( Mercury::NetworkInterface & interface ) :
	ManagerAppGateway( interface, DBAppMgrInterface::retireApp )
{}


/**
 *	This method changes the address to DBAppMgr.
 *
 *	@param address 	The new DBAppMgr address.
 */
void DBAppMgrGateway::address( const Mercury::Address & address )
{
	channel_.addr( address );
}


/**
 *	This method is used to register this DBApp to the DBAppMgr.
 */
void DBAppMgrGateway::addDBApp( Mercury::ReplyMessageHandler * pReplyHandler )
{
	Mercury::Bundle & bundle = channel_.bundle();
	bundle.startRequest( DBAppMgrInterface::addDBApp, pReplyHandler );

	channel_.send();
}


/**
 *	This method notifies DBAppMgr from DBApp-Alpha that server startup
 *	initialisation has been completed.
 */
void DBAppMgrGateway::notifyServerStartupComplete()
{
	Mercury::Bundle & bundle = channel_.bundle();
	bundle.startMessage( DBAppMgrInterface::serverHasStarted );

	channel_.send();
}


/**
 *	This method sends recovery information about this DBApp to a newly started
 *	instance of DBAppMgr.
 */
void DBAppMgrGateway::recoverDBApp( DBAppID id )
{
	DBAppMgrInterface::recoverDBAppArgs & args =
		args.start( channel_.bundle() );
	args.id = id;
	channel_.send();
}


/**
 *	This method makes a request to the DBAppMgr to start a controlled shutdown.
 */
void DBAppMgrGateway::requestControlledShutDown()
{
	DBAppMgrInterface::controlledShutDownArgs & args =
		args.start( channel_.bundle() );

	args.stage = SHUTDOWN_REQUEST;

	channel_.send();
}


BW_END_NAMESPACE

// dbappmgr_gateway.cpp

#if defined( _WIN32 )
#pragma pop_macro( "interface" )
#endif
