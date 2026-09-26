#include "pch.hpp"

#include "cstdmf/checksum_stream.hpp"
#include "cstdmf/md5.hpp"
#include "cstdmf/memory_stream.hpp"

#include "connection/client_server_protocol_version.hpp"
#include "connection/replay_data.hpp"
#include "connection/replay_header.hpp"
#include "connection/replay_metadata.hpp"

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


// -----------------------------------------------------------------------------
// Section: ReplayMetaData
// -----------------------------------------------------------------------------

// add/hasKey/get behave as a small string map; re-adding a key replaces the
// value without growing the collection.
TEST( ReplayMetaData_collection )
{
	ReplayMetaData metaData;
	CHECK_EQUAL( 0, int( metaData.size() ) );

	metaData.add( "key1", "value1" );
	metaData.add( "key2", "value2" );
	CHECK_EQUAL( 2, int( metaData.size() ) );

	CHECK( metaData.hasKey( "key1" ) );
	CHECK( !metaData.hasKey( "missing" ) );

	CHECK_EQUAL( BW::string( "value1" ), metaData.get( "key1" ) );
	CHECK_EQUAL( BW::string( "fallback" ),
		metaData.get( "missing", "fallback" ) );

	metaData.add( "key1", "replaced" );
	CHECK_EQUAL( 2, int( metaData.size() ) );
	CHECK_EQUAL( BW::string( "replaced" ), metaData.get( "key1" ) );
}


// A signed meta-data block round trips through readFromStream, and flipping
// a value byte is detected by the checksum.
TEST( ReplayMetaData_signedRoundTrip )
{
	ChecksumSchemePtr pScheme = MD5SumScheme::create();

	ReplayMetaData metaData;
	metaData.init( pScheme );
	metaData.add( "recording_name", "test" );
	metaData.add( "version", "1" );

	MemoryOStream stream;
	metaData.addToStream( stream );

	// StreamSize field + packed pairs + 16 byte MD5 signature.
	CHECK_EQUAL( 4 + metaData.streamSize() + 16, stream.size() );

	// The meta-data read path uses a ChecksumIStream that never resets its
	// scheme, so each read needs a scheme with fresh MD5 state.
	ReplayMetaData readMetaData;
	readMetaData.init( MD5SumScheme::create() );
	MemoryIStream reader( stream.data(), stream.size() );
	BW::string errorString;
	CHECK( readMetaData.readFromStream( reader, NULL, &errorString ) );
	CHECK( errorString.empty() );
	CHECK_EQUAL( metaData.size(), readMetaData.size() );
	CHECK_EQUAL( BW::string( "test" ),
		readMetaData.get( "recording_name" ) );
	CHECK_EQUAL( 0, int( reader.remainingLength() ) );

	// Corrupt a byte inside the first value ("test") without disturbing any
	// length fields: the checksum catches it.
	char * pBytes = static_cast< char * >( stream.data() );
	pBytes[ 4 + 1 + 14 + 1 ] ^= 0xFF;

	readMetaData.init( MD5SumScheme::create() );
	MemoryIStream tamperedReader( stream.data(), stream.size() );
	CHECK( !readMetaData.readFromStream( tamperedReader, NULL,
		&errorString ) );
	CHECK( !errorString.empty() );
}


// A meta-data block read without a scheme reports the expected block length
// and transfers the signature bytes to the caller.
TEST( ReplayMetaData_unverifiedSignatureTransfer )
{
	ReplayMetaData metaData;
	metaData.init( /* signatureLength */ 16 );
	metaData.add( "k", "v" );

	// Hand-craft the block: StreamSize, the pairs, then the signature.
	MemoryOStream crafted;
	crafted << uint32( metaData.streamSize() );
	crafted << BW::string( "k" ) << BW::string( "v" );

	char signature[ 16 ];
	memset( signature, 0xAB, sizeof( signature ) );
	crafted.addBlob( signature, sizeof( signature ) );

	size_t metaDataLength = 0;
	CHECK( ReplayMetaData::checkSufficientLength( crafted.data(),
		crafted.size(), 16, metaDataLength ) );
	CHECK_EQUAL( crafted.size(), metaDataLength );
	CHECK( !ReplayMetaData::checkSufficientLength( crafted.data(),
		crafted.size() - 1, 16, metaDataLength ) );

	ReplayMetaData readMetaData;
	readMetaData.init( 16 );
	MemoryOStream readSignature;
	MemoryIStream reader( crafted.data(), crafted.size() );
	BW::string errorString;
	CHECK( readMetaData.readFromStream( reader, &readSignature,
		&errorString ) );
	CHECK_EQUAL( 16, int( readSignature.size() ) );
	CHECK_EQUAL( BW::string( "v" ), readMetaData.get( "k" ) );
}


BW_END_NAMESPACE

// test_replay_header.cpp
