#include "pch.hpp"

#include "cstdmf/grow_only_pod_allocator.hpp"


BW_BEGIN_NAMESPACE

TEST( GrowOnlyPodAllocator_singleAllocation )
{
	GrowOnlyPodAllocator allocator( 1024 );

	uint32 * pInt = (uint32 *)allocator.allocate( sizeof( uint32 ) );

	CHECK( pInt != NULL );

	*pInt = 0xDEADBEEFu;
	CHECK_EQUAL( 0xDEADBEEFu, *pInt );
}


TEST( GrowOnlyPodAllocator_multipleAllocations )
{
	GrowOnlyPodAllocator allocator( 4096 );

	// Allocate many small blocks; they must not overlap.
	static const int NUM_ALLOCS = 100;
	void * blocks[ NUM_ALLOCS ];

	for (int i = 0; i < NUM_ALLOCS; ++i)
	{
		blocks[ i ] = allocator.allocate( 32 );
		CHECK( blocks[ i ] != NULL );

		memset( blocks[ i ], i, 32 );
	}

	// Verify the contents are still intact.
	for (int i = 0; i < NUM_ALLOCS; ++i)
	{
		const unsigned char * p =
			(const unsigned char *)blocks[ i ];
		for (int j = 0; j < 32; ++j)
		{
			CHECK_EQUAL( (unsigned char)i, p[ j ] );
		}
	}
}


TEST( GrowOnlyPodAllocator_pageOverflow )
{
	GrowOnlyPodAllocator allocator( 256 );

	// Force several new pages to be allocated.
	for (int i = 0; i < 20; ++i)
	{
		void * p = allocator.allocate( 128 );
		CHECK( p != NULL );
		memset( p, 0, 128 );
	}
}


TEST( GrowOnlyPodAllocator_deallocateIsNoOp )
{
	GrowOnlyPodAllocator allocator( 1024 );

	void * p = allocator.allocate( 16 );

	// Deallocation does nothing, the memory stays valid.
	allocator.deallocate( p );

	memset( p, 0xAB, 16 );
}


TEST( GrowOnlyPodAllocator_releaseAll )
{
	GrowOnlyPodAllocator allocator( 256 );

	// Allocate over multiple pages.
	for (int i = 0; i < 10; ++i)
	{
		allocator.allocate( 64 );
	}

	allocator.releaseAll();

	// The allocator is usable again after a release.
	void * p = allocator.allocate( 64 );
	CHECK( p != NULL );
}


TEST( GrowOnlyPodAllocator_printUsageStats )
{
	GrowOnlyPodAllocator allocator( 512 );

	allocator.allocate( 100 );
	allocator.allocate( 100 );

	// Mostly to make sure it doesn't crash.
	allocator.printUsageStats();
}

BW_END_NAMESPACE

// test_grow_only_pod_allocator.cpp
