#include "pch.hpp"

#include "cstdmf/memory_stream.hpp"

#include "connection/data_download.hpp"
#include "connection/download_segment.hpp"

#include <string.h>


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

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
 *	Serialises a string into a fresh input stream, the shape
 * setDesc() consumes.
 */
MemoryIStream * stringStream( const char * text )
{
	MemoryOStream out;
	BW::string value( text );
	out << value;
	int n = out.size();
	char * buf = new char[ n ];
	memcpy( buf, out.retrieve( n ), n );
	return new OwnedIStream( buf, n );
}

} // end namespace (anonymous)


// A download completes only when both the description and the final
// segment have arrived, in any order, and the description survives in
// the record alongside its id.
TEST( DataDownload_segmentsAndCompletion )
{
	DataDownload download( 7 );
	CHECK_EQUAL( uint16_t( 7 ), uint16_t( download.id() ) );
	CHECK( download.empty() );
	CHECK( download.pDesc() == NULL );
	CHECK( !download.complete() );

	// Segments alone are not enough.
	download.insert( new DownloadSegment( "abc", 3, 0 ), /* isLast */ false );
	CHECK( !download.complete() );

	// The final segment without a description is not enough either.
	download.insert( new DownloadSegment( "de", 2, 1 ), /* isLast */ true );
	CHECK( !download.complete() );

	// The description completes it - note it may arrive after segments.
	MemoryIStream * pDescStream = stringStream( "my download" );
	download.setDesc( *pDescStream );
	delete pDescStream;

	CHECK( download.complete() );
	CHECK( download.pDesc() != NULL );
	CHECK_EQUAL( BW::string( "my download" ), *download.pDesc() );
	CHECK_EQUAL( 2u, download.size() );
}


// write() concatenates the segment payloads in insertion order - the
// seq_ numbers the segments carry are not used for ordering (despite
// the "sorted fashion" wording in the method comment).
TEST( DataDownload_writeOrderIsInsertionOrder )
{
	DataDownload download( 1 );

	download.insert( new DownloadSegment( "second", 6, 1 ),
		/* isLast */ false );
	download.insert( new DownloadSegment( "first...", 8, 0 ),
		/* isLast */ false );
	download.insert( new DownloadSegment( "!", 1, 2 ),
		/* isLast */ true );

	MemoryIStream * pDescStream = stringStream( "desc" );
	download.setDesc( *pDescStream );
	delete pDescStream;
	CHECK( download.complete() );

	MemoryOStream out;
	download.write( out );
	CHECK_EQUAL( 15, out.size() );
	CHECK( memcmp( out.retrieve( out.size() ), "secondfirst...!",
		15 ) == 0 );

	// A second download completes the same way; both destruct cleanly
	// (the leak checker at exit fails the binary otherwise).
	DataDownload other( 2 );
	other.insert( new DownloadSegment( "x", 1, 0 ), /* isLast */ true );
	MemoryIStream * pOtherDesc = stringStream( "d2" );
	other.setDesc( *pOtherDesc );
	delete pOtherDesc;
	CHECK( other.complete() );
}

BW_END_NAMESPACE

// test_data_download.cpp
