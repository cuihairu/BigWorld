#include "pch.hpp"

#include "network/symmetric_block_cipher.hpp"

#include <string.h>

BW_BEGIN_NAMESPACE

/**
 * Tests for Mercury::SymmetricBlockCipher (Blowfish, ECB).
 *
 * The cipher implementation itself comes from OpenSSL (>= 3.0 via vcpkg) but
 * is exercised through the engine's BlockCipher interface; these tests pin
 * the wire behaviour (block size, known-answer vector, key handling) so the
 * OpenSSL upgrade cannot silently change the encrypted stream format.
 */

// The classic Eric Young Blowfish test vector: key "abcdefghijklmnopqrstuvwxyz",
// plaintext "BLOWFISH" -> ciphertext 32 4e d0 fe f4 13 a2 03.
TEST( symmetric_block_cipher_known_answer )
{
	Mercury::BlockCipherPtr pCipher =
		Mercury::SymmetricBlockCipher::create(
			Mercury::BlockCipher::Key( "abcdefghijklmnopqrstuvwxyz" ) );

	CHECK( pCipher );

	if (!pCipher)
	{
		return;
	}

	CHECK( pCipher->blockSize() == 8 );

	const uint8 plaintext[8] = { 'B','L','O','W','F','I','S','H' };
	const uint8 expected[8] = { 0x32,0x4E,0xD0,0xFE,0xF4,0x13,0xA2,0x03 };
	uint8 ciphertext[8];
	uint8 roundtrip[8];

	CHECK( pCipher->encryptBlock( plaintext, ciphertext ) );
	CHECK( memcmp( ciphertext, expected, sizeof( expected ) ) == 0 );

	CHECK( pCipher->decryptBlock( ciphertext, roundtrip ) );
	CHECK( memcmp( roundtrip, plaintext, sizeof( plaintext ) ) == 0 );
}


// Every supported key size must encrypt/decrypt back to the original block.
TEST( symmetric_block_cipher_key_sizes )
{
	const size_t keySizes[] =
	{
		Mercury::SymmetricBlockCipher::MIN_KEY_SIZE,
		Mercury::SymmetricBlockCipher::DEFAULT_KEY_SIZE,
		Mercury::SymmetricBlockCipher::MAX_KEY_SIZE,
	};

	const uint8 block[8] = { 1,2,3,4,5,6,7,8 };

	for (size_t i = 0; i < sizeof( keySizes ) / sizeof( keySizes[0] ); ++i)
	{
		Mercury::BlockCipherPtr pCipher =
			Mercury::SymmetricBlockCipher::create( keySizes[ i ] );
		CHECK( pCipher );

		if (!pCipher)
		{
			continue;
		}

		// The generated key must honour the requested size and be repeatable
		// across lookups (the same cipher object reports the same key).
		CHECK( pCipher->key().size() == keySizes[ i ] );
		CHECK( pCipher->key() == pCipher->key() );

		uint8 ciphertext[8];
		uint8 roundtrip[8];
		CHECK( pCipher->encryptBlock( block, ciphertext ) );
		CHECK( pCipher->decryptBlock( ciphertext, roundtrip ) );
		CHECK( memcmp( roundtrip, block, sizeof( block ) ) == 0 );
	}
}


// Two ciphers created from the same key material must agree; encrypting the
// same block twice must be deterministic (ECB).
TEST( symmetric_block_cipher_deterministic )
{
	Mercury::BlockCipher::Key key( "0123456789abcdef0123456789abcdef" );

	Mercury::BlockCipherPtr pA = Mercury::SymmetricBlockCipher::create( key );
	Mercury::BlockCipherPtr pB = Mercury::SymmetricBlockCipher::create( key );
	CHECK( pA );
	CHECK( pB );

	const uint8 block[8] = { 0xDE,0xAD,0xBE,0xEF,0xBA,0xAD,0xF0,0x0D };
	uint8 ctA[8], ctB[8], ctAgain[8];

	CHECK( pA->encryptBlock( block, ctA ) );
	CHECK( pB->encryptBlock( block, ctB ) );
	CHECK( memcmp( ctA, ctB, sizeof( ctA ) ) == 0 );

	CHECK( pA->encryptBlock( block, ctAgain ) );
	CHECK( memcmp( ctA, ctAgain, sizeof( ctA ) ) == 0 );
}

BW_END_NAMESPACE
