#ifndef UTIL_HPP
#define UTIL_HPP

#include "common.hpp"

#include <stdlib.h>

#if defined( _WIN32 )
// GetModuleFileNameA for exePath
#include "cstdmf/cstdmf_windows.hpp"
#endif // defined( _WIN32 )

#include "math/vector2.hpp"
#include "math/vector3.hpp"
#include "network/basictypes.hpp"


BW_BEGIN_NAMESPACE

/**
 *	This namespace contains simple utility functions.
 */
namespace ServerUtil
{
	/**
	 *	This function returns the projection of a Vector3 onto the XZ plane.
	 */
	inline
	Vector2 projectXZ( const Vector3 & pos )
	{
		return Vector2( pos.x, pos.z );
	}


	/**
	 *	This function returns the square of the input value.
	 */
	template <class X>
	inline X sqr( X x )
	{
		return x * x;
	}


	/**
	 *	This returns a random value between the two input values.
	 */
	inline float randomPos( long min, long max )
	{
		return (float)((rand() % (max - min)) + min);
	}

	inline float distSqrBetween(const Vector2 & v1,
			const Vector2 & v2)
	{
		return sqr(v1.x - v2.x) + sqr(v1.y - v2.y);
	}


	/**
	 *	This function returns the square of the distance between the two input
	 *	positions.
	 */
	inline float distSqrBetween(const Vector3 & v1,
			const Vector3 & v2)
	{
		return sqr(v1.x - v2.x) +
			sqr(v1.y - v2.y) +
			sqr(v1.z - v2.z);
	}


	/**
	 *	This function converts from bits per second to a packet size (in bytes
	 *	per	packets).
	 *
	 *	@param bitsPerSecond	The data rate in bits per second that is to be
	 *							converted.
	 *	@param updateHertz		The number of packets sent per second.
	 *
	 *	@return The packet size required to achieve the input data rate.
	 */
	inline
	int bpsToPacketSize( int bitsPerSecond, int updateHertz )
	{
		return (bitsPerSecond / (NETWORK_BITS_PER_BYTE * updateHertz)) -
			UDP_OVERHEAD;
	}


	/**
	 *	This function converts from bits per second to bytes per tick.
	 *
	 *	@param bitsPerSecond	The data rate in bits per second that is to be
	 *							converted.
	 *	@param updateHertz		The number of packets sent per second.
	 *
	 *	@return Byte count
	 */
	inline
	int bpsToBytesPerTick( int bitsPerSecond, int updateHertz )
	{
		return (bitsPerSecond / (NETWORK_BITS_PER_BYTE * updateHertz));
	}


	/**
	 *	This function converts from a packet size (in bytes per packets) to the
	 *	resultant bits per second data rate.
	 *
	 *	@param packetSize		The size of the packets being sent in bytes.
	 *	@param updateHertz		The number of packets sent per second.
	 *
	 *	@return The packet size required to achieve the input data rate.
	 */
	inline
	int packetSizeToBPS( int packetSize, int updateHertz )
	{
		return (packetSize + UDP_OVERHEAD) * updateHertz *
			NETWORK_BITS_PER_BYTE;
	}


	/**
	 *	This function returns the executables full path. On POSIX this
	 *	follows the /proc/<pid>/exe link, which only works on Linux - on
	 *	other flavours of Unix the /proc filesystem is structured
	 *	differently. Windows asks the loader instead.
	 */
	inline
	BW::string exePath()
	{
		char fullExePath[1024];

#if defined( _WIN32 )
		// Windows has no /proc and no readlink (C3861 in the root
		// superproject build).
		DWORD ret = GetModuleFileNameA( NULL, fullExePath,
			sizeof( fullExePath ) );

		if ((ret == 0) || (ret >= sizeof( fullExePath )))
		{
			return BW::string();
		}

		return BW::string( fullExePath, ret );
#else
		pid_t pid = getpid();

		char linkPath[64];
		snprintf( linkPath, sizeof(linkPath), "/proc/%i/exe", pid );

		// Read the symbolic link
		int ret = readlink( linkPath, fullExePath, sizeof(fullExePath) );

		if ((ret == -1) || (ret >= int(sizeof(fullExePath))))
		{
			return BW::string();
		}

		return BW::string( fullExePath, ret );
#endif // defined( _WIN32 )
	}


	/**
	 * 	This function returns the executables directory.
	 */
	inline
	BW::string exeDir()
	{
		BW::string ourExePath = exePath();
#if defined( _WIN32 )
		BW::string::size_type slashPos = ourExePath.find_last_of( '\\' );
#else
		BW::string::size_type slashPos = ourExePath.find_last_of( '/' );
#endif // defined( _WIN32 )
		return BW::string( ourExePath, 0, slashPos );
	}
}; // namespace Util


#ifdef CODE_INLINE
//#include "util.ipp"
#endif

BW_END_NAMESPACE

#endif // UTIL_HPP

/*util.hpp*/
