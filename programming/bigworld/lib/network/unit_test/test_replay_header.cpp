#include "pch.hpp"

#include "cstdmf/checksum_stream.hpp"
#include "cstdmf/md5.hpp"
#include "cstdmf/memory_stream.hpp"

#include "connection/client_server_protocol_version.hpp"
#include "connection/replay_data.hpp"
#include "connection/replay_header.hpp"

#include <string.h>


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

/**
 *	This helper computes the digest used for the header fixtures.
 */
MD5::Digest fixtureDigest()
{
	MD5 md5;
	md5.append( "BigWorld replay header test", 27 );

	MD5::Digest digest;
	md5.getDigest( digest );

	return digest;
}


/**
 *	This helper creates a fixed NONCE_SIZE byte nonce string.
 */
BW::string fixtureNonce()
{
	char nonceBytes[ ReplayHeader::NONCE_SIZE ] =
		{ 1, 2, 3, 4, 5, 6, 7, 8 };
	return BW::string( nonceBytes, ReplayHeader::NONCE_SIZE );
}


/**
 *	This helper streams a header out with the given scheme and returns the
 *	resulting bytes.
 */
MemoryOStream & writeHeader( MemoryOStream & stream,
		ChecksumSchemePtr pScheme )
{
	ReplayHeader header( pScheme, fixtureDigest(), 10, fixtureNonce() );
	header.addToStream( stream, NULL );

	return stream;
}


} // end namespace (anonymous)


// -----------------------------------------------------------------------------
// Section: ReplayHeader
// -----------------------------------------------------------------------------

// A written header round trips through initFromStream: all fields are
// restored and the whole header is consumed. The stream size accessors
// agree with the bytes actually written.
TEST( ReplayHeader_writeReadRoundTrip )
{
	ChecksumSchemePtr pScheme = MD5SumScheme::create();

	MemoryOStream stream;
	writeHeader( stream, pScheme );

	ReplayHeader header( pScheme, fixtureDigest(), 10, fixtureNonce() );
	CHECK_EQUAL( int( header.streamSize() ), stream.size() );
	CHECK_EQUAL( header.streamSize(),
		ReplayHeader::calculateStreamSize( pScheme ) );

	ReplayHeader readHeader( pScheme );
	MemoryIStream reader( stream.data(), stream.size() );
	BW::string errorString;
	CHECK( readHeader.initFromStream( reader, NULL, &errorString ) );
	CHECK( errorString.empty() );
	CHECK_EQUAL( 0, int( reader.remainingLength() ) );

	CHECK( readHeader.version().supports(
		ClientServerProtocolVersion::currentVersion() ) );
	CHECK( fixtureDigest() == readHeader.digest() );
	CHECK_EQUAL( 10U, readHeader.gameUpdateFrequency() );

	// The server-side writer stamps the header with the current time.
	CHECK( readHeader.timestamp() > 0 );

	// MD5 signatures are 16 bytes and numTicks defaults to zero.
	CHECK_EQUAL( size_t( 16 ), readHeader.reportedSignatureLength() );
	CHECK_EQUAL( 0U, readHeader.numTicks() );
}


// Flipping a byte in the signature makes initFromStream fail with an error
// string.
TEST( ReplayHeader_tamperDetection )
{
	ChecksumSchemePtr pScheme = MD5SumScheme::create();

	MemoryOStream stream;
	writeHeader( stream, pScheme );

	// The signature sits just before the trailing numTicks field, i.e. in
	// the byte range [size - 20, size - 4).
	char * pBytes = static_cast< char * >( stream.data() );
	pBytes[ stream.size() - 6 ] ^= 0xFF;

	ReplayHeader readHeader( pScheme );
	MemoryIStream reader( stream.data(), stream.size() );
	BW::string errorString;
	CHECK( !readHeader.initFromStream( reader, NULL, &errorString ) );
	CHECK( !errorString.empty() );
}


// A header from an unsupported protocol version is rejected before the
// signature is even checked.
TEST( ReplayHeader_versionMismatch )
{
	ChecksumSchemePtr pScheme = MD5SumScheme::create();

	MemoryOStream stream;
	writeHeader( stream, pScheme );

	// The version is streamed subpatch, patch, minor, major; bumping the
	// subpatch byte makes it differ from the current version.
	char * pBytes = static_cast< char * >( stream.data() );
	pBytes[0] = CLIENT_SERVER_PROTOCOL_VERSION_SUBPATCH + 1;

	ReplayHeader readHeader( pScheme );
	MemoryIStream reader( stream.data(), stream.size() );
	BW::string errorString;
	CHECK( !readHeader.initFromStream( reader, NULL, &errorString ) );
	CHECK_EQUAL( BW::string( "Read replay header error: version mismatch" ),
		errorString );
}


// Reading without verification skips the signature check and hands the raw
// signature bytes to the caller.
TEST( ReplayHeader_unverifiedSignatureTransfer )
{
	ChecksumSchemePtr pScheme = MD5SumScheme::create();

	MemoryOStream stream;
	writeHeader( stream, pScheme );

	ReplayHeader readHeader( pScheme );
	MemoryIStream reader( stream.data(), stream.size() );
	MemoryOStream signature;
	CHECK( readHeader.initFromStream( reader, &signature, NULL,
		/* shouldVerify */ false ) );

	CHECK_EQUAL( 16, int( signature.size() ) );
	CHECK( fixtureDigest() == readHeader.digest() );
	CHECK_EQUAL( 0, int( reader.remainingLength() ) );
}


// checkSufficientLength reports the exact header size once the signature
// length field is readable, and reports insufficiency otherwise.
TEST( ReplayHeader_checkSufficientLength )
{
	ChecksumSchemePtr pScheme = MD5SumScheme::create();

	MemoryOStream stream;
	writeHeader( stream, pScheme );

	size_t headerSize = 0;
	CHECK( ReplayHeader::checkSufficientLength( stream.data(), stream.size(),
		headerSize ) );
	CHECK_EQUAL( stream.size(), int( headerSize ) );

	// One byte short: not sufficient, but the size is still reported.
	CHECK( !ReplayHeader::checkSufficientLength( stream.data(),
		stream.size() - 1, headerSize ) );
	CHECK_EQUAL( stream.size(), int( headerSize ) );

	// Too short to even hold the signature length field: not sufficient and
	// the size output is left untouched.
	headerSize = 99;
	CHECK( !ReplayHeader::checkSufficientLength( stream.data(), 4,
		headerSize ) );
	CHECK_EQUAL( 99, int( headerSize ) );
}


// The numTicks field sits immediately before the end of the header.
TEST( ReplayHeader_numTicksFieldOffset )
{
	ChecksumSchemePtr pScheme = MD5SumScheme::create();

	MemoryOStream stream;
	writeHeader( stream, pScheme );

	ReplayHeader header( pScheme, fixtureDigest(), 10, fixtureNonce() );
	CHECK_EQUAL( header.streamSize() - sizeof( GameTime ),
		size_t( ReplayHeader::numTicksFieldOffset( pScheme ) ) );
}


// -----------------------------------------------------------------------------
// Section: ClientServerProtocolVersion
// -----------------------------------------------------------------------------

// The protocol version streams as four bytes and round trips exactly.
TEST( ClientServerProtocolVersion_streamRoundTrip )
{
	ClientServerProtocolVersion version =
		ClientServerProtocolVersion::currentVersion();

	MemoryOStream stream;
	stream << version;
	CHECK_EQUAL( 4, int( stream.size() ) );

	ClientServerProtocolVersion readVersion;
	MemoryIStream reader( stream.data(), stream.size() );
	reader >> readVersion;

	CHECK( version.supports( readVersion ) );
	CHECK( readVersion.supports( version ) );

	// The current release version renders as a plain dotted string (update
	// this if the CLIENT_SERVER_PROTOCOL_VERSION_* macros change).
	CHECK_EQUAL( BW::string( "2.9.0" ),
		BW::string( readVersion.c_str() ) );
}


// supports() is exact equality across all four version components.
TEST( ClientServerProtocolVersion_supportsSemantics )
{
	ClientServerProtocolVersion version( 2, 9, 0, 0 );
	ClientServerProtocolVersion same( 2, 9, 0, 0 );
	ClientServerProtocolVersion otherSubpatch( 2, 9, 0, 1 );
	ClientServerProtocolVersion otherMinor( 2, 8, 0, 0 );

	CHECK( version.supports( version ) );
	CHECK( version.supports( same ) );
	CHECK( !version.supports( otherSubpatch ) );
	CHECK( !version.supports( otherMinor ) );
}


BW_END_NAMESPACE

// test_replay_header.cpp
