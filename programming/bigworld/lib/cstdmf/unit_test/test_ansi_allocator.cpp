#include "pch.hpp"

#include "cstdmf/ansi_allocator.hpp"
#include "cstdmf/string_builder.hpp"

#include <string.h>


BW_BEGIN_NAMESPACE

// AnsiAllocator is a thin, stateless wrapper over the C runtime
// allocator, so every guarantee it offers is one the platform already
// makes: non-NULL blocks, contents preserved across a realloc, real
// alignment, and a size report that is a lower bound only.

// allocate()/memorySize()/deallocate(): a usable block, and a size
// report that never promises less than was asked for.
TEST( AnsiAllocator_allocateAndDeallocate )
{
	void * pBlock = AnsiAllocator::allocate( 64 );
	CHECK( pBlock != NULL );
	// memorySize() is the platform's usable size, which rounds the
	// request up - hence a lower bound, not an equality.
	CHECK( AnsiAllocator::memorySize( pBlock ) >= size_t( 64 ) );

	memset( pBlock, 0xAB, 64 );
	AnsiAllocator::deallocate( pBlock );
}


// reallocate() moves the block and keeps the old contents, and the new
// block is reported at its new size.
TEST( AnsiAllocator_reallocateKeepsContents )
{
	char * pBlock = (char *)AnsiAllocator::allocate( 8 );
	CHECK( pBlock != NULL );
	memcpy( pBlock, "abcdefg", 8 );

	char * pGrown = (char *)AnsiAllocator::reallocate( pBlock, 256 );
	CHECK( pGrown != NULL );
	CHECK( memcmp( pGrown, "abcdefg", 8 ) == 0 );
	CHECK( AnsiAllocator::memorySize( pGrown ) >= size_t( 256 ) );

	// Shrinking keeps the data too.
	char * pShrunk = (char *)AnsiAllocator::reallocate( pGrown, 8 );
	CHECK( pShrunk != NULL );
	CHECK( memcmp( pShrunk, "abcdefg", 8 ) == 0 );
	AnsiAllocator::deallocate( pShrunk );
}


// allocateAligned() honours the requested alignment for every power of
// two a caller would realistically ask for, with a size that is not a
// multiple of the alignment, and deallocateAligned() takes it back.
TEST( AnsiAllocator_alignedAllocation )
{
	for ( size_t alignment = sizeof( void* ); alignment <= 64;
		alignment *= 2 )
	{
		char * pBlock =
			(char *)AnsiAllocator::allocateAligned( 100, alignment );
		CHECK( pBlock != NULL );
		CHECK_EQUAL( 0u, (unsigned)( reinterpret_cast< size_t >( pBlock ) %
			alignment ) );
		CHECK( AnsiAllocator::memorySize( pBlock ) >= size_t( 100 ) );

		memset( pBlock, 0x5A, 100 );
		AnsiAllocator::deallocateAligned( pBlock );
	}

	// An alignment that is not a multiple of sizeof( void* ) is
	// rejected by the platform, and the wrapper reports that as a NULL
	// block rather than handing back a misaligned one.
	CHECK( AnsiAllocator::allocateAligned( 16, 3 ) == NULL );
}


// debugReport() and onThreadFinish() are deliberate no-ops: the first
// must leave the builder untouched, the second must be safe to call at
// any point in the process lifetime.
TEST( AnsiAllocator_noOpHooks )
{
	StringBuilder builder( 64 );
	builder.append( "not a report" );

	AnsiAllocator::debugReport( builder );
	CHECK_EQUAL( size_t( strlen( "not a report" ) ), builder.length() );

	AnsiAllocator::onThreadFinish();
}

BW_END_NAMESPACE

// test_ansi_allocator.cpp
