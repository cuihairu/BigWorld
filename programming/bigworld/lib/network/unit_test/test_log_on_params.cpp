#include "pch.hpp"

#include "cstdmf/md5.hpp"
#include "cstdmf/memory_stream.hpp"

#include "connection/log_on_params.hpp"
#include "connection/stream_encoder.hpp"

#include <string.h>


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

/**
 *	This helper fills a digest with an identifiable byte pattern.
 */
MD5::Digest fixtureDigest()
{
	MD5::Digest digest;
	for (size_t i = 0; i < MD5::Digest::NUM_BYTES; ++i)
	{
		digest.bytes[ i ] = (unsigned char)( 0xA0 + i );
	}
	return digest;
}


/**
 *	A pass-through StreamEncoder that copies its input to its output
 *	unchanged, counts calls, and can be told to fail either direction.
 */
class TestStreamEncoder : public StreamEncoder
{
public:
	TestStreamEncoder( bool shouldFail = false ) :
		shouldFail_( shouldFail ),
		encryptCalls_( 0 ),
		decryptCalls_( 0 )
	{}

	virtual bool encrypt( BinaryIStream & clearText,
			BinaryOStream & cipherText ) const
	{
		++encryptCalls_;
		if (shouldFail_)
		{
			return false;
		}
		this->copyAll( clearText, cipherText );
		return true;
	}

	virtual bool decrypt( BinaryIStream & cipherText,
			BinaryOStream & clearText ) const
	{
		++decryptCalls_;
		if (shouldFail_)
		{
			return false;
		}
		this->copyAll( cipherText, clearText );
		return true;
	}

	int encryptCalls() const	{ return encryptCalls_; }
	int decryptCalls() const	{ return decryptCalls_; }

private:
	static void copyAll( BinaryIStream & from, BinaryOStream & to )
	{
		while (from.remainingLength() > 0)
		{
			int chunk = from.remainingLength();
			to.addBlob( from.retrieve( chunk ), chunk );
		}
	}

	bool shouldFail_;
	mutable int encryptCalls_;
	mutable int decryptCalls_;
};


/**
 *	MemoryIStream subclass that frees the buffer it reads from.
 *	(MemoryIStream borrows the pointer; it does not own it.)
 */
class OwnedIStream : public MemoryIStream
{
public:
	OwnedIStream( char * pBuffer, int length ) :
		MemoryIStream( pBuffer, length ),
		pBuffer_( pBuffer )
	{}
	virtual ~OwnedIStream() { delete[] pBuffer_; }

private:
	char * pBuffer_;
};


/**
 *	Serialises the given parameters and hands back an input stream over
 *	the produced bytes (the encoding itself is covered by the caller's
 *	size assertions).
 */
MemoryIStream * streamOf( const LogOnParams & params,
	LogOnParams::Flags flags = LogOnParams::PASS_THRU,
	const StreamEncoder * pEncoder = NULL )
{
	MemoryOStream out;
	params.addToStream( out, flags, pEncoder );
	int n = out.size();
	char * buf = new char[ n ];
	memcpy( buf, out.data(), n );
	return new OwnedIStream( buf, n );
}

} // end namespace (anonymous)


// A full parameter set survives a plain-text stream round trip:
// username/password/encryptionKey/digest/nonce all come back unchanged,
// and the wire form has the flags byte, the 16-byte digest and the
// 4-byte nonce laid out after the three strings.
TEST( LogOnParams_plainRoundTrip )
{
	LogOnParams out( "user", "pass", "key" );
	out.digest( fixtureDigest() );

	MemoryOStream stream;
	CHECK( out.addToStream( stream ) );

	// 1 flags byte + 16 digest + 4 nonce + the three strings. Each
	// BW::string is written by appendString(): one packed-int length
	// byte (all three strings are shorter than 255) plus the payload
	// chars, hence the three extra bytes (1+16+4+3+4+4+3==35).
	CHECK_EQUAL( 1 + 16 + 4 + 3 + int( strlen( "user" ) +
		strlen( "pass" ) + strlen( "key" ) ), stream.size() );

	// The size is taken first: retrieve() consumes the stream, and the
	// order in which the two constructor arguments are evaluated is not
	// specified, so a second size() call could well see an empty stream.
	const int plainBytes = stream.size();
	MemoryIStream in( stream.retrieve( plainBytes ), plainBytes );
	LogOnParams back;
	CHECK( back.readFromStream( in ) );
	CHECK_EQUAL( 0, in.remainingLength() );

	CHECK_EQUAL( BW::string( "user" ), back.username() );
	CHECK_EQUAL( BW::string( "pass" ), back.password() );
	CHECK_EQUAL( BW::string( "key" ), back.encryptionKey() );
	CHECK( back.digest() == fixtureDigest() );
	// Copy the class constant into a local so no ODR-use of it can
	// creep in through the comparison.
	const int HAS_ALL = LogOnParams::HAS_ALL;
	CHECK_EQUAL( HAS_ALL, int( back.flags() ) );

	// The nonce is random, but it is carried verbatim - two objects
	// written from the same one compare equal after the trip.
	LogOnParams copyOfOut( out );
	CHECK( out.operator==( copyOfOut ) );
	MemoryIStream * pIn2 = streamOf( out );
	LogOnParams back2;
	CHECK( back2.readFromStream( *pIn2 ) );
	CHECK( back2.operator==( out ) );
	delete pIn2;
}


// The flags argument decides which optional fields travel: without
// HAS_DIGEST the digest stays home and the digest read-back is skipped
// (a freshly constructed digest on the reading side stays empty), and
// default-constructed parameters stream their empty strings fine.
TEST( LogOnParams_flagControl )
{
	LogOnParams out( "user", "pass", "key" );
	out.digest( fixtureDigest() );

	const size_t DIGEST_BYTES = MD5::Digest::NUM_BYTES;

	// Suppress the digest on the wire.
	MemoryOStream withoutDigest;
	out.addToStream( withoutDigest, 0 );

	MemoryOStream withDigest;
	out.addToStream( withDigest );

	CHECK_EQUAL( int( DIGEST_BYTES ), withDigest.size() -
		withoutDigest.size() );

	const int leanBytes = withoutDigest.size();
	MemoryIStream in( withoutDigest.retrieve( leanBytes ), leanBytes );
	LogOnParams back;
	CHECK( back.readFromStream( in ) );
	CHECK_EQUAL( 0, int( back.flags() ) );
	CHECK_EQUAL( BW::string( "user" ), back.username() );
	// The digest was neither written nor read.
	CHECK( back.digest().isEmpty() );

	// Default construction gives empty strings that still round trip.
	LogOnParams empty;
	MemoryIStream * pEmptyIn = streamOf( empty );
	LogOnParams emptyBack;
	CHECK( emptyBack.readFromStream( *pEmptyIn ) );
	CHECK_EQUAL( BW::string(), emptyBack.username() );
	CHECK_EQUAL( BW::string(), emptyBack.password() );
	CHECK_EQUAL( BW::string(), emptyBack.encryptionKey() );
	CHECK_EQUAL( 0, pEmptyIn->remainingLength() );
	delete pEmptyIn;
}


// With an encoder attached, addToStream/readFromStream route through
// encrypt/decrypt exactly once, the pass-through encoder preserves the
// plain layout, and a failing encoder fails the call without producing
// a readable object.
TEST( LogOnParams_encoderRouting )
{
	TestStreamEncoder encoder;

	LogOnParams out( "user", "pass", "key" );
	out.digest( fixtureDigest() );

	MemoryOStream encrypted;
	CHECK( out.addToStream( encrypted, LogOnParams::PASS_THRU,
		&encoder ) );
	CHECK_EQUAL( 1, encoder.encryptCalls() );

	// The pass-through encoder leaves the plain layout in place.
	MemoryOStream plain;
	out.addToStream( plain );
	CHECK_EQUAL( encrypted.size(), plain.size() );
	// retrieve() is destructive (it advances the read pointer), so capture
	// the bytes once and reuse the buffer for both the comparison and the
	// input stream that follows.
	const void * pPlain = plain.retrieve( plain.size() );
	const void * pEncrypted = encrypted.retrieve( encrypted.size() );
	CHECK( memcmp( pPlain, pEncrypted, plain.size() ) == 0 );

	MemoryIStream in( pEncrypted, encrypted.size() );
	LogOnParams back;
	CHECK( back.readFromStream( in, &encoder ) );
	CHECK_EQUAL( 1, encoder.decryptCalls() );
	CHECK_EQUAL( BW::string( "user" ), back.username() );

	// Failing directions propagate as a false return.
	TestStreamEncoder broken( /* shouldFail */ true );
	MemoryOStream nowhere;
	CHECK( !out.addToStream( nowhere, LogOnParams::PASS_THRU, &broken ) );
	CHECK_EQUAL( 1, broken.encryptCalls() );

	MemoryIStream * pIn = streamOf( out );
	LogOnParams unread;
	CHECK( !unread.readFromStream( *pIn, &broken ) );
	CHECK_EQUAL( 1, broken.decryptCalls() );
	delete pIn;
}


// The streaming operators wrap the same non-encrypting calls.
TEST( LogOnParams_streamingOperators )
{
	LogOnParams out( "user", "pass", "key" );
	out.digest( fixtureDigest() );

	MemoryOStream stream;
	stream << out;

	const int bytes = stream.size();
	MemoryIStream in( stream.retrieve( bytes ), bytes );
	LogOnParams back;
	in >> back;
	CHECK_EQUAL( 0, in.remainingLength() );
	CHECK_EQUAL( BW::string( "user" ), back.username() );
	CHECK_EQUAL( BW::string( "pass" ), back.password() );
	CHECK_EQUAL( BW::string( "key" ), back.encryptionKey() );
	CHECK( back.digest() == fixtureDigest() );
}

BW_END_NAMESPACE

// test_log_on_params.cpp
