#ifndef SCRIPT_APP_HPP
#define SCRIPT_APP_HPP

#include "server_app.hpp"

#include "pyscript/script_events.hpp"

#if defined( _WIN32 )
// MSVC's SDK leaves `#define interface struct` (rpcndr.h via d3d9.h / shlobj /
// shlwapi / d3d10) live for the rest of the TU, which rewrites the
// `interface`-named declarations in this file into syntax errors (C2332/C2027
// in the root superproject build). Off for this file only; the pop below
// restores it for whatever SDK COM header comes later in the TU.
#pragma push_macro( "interface" )
#undef interface
#endif

#define SCRIPT_APP_HEADER SERVER_APP_HEADER


BW_BEGIN_NAMESPACE

class PythonServer;

/**
 *	This class is a comman base class for apps with Python scripting.
 */
class ScriptApp : public ServerApp
{
public:
	ScriptApp( Mercury::EventDispatcher & mainDispatcher,
			Mercury::NetworkInterface & interface );
	virtual ~ScriptApp();

	virtual bool init( int argc, char * argv[] );

	bool initPersonality();
	bool triggerOnInit( bool isReload = false );

	void startPythonServer( uint16 port, int appID );

	ScriptEvents & scriptEvents() { return scriptEvents_; }

	bool triggerDelayableEvent( const char * eventName, PyObject * pArgs );

protected:
	virtual void fini();

	bool initScript( const char * componentName,
			const char * scriptPath1, const char * scriptPath2 = NULL );

private:
	ScriptEvents scriptEvents_;
	PythonServer * pPythonServer_;
};

BW_END_NAMESPACE


#if defined( _WIN32 )
#pragma pop_macro( "interface" )
#endif
#endif // SCRIPT_APP_HPP
