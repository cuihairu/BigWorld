#include "pch.hpp"

#include "cstdmf/cstdmf_init.hpp"
#include "resmgr/bdiff.hpp"
#include "test_harness.hpp"

#include <stdio.h>
#include <string.h>


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

/**
 *	The block size performDiff() is tuned for: six bytes for a diff record,
 *	two for a literal length and one spare byte before it will even look at
 *	a block of input. Inputs that size or less are refused outright.
 */
const unsigned int BS = 9;

/**
 *	performDiff() has no instance state; the fixture only brings up a CStdMf
 *	because it logs the trailing literal block with INFO_MSG.
 */
class Fixture : public ResMgrUnitTestHarness
{
public:
	Fixture() : ResMgrUnitTestHarness() { new CStdMf; }
	~Fixture() { delete CStdMf::pInstance(); }
};


/**
 *	Slurp a diff stream into a buffer. performDiff() only ever writes through
 *	stdio, so the data has to be flushed before it can be read back.
 */
BW::vector<unsigned char> slurp( FILE * f, long & sizeOut )
{
	fflush( f );
	fseek( f, 0, SEEK_END );
	sizeOut = ftell( f );
	fseek( f, 0, SEEK_SET );

	BW::vector<unsigned char> buf;
	if (sizeOut > 0)
	{
		buf.resize( (size_t)sizeOut );
		if (fread( &buf[0], 1, (size_t)sizeOut, f ) != (size_t)sizeOut)
		{
			sizeOut = -1;
			buf.clear();
		}
	}
	return buf;
}


/**
 *	Replay a diff stream against the "first" buffer, the way a patcher would.
 *
 *	Record format, all counts in the host's byte order (both sides of this
 *	test are the same process):
 *	 - unsigned short with the top bit clear: that many literal bytes follow
 *	 - unsigned short with the top bit set, followed by an unsigned source
 *	   offset: a match of (count & 0x7FFF) bytes taken from first[offset]
 *
 *	Stops at the first malformed record, so a broken stream can never read
 *	past the end of either buffer.
 */
BW::vector<unsigned char> apply( const BW::vector<unsigned char> & diff,
								 const BW::vector<unsigned char> & first )
{
	BW::vector<unsigned char> out;
	size_t p = 0;
	while (p + sizeof(unsigned short) <= diff.size())
	{
		unsigned short count = 0;
		memcpy( &count, &diff[p], sizeof( count ) );
		p += sizeof( count );

		if (count & 0x8000)
		{
			if (p + sizeof(unsigned) > diff.size())
			{
				break;
			}
			unsigned offset = 0;
			memcpy( &offset, &diff[p], sizeof( offset ) );
			p += sizeof( offset );

			const size_t len = (size_t)(count & 0x7FFF);
			if (offset + len > first.size())
			{
				break;
			}
			out.insert( out.end(), first.begin() + offset,
				first.begin() + offset + len );
		}
		else
		{
			if (p + count > diff.size())
			{
				break;
			}
			out.insert( out.end(), diff.begin() + p, diff.begin() + p + count );
			p += count;
		}
	}
	return out;
}


/**
 *	Deterministic pseudo random filler. The high bits of an LCG make it fine
 *	as incompressible input for these tests: long enough sequences are free
 *	of repeated 9 byte blocks, which is what the excess literal test needs.
 */
BW::vector<unsigned char> pseudoRandom( uint32 seed, size_t n )
{
	BW::vector<unsigned char> buf;
	buf.resize( n );
	for ( size_t i = 0; i < n; i++ )
	{
		seed = 1103515245u * seed + 12345u;
		buf[i] = (unsigned char)((seed >> 16) & 0xFF);
	}
	return buf;
}


/**
 *	A printable run of bytes, as a BW::vector for insertion.
 */
BW::vector<unsigned char> text( const char * s )
{
	return BW::vector<unsigned char>( s, s + strlen( s ) );
}

} // end namespace (anonymous)


// Inputs of BS bytes or less are not worth diffing, and nothing is written.
TEST_F( Fixture, bdiff_refusesTinyInputs )
{
	FILE * f = tmpfile();
	ASSERT_WITH_MESSAGE( f != NULL, "could not create a temporary file" );

	// both sides short, in turn
	const char * tiny = "123456789";
	BW::vector<unsigned char> nine( tiny, tiny + BS );
	BW::vector<unsigned char> eight( tiny, tiny + BS - 1 );

	CHECK( !performDiff( nine, nine, f ) );
	CHECK( !performDiff( eight, eight, f ) );

	// only one side short
	BW::vector<unsigned char> big = pseudoRandom( 12345, BS + 1 );
	CHECK( !performDiff( big, nine, f ) );
	CHECK( !performDiff( nine, big, f ) );

	// empty
	BW::vector<unsigned char> empty;
	CHECK( !performDiff( empty, empty, f ) );

	// none of that may have written a byte
	CHECK_EQUAL( 0L, ftell( f ) );
	fclose( f );
}


// Two identical buffers collapse into a single match record: a two byte
// "length | 0x8000" plus the four byte source offset, six bytes in total.
TEST_F( Fixture, bdiff_identicalBuffersAreOneMatchRecord )
{
	BW::vector<unsigned char> v1 = pseudoRandom( 7, 20 );

	FILE * f = tmpfile();
	ASSERT_WITH_MESSAGE( f != NULL, "could not create a temporary file" );

	CHECK( performDiff( v1, v1, f ) );

	long size = 0;
	BW::vector<unsigned char> diff = slurp( f, size );
	fclose( f );

	ASSERT_WITH_MESSAGE( size == 6, "expected a six byte diff" );
	// the count is a host order unsigned short, so the low byte comes first
	CHECK_EQUAL( 20, (int)((diff[0] | (diff[1] << 8)) & 0x7FFF) );
	CHECK( (diff[1] & 0x80) != 0 );	// a match record, not a literal
	CHECK_EQUAL( 0, (int)diff[2] );	// source offset 0
	CHECK_EQUAL( 0, (int)diff[3] );
	CHECK_EQUAL( 0, (int)diff[4] );
	CHECK_EQUAL( 0, (int)diff[5] );

	CHECK( apply( diff, v1 ) == v1 );
}


// A differing head is emitted as a literal record, the shared tail as a
// match record pointing back into the first buffer.
TEST_F( Fixture, bdiff_literalHeadThenMatch )
{
	// "cdABCDEFGHIJKLMNOPQRST" against "abABCDEFGHIJKLMNOPQRST"
	BW::vector<unsigned char> tail = text( "ABCDEFGHIJKLMNOPQRST" );

	BW::vector<unsigned char> v1 = text( "ab" );
	v1.insert( v1.end(), tail.begin(), tail.end() );

	BW::vector<unsigned char> v2 = text( "cd" );
	v2.insert( v2.end(), tail.begin(), tail.end() );

	FILE * f = tmpfile();
	ASSERT_WITH_MESSAGE( f != NULL, "could not create a temporary file" );

	CHECK( performDiff( v1, v2, f ) );

	long size = 0;
	BW::vector<unsigned char> diff = slurp( f, size );
	fclose( f );

	// two literal bytes, then a twenty byte match at source offset two
	ASSERT_WITH_MESSAGE( size == 10, "expected a ten byte diff" );
	CHECK_EQUAL( 2, (int)(diff[0] | (diff[1] << 8)) );
	CHECK_EQUAL( 'c', (int)diff[2] );
	CHECK_EQUAL( 'd', (int)diff[3] );
	CHECK_EQUAL( 20 | 0x8000, (int)(diff[4] | (diff[5] << 8)) );
	CHECK_EQUAL( 2, (int)diff[6] );

	CHECK( apply( diff, v1 ) == v2 );
}


// A literal block bigger than the stdio buffer is emitted a byte at a time,
// so the stream can fail half way through the loop.
TEST_F( Fixture, bdiff_writeErrorsAreReported )
{
	// Two 5000 byte pseudo random heads with no common 9 byte block, then a
	// shared 20 byte tail: the head has to go out as one literal record,
	// which is longer than any stdio buffer.
	BW::vector<unsigned char> tail = text( "QWERTYUIOPASDFGHJKLZ" );

	BW::vector<unsigned char> v1 = pseudoRandom( 11, 5000 );
	v1.insert( v1.end(), tail.begin(), tail.end() );

	BW::vector<unsigned char> v2 = pseudoRandom( 22, 5000 );
	v2.insert( v2.end(), tail.begin(), tail.end() );

	// sanity: the same pair does diff cleanly into a readable stream
	FILE * ok = tmpfile();
	ASSERT_WITH_MESSAGE( ok != NULL, "could not create a temporary file" );
	CHECK( performDiff( v1, v2, ok ) );
	long okSize = 0;
	BW::vector<unsigned char> good = slurp( ok, okSize );
	fclose( ok );
	CHECK( apply( good, v1 ) == v2 );

	// /dev/full takes buffered writes and fails on the flush, which is the
	// "the literal loop hit an error" arm. Not available everywhere.
	FILE * full = fopen( "/dev/full", "w" );
	if (full != NULL)
	{
		CHECK( !performDiff( v1, v2, full ) );
		fclose( full );
	}

	// A stream opened for reading cannot be written to at all, so the very
	// first record fails instead.
	BW::vector<unsigned char> same = pseudoRandom( 3, 20 );
	FILE * ro = fopen( "/dev/null", "r" );
	if (ro != NULL)
	{
		CHECK( !performDiff( same, same, ro ) );
		fclose( ro );
	}
}


// A run longer than 32767 bytes with nothing matchable in it is flushed as a
// literal record, and the final record picks up whatever follows it.
//
// This pins down a real defect: the flush sets lastMatch to i + 1, so v2[i]
// itself is never emitted and the reconstructed stream is one byte short.
// performDiff() does report failure here (the output grew larger than the
// input), but the caller cannot tell that apart from "not worth patching".
TEST_F( Fixture, bdiff_excessLiteralFlushDropsOneByte )
{
	// 40000 bytes each with no 9 byte block in common, so no match is found
	BW::vector<unsigned char> v1 = pseudoRandom( 1, 40000 );
	BW::vector<unsigned char> v2 = pseudoRandom( 987654321u, 40000 );

	FILE * f = tmpfile();
	ASSERT_WITH_MESSAGE( f != NULL, "could not create a temporary file" );

	CHECK( !performDiff( v1, v2, f ) );

	long size = 0;
	BW::vector<unsigned char> diff = slurp( f, size );
	fclose( f );

	// two literal records: 32767 bytes, then 40000 - 32768
	ASSERT_WITH_MESSAGE( size == 40003, "expected two literal records" );
	CHECK_EQUAL( 32767, (int)(diff[0] | (diff[1] << 8)) );
	const size_t secondCountAt = 2 + 32767;
	CHECK_EQUAL( 40000 - 32768,
		(int)(diff[secondCountAt] | (diff[secondCountAt + 1] << 8)) );

	// what comes back is v2 without v2[32767]
	BW::vector<unsigned char> expected;
	expected.insert( expected.end(), v2.begin(), v2.begin() + 32767 );
	expected.insert( expected.end(), v2.begin() + 32768, v2.end() );

	BW::vector<unsigned char> got = apply( diff, v1 );
	CHECK_EQUAL( (int)v2.size() - 1, (int)got.size() );
	CHECK( got == expected );
}

BW_END_NAMESPACE

// test_bdiff.cpp
