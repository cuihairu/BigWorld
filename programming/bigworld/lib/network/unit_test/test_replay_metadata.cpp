#include "pch.hpp"

#include "cstdmf/checksum_stream.hpp"
#include "cstdmf/memory_stream.hpp"

#include "connection/replay_metadata.hpp"

#include <string.h>


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

/**
 *	Copies a stream's bytes, so that more than one input stream can be built
 *	over the same data - MemoryIStream only borrows its buffer, and it
 *	consumes what it reads.
 */
void copyOf( MemoryOStream & target, MemoryOStream & source )
{
	target.addBlob( source.data(), source.size() );
}


/**
 *	The packed size a string occupies on the wire: one length byte for a
 *	short string, three for a long one, plus the characters.
 */
size_t packedSize( const BW::string & value )
{
	return ( value.size() < 256 ? 1 : 3 ) + value.size();
}


/**
 *	Whether two regions of n bytes hold the same data. Wrapped up like this
 *	so that the comma inside memcmp()'s argument list never reaches a test
 *	macro, which would take it for a separator.
 */
bool sameBytes( const char * lhs, const void * rhs, size_t n )
{
	return memcmp( lhs, rhs, n ) == 0;
}


/**
 *	Writes a signed meta-data block into a single stream, as a replay file
 *	would hold it. addToStream() appends the signature to the block itself
 *	as well as handing it back, so the block needs no further assembly.
 */
void writeSignedBlock( MemoryOStream & wire, const ReplayMetaData & meta )
{
	MemoryOStream block;
	MemoryOStream signature;
	meta.addToStream( block, &signature );

	copyOf( wire, block );
}


/**
 *	Whether a signed block ends in the very signature that addToStream()
 *	hands back - the two come from the same digest.
 */
bool blockCarriesItsOwnSignature( const ReplayMetaData & meta )
{
	MemoryOStream block;
	MemoryOStream signature;
	meta.addToStream( block, &signature );

	return signature.size() == 16 &&
		sameBytes( static_cast< const char * >( block.data() ) +
			block.size() - 16, signature.data(), 16 );
}

} // end namespace (anonymous)


// A fresh block is empty; adding a key tracks the packed size of the pair,
// and re-adding a key replaces the pair rather than adding a second one.
TEST( ReplayMetaData_collection )
{
	ReplayMetaData meta;

	CHECK_EQUAL( 0u, unsigned( meta.size() ) );
	CHECK_EQUAL( 0u, unsigned( meta.streamSize() ) );
	CHECK( !meta.hasKey( "key" ) );
	CHECK_EQUAL( BW::string( "" ), meta.get( "key" ) );
	CHECK_EQUAL( BW::string( "fallback" ), meta.get( "key", "fallback" ) );

	meta.add( "key", "value" );
	CHECK_EQUAL( 1u, unsigned( meta.size() ) );
	CHECK( meta.hasKey( "key" ) );
	CHECK_EQUAL( BW::string( "value" ), meta.get( "key" ) );
	CHECK_EQUAL( unsigned( packedSize( "key" ) + packedSize( "value" ) ),
		unsigned( meta.streamSize() ) );

	// Adding the same key again replaces the value and the tracked size.
	meta.add( "key", "a much longer value" );
	CHECK_EQUAL( 1u, unsigned( meta.size() ) );
	CHECK_EQUAL( BW::string( "a much longer value" ), meta.get( "key" ) );
	CHECK_EQUAL( unsigned( packedSize( "key" ) +
			packedSize( "a much longer value" ) ),
		unsigned( meta.streamSize() ) );

	// A string of 255 characters or more costs an extra length byte.
	const BW::string longKey( 300, 'k' );
	meta.add( longKey, "v" );
	CHECK_EQUAL( 2u, unsigned( meta.size() ) );
	CHECK_EQUAL( unsigned( packedSize( "key" ) +
			packedSize( "a much longer value" ) +
			packedSize( longKey ) + packedSize( "v" ) ),
		unsigned( meta.streamSize() ) );

	// Iteration walks the same pairs that get()/hasKey() see.
	size_t counted = 0;
	for (ReplayMetaData::const_iterator iter = meta.begin();
			iter != meta.end(); ++iter )
	{
		CHECK( meta.hasKey( iter->first ) );
		CHECK_EQUAL( iter->second, meta.get( iter->first ) );
		++counted;
	}
	CHECK_EQUAL( 2u, unsigned( counted ) );

	meta.clear();
	CHECK_EQUAL( 0u, unsigned( meta.size() ) );
	CHECK_EQUAL( 0u, unsigned( meta.streamSize() ) );
	CHECK( !meta.hasKey( "key" ) );
}


// A signed block round trips: the reader recovers the pairs and the exact
// signature bytes, and consumes the whole of the stream.
TEST( ReplayMetaData_signedRoundTrip )
{
	ReplayMetaData writer;
	writer.init( MD5SumScheme::create() );
	writer.add( "app", "bigworld" );
	writer.add( "map", "Shark" );

	// addToStream() writes the signature into the block as well as handing
	// it back, so a signed block carries its own signature. That probe has
	// to run on a twin, not on the writer below: addToStream() wraps the
	// stream in a ChecksumOStream with shouldReset == false (a scheme is
	// meant to be able to feed a larger stream), so a second write through
	// the same scheme folds both payloads into one digest, and the reader
	// - which verifies a single block - would refuse the signature.
	ReplayMetaData probe;
	probe.init( MD5SumScheme::create() );
	probe.add( "app", "bigworld" );
	probe.add( "map", "Shark" );
	CHECK( blockCarriesItsOwnSignature( probe ) );

	MemoryOStream wire;
	writeSignedBlock( wire, writer );

	// 4 bytes of block size, the two pairs, and an MD5 signature.
	CHECK_EQUAL( 4 + int( writer.streamSize() ) + 16, wire.size() );

	ReplayMetaData reader;
	reader.init( MD5SumScheme::create() );

	MemoryOStream wireBytesCopy;
	copyOf( wireBytesCopy, wire );
	MemoryIStream in( wireBytesCopy.data(), wireBytesCopy.size() );

	MemoryOStream signature;
	BW::string error;
	CHECK( reader.readFromStream( in, &signature, &error ) );
	CHECK( error.empty() );
	CHECK_EQUAL( 0, in.remainingLength() );

	CHECK_EQUAL( 2u, unsigned( reader.size() ) );
	CHECK_EQUAL( BW::string( "bigworld" ), reader.get( "app" ) );
	CHECK_EQUAL( BW::string( "Shark" ), reader.get( "map" ) );
	CHECK_EQUAL( unsigned( writer.streamSize() ),
		unsigned( reader.streamSize() ) );

	// The signature is MD5, and it is the same one the writer produced.
	CHECK_EQUAL( 16, signature.size() );
	CHECK( sameBytes( static_cast< const char * >( wire.data() ) +
		wire.size() - 16, signature.data(), 16 ) );

	// An empty block is legal, and so is a block read without asking for
	// the signature back.
	ReplayMetaData emptyWriter;
	emptyWriter.init( MD5SumScheme::create() );
	MemoryOStream emptyWire;
	writeSignedBlock( emptyWire, emptyWriter );
	CHECK_EQUAL( 4 + 16, emptyWire.size() );

	ReplayMetaData emptyReader;
	emptyReader.init( MD5SumScheme::create() );
	MemoryOStream emptyBytesCopy;
	copyOf( emptyBytesCopy, emptyWire );
	MemoryIStream emptyIn( emptyBytesCopy.data(), emptyBytesCopy.size() );
	CHECK( emptyReader.readFromStream( emptyIn ) );
	CHECK_EQUAL( 0u, unsigned( emptyReader.size() ) );
}


// Altering the meta-data itself invalidates the signature it carries.
TEST( ReplayMetaData_rejectsTamperedBlock )
{
	ReplayMetaData writer;
	writer.init( MD5SumScheme::create() );
	writer.add( "app", "bigworld" );

	MemoryOStream wire;
	writeSignedBlock( wire, writer );

	// Keep every length intact and change a character of the value, so
	// the block still parses and only the checksum disagrees.
	MemoryOStream tampered;
	copyOf( tampered, wire );
	char * pEdit = static_cast< char * >( tampered.data() );
	pEdit[ 4 + 1 + 1 + 3 ] = 'X';
	CHECK_EQUAL( wire.size(), tampered.size() );

	ReplayMetaData reader;
	reader.init( MD5SumScheme::create() );

	MemoryOStream tamperedBytesCopy;
	copyOf( tamperedBytesCopy, tampered );
	MemoryIStream in( tamperedBytesCopy.data(), tamperedBytesCopy.size() );

	MemoryOStream signature;
	BW::string error;
	CHECK( !reader.readFromStream( in, &signature, &error ) );
	CHECK( !error.empty() );
}


// Without a checksum scheme the block is taken on trust, and the
// signature that follows it is handed back verbatim - which is how a
// replay file is read before its key is known.
TEST( ReplayMetaData_unverifiedRead )
{
	ReplayMetaData writer;
	writer.init( MD5SumScheme::create() );
	writer.add( "app", "bigworld" );

	MemoryOStream wire;
	writeSignedBlock( wire, writer );

	ReplayMetaData reader;
	reader.init( size_t( 16 ) );
	MemoryOStream wireBytesCopy;
	copyOf( wireBytesCopy, wire );
	MemoryIStream in( wireBytesCopy.data(), wireBytesCopy.size() );

	MemoryOStream signature;
	BW::string error;
	CHECK( reader.readFromStream( in, &signature, &error ) );
	CHECK_EQUAL( BW::string( "bigworld" ), reader.get( "app" ) );
	CHECK_EQUAL( 16, signature.size() );
	CHECK( sameBytes( static_cast< const char * >( wire.data() ) +
		wire.size() - 16, signature.data(), 16 ) );

	// Passing no signature stream at all is fine too, and init() clears
	// whatever the object was holding before.
	ReplayMetaData other;
	other.add( "stale", "value" );
	other.init( size_t( 16 ) );
	CHECK_EQUAL( 0u, unsigned( other.size() ) );

	MemoryOStream wireBytes2Copy;
	copyOf( wireBytes2Copy, wire );
	MemoryIStream in2( wireBytes2Copy.data(), wireBytes2Copy.size() );
	CHECK( other.readFromStream( in2 ) );
	CHECK_EQUAL( 1u, unsigned( other.size() ) );
}


// A block that claims more data than the stream holds, and one whose
// contents do not parse, are both refused with distinct messages.
TEST( ReplayMetaData_rejectsMalformedBlock )
{
	// The size header promises far more than is there.
	MemoryOStream truncated;
	uint32 promised = 1000;
	truncated << promised;

	ReplayMetaData reader;
	reader.init( size_t( 16 ) );
	MemoryOStream truncatedBytesCopy;
	copyOf( truncatedBytesCopy, truncated );
	MemoryIStream in( truncatedBytesCopy.data(), truncatedBytesCopy.size() );
	BW::string error;
	CHECK( !reader.readFromStream( in, NULL, &error ) );
	CHECK_EQUAL( BW::string( "Insufficient data for meta-data" ), error );

	// The other corruption shape - an honest size header whose payload
	// holds a string that runs off the end - is not testable here: the
	// overread inside the loop hits MemoryIStream::retrieve()'s dev
	// assertion, which aborts the test process before the "Meta-data
	// corrupted" error flag is ever set (release builds only).
}


// The static length check is what lets a replay reader skip a block it
// cannot parse yet; it reports the block size it worked out.
TEST( ReplayMetaData_checkSufficientLength )
{
	ReplayMetaData writer;
	writer.init( MD5SumScheme::create() );
	writer.add( "app", "bigworld" );

	MemoryOStream wire;
	writeSignedBlock( wire, writer );
	MemoryOStream wireCopy;
	copyOf( wireCopy, wire );

	size_t metaDataLength = 0;
	CHECK( ReplayMetaData::checkSufficientLength( wireCopy.data(),
		unsigned( wireCopy.size() ), 16, metaDataLength ) );
	CHECK_EQUAL( unsigned( 4 + writer.streamSize() + 16 ),
		unsigned( metaDataLength ) );

	// One byte short of the signature is one byte short of the block.
	CHECK( !ReplayMetaData::checkSufficientLength( wireCopy.data(),
		unsigned( wireCopy.size() ) - 1, 16, metaDataLength ) );

	// Not even the size header yet.
	CHECK( !ReplayMetaData::checkSufficientLength( wireCopy.data(),
		3u, 16, metaDataLength ) );
	CHECK( !ReplayMetaData::checkSufficientLength( wireCopy.data(),
		0u, 16, metaDataLength ) );
}


// swap() exchanges the contents of two blocks wholesale, which is how a
// block is handed over without copying.
TEST( ReplayMetaData_swap )
{
	ReplayMetaData first;
	first.init( MD5SumScheme::create() );
	first.add( "a", "1" );

	ReplayMetaData second;
	second.init( MD5SumScheme::create() );
	second.add( "b", "2" );
	second.add( "c", "3" );

	const size_t firstSize = first.streamSize();
	const size_t secondSize = second.streamSize();

	first.swap( second );

	CHECK_EQUAL( 2u, unsigned( first.size() ) );
	CHECK( first.hasKey( "b" ) );
	CHECK_EQUAL( BW::string( "2" ), first.get( "b" ) );
	CHECK_EQUAL( unsigned( secondSize ), unsigned( first.streamSize() ) );

	CHECK_EQUAL( 1u, unsigned( second.size() ) );
	CHECK( second.hasKey( "a" ) );
	CHECK_EQUAL( BW::string( "1" ), second.get( "a" ) );
	CHECK_EQUAL( unsigned( firstSize ), unsigned( second.streamSize() ) );
}

BW_END_NAMESPACE

// test_replay_metadata.cpp
