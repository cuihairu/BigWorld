#include "pch.hpp"

#include "cstdmf/memory_stream.hpp"

#include "network/zip_stream.hpp"

#include <string.h>
#include <zlib.h>

BW_BEGIN_NAMESPACE

/**
 *	Tests for ZipOStream/ZipIStream (zlib deflate streams).
 *
 *	zlib itself now comes from vcpkg (>= 1.3) instead of the vendored 1.2.8;
 *	these tests pin the container format the engine writes on the wire: a
 *	string-encoded original length, a string-encoded compressed length, then
 *	the raw zlib stream.
 */

namespace // (anonymous)
{

// Fills n bytes with a predictable, compressible-but-not-trivial pattern.
void fillPattern( BW::vector<uint8> &data, size_t n )
{
	data.resize( n );
	for (size_t i = 0; i < n; ++i)
	{
		data[ i ] = (uint8)(( i * 7 + ( i >> 8 ) ) & 0xff);
	}
}

} // end namespace (anonymous)


TEST( zip_stream_roundtrip )
{
	const size_t lengths[] = { 1, 7, 100, 1024, 100000 };

	for (size_t l = 0; l < sizeof( lengths ) / sizeof( lengths[0] ); ++l)
	{
		BW::vector<uint8> original;
		fillPattern( original, lengths[l] );

		// Compress into a memory stream. ZipOStream does the real work in its
		// destructor, so it has to go out of scope before we read the result.
		MemoryOStream compressedStream;
		{
			ZipOStream zipStream( compressedStream );
			zipStream.addBlob( &original[0], (int)original.size() );
		}

		CHECK( compressedStream.size() > 0 );

		// The length prefix must let readers pre-allocate exactly.
		CHECK( (size_t)ZipIStream::peekOriginalLength( compressedStream ) ==
			original.size() );

		// Decompress and compare. ZipIStream is a MemoryIStream over the
		// inflated payload, so remainingLength() is the original size.
		ZipIStream unzipStream( compressedStream );
		CHECK( !unzipStream.error() );
		CHECK( (size_t)unzipStream.remainingLength() == original.size() );

		const uint8 *recovered =
			(const uint8 *)unzipStream.retrieve( (int)original.size() );

		CHECK( recovered != NULL );
		if (recovered)
		{
			CHECK( memcmp( &original[0 ], recovered, original.size() ) == 0 );
		}

		// The reader consumed the whole container.
		CHECK( compressedStream.remainingLength() == 0 );
	}
}


TEST( zip_stream_peek_original_length_leaves_stream_intact )
{
	BW::vector<uint8> original;
	fillPattern( original, 5000 );

	MemoryOStream compressedStream;
	{
		ZipOStream zipStream( compressedStream );
		zipStream.addBlob( &original[0], (int)original.size() );
	}

	const int totalLength = compressedStream.size();

	const int first = ZipIStream::peekOriginalLength( compressedStream );
	const int second = ZipIStream::peekOriginalLength( compressedStream );
	CHECK( first == 5000 );
	CHECK( second == 5000 );

	// Peeking must not consume anything: the stream is still fully readable.
	CHECK( compressedStream.remainingLength() == totalLength );

	ZipIStream unzipStream( compressedStream );
	CHECK( !unzipStream.error() );
	CHECK( unzipStream.remainingLength() == 5000 );

	const uint8 *recovered = (const uint8 *)unzipStream.retrieve( 5000 );
	CHECK( recovered != NULL );
	if (recovered)
	{
		CHECK( memcmp( &original[0 ], recovered, original.size() ) == 0 );
	}
}


TEST( zip_stream_rejects_compressed_length_past_end_of_stream )
{
	// A well-formed header claiming a huge compressed payload, with no
	// payload bytes behind it. init() must notice that the declared length
	// runs past the end of the stream and report the error rather than reading
	// out of bounds.
	MemoryOStream bogus;
	bogus.writeStringLength( 1024 );
	bogus.writeStringLength( 999999 );
	CHECK( bogus.size() == 8 );

	MemoryIStream input( (const char *)bogus.data(), bogus.size() );

	ZipIStream unzipStream;
	CHECK( !unzipStream.init( input ) );
	CHECK( unzipStream.error() );
	CHECK( unzipStream.remainingLength() == 0 );
}


TEST( zip_stream_init_with_caller_supplied_buffer )
{
	BW::vector<uint8> original;
	fillPattern( original, 2000 );

	MemoryOStream compressedStream;
	{
		ZipOStream zipStream( compressedStream );
		zipStream.addBlob( &original[0], (int)original.size() );
	}

	const char *bytes = (const char *)compressedStream.data();
	const int totalLength = compressedStream.size();

	// A buffer that cannot hold the inflated payload must be rejected, and the
	// stream must report the error rather than silently truncating.
	{
		unsigned char tooSmall[ 16 ];
		MemoryIStream input( bytes, totalLength );

		ZipIStream unzipStream;
		CHECK( !unzipStream.init( input, tooSmall, sizeof( tooSmall ) ) );
		CHECK( unzipStream.error() );
		CHECK( unzipStream.remainingLength() == 0 );
	}

	BW::vector<uint8> bigBuffer( original.size() );

	// A large enough buffer is used in place of an allocation.
	{
		MemoryIStream input( bytes, totalLength );

		ZipIStream unzipStream;
		CHECK( unzipStream.init( input, &bigBuffer[0], bigBuffer.size() ) );
		CHECK( !unzipStream.error() );
		CHECK( (size_t)unzipStream.remainingLength() == original.size() );
		CHECK( memcmp( &bigBuffer[0], &original[0], original.size() ) == 0 );
	}

	// The caller still owns the buffer after the ZipIStream that decompressed
	// into it has been destroyed (no double free, no dangling pointer).
	bigBuffer[ 0 ] = 0xA5;
	CHECK( bigBuffer[ 0 ] == 0xA5 );
}

/**
 *	ZipIStream used to size the destination buffer straight from the
 *	peer-declared decompressed size with no cap, so a tiny compressed payload
 *	declaring an implausibly large decompressed size could demand a huge
 *	allocation. DEFLATE's worst-case expansion is 1032:1, so such a declaration
 *	cannot come from a real zlib stream; init() must reject it before
 *	allocating (CR-P2-21).
 */
TEST( zip_stream_rejects_original_length_beyond_deflate_ratio )
{
	BW::vector<uint8> original;
	fillPattern( original, 64 );

	// A valid zlib blob of that small payload.
	uLongf compressBoundLen = compressBound( original.size() );
	BW::vector<uint8> compressed( compressBoundLen );
	uLongf compressLen = compressBoundLen;
	CHECK( compress2( &compressed[0], &compressLen, &original[0],
			original.size(), Z_DEFAULT_COMPRESSION ) == Z_OK );

	// A well-formed container except that the declared decompressed size is
	// far beyond what this compressed payload could ever inflate to. Without
	// the ratio bound init() would allocate a ~16 MB buffer and, because the
	// blob is real, succeed; with the bound it is rejected before allocating.
	MemoryOStream container;
	container.writeStringLength( 0xFFFFFF );
	container.writeStringLength( (int)compressLen );
	container.addBlob( &compressed[0], (int)compressLen );

	MemoryIStream input( (const char *)container.data(), container.size() );

	ZipIStream unzipStream;
	CHECK( !unzipStream.init( input ) );
	CHECK( unzipStream.error() );
	CHECK( unzipStream.remainingLength() == 0 );
}

BW_END_NAMESPACE

// test_zip_stream.cpp
