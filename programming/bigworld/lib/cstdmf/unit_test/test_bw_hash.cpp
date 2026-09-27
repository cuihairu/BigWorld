#include "pch.hpp"

#include "cstdmf/bw_hash.hpp"

#include <utility>


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

// The published 64-bit FNV-1a vectors (offset basis
// 0xcbf29ce484222325, prime 0x100000001b3). The hash width follows
// sizeof( size_t ), which is 8 on this 64-bit build - a 32-bit build
// picks different constants in the same source.
const size_t FNV_EMPTY = 0xcbf29ce484222325ULL;
const size_t FNV_A = 0xaf63dc4c8601ec8cULL;
const size_t FNV_FOOBAR = 0x85944171f73967e8ULL;
const size_t FNV_HELLO = 0xa430d84680aabd0bULL;
const size_t FNV_A_NUL_B = 0xe5d29919042666b2ULL;

} // end namespace (anonymous)


// hash_string() is plain FNV-1a. These vectors pin both the offset
// basis and the prime, and the two overloads - one over a NUL-
// terminated string, one over an explicit byte count - agree whenever
// the data has no embedded NUL.
TEST( BWHash_fnv1aVectors )
{
	CHECK_EQUAL( FNV_EMPTY, hash_string( "" ) );
	CHECK_EQUAL( FNV_A, hash_string( "a" ) );
	CHECK_EQUAL( FNV_FOOBAR, hash_string( "foobar" ) );
	CHECK_EQUAL( FNV_HELLO, hash_string( "hello" ) );

	CHECK_EQUAL( FNV_FOOBAR, hash_string( "foobar", 6 ) );
	// Empty input is the untouched offset basis: the loop never runs.
	CHECK_EQUAL( FNV_EMPTY, hash_string( "", 0 ) );

	// The counted overload hashes raw bytes, so it keeps going past an
	// embedded NUL - something the NUL-terminated one cannot see.
	CHECK_EQUAL( FNV_A_NUL_B, hash_string( "a\0b", 3 ) );
	CHECK( hash_string( "a\0b", 3 ) != hash_string( "a" ) );
}


// The hash is stable for equal input and separates inputs that differ
// in length, content, case or a trailing NUL byte.
TEST( BWHash_isDeterministicAndSensitive )
{
	CHECK_EQUAL( hash_string( "BigWorld" ), hash_string( "BigWorld" ) );

	CHECK( hash_string( "foo" ) != hash_string( "foob" ) );
	CHECK( hash_string( "foo" ) != hash_string( "bar" ) );
	CHECK( hash_string( "foo" ) != hash_string( "Foo" ) );

	// A trailing NUL is hashed data, not a terminator, for the counted
	// overload.
	CHECK( hash_string( "foo\0", 4 ) != hash_string( "foo", 3 ) );
}


// hash_combine() folds a value into a running seed, so the outcome
// depends on the seed, on the value, and on the order of the calls -
// and it is deterministic for a given sequence.
TEST( BWHash_hashCombine )
{
	size_t forwards = 0;
	hash_combine( forwards, 1 );
	hash_combine( forwards, 2 );

	size_t backwards = 0;
	hash_combine( backwards, 2 );
	hash_combine( backwards, 1 );

	CHECK( forwards != backwards );

	size_t again = 0;
	hash_combine( again, 1 );
	hash_combine( again, 2 );
	CHECK_EQUAL( forwards, again );

	// Folding a value twice is not the same as folding it once.
	size_t once = 0;
	size_t twice = 0;
	hash_combine( once, 7 );
	hash_combine( twice, 7 );
	hash_combine( twice, 7 );
	CHECK( once != twice );

	// A non-zero starting seed is folded in, not overwritten.
	size_t fromZero = 0;
	size_t fromOne = 1;
	hash_combine( fromZero, 42 );
	hash_combine( fromOne, 42 );
	CHECK( fromZero != fromOne );
}


// std::hash< std::pair > is specialised in bw_hash.hpp: it runs both
// halves through hash_combine, so it is stable and order sensitive.
TEST( BWHash_pairSpecialisation )
{
	BW::hash< std::pair< int, int > > pairHash;

	CHECK_EQUAL( pairHash( std::make_pair( 1, 2 ) ),
		pairHash( std::make_pair( 1, 2 ) ) );
	CHECK( pairHash( std::make_pair( 1, 2 ) ) !=
		pairHash( std::make_pair( 2, 1 ) ) );
}

BW_END_NAMESPACE

// test_bw_hash.cpp
