#include "pch.hpp"

#include "cstdmf/blob_or_null.hpp"
#include "cstdmf/memory_stream.hpp"


BW_BEGIN_NAMESPACE

TEST( BlobOrNull_construction )
{
	BlobOrNull nullBlob;

	CHECK( nullBlob.isNull() );
	CHECK_EQUAL( 0u, nullBlob.length() );
	CHECK( nullBlob.pData() == NULL );

	char data[] = "data";

	BlobOrNull blob( data, 4 );

	CHECK( !blob.isNull() );
	CHECK_EQUAL( 4u, blob.length() );
	CHECK( blob.pData() == data );
}


TEST( BlobOrNull_streamRoundTrip )
{
	MemoryOStream os;

	char data[] = "some blob data";

	BlobOrNull outBlob( data, 14 );
	os << outBlob;

	CHECK( os.error() == false );

	MemoryIStream is( os.retrieve( os.size() ), os.size() );

	BlobOrNull inBlob;
	is >> inBlob;

	CHECK( !inBlob.isNull() );
	CHECK_EQUAL( 14u, inBlob.length() );
	CHECK_EQUAL( 0, memcmp( data, inBlob.pData(), 14 ) );
}


TEST( BlobOrNull_nullStreamRoundTrip )
{
	MemoryOStream os;

	BlobOrNull outNull;
	os << outNull;

	MemoryIStream is( os.retrieve( os.size() ), os.size() );

	BlobOrNull inBlob;
	is >> inBlob;

	CHECK( inBlob.isNull() );
	CHECK_EQUAL( 0u, inBlob.length() );
}


TEST( BlobOrNull_emptyStreamRoundTrip )
{
	MemoryOStream os;

	// An empty, non null blob.
	BlobOrNull outEmpty( "", 0 );
	os << outEmpty;

	MemoryIStream is( os.retrieve( os.size() ), os.size() );

	BlobOrNull inBlob;
	is >> inBlob;

	CHECK( !inBlob.isNull() );
	CHECK_EQUAL( 0u, inBlob.length() );
}

BW_END_NAMESPACE

// test_blob_or_null.cpp
