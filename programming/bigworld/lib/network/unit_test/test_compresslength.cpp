#include <stdio.h>
#include <string.h>
#include "cstdmf/memory_stream.hpp"
#include "network/interface_element.hpp"
#include "network/udp_bundle.hpp"
#include "network/udp_bundle_processor.hpp"

#include "CppUnitLite2/src/CppUnitLite2.h"


BW_BEGIN_NAMESPACE

#define STEP 1000
#define STEPTYPE unsigned int
#define FILL_VALUE( i, j ) (STEPTYPE)( 0xffff0000 | i )
#define LAST_ROW 0xAB

class TestCompressLength
{
private:
	static void prepareBundle(
		Mercury::UDPBundle & bundle,
		unsigned int size,
		vector< void* > & packets )
	{
		unsigned i;
		int left;

		for (i = 0, left = size; left > STEP ; i++, left -= STEP)
		{
			// Write the data to a stack buffer first as gcc will
			// optimise for 4-byte alignment if we cast the result of
			// bundle.reserve directly to STEPTYPE* (because aliasing)
			BW_STATIC_ASSERT( STEP % sizeof( STEPTYPE ) == 0,
				STEP_must_contain_whole_number_of_STEPTYPE_values );
			STEPTYPE p[ STEP/sizeof( STEPTYPE ) ];
			for (unsigned j = 0; j < STEP/sizeof( STEPTYPE ); j++)
			{
				p[ j ] = FILL_VALUE( i, j );
			}
			void * pOut = bundle.reserve( STEP );
			memcpy( pOut, p, STEP );
			packets.push_back( pOut );
		}
		packets.push_back( bundle.reserve( left ) ); // last packet
		memset( packets.back(), LAST_ROW, left );
	}


	static bool arePacketsModified( vector< void* > & packets )
	{
		unsigned i;

		for (i = 0; i + 1 < packets.size(); i++)
		{
			STEPTYPE * p = (STEPTYPE *) packets[ i ];
			for (unsigned j = 0; j < STEP/sizeof( STEPTYPE ); j++)
			{
				if (p[ j ] != FILL_VALUE( i, j ))
				{
					return true;
				}
			}
		}

		/* don't do the full check for the last packet (see prepareBundle()
		 * above) because we don't know the size
		 * maybe we need to store <void*, size> pairs)
		 */
		unsigned char * p = (unsigned char *) packets.back();
		return *p != LAST_ROW;
	}

	Mercury::InterfaceElement & ie_;
	unsigned int size_;

public:
	TestCompressLength( Mercury::InterfaceElement & ie, unsigned int size ) :
		ie_( ie ),
		size_( size )
	{
	}

#define IS_LENGTH_EXPANDED( length, ie ) \
	( length >= (unsigned int) ((1ll << ((ie).lengthParam() * 8)) - 1) )

	TEST_SUB_FUNCTOR_OPERATOR
	{
		/* Unfortunately one cannot use compresslength without really allocating 
		   a message of this size since it uses its own value to copy a chunk of 
		   the packet to where it came from. */
		Mercury::UDPBundle testBundle;
		vector< void* > packets;

		testBundle.startMessage( ie_ );
		packets.reserve( size_ / STEP + 1 );
		prepareBundle( testBundle, size_, packets );
		testBundle.finalise();

		Mercury::UDPBundleProcessor processor( testBundle.pFirstPacket() );

		unsigned int length;
		{	// block limits scope of iter
			Mercury::UDPBundleProcessor::iterator iter = processor.begin();
			length = iter.unpack( ie_ ).length;
			CHECK_EQUAL( size_, length );

			//should be not modified
			CHECK( !IS_LENGTH_EXPANDED( length, ie_ ) ||
				!arePacketsModified( packets ) ); //test unpack()ing

			iter ++;
		}

		{	// block limits scope of iter
			Mercury::UDPBundleProcessor::iterator iter = processor.begin();
			length = iter.unpack( ie_ ).length;
			// we'll get length == -1 if operator++() above hasn't repacked buff
			CHECK_EQUAL( size_, length );
		}

		// destructor of the above block's iter should repack the buffer
		CHECK( !IS_LENGTH_EXPANDED( length, ie_ ) ||
			arePacketsModified( packets ) );

		{	// block limits scope of iter
			Mercury::UDPBundleProcessor::iterator iter = processor.begin();
			length = iter.unpack( ie_ ).length;
			CHECK_EQUAL( size_, length );
		}

		// destructor of the above block's iter should repack the buffer
		CHECK( !IS_LENGTH_EXPANDED( length, ie_ ) || 
			arePacketsModified( packets ) );
	}
};


TEST( CompressLength )
{
	// Try each length parameter 
	for (int lengthParam = 1; lengthParam <= 4; lengthParam++)
	{
		Mercury::InterfaceElement ie( "TestElement", 1, 
									  Mercury::VARIABLE_LENGTH_MESSAGE,
									  lengthParam );

		// Try a zero length message
		TEST_SUB_FUNCTOR_NAMED( TestCompressLength( ie, 0x0 ), "0" );

		// Poke around the edges with various awkward bits set
		const int nHeads = 4, nBodies = 5;
		const unsigned char heads [nHeads ] =       {0x01, 0xA0, 0xA1, 0xFF};
		const unsigned char bodies[nBodies] = {0x00, 0x01, 0xA0, 0xA1, 0xFF};

		for (int head = 0; head < nHeads ; head++)
		{
			BW::ostringstream name;
			name << "test value: " << heads[head];
			TEST_SUB_FUNCTOR_NAMED(
				TestCompressLength( ie, heads[head] ), name.str() );

			for (int body = 0; body < nBodies; body++)
			{
				unsigned int testvalue;
				testvalue = heads[head];

				/* Testing around the 16mb mark is useful, but beyond that 
				   execution time will balloon to something insane */ 
				for (int length = 1; length < (heads[head] == 0x1 ? 4 : 3 ); 
					 length++)
				{
					testvalue = (testvalue << 8) | bodies[body];
					BW::ostringstream name;
					name << "test value: " << testvalue;
					TEST_SUB_FUNCTOR_NAMED(
						TestCompressLength( ie, testvalue ), name.str() );
				}
			}
		}
	}
}

/**
 *	A message whose declared length spans several packets must be assembled
 *	from the chain by iterator::data(). This pins the split-message path that
 *	the CR-P2-22 bound guards, so that the bound is never tight enough to
 *	refuse a legitimate spanning message.
 */
TEST( BundleProcessor_spanningMessageDataAssembles )
{
	Mercury::InterfaceElement ie( "TestSpanningElement", 1,
		Mercury::VARIABLE_LENGTH_MESSAGE, 4 );

	const int PAYLOAD = 3000;
	const int CHUNK = 1000;
	BW::vector<uint8> payload( PAYLOAD );
	for (int i = 0; i < PAYLOAD; ++i)
	{
		payload[ i ] = (uint8)( i * 13 + 7 );
	}

	Mercury::UDPBundle bundle;
	bundle.startMessage( ie );
	for (int off = 0; off < PAYLOAD; off += CHUNK)
	{
		const int n = ((PAYLOAD - off) < CHUNK) ? (PAYLOAD - off) : CHUNK;
		memcpy( bundle.reserve( n ), &payload[ off ], n );
	}
	bundle.finalise();

	Mercury::UDPBundleProcessor processor( bundle.pFirstPacket() );
	Mercury::UDPBundleProcessor::iterator iter = processor.begin();

	int length = iter.unpack( ie ).length;
	CHECK_EQUAL( PAYLOAD, length );

	const char * pData = iter.data();
	CHECK( pData != NULL );
	if (pData)
	{
		CHECK( memcmp( pData, &payload[0], PAYLOAD ) == 0 );
	}
}


/**
 *	iterator::data() used to size its temporary buffer straight from the
 *	peer-declared message length before checking the packet chain actually
 *	holds that much, so a corrupt declaration could demand an enormous
 *	allocation. It now sums the chain's real capacity and refuses anything
 *	beyond it (CR-P2-22).
 */
TEST( BundleProcessor_overlongSpanningMessageYieldsNull )
{
	Mercury::InterfaceElement ie( "TestOverlongElement", 1,
		Mercury::VARIABLE_LENGTH_MESSAGE, 4 );

	const int PAYLOAD = 3000;
	const int CHUNK = 1000;
	BW::vector<uint8> payload( PAYLOAD, 0xAB );

	Mercury::UDPBundle bundle;
	bundle.startMessage( ie );
	for (int off = 0; off < PAYLOAD; off += CHUNK)
	{
		const int n = ((PAYLOAD - off) < CHUNK) ? (PAYLOAD - off) : CHUNK;
		memcpy( bundle.reserve( n ), &payload[ off ], n );
	}
	bundle.finalise();

	// Rewrite the declared length to something no chain this small could hold.
	// It must stay below the 0x80000000 overflow sentinel that unpack rejects,
	// and below INT_MAX - offset_ so data()'s own length check does not wrap.
	uint32 overlong = BW_HTONL( 0x100000 );
	memcpy( bundle.pFirstPacket()->data() + Mercury::Packet::HEADER_SIZE +
			sizeof( Mercury::MessageID ), &overlong, sizeof( overlong ) );

	Mercury::UDPBundleProcessor processor( bundle.pFirstPacket() );
	Mercury::UDPBundleProcessor::iterator iter = processor.begin();

	int length = iter.unpack( ie ).length;
	CHECK_EQUAL( 0x100000, length );

	// The chain cannot hold the declared payload: data() must decline rather
	// than size a buffer from the declaration.
	CHECK( iter.data() == NULL );
}

BW_END_NAMESPACE

// test_compresslength.cpp
