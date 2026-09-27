#include "pch.hpp"

#include "network/file_stream.hpp"

#include "cstdmf/bw_vector.hpp"
#include "cstdmf/memory_stream.hpp"

#include <string.h>
#include <unistd.h>


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

/**
 *	A derived probe that exposes the protected LRU-queue internals the
 *	eviction tests need to observe. FileStream::open() bounds the
 *	process-global most-recently-used handle list via s_openFiles_
 *	(file_stream.cpp:300-308), which is protected.
 */
class ProbeFileStream : public FileStream
{
public:
	ProbeFileStream( const char * pPath, const char * pMode ) :
		FileStream( pPath, pMode )
	{
	}

	// Number of handles currently registered in the LRU queue.
	static size_t openFileCount() { return s_openFiles_.size(); }

	// The handle cap enforced by FileStream::open().
	static unsigned maxOpenFiles() { return MAX_OPEN_FILES; }

	// The disk-read buffer's current capacity.
	int readBufSize() const { return readBufSize_; }
};


/**
 *	A unique-per-process scratch path, so parallel test runs cannot collide.
 */
BW::string scratchPath( const char * tag )
{
	char buf[ 128 ];
	snprintf( buf, sizeof( buf ), "/tmp/fs_test_%d_%s.bin",
		static_cast< int >( getpid() ), tag );
	return BW::string( buf );
}


/**
 *	The byte at offset i of a file of the given flavour. The two flavours
 *	give visually distinct byte sequences, so a stream reading the wrong
 *	file can never pass by accident.
 */
unsigned char patternByte( unsigned char i, unsigned char flavour )
{
	return static_cast< unsigned char >( i * flavour + 1 );
}


/**
 *	Whether the file at path holds exactly len pattern bytes of the given
 *	flavour and nothing else.
 */
bool diskHasPattern( const BW::string & path, unsigned char flavour, int len )
{
	FILE * pFile = fopen( path.c_str(), "rb" );
	if (pFile == NULL)
	{
		return false;
	}

	bool matched = true;
	for (int i = 0; i < len; ++i)
	{
		int c = fgetc( pFile );
		if ((c == EOF) || (c != patternByte( (unsigned char)i, flavour )))
		{
			matched = false;
			break;
		}
	}

	if (matched && (fgetc( pFile ) != EOF))
	{
		matched = false;
	}

	fclose( pFile );
	return matched;
}

} // end namespace (anonymous)


// -----------------------------------------------------------------------------
// Section: write path
// -----------------------------------------------------------------------------

// commit() pushes the in-memory buffer to disk, reports success, resets the
// memory buffer, and the file on disk then holds exactly the committed
// bytes. length() never counts uncommitted bytes (file_stream.hpp:119-121 is
// explicit that the disk size excludes data not yet committed).
TEST( FileStream_commitWritesToDisk )
{
	BW::string path = scratchPath( "commit" );
	::remove( path.c_str() );

	unsigned char payload[ 20 ];
	for (int i = 0; i < 20; ++i)
	{
		payload[ i ] = patternByte( (unsigned char)i, 3 );
	}

	{
		ProbeFileStream stream( path.c_str(), "wb" );
		CHECK( stream.good() );

		stream.addBlob( payload, sizeof( payload ) );

		// Buffered in memory, not yet on disk.
		CHECK_EQUAL( 20, stream.size() );
		CHECK_EQUAL( 0, stream.length() );

		CHECK( stream.commit() );
		CHECK( stream.good() );

		// The memory buffer was reset by commit().
		CHECK_EQUAL( 0, stream.size() );
	}

	CHECK( diskHasPattern( path, 3, 20 ) );
	CHECK( ProbeFileStream::openFileCount() == 0 );

	::remove( path.c_str() );
}


// Destroying a FileStream with uncommitted bytes in the buffer commits them:
// close() calls commit() whenever the memory buffer is non-empty
// (file_stream.cpp:324-327).
TEST( FileStream_destructorAutoCommits )
{
	BW::string path = scratchPath( "dtor" );
	::remove( path.c_str() );

	{
		ProbeFileStream stream( path.c_str(), "wb" );

		unsigned char payload[ 8 ];
		for (int i = 0; i < 8; ++i)
		{
			payload[ i ] = patternByte( (unsigned char)i, 5 );
		}
		stream.addBlob( payload, sizeof( payload ) );

		CHECK_EQUAL( 8, stream.size() );
		CHECK( stream.good() );
	}

	CHECK( diskHasPattern( path, 5, 8 ) );

	::remove( path.c_str() );
}


// -----------------------------------------------------------------------------
// Section: read path
// -----------------------------------------------------------------------------

// The read path is disk-backed: retrieve() is an override that reads from
// the FILE* rather than the memory buffer, and a request larger than
// INIT_READ_BUF_SIZE (128 bytes) regrows the buffer to exactly the request
// (file_stream.cpp:188-213).
TEST( FileStream_readBackGrowsReadBuf )
{
	BW::string path = scratchPath( "read" );
	::remove( path.c_str() );

	const int LEN = 200;
	unsigned char payload[ LEN ];
	for (int i = 0; i < LEN; ++i)
	{
		payload[ i ] = patternByte( (unsigned char)i, 7 );
	}

	{
		ProbeFileStream writer( path.c_str(), "wb" );
		writer.addBlob( payload, sizeof( payload ) );
		CHECK( writer.commit() );
	}

	ProbeFileStream reader( path.c_str(), "rb" );

	// The read buffer starts at INIT_READ_BUF_SIZE. (Copied into a local
	// first: CHECK_EQUAL ODR-uses class-scope static consts, and this one
	// has no out-of-class definition.)
	const int initBufSize = FileStream::INIT_READ_BUF_SIZE;
	CHECK_EQUAL( initBufSize, reader.readBufSize() );

	const unsigned char * pBytes =
		static_cast< const unsigned char * >( reader.retrieve( LEN ) );

	CHECK( pBytes != NULL );
	CHECK( !reader.error() );
	CHECK( memcmp( pBytes, payload, sizeof( payload ) ) == 0 );
	CHECK_EQUAL( LEN, reader.readBufSize() );

	::remove( path.c_str() );
}


// A retrieve() past the end of the file fails exactly N reads: the error
// flag is raised and - with errno zeroed by retrieve() and a short read
// never setting it - strerror() falls back to the stored message rather
// than the libc one (file_stream.cpp:52-62, 204-210).
TEST( FileStream_shortReadFlagsError )
{
	BW::string path = scratchPath( "short" );
	::remove( path.c_str() );

	{
		ProbeFileStream writer( path.c_str(), "wb" );
		unsigned char payload[ 8 ] = { 1, 2, 3, 4, 5, 6, 7, 8 };
		writer.addBlob( payload, sizeof( payload ) );
		CHECK( writer.commit() );
	}

	ProbeFileStream reader( path.c_str(), "rb" );
	reader.retrieve( 16 );

	CHECK( reader.error() );
	CHECK( !reader.good() );
	CHECK( strcmp( reader.strerror(),
		"Couldn't read desired number of bytes from disk" ) == 0 );

	::remove( path.c_str() );
}


// -----------------------------------------------------------------------------
// Section: seeking and sizing
// -----------------------------------------------------------------------------

// tell/seek/length/stat all operate on the on-disk file: seeks are reported
// by tell, length is the disk size (never the buffered size), and stat
// agrees with length.
TEST( FileStream_tellSeekLengthStat )
{
	BW::string path = scratchPath( "seek" );
	::remove( path.c_str() );

	ProbeFileStream stream( path.c_str(), "wb" );
	CHECK( stream.good() );

	unsigned char payload[ 20 ];
	for (int i = 0; i < 20; ++i)
	{
		payload[ i ] = patternByte( (unsigned char)i, 9 );
	}
	stream.addBlob( payload, sizeof( payload ) );
	CHECK( stream.commit() );

	CHECK_EQUAL( 20, stream.length() );

	// seek returns 0 on success and tell reports the new position.
	CHECK_EQUAL( 0, stream.seek( 5 ) );
	CHECK_EQUAL( 5, stream.tell() );

	CHECK_EQUAL( 0, stream.seek( 0, SEEK_END ) );
	CHECK_EQUAL( 20, stream.tell() );

	// Bytes added to the memory buffer after the commit are not part of the
	// disk length.
	unsigned char more[ 5 ] = { 0xaa, 0xbb, 0xcc, 0xdd, 0xee };
	stream.addBlob( more, sizeof( more ) );
	CHECK_EQUAL( 20, stream.length() );

	struct stat statInfo;
	CHECK_EQUAL( 0, stream.stat( &statInfo ) );
	CHECK_EQUAL( 20, (int)statInfo.st_size );

	::remove( path.c_str() );
}


// A path that cannot be opened fails the construction, and every disk-facing
// call guards on open(): tell/seek/length/stat all report failure, and
// retrieve() hands back the (unused) read buffer rather than NULL - the
// caller must check error() (file_stream.cpp:280-284 and the guards at the
// head of each method).
TEST( FileStream_openFailureFlagsError )
{
	size_t countBefore = ProbeFileStream::openFileCount();

	// Never created on disk.
	BW::string path = scratchPath( "missing" );
	::remove( path.c_str() );

	ProbeFileStream stream( path.c_str(), "rb" );

	CHECK( stream.error() );
	CHECK( !stream.good() );

	CHECK_EQUAL( -1, stream.tell() );
	CHECK_EQUAL( -1, stream.seek( 0 ) );
	CHECK_EQUAL( -1, stream.length() );

	struct stat statInfo;
	CHECK_EQUAL( -1, stream.stat( &statInfo ) );

	CHECK( stream.retrieve( 4 ) != NULL );
	CHECK( stream.error() );

	// A failed open returns before registering in the LRU queue.
	CHECK( ProbeFileStream::openFileCount() == countBefore );
}


// -----------------------------------------------------------------------------
// Section: LRU handle management
// -----------------------------------------------------------------------------

// The most-recently-used queue never holds more than MAX_OPEN_FILES (20)
// handles: the 21st concurrent open evicts the least-recently-used one, and
// destroying every stream drains the queue to empty (file_stream.cpp:300-308).
TEST( FileStream_lruQueueCap )
{
	BW::string path = scratchPath( "lru" );
	::remove( path.c_str() );

	// Seed the flood streams with a file to open.
	{
		ProbeFileStream seed( path.c_str(), "wb" );
		unsigned char payload[ 4 ] = { 0 };
		seed.addBlob( payload, sizeof( payload ) );
		CHECK( seed.commit() );
	}

	// Local copy first: MAX_OPEN_FILES has no out-of-class definition.
	const unsigned maxOpen = ProbeFileStream::maxOpenFiles();
	CHECK_EQUAL( 20, maxOpen );

	{
		BW::vector< ProbeFileStream * > flood;
		for (unsigned i = 0; i < maxOpen + 5; ++i)
		{
			flood.push_back( new ProbeFileStream( path.c_str(), "rb" ) );
			CHECK( flood.back()->good() );

			// The cap holds after every open.
			CHECK( ProbeFileStream::openFileCount() <= maxOpen );
		}

		CHECK_EQUAL( (int)maxOpen, (int)ProbeFileStream::openFileCount() );

		for (BW::vector< ProbeFileStream * >::iterator it = flood.begin();
			it != flood.end(); ++it)
		{
			delete *it;
		}
	}

	CHECK( ProbeFileStream::openFileCount() == 0 );

	::remove( path.c_str() );
}


// A stream evicted while not in use is transparently reopened on the next
// disk call, and the position it held at close time is restored - the
// real-open branch fseeks to the saved offset when it is non-zero
// (file_stream.cpp:289-297).
TEST( FileStream_lruEvictionRestoresPosition )
{
	BW::string path = scratchPath( "evict" );
	::remove( path.c_str() );

	const int LEN = 40;
	unsigned char payload[ LEN ];
	for (int i = 0; i < LEN; ++i)
	{
		payload[ i ] = patternByte( (unsigned char)i, 11 );
	}

	{
		ProbeFileStream writer( path.c_str(), "wb" );
		writer.addBlob( payload, sizeof( payload ) );
		CHECK( writer.commit() );
	}

	ProbeFileStream reader( path.c_str(), "rb" );

	// Read the first ten bytes: the FILE position is now 10.
	const unsigned char * pFirst =
		static_cast< const unsigned char * >( reader.retrieve( 10 ) );
	CHECK( !reader.error() );
	CHECK( memcmp( pFirst, payload, 10 ) == 0 );

	// Flood the queue so the reader becomes the least-recently-used handle
	// and is closed underneath us. (maxOpen floods: the reader was the
	// oldest entry, so the final push evicts exactly it.)
	{
		BW::vector< ProbeFileStream * > flood;
		for (unsigned i = 0; i < ProbeFileStream::maxOpenFiles(); ++i)
		{
			flood.push_back( new ProbeFileStream( path.c_str(), "rb" ) );
		}

		CHECK_EQUAL( (int)ProbeFileStream::maxOpenFiles(),
			(int)ProbeFileStream::openFileCount() );

		for (BW::vector< ProbeFileStream * >::iterator it = flood.begin();
			it != flood.end(); ++it)
		{
			delete *it;
		}
	}

	// The next read transparently reopens and resumes at offset 10.
	const unsigned char * pSecond =
		static_cast< const unsigned char * >( reader.retrieve( 10 ) );
	CHECK( !reader.error() );
	CHECK( memcmp( pSecond, payload + 10, 10 ) == 0 );

	::remove( path.c_str() );
}


// Two streams held open at once keep their FILE positions independent, even
// though each open() on the second one shuffles the first through the middle
// of the LRU queue (the remove-and-repush branch, file_stream.cpp:271-275).
TEST( FileStream_alternatingStreamsStayDistinct )
{
	BW::string pathA = scratchPath( "altA" );
	BW::string pathB = scratchPath( "altB" );
	::remove( pathA.c_str() );
	::remove( pathB.c_str() );

	unsigned char a[ 8 ], b[ 8 ];
	for (int i = 0; i < 8; ++i)
	{
		a[ i ] = patternByte( (unsigned char)i, 13 );
		b[ i ] = patternByte( (unsigned char)i, 17 );
	}

	{
		ProbeFileStream writerA( pathA.c_str(), "wb" );
		writerA.addBlob( a, sizeof( a ) );
		CHECK( writerA.commit() );
	}
	{
		ProbeFileStream writerB( pathB.c_str(), "wb" );
		writerB.addBlob( b, sizeof( b ) );
		CHECK( writerB.commit() );
	}

	ProbeFileStream ra( pathA.c_str(), "rb" );
	ProbeFileStream rb( pathB.c_str(), "rb" );

	const unsigned char * pA1 =
		static_cast< const unsigned char * >( ra.retrieve( 4 ) );
	CHECK( memcmp( pA1, a, 4 ) == 0 );

	const unsigned char * pB1 =
		static_cast< const unsigned char * >( rb.retrieve( 4 ) );
	CHECK( memcmp( pB1, b, 4 ) == 0 );

	const unsigned char * pA2 =
		static_cast< const unsigned char * >( ra.retrieve( 4 ) );
	CHECK( memcmp( pA2, a + 4, 4 ) == 0 );

	const unsigned char * pB2 =
		static_cast< const unsigned char * >( rb.retrieve( 4 ) );
	CHECK( memcmp( pB2, b + 4, 4 ) == 0 );

	CHECK( !ra.error() );
	CHECK( !rb.error() );

	::remove( pathA.c_str() );
	::remove( pathB.c_str() );
}


// -----------------------------------------------------------------------------
// Section: combined read/write on one handle
// -----------------------------------------------------------------------------

// A read-write handle can commit and read back through itself. setMode()
// performs the ANSI-required position-call interleave between the write and
// the read phase (file_stream.cpp:239-253); commit() leaves the position at
// the end of what was written, so a seek(0) precedes the read-back.
TEST( FileStream_writeThenReadSameStream )
{
	BW::string path = scratchPath( "rw" );
	::remove( path.c_str() );

	ProbeFileStream stream( path.c_str(), "w+b" );
	CHECK( stream.good() );

	unsigned char payload[ 12 ];
	for (int i = 0; i < 12; ++i)
	{
		payload[ i ] = patternByte( (unsigned char)i, 19 );
	}
	stream.addBlob( payload, sizeof( payload ) );
	CHECK( stream.commit() );

	CHECK_EQUAL( 0, stream.seek( 0 ) );

	const void * pRound = stream.retrieve( 12 );
	CHECK( !stream.error() );
	CHECK( memcmp( pRound, payload, sizeof( payload ) ) == 0 );

	::remove( path.c_str() );
}


BW_END_NAMESPACE

// test_file_stream.cpp