#ifndef DB_APP_INTERFACE_UTILS_HPP
#define DB_APP_INTERFACE_UTILS_HPP

#include "pyscript/script.hpp"
#include "network/channel.hpp"

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

namespace Mercury
{
	class NetworkInterface;
}
class BinaryIStream;
class BinaryOStream;

namespace DBAppInterfaceUtils
{
	bool executeRawDatabaseCommand( const BW::string & command,
		PyObjectPtr pResultHandler, Mercury::Channel & channel );

	bool executeRawDatabaseCommand( const BW::string & command,
		PyObjectPtr pResultHandler, Mercury::NetworkInterface & interface,
		const Mercury::Address & dbAppAddr );
}

BW_END_NAMESPACE


#if defined( _WIN32 )
#pragma pop_macro( "interface" )
#endif
#endif // DB_APP_INTERFACE_UTILS_HPP
