#include "nat_config.hpp"

// Winsock2 provides AF_INET and struct in_addr; inet_pton itself lives in
// ws2tcpip.h (C3861 in the root superproject build), so only the include
// list differs from POSIX (same split as lib/network/address_resolver.cpp).
#if defined( _WIN32 )
	#include "cstdmf/cstdmf_windows.hpp"
	#include <Winsock2.h>
	#include <ws2tcpip.h>
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
#endif

BW_BEGIN_NAMESPACE

namespace
{

// inet_addr's return type differs per platform (in_addr_t on POSIX, struct
// in_addr on Windows) and it is deprecated under /WX on MSVC (C4996), so the
// three lookups below go through inet_pton instead. INADDR_NONE preserves
// inet_addr's "not a dotted quad" sentinel for the ERROR_MSG callers.
uint32 inetAddressFromString( const BW::string & address )
{
	struct in_addr parsed;

	if (::inet_pton( AF_INET, address.c_str(), &parsed ) != 1)
	{
		return INADDR_NONE;
	}

	return parsed.s_addr;
}

} // anonymous namespace

BW_END_NAMESPACE

// These need to be defined before including server_app_option_macros.hpp
#define BW_CONFIG_CLASS NATConfig
#define BW_CONFIG_PREFIX "networkAddressTranslation/"
#include "server/server_app_option_macros.hpp"


BW_BEGIN_NAMESPACE

// -----------------------------------------------------------------------------
// Section: General settings
// -----------------------------------------------------------------------------

BW_OPTION_RO( BW::string, localNetMask, "" );
BW_OPTION_RO( BW::string, externalAddress, "" );


NetMask NATConfig::s_netMask_;
uint32 NATConfig::s_externalIP_;
NATConfig::ExternalIPs NATConfig::s_externalIPs_;


/**
 *	This method returns whether an IP address belongs to the internal network.
 */
bool NATConfig::isInternalIP( uint32 ip )
{
	return s_netMask_.containsAddress( ip );
}


/**
 *	This method returns whether an IP address does not belong to the internal
 *	network.
 */
bool NATConfig::isExternalIP( uint32 ip )
{
	return !isInternalIP( ip );
}


/**
 *	This method returns the external IP address associated with the given
 *	internal IP.
 */
uint32 NATConfig::externalIPFor( uint32 internalIP )
{
	ExternalIPs::const_iterator iter = s_externalIPs_.find( internalIP );

	return (iter != s_externalIPs_.end()) ? iter->second : s_externalIP_;
}


/**
 *	This method should be called if the application makes use of NATConfig.
 */
bool NATConfig::postInit()
{
	if (localNetMask() != "")
	{
		if (!s_netMask_.parse( localNetMask().c_str() ))
		{
			ERROR_MSG( "LoginApp::init: Failed to parse "
					"networkAddressTranslation/localNetMask '%s' from bw.xml. "
					"It should use CIDR notation. e.g. 192.168.0.0/16\n",
				localNetMask().c_str() );

			return false;
		}
	}

	s_externalIP_ = inetAddressFromString( externalAddress() );

	DataSectionPtr pExternalAddresses =
		BWConfig::getSection( "networkAddressTranslation/externalAddresses" );

	if (pExternalAddresses)
	{
		DataSection::iterator iter = pExternalAddresses->begin();

		while (iter != pExternalAddresses->end())
		{
			DataSectionPtr pDS = *iter;
			uint32 intIP = inetAddressFromString( pDS->readString( "internal" ) );
			uint32 extIP = inetAddressFromString( pDS->readString( "external" ) );

			s_externalIPs_[ intIP ] = extIP;

			++iter;
		}
	}

	return true;
}

BW_END_NAMESPACE

// nat_config.cpp
