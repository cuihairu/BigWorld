#include "memhook.hpp"
#include "../cstdmf/config.hpp"
#include <new>

#ifndef MF_SERVER
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE
#endif

static BW::Memhook::AllocFuncs s_allocFuncs;
static bool s_initialised = false;

static void init()
{
	extern void memHookInitFunc();
	if (!s_initialised)
	{
		s_initialised = true;
		memHookInitFunc();
	}
}

void BW::Memhook::allocFuncs( const AllocFuncs& allocFuncs )
{
	s_allocFuncs = allocFuncs;
}

const BW::Memhook::AllocFuncs& BW::Memhook::allocFuncs()
{
	return s_allocFuncs;
}


NOINLINE
void * operator new( size_t sz )
{
	init();
	return (*s_allocFuncs.new_)( sz );
}


NOINLINE
void * operator new[]( size_t sz )
{
	init();
	return (*s_allocFuncs.newArray_)( sz );
}


NOINLINE
void * operator new( size_t sz, const std::nothrow_t & nt ) noexcept
{
	init();
	return (*s_allocFuncs.newNoThrow_)( sz, nt );
}


NOINLINE
void * operator new[]( size_t sz, const std::nothrow_t & nt ) noexcept
{
	init();
	return (*s_allocFuncs.newArrayNoThrow_)( sz, nt );
}

NOINLINE
void operator delete( void * p ) noexcept
{
	init();
	(*s_allocFuncs.delete_)( p );
}

NOINLINE
void operator delete( void * p, const std::nothrow_t & nt ) noexcept
{
	init();
	(*s_allocFuncs.deleteNoThrow_)( p, nt );
}

NOINLINE
void operator delete[]( void * p ) noexcept
{
	init();
	(*s_allocFuncs.deleteArray_)( p );
}

// BIGWORLD_BEGIN(3.13 migration)
// Sized deallocation overloads. gcc 15 makes -Wsized-deallocation an error
// (-Werror in this component) when an unsized operator delete is declared
// without its sized counterpart. The hook table has no sized variants, so
// these simply forward to the unsized hooks, preserving the 2.7-era
// behaviour where sized and unsized deletes both funnel into the same
// allocator.
NOINLINE
void operator delete( void * p, std::size_t /*sz*/ ) noexcept
{
	init();
	(*s_allocFuncs.delete_)( p );
}

NOINLINE
void operator delete[]( void * p, std::size_t /*sz*/ ) noexcept
{
	init();
	(*s_allocFuncs.deleteArray_)( p );
}
// BIGWORLD_END

NOINLINE
void operator delete[]( void * p, const std::nothrow_t & nt ) noexcept
{
	init();
	(*s_allocFuncs.deleteArrayNoThrow_)( p, nt );
}

#if BW_STATIC_LINKING
extern "C" void* MEMHOOK_CDECL malloc( size_t size )
{
	init();
	return (*s_allocFuncs.malloc_)( size );
}

extern "C" void* MEMHOOK_CDECL _calloc_impl( size_t count, size_t size, int*  )
{
	init();
	size_t len = count * size;
	void * p = (*s_allocFuncs.malloc_)( len );
	char * t = (char*)p;
	for (size_t i = 0; i < len; ++i)
		*t++ = 0;
	return p;
}

extern "C" void MEMHOOK_CDECL free( void * p )
{
	init();
	(*s_allocFuncs.free_)( p );
}

extern "C" void* MEMHOOK_CDECL realloc( void * p, size_t size )
{
	return (*s_allocFuncs.realloc_)( p, size );
}

extern "C" size_t MEMHOOK_CDECL _msize (void * pblock)
{
	return (*s_allocFuncs.msizeFunc_)( pblock );
}

extern "C" void* MEMHOOK_CDECL _aligned_malloc( size_t size, size_t alignment )
{
	return (*s_allocFuncs.alignedMalloc_)( size, alignment );
}

extern "C" void MEMHOOK_CDECL _aligned_free( void * p )
{
	(*s_allocFuncs.alignedFree_)( p );
}

#endif // BW_STATIC_LINKING
