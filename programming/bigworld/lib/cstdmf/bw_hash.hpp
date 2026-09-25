#ifndef BW_HASH_HPP
#define BW_HASH_HPP

#include "cstdmf/cstdmf_dll.hpp"

// BIGWORLD(c++23 migration): all supported toolchains provide std::hash in
// <functional>; the old std::tr1 indirection is gone.
#include <functional>
#define BW_HASH_NAMESPACE_BEGIN namespace std {
#define BW_HASH_NAMESPACE_END }
#define BW_HASH_NAMESPACE std::

#include <cstddef>
#include <utility>

namespace BW
{
	CSTDMF_DLL std::size_t hash_string( const void* ptr, std::size_t size );
	CSTDMF_DLL std::size_t hash_string( const char* str );

using std::hash;

///For combining hashes from multiple sources.
template<typename T>
void hash_combine( std::size_t & seed, const T & value )
{
	BW::hash<T> hasher;
	seed ^= hasher( value ) +
		//A random value.
		0xBC6EF372 +
		//make sure bits spread across the output even if input hashes
		//have a small output range.
		(seed << 5) + (seed >> 3);
}
} // namespace BW

BW_HASH_NAMESPACE_BEGIN

///Specialize hashing for std::pair.
template<typename S, typename T>
struct hash< std::pair< S, T > >
{
public:
	std::size_t operator()( const std::pair< S, T > & v ) const
	{
		std::size_t seed = 0;
		BW::hash_combine( seed, v.first );
		BW::hash_combine( seed, v.second );
		return seed;
	}
};

BW_HASH_NAMESPACE_END

#endif // BW_HASH_HPP
