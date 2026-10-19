#include "script/first_include.hpp"		// See http://docs.python.org/api/includes.html

#include "cell_app_channel.hpp"
#include "cellapp.hpp"
#include "cellapp_config.hpp"

#include "network/event_dispatcher.hpp"

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
// Section: CellAppChannel
// -----------------------------------------------------------------------------

/**
 *	Constructor.
 */
CellAppChannel::CellAppChannel( const Mercury::Address & addr ) :
	Mercury::ChannelOwner( CellApp::instance().interface(), addr ),
	mark_( 0 ),
	lastReceivedTime_( 0 )
{
	TRACE_MSG( "CellAppChannel::CellAppChannel: Created to %s\n",
		this->channel().c_str() );

	this->channel().isLocalRegular( true );
	this->channel().isRemoteRegular( false );
}

BW_END_NAMESPACE

// cell_app_channel.cpp

#if defined( _WIN32 )
#pragma pop_macro( "interface" )
#endif
