#include "murmur_hash.hpp"


BW_BEGIN_NAMESPACE


/**
 *	This function implements the MurmurHash2 function for a 64-bit word size.
 *
 *	Adapted from the MurmurHash2 implementation at:
 *
 *		https://sites.google.com/site/murmurhash/
 *
 *	With some modifications below to make constants function-static, and to use
 *	size_t instead of int for the length parameter.
 *
 *	@param key  	The data to hash.
 *	@param len	 	The length of the data to hash.
 *	@param seed		Value to seed the hash.
 *
 *	@return			A hash of the data.
 */
uint64 MurmurHash::hash( const void * key, size_t len, uint seed )
{
	static const uint64_t m = 0xc6a4a7935bd1e995;
	static const int r = 47;

	uint64_t h = seed ^ (len * m);

	const uint64_t * data = (const uint64_t *)key;
	const uint64_t * end = data + (len/8);

	while(data != end)
	{
		uint64_t k = *data++;

		k *= m;
		k ^= k >> r;
		k *= m;

		h ^= k;
		h *= m;
	}

	const unsigned char * data2 = (const unsigned char*)data;

	switch(len & 7)
	{
	case 7: h ^= uint64_t(data2[6]) << 48;	/* fall through */
	case 6: h ^= uint64_t(data2[5]) << 40;	/* fall through */
	case 5: h ^= uint64_t(data2[4]) << 32;	/* fall through */
	case 4: h ^= uint64_t(data2[3]) << 24;	/* fall through */
	case 3: h ^= uint64_t(data2[2]) << 16;	/* fall through */
	case 2: h ^= uint64_t(data2[1]) << 8;	/* fall through */
	case 1: h ^= uint64_t(data2[0]);
		h *= m;
	};

	h ^= h >> r;
	h *= m;
	h ^= h >> r;

	return h;
}


BW_END_NAMESPACE


// murmur_hash.cpp
