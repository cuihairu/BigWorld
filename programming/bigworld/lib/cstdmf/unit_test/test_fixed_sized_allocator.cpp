#include "pch.hpp"

#include "cstdmf/allocator.hpp"
#include "cstdmf/fixed_sized_allocator.hpp"
#include "cstdmf/stdmf.hpp"

/**
 *	Direct drive of BW::FixedSizedAllocator (coverage batch 21).
 *
 *	Note: the two pre-existing FixedSizedAllocator test groups in
 *	test_allocate.cpp (FixedSizedAllocator_testFull1/testFull2) are guarded
 *	by ENABLE_FIXED_SIZED_POOL_ALLOCATOR, which is
 *	(defined(_WIN32) || (defined(MF_SERVER) && ENABLE_MEMORY_DEBUG))
 *	per config.hpp:123 - always 0 on the el7 server test build. That is why
 *	lib/cstdmf/fixed_sized_allocator.cpp sat at 0% coverage despite those
 *	tests existing. This file has no such guard: the class itself is always
 *	compiled into libcstdmf.
 */

BW_BEGIN_NAMESPACE

namespace
{
	const size_t kSizes[] = { 8, 16, 32, 64 };
	const size_t kPoolSize = 128;

	// The destructor unconditionally calls (*free_)( poolSpans_, ... ) even
	// when no pool was ever created, so the hooks must be installed before
	// any destructor runs - otherwise it dereferences a null function
	// pointer.
	void installHeapHooks( BW::FixedSizedAllocator & allocator )
	{
		allocator.setLargeAllocationHooks(
			&BW::Allocator::heapAllocate,
			&BW::Allocator::heapDeallocate,
			&BW::Allocator::heapReallocate,
			&BW::Allocator::heapMemorySize );
	}
}


// Section: FixedSizedAllocator direct drive (coverage batch 21)

TEST( FixedSizedAllocator_constructFixed_queryAccessors )
{
	BW::FixedSizedAllocator allocator(
		kSizes, ARRAY_SIZE( kSizes ), kPoolSize, "batch21-fixed", false );
	installHeapHooks( allocator );

	CHECK_EQUAL( kPoolSize, allocator.getPoolSize() );

	// Pool exists / query table before any allocation
	CHECK( allocator.poolExistsForSize( 8 ) );
	CHECK( allocator.poolExistsForSize( 64 ) );
	CHECK( !allocator.poolExistsForSize( 65 ) );

	// No pool header yet: empty-queries report empty/full/no-free
	CHECK( allocator.isAllPoolsEmptyForSize( 8 ) );
	CHECK( !allocator.isAllPoolsEmptyForSize( 65 ) );
	CHECK_EQUAL( size_t( 0 ),
		allocator.getFirstPoolNumFreeForSize( 8 ) );
	CHECK_EQUAL( size_t( 0 ),
		allocator.getFirstPoolNumFreeForSize( 65 ) );
	CHECK( allocator.isLastPoolFullForSize( 8 ) );
	CHECK( !allocator.isLastPoolFullForSize( 65 ) );

	CHECK_EQUAL( size_t( kPoolSize / 32 ),
		allocator.getNumPoolItemsForSize( 32 ) );
}


TEST( FixedSizedAllocator_constructAuto_emptyTableQueries )
{
	// auto-pool constructor: sizes table starts empty (numPools_ == 0)
	BW::FixedSizedAllocator allocator(
		/* maxPoolSize */ 4096, /* poolSize */ 64, "batch21-auto" );
	installHeapHooks( allocator );

	CHECK_EQUAL( size_t( 64 ), allocator.getPoolSize() );

	// Every size-query falls through the empty loop
	CHECK( !allocator.poolExistsForSize( 32 ) );
	CHECK( !allocator.isAllPoolsEmptyForSize( 32 ) );
	CHECK( !allocator.isLastPoolFullForSize( 32 ) );
	CHECK_EQUAL( size_t( 0 ),
		allocator.getFirstPoolNumFreeForSize( 32 ) );
}


TEST( FixedSizedAllocator_deallocateNull_isNoop )
{
	BW::FixedSizedAllocator allocator(
		kSizes, ARRAY_SIZE( kSizes ), kPoolSize, "batch21-null", false );
	installHeapHooks( allocator );

	// early-out on null pointer; nothing to CHECK, just must not crash
	allocator.deallocate( NULL );

	// also destroyed with hooks installed but poolSpans_ never allocated:
	// destructor calls (*free_)( NULL ) which heapDeallocate tolerates
}


TEST( FixedSizedAllocator_largeAllocation_fallsBackToHeap )
{
	BW::FixedSizedAllocator allocator(
		kSizes, ARRAY_SIZE( kSizes ), kPoolSize, "batch21-heap", false );
	installHeapHooks( allocator );

	// larger than every pool size and no auto pools -> findPool returns -1
	// and the request goes straight through the heap hooks
	void * ptr = allocator.allocate( 4096 );
	CHECK( ptr != NULL );
	CHECK( !allocator.poolExistsForSize( 4096 ) );

	// memorySize on a heap pointer
	CHECK_EQUAL( BW::Allocator::heapMemorySize( ptr ),
		allocator.memorySize( ptr ) );

	// allocationInfo on a heap pointer: flags cleared, size from memsize_
	BW::FixedSizedAllocator::AllocationInfo info;
	allocator.allocationInfo( ptr, info );
	CHECK_EQUAL( uint32( 0 ), info.allocFlags_ );
	CHECK_EQUAL( BW::Allocator::heapMemorySize( ptr ), info.allocSize_ );

	// deallocate goes straight back to the heap hook (no pool lookup hit)
	allocator.deallocate( ptr );
	CHECK( !allocator.poolExistsForSize( 4096 ) );
}


TEST( FixedSizedAllocator_poolAllocation_buildsPoolChain )
{
	BW::FixedSizedAllocator allocator(
		kSizes, ARRAY_SIZE( kSizes ), kPoolSize, "batch21-pool", false );
	installHeapHooks( allocator );

	// first allocation for a size creates a pool (free-block chain, span)
	void * p1 = allocator.allocate( 8 );
	CHECK( p1 != NULL );
	CHECK_EQUAL( size_t( kPoolSize / 8 - 1 ),
		allocator.getFirstPoolNumFreeForSize( 8 ) );
	CHECK( !allocator.isAllPoolsEmptyForSize( 8 ) );
	CHECK( !allocator.isLastPoolFullForSize( 8 ) );

	// memorySize / allocationInfo on a pool pointer
	CHECK_EQUAL( size_t( 8 ), allocator.memorySize( p1 ) );
	BW::FixedSizedAllocator::AllocationInfo info;
	allocator.allocationInfo( p1, info );
	CHECK_EQUAL( uint32( BW::Allocator::IF_POOL_ALLOC ), info.allocFlags_ );
	CHECK_EQUAL( size_t( 8 ), info.allocSize_ );

	// fill the first pool; the 16th slot takes the "last available slot"
	// arm where firstFree_ reads back as null after the block is consumed
	void * fill[15];
	for (int i = 0; i < 15; ++i)
	{
		fill[i] = allocator.allocate( 8 );
		CHECK( fill[i] != NULL );
	}
	CHECK_EQUAL( size_t( 0 ),
		allocator.getFirstPoolNumFreeForSize( 8 ) );
	CHECK( allocator.isLastPoolFullForSize( 8 ) );

	// pool full: the next request creates a second pool linked at the head
	void * q1 = allocator.allocate( 8 );
	CHECK( q1 != NULL );
	CHECK_EQUAL( size_t( kPoolSize / 8 - 1 ),
		allocator.getFirstPoolNumFreeForSize( 8 ) );

	// free the head pool first: prevPool_ == null arm with nextPool_
	// non-null (the surviving tail adopts the link)
	allocator.deallocate( q1 );

	// free the first pool entirely: it is now the only pool, so both
	// prevPool_ and nextPool_ are null and the span array collapses
	allocator.deallocate( p1 );
	for (int i = 0; i < 15; ++i)
	{
		allocator.deallocate( fill[i] );
	}
	CHECK( allocator.isAllPoolsEmptyForSize( 8 ) );
	CHECK( allocator.isLastPoolFullForSize( 8 ) );
}


TEST( FixedSizedAllocator_threePoolList_relinkHead )
{
	BW::FixedSizedAllocator allocator(
		kSizes, ARRAY_SIZE( kSizes ), kPoolSize, "batch21-list", false );
	installHeapHooks( allocator );

	// build three pools for size 8: list is pool3(head) -> pool2 -> pool1
	void * p1[16];
	void * p2[16];
	void * p3[16];
	for (int i = 0; i < 16; ++i) { p1[i] = allocator.allocate( 8 ); }
	for (int i = 0; i < 16; ++i) { p2[i] = allocator.allocate( 8 ); }
	for (int i = 0; i < 16; ++i) { p3[i] = allocator.allocate( 8 ); }
	CHECK( p1[0] != NULL );
	CHECK( p2[0] != NULL );
	CHECK( p3[0] != NULL );

	// all three pools full: last-pool-full walks the whole list
	CHECK( allocator.isLastPoolFullForSize( 8 ) );

	// free two blocks of the middle pool: it still has free slots but is
	// not the head of the list
	allocator.deallocate( p2[0] );
	allocator.deallocate( p2[1] );

	// next allocation finds pool3 full, pool2 with space: the relink arm
	// unlinks pool2 (prev and next both non-null) and moves it to the
	// head. The head pool now reports its remaining free slot.
	void * relinked = allocator.allocate( 8 );
	CHECK( relinked != NULL );
	CHECK_EQUAL( size_t( 1 ),
		allocator.getFirstPoolNumFreeForSize( 8 ) );
	// head has space -> last-pool-full is false via the mid-list arm
	CHECK( !allocator.isLastPoolFullForSize( 8 ) );

	// consume the relinked head's last free slot (no relink: numFree_ 0)
	void * lastFree = allocator.allocate( 8 );
	CHECK( lastFree != NULL );
	CHECK_EQUAL( size_t( 0 ),
		allocator.getFirstPoolNumFreeForSize( 8 ) );

	// now the tail (pool1) holds the only free slots: the next allocation
	// relinks the TAIL - prevPool_ non-null, nextPool_ null arm
	allocator.deallocate( p1[0] );
	allocator.deallocate( p1[1] );
	void * tailRelinked = allocator.allocate( 8 );
	CHECK( tailRelinked != NULL );
	CHECK_EQUAL( size_t( 1 ),
		allocator.getFirstPoolNumFreeForSize( 8 ) );

	// release everything: removal arms fire across the list shapes.
	// relinked/lastFree were recycled out of pool2's two scratch blocks and
	// tailRelinked out of pool1's, so those scratch slots must not be freed
	// again here - freeing the recycled pointers covers them, and the one
	// pool1 scratch block that was never recycled is already free.
	allocator.deallocate( relinked );
	allocator.deallocate( lastFree );
	allocator.deallocate( tailRelinked );
	for (int i = 2; i < 16; ++i) { allocator.deallocate( p1[i] ); }
	for (int i = 2; i < 16; ++i) { allocator.deallocate( p2[i] ); }
	for (int i = 0; i < 16; ++i) { allocator.deallocate( p3[i] ); }
	CHECK( allocator.isAllPoolsEmptyForSize( 8 ) );
}


TEST( FixedSizedAllocator_poolRemoval_allThreeArms )
{
	BW::FixedSizedAllocator allocator(
		kSizes, ARRAY_SIZE( kSizes ), kPoolSize, "batch21-remove", false );
	installHeapHooks( allocator );

	// three full pools: pool3(head) -> pool2 -> pool1
	void * p1[16];
	void * p2[16];
	void * p3[16];
	for (int i = 0; i < 16; ++i) { p1[i] = allocator.allocate( 8 ); }
	for (int i = 0; i < 16; ++i) { p2[i] = allocator.allocate( 8 ); }
	for (int i = 0; i < 16; ++i) { p3[i] = allocator.allocate( 8 ); }

	// middle pool empties first: prevPool_ and nextPool_ both non-null
	for (int i = 0; i < 16; ++i) { allocator.deallocate( p2[i] ); }

	// tail pool empties next: prevPool_ non-null, nextPool_ null
	for (int i = 0; i < 16; ++i) { allocator.deallocate( p1[i] ); }

	// head pool empties last: prevPool_ null, nextPool_ null
	for (int i = 0; i < 16; ++i) { allocator.deallocate( p3[i] ); }

	CHECK( allocator.isAllPoolsEmptyForSize( 8 ) );
	// spans collapsed to null: a fresh pool must re-init the span array
	void * fresh = allocator.allocate( 8 );
	CHECK( fresh != NULL );
	allocator.deallocate( fresh );
	CHECK( allocator.isAllPoolsEmptyForSize( 8 ) );
}


TEST( FixedSizedAllocator_singleItemPool_firstFreeNullArm )
{
	// one item per pool (poolSize == allocSize) exercises the firstFree_
	// == null arm right after pool creation
	const size_t sizes[] = { 64 };
	BW::FixedSizedAllocator allocator(
		sizes, ARRAY_SIZE( sizes ), /* poolSize */ 64,
		"batch21-single", false );
	installHeapHooks( allocator );

	CHECK_EQUAL( size_t( 1 ), allocator.getNumPoolItemsForSize( 64 ) );

	void * p = allocator.allocate( 64 );
	CHECK( p != NULL );
	CHECK_EQUAL( size_t( 0 ),
		allocator.getFirstPoolNumFreeForSize( 64 ) );
	CHECK( allocator.isLastPoolFullForSize( 64 ) );

	// second request: pool is full, a fresh single-item pool is created
	// and inserted ahead of the existing one
	void * q = allocator.allocate( 64 );
	CHECK( q != NULL );
	CHECK( q != p );
	CHECK_EQUAL( size_t( 0 ),
		allocator.getFirstPoolNumFreeForSize( 64 ) );

	// freeing the old pool first: prevPool_ non-null, nextPool_ null
	allocator.deallocate( p );
	// freeing the head pool last: prevPool_ null, nextPool_ null
	allocator.deallocate( q );
	CHECK( allocator.isAllPoolsEmptyForSize( 64 ) );
}


TEST( FixedSizedAllocator_reallocate_branches )
{
	BW::FixedSizedAllocator allocator(
		kSizes, ARRAY_SIZE( kSizes ), kPoolSize, "batch21-realloc", false );
	installHeapHooks( allocator );

	// null pointer: skips the lookup block entirely, allocates fresh
	void * fresh = allocator.reallocate( NULL, 8 );
	CHECK( fresh != NULL );
	CHECK_EQUAL( size_t( 8 ), allocator.memorySize( fresh ) );
	allocator.deallocate( fresh );

	// same-pool fast path: request 12 still resolves to the 16-byte pool,
	// so the original pointer is returned as-is without copying
	void * p16 = allocator.allocate( 16 );
	CHECK( p16 != NULL );
	char pattern[16];
	for (int i = 0; i < 16; ++i) { pattern[i] = static_cast<char>( i + 1 ); }
	memcpy( p16, pattern, sizeof( pattern ) );
	void * samePtr = allocator.reallocate( p16, 12 );
	CHECK( samePtr == p16 );
	CHECK( memcmp( samePtr, pattern, sizeof( pattern ) ) == 0 );

	// pool -> different pool size: allocates from the 64-byte pool, copies
	// min( oldSize, newSize ) bytes and returns the old block to its pool
	void * p64 = allocator.reallocate( p16, 50 );
	CHECK( p64 != NULL );
	CHECK( p64 != p16 );
	CHECK_EQUAL( size_t( 64 ), allocator.memorySize( p64 ) );
	CHECK( memcmp( p64, pattern, sizeof( pattern ) ) == 0 );
	// p16 went back to the pool: a fresh request yields a live pool block
	void * p16again = allocator.allocate( 16 );
	CHECK( p16again != NULL );

	// pool -> oversized: the pool hint is -1 so allocation falls back to
	// the heap hooks while the old block returns to its pool
	void * toHeap = allocator.reallocate( p64, 4096 );
	CHECK( toHeap != NULL );
	CHECK_EQUAL( BW::Allocator::heapMemorySize( toHeap ),
		allocator.memorySize( toHeap ) );
	CHECK( memcmp( toHeap, pattern, sizeof( pattern ) ) == 0 );

	// heap -> larger heap: straight through the realloc hook
	void * bigger = allocator.reallocate( toHeap, 8192 );
	CHECK( bigger != NULL );
	CHECK_EQUAL( BW::Allocator::heapMemorySize( bigger ),
		allocator.memorySize( bigger ) );

	// heap -> pool: copies min( poolSize, size ) and frees the heap block
	void * backToPool = allocator.reallocate( bigger, 8 );
	CHECK( backToPool != NULL );
	CHECK_EQUAL( size_t( 8 ), allocator.memorySize( backToPool ) );
	CHECK( memcmp( backToPool, pattern, 8 ) == 0 );

	allocator.deallocate( backToPool );
	allocator.deallocate( p16again );
}


TEST( FixedSizedAllocator_autoFindPool_appendsSizes )
{
	// auto pools: unknown sizes within maxPoolSize register a new pool size
	BW::FixedSizedAllocator allocator(
		/* maxPoolSize */ 4096, /* poolSize */ 64, "batch21-append" );
	installHeapHooks( allocator );

	// registers size 32 (empty table: appends at slot 0); the two-item
	// pool has one item left after this allocation
	void * p32 = allocator.allocate( 32 );
	CHECK( p32 != NULL );
	CHECK( allocator.poolExistsForSize( 32 ) );
	CHECK( allocator.poolExistsForSize( 16 ) );
	CHECK_EQUAL( size_t( 64 / 32 - 1 ),
		allocator.getFirstPoolNumFreeForSize( 32 ) );

	// registers a larger size: appends at slot 1, sort-shift does not run
	// (already in increasing order). 64/48 == 1 item per pool.
	void * p48 = allocator.allocate( 48 );
	CHECK( p48 != NULL );
	CHECK( allocator.poolExistsForSize( 48 ) );
	CHECK( allocator.poolExistsForSize( 40 ) );
	CHECK( allocator.isLastPoolFullForSize( 48 ) );
	CHECK_EQUAL( size_t( 0 ),
		allocator.getFirstPoolNumFreeForSize( 48 ) );

	// beyond maxPoolSize: findPool returns -1 and the heap hook serves it
	void * big = allocator.allocate( 5000 );
	CHECK( big != NULL );
	CHECK_EQUAL( BW::Allocator::heapMemorySize( big ),
		allocator.memorySize( big ) );

	allocator.deallocate( big );
	allocator.deallocate( p48 );
	allocator.deallocate( p32 );
}


TEST( FixedSizedAllocator_autoPoolSpans_growAndCollapse )
{
	// poolSize 64 with 64-byte items: one item per pool, so every
	// allocation adds a pool span. The 65th span doubles the span array.
	BW::FixedSizedAllocator allocator(
		/* maxPoolSize */ 4096, /* poolSize */ 64, "batch21-spans" );
	installHeapHooks( allocator );

	void * ptrs[65];
	for (int i = 0; i < 65; ++i)
	{
		ptrs[i] = allocator.allocate( 64 );
		CHECK( ptrs[i] != NULL );
	}
	// the newest (head) pool is full after its single slot was taken
	CHECK_EQUAL( size_t( 0 ),
		allocator.getFirstPoolNumFreeForSize( 64 ) );

	// freeing every block empties each pool; the last one collapses the
	// span array back to null (poolSpanUsed_ == 0 arm)
	for (int i = 0; i < 65; ++i)
	{
		allocator.deallocate( ptrs[i] );
	}
	CHECK( allocator.isAllPoolsEmptyForSize( 64 ) );

	// a fresh allocation re-initialises the span array from scratch
	void * again = allocator.allocate( 64 );
	CHECK( again != NULL );
	CHECK_EQUAL( size_t( 64 ), allocator.memorySize( again ) );
	allocator.deallocate( again );
}


TEST( FixedSizedAllocator_destructor_freesOutstandingPools )
{
	// pools deliberately left allocated: the destructor walks every size
	// chain, frees the pool blocks and the span array
	{
		BW::FixedSizedAllocator allocator(
			kSizes, ARRAY_SIZE( kSizes ), kPoolSize,
			"batch21-dtor", false );
		installHeapHooks( allocator );

		void * a = allocator.allocate( 8 );
		void * b = allocator.allocate( 16 );
		void * c = allocator.allocate( 64 );
		CHECK( a != NULL );
		CHECK( b != NULL );
		CHECK( c != NULL );
		// no deallocate: destructor does the releasing
	}

	// if we got here the destructor completed without crashing
	CHECK( true );
}

BW_END_NAMESPACE

// test_fixed_sized_allocator.cpp
