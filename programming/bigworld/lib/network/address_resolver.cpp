#include "address_resolver.hpp"

#if defined( _WIN32 )
	// Winsock2 provides the socket core (socket/bind, hostent/gethostbyname,
	// AF_INET, sockaddr_in); getaddrinfo/freeaddrinfo and inet_ntop live in
	// ws2tcpip.h (C3861 in the root superproject build), so only the include
	// list differs from POSIX and the body below is shared. Lean windows.h
	// first, same order as endpoint.hpp.
	#include "cstdmf/cstdmf_windows.hpp"
	#include <Winsock2.h>
	#include <ws2tcpip.h>
#else
	#include <netdb.h>
	#include <arpa/inet.h>
	#include <sys/socket.h>
	#include <sys/types.h>
#endif

BW_BEGIN_NAMESPACE

/**
 *	Resolve a host name to a dotted-decimal string representation of its IP
 *	address.
 *
 *	@param hostName		The host name to resolve.
 *	@param out 			If successful, this is filled with the dotted-decimal
 *						representation of the hostname's resolved address.
 *
 *	@return				true on success, false otherwise.
 */
bool Mercury::AddressResolver::resolve( const BW::string & hostName, 
		BW::string & out )
{
#if defined( EMSCRIPTEN )
	// No getaddrinfo(), fall back to gethostbyname() which is implemented.

	struct hostent * hostInfo = gethostbyname( hostName.c_str() );
	if (!hostInfo)
	{
		return false;
	}

	out.assign( hostInfo->h_name );
	return true;

#else // !defined( EMSCRIPTEN )

	struct addrinfo * addrInfo = NULL;
	int ret;
	if (0 != (ret = getaddrinfo( hostName.c_str(), NULL, NULL, &addrInfo )))
	{
		return false;
	}

	if (addrInfo->ai_family != AF_INET)
	{
		// We don't support anything other than IPv4 currently.
		return false;
	}

	char buf[64];
	struct sockaddr_in * addr = 
		reinterpret_cast< struct sockaddr_in * >( addrInfo->ai_addr );

	if (NULL == inet_ntop( addrInfo->ai_family, &(addr->sin_addr), 
			buf, sizeof( buf ) ))
	{
		return false;
	}
	out.assign( buf );

	freeaddrinfo( addrInfo );

	return true;

#endif // defined( EMSCRIPTEN )
}

BW_END_NAMESPACE

// address_resolver.cpp
