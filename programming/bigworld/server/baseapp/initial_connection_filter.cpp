#include "initial_connection_filter.hpp"

#include "network/packet_receiver.hpp"
#include "network/basictypes.hpp"

#include "connection/baseapp_ext_interface.hpp"

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


InitialConnectionFilterPtr InitialConnectionFilter::create()
{
	return new InitialConnectionFilter();
}

/**
 *  Destructor.
 */
InitialConnectionFilter::~InitialConnectionFilter()
{
}
/**
 *	This method processes an off channel, raw packet from external source.
 *	On a BaseApp, the only such packet we expect is a single 
 *	BaseAppExtInterface::baseAppLogin request
 */
Mercury::Reason InitialConnectionFilter::recv( Mercury::PacketReceiver & receiver, 
									const Mercury::Address & addr,
									Mercury::Packet * pPacket, 
									Mercury::ProcessSocketStatsHelper * pStatsHelper )
{
	if (//Allow a request only
		(pPacket->flags() == Mercury::Packet::FLAG_HAS_REQUESTS) &&
		//data is 16 bytes long:
		(pPacket->totalSize() ==
				sizeof( Mercury::Packet::Flags ) +
				sizeof( Mercury::MessageID ) +
				sizeof( BaseAppExtInterface::baseAppLoginArgs ) +
				sizeof( Mercury::ReplyID ) +
				2 * sizeof( Mercury::Packet::Offset )) &&
		//msgId is - 'baseAppLogin'.
		(pPacket->body()[0] == BaseAppExtInterface::baseAppLogin.id()))
	{
		return this->Mercury::PacketFilter::recv(
					receiver, addr, pPacket, pStatsHelper );
	}

	if (receiver.isVerbose())
	{
		WARNING_MSG( "InitialConnectionFilter::recv( %s ): "
			"Dropping unexpected packet on BaseApps "
			"external interface (noise/malicious) \n",
			addr.c_str() );
	}

	return Mercury::REASON_GENERAL_NETWORK;  //drop the packet
}


/*
 *	Override from PacketFilter.
 */
int InitialConnectionFilter::maxSpareSize()
{
	return 0;
}


BW_END_NAMESPACE

#if defined( _WIN32 )
#pragma pop_macro( "interface" )
#endif
