#include "pch.hpp"

#include "network/block_cipher.hpp"
#include "network/encryption_filter.hpp"
#include "network/symmetric_block_cipher.hpp"

#include "cstdmf/memory_stream.hpp"

#include <string.h>


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

const char BLOWFISH_KEY[] = "abcdefghijklmnopqrstuvwxyz";

/**
 *	A 16-byte plaintext made of two visually distinct blocks, so that a
 *	mis-decrypted block can never pass by accident.
 */
const unsigned char PLAIN_TWO_BLOCKS[16] =
{
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
	0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00
};


/**
 *	Whether n bytes at two locations agree.
 */
bool sameBytes( const void * lhs, const void * rhs, size_t n )
{
	return memcmp( lhs, rhs, n ) == 0;
}

} // end namespace (anonymous)


// -----------------------------------------------------------------------------
// Section: EncryptionFilter (stream API)
// -----------------------------------------------------------------------------

// The stream round trip recovers the plaintext. encryptStream pads the input
// stream up to a whole number of blocks in place, so the cipher text is the
// input rounded up to the cipher block size, and the decrypt output carries
// the zero padding at its tail.
TEST( EncryptionFilter_streamRoundTrip )
{
	Mercury::BlockCipherPtr pCipher =
		Mercury::SymmetricBlockCipher::create(
			Mercury::BlockCipher::Key( BLOWFISH_KEY ) );
	CHECK_EQUAL( 8u, pCipher->blockSize() );

	Mercury::EncryptionFilterPtr pFilter =
		Mercury::EncryptionFilter::create( pCipher );
	CHECK( pFilter.exists() );
	CHECK( pFilter->key() == pCipher->key() );

	// 20 bytes is not a multiple of the 8-byte block size.
	MemoryOStream clear;
	clear.addBlob( PLAIN_TWO_BLOCKS, sizeof( PLAIN_TWO_BLOCKS ) );
	clear.addBlob( "four", 4 );
	CHECK_EQUAL( 20, clear.size() );

	MemoryOStream cipher;
	pFilter->encryptStream( clear, cipher );

	// Padded up to 24 in place (the clear stream grew), and the cipher text
	// is exactly the padded length.
	CHECK_EQUAL( 24, clear.size() );
	CHECK_EQUAL( 24, cipher.size() );

	MemoryIStream cipherIn( cipher.data(), cipher.size() );
	MemoryOStream recovered;
	CHECK( pFilter->decryptStream( cipherIn, recovered ) );
	CHECK_EQUAL( 24, recovered.size() );

	const char * pClear = static_cast< const char * >( recovered.data() );
	CHECK( sameBytes( pClear, PLAIN_TWO_BLOCKS,
		sizeof( PLAIN_TWO_BLOCKS ) ) );
	// The four padding bytes decrypt to zeros.
	const char zeroTail[4] = { 0 };
	CHECK( sameBytes( pClear + 20, zeroTail, sizeof( zeroTail ) ) );
}


// Input already sized to a whole number of blocks needs no padding: the
// cipher text is the same length and the round trip is exact.
TEST( EncryptionFilter_streamRoundTripExactMultiple )
{
	Mercury::BlockCipherPtr pCipher =
		Mercury::SymmetricBlockCipher::create(
			Mercury::BlockCipher::Key( BLOWFISH_KEY ) );

	Mercury::EncryptionFilterPtr pFilter =
		Mercury::EncryptionFilter::create( pCipher );

	MemoryOStream clear;
	clear.addBlob( PLAIN_TWO_BLOCKS, sizeof( PLAIN_TWO_BLOCKS ) );

	MemoryOStream cipher;
	pFilter->encryptStream( clear, cipher );

	CHECK_EQUAL( 16, clear.size() );
	CHECK_EQUAL( 16, cipher.size() );

	MemoryIStream cipherIn( cipher.data(), cipher.size() );
	MemoryOStream recovered;
	CHECK( pFilter->decryptStream( cipherIn, recovered ) );

	CHECK_EQUAL( 16, recovered.size() );
	CHECK( sameBytes( recovered.data(), PLAIN_TWO_BLOCKS,
		sizeof( PLAIN_TWO_BLOCKS ) ) );
}


// With the identity cipher the block chaining is laid bare: the first
// cipher block is the plain first block, and every later block is the
// plaintext block XORed with the one before it (this is what the chained
// XOR in EncryptionFilter::encrypt buys - it is the exact inverse in
// decrypt()). The NullCipher has an empty key and the stream API does not
// require one, unlike the packet send/recv paths.
TEST( EncryptionFilter_nullCipherRevealsChain )
{
	Mercury::BlockCipherPtr pCipher = Mercury::NullCipher::create( 8 );
	// Empty key: only the stream API is usable with this cipher.
	CHECK( pCipher->key().empty() );

	Mercury::EncryptionFilterPtr pFilter =
		Mercury::EncryptionFilter::create( pCipher );

	MemoryOStream clear;
	clear.addBlob( PLAIN_TWO_BLOCKS, sizeof( PLAIN_TWO_BLOCKS ) );

	MemoryOStream cipher;
	pFilter->encryptStream( clear, cipher );
	CHECK_EQUAL( 16, cipher.size() );

	const unsigned char * pCipherBytes =
		static_cast< const unsigned char * >( cipher.data() );

	// Block 0 is copied through unchanged.
	CHECK( sameBytes( pCipherBytes, PLAIN_TWO_BLOCKS, 8 ) );

	// Block 1 is plaintext block 1 XOR plaintext block 0.
	unsigned char expectedSecond[ 8 ];
	for (size_t i = 0; i < 8; ++i)
	{
		expectedSecond[ i ] = PLAIN_TWO_BLOCKS[ 8 + i ] ^
			PLAIN_TWO_BLOCKS[ i ];
	}
	CHECK( sameBytes( pCipherBytes + 8, expectedSecond, 8 ) );

	// And decrypting folds the chain back.
	MemoryIStream cipherIn( cipher.data(), cipher.size() );
	MemoryOStream recovered;
	CHECK( pFilter->decryptStream( cipherIn, recovered ) );
	CHECK( sameBytes( recovered.data(), PLAIN_TWO_BLOCKS,
		sizeof( PLAIN_TWO_BLOCKS ) ) );
}


// A cipher text whose length is not a whole number of blocks is refused.
// Note the output stream still grows by the reserve that happens before
// the decryption attempt - the failure is reported by the return value.
TEST( EncryptionFilter_decryptStreamRejectsNonMultiple )
{
	Mercury::BlockCipherPtr pCipher =
		Mercury::SymmetricBlockCipher::create(
			Mercury::BlockCipher::Key( BLOWFISH_KEY ) );

	Mercury::EncryptionFilterPtr pFilter =
		Mercury::EncryptionFilter::create( pCipher );

	// 10 bytes is not a multiple of the 8-byte block size.
	unsigned char junk[ 10 ];
	memset( junk, 0xa5, sizeof( junk ) );
	MemoryIStream cipherIn( junk, sizeof( junk ) );

	MemoryOStream recovered;
	CHECK( !pFilter->decryptStream( cipherIn, recovered ) );
	CHECK_EQUAL( 10, recovered.size() );
	CHECK_EQUAL( 0, cipherIn.remainingLength() );
}


// Swapping two cipher blocks corrupts exactly the leading block of the
// moved region and no more: out0 = D(C1) = P1 XOR P0 (garbage), and out1
// = D(C0) XOR out0 = P0 XOR P1 XOR P0 = P1 - the chain folds back to the
// truth after one block. Rearrangement detection is therefore completed
// by the magic-footer position check in recv(), not by wholesale
// garbling; the stream layer only garbles the head.
TEST( EncryptionFilter_blockRearrangementBreaksChain )
{
	Mercury::BlockCipherPtr pCipher =
		Mercury::SymmetricBlockCipher::create(
			Mercury::BlockCipher::Key( BLOWFISH_KEY ) );

	Mercury::EncryptionFilterPtr pFilter =
		Mercury::EncryptionFilter::create( pCipher );

	MemoryOStream clear;
	clear.addBlob( PLAIN_TWO_BLOCKS, sizeof( PLAIN_TWO_BLOCKS ) );

	MemoryOStream cipher;
	pFilter->encryptStream( clear, cipher );

	// Swap the two blocks.
	char * pBytes = static_cast< char * >( cipher.data() );
	char swapped[ 8 ];
	memcpy( swapped, pBytes, 8 );
	memcpy( pBytes, pBytes + 8, 8 );
	memcpy( pBytes + 8, swapped, 8 );

	MemoryIStream cipherIn( cipher.data(), cipher.size() );
	MemoryOStream recovered;
	CHECK( pFilter->decryptStream( cipherIn, recovered ) );

	const char * pClear = static_cast< const char * >( recovered.data() );
	// The leading block is garbled (it equals P1 XOR P0, not P0 - the
	// fixture blocks are distinct and nonzero).
	CHECK( !sameBytes( pClear, PLAIN_TWO_BLOCKS, 8 ) );
	// The chain has self-healed: the second block is exactly P1.
	CHECK( sameBytes( pClear + 8, PLAIN_TWO_BLOCKS + 8, 8 ) );
	// Overall the plaintext is not recovered.
	CHECK( !sameBytes( pClear, PLAIN_TWO_BLOCKS,
		sizeof( PLAIN_TWO_BLOCKS ) ) );
}


// maxSpareSize is the block size, plus room for the magic footer, plus a
// fixed MTU allowance - it scales with the cipher's block size.
TEST( EncryptionFilter_maxSpareSize )
{
	Mercury::EncryptionFilterPtr pBlowfishFilter =
		Mercury::EncryptionFilter::create(
			Mercury::SymmetricBlockCipher::create(
				Mercury::BlockCipher::Key( BLOWFISH_KEY ) ) );
	// 8-byte Blowfish blocks + 4-byte magic + 200-byte MTU allowance.
	CHECK_EQUAL( 212, pBlowfishFilter->maxSpareSize() );

	Mercury::EncryptionFilterPtr pNullFilter =
		Mercury::EncryptionFilter::create( Mercury::NullCipher::create( 16 ) );
	CHECK_EQUAL( 220, pNullFilter->maxSpareSize() );
}


BW_END_NAMESPACE

// test_encryption_filter.cpp
