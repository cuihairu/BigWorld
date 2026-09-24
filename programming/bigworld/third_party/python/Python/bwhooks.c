/* Bigworld hooks */

#include "Python.h"
#include "bwhooks.h"

static BW_Py_Hooks s_hooks = { 0 };

/* BIGWORLD_BEGIN
 * BW_Py_calloc is new in the Python 3.13 port: PyMem_SetAllocator's
 * PyMemAllocatorEx requires a calloc entry point which the 2.7 hook
 * interface did not have. Zero-argument products of zero are avoided the
 * same way malloc/realloc clamp their size to 1. */
void* BW_Py_calloc( size_t nelem, size_t elsize )
{
	if (s_hooks.mallocHook == NULL)
	{
		return calloc( nelem, elsize );
	}

	// Route through the malloc hook to keep allocations observable.
	size_t total = nelem * elsize;
	if (total == 0)
	{
		total = 1;
	}

	void * mem = s_hooks.mallocHook( total );
	if (mem != NULL)
	{
		memset( mem, 0, total );
	}

	return mem;
}
/* BIGWORLD_END */

void* BW_Py_malloc( size_t size )
{
	// Enforce a size of at least 1.
	// Original python allocations did this so we must
	//provide the same interface. (see pymem.h).
	// Without this, free's sometimes complain of freeing
	// memory from an alternate heap.
	if (size == 0)
	{
		size = 1;
	}

	if (s_hooks.mallocHook)
	{
		return s_hooks.mallocHook( size );
	}
	else
	{
		return malloc( size );
	}
}


void BW_Py_free( void * mem )
{
	if (s_hooks.freeHook)
	{
		s_hooks.freeHook( mem );
	}
	else
	{
		free( mem );
	}
}


void* BW_Py_realloc( void * mem, size_t size )
{
	// Enforce a size of at least 1.
	// Original python allocations did this so we must
	//provide the same interface. (see pymem.h).
	// Without this, free's sometimes complain of freeing
	// memory from an alternate heap.
	if (size == 0)
		size = 1;

	if (s_hooks.reallocHook)
	{
		return s_hooks.reallocHook( mem, size );
	}
	else
	{
		return realloc( mem, size );
	}
}


void BW_Py_memoryTrackingIgnoreBegin( void )
{
	if (s_hooks.ignoreAllocsBeginHook)
	{
		s_hooks.ignoreAllocsBeginHook();
	}
}


void BW_Py_memoryTrackingIgnoreEnd( void )
{
	if (s_hooks.ignoreAllocsEndHook)
	{
		s_hooks.ignoreAllocsEndHook();
	}
}


void BW_Py_setHooks( BW_Py_Hooks * hooks )
{
	if (hooks)
	{
		s_hooks = *hooks;
	}
	else
	{
		memset( &s_hooks, 0, sizeof( BW_Py_Hooks ) );
	}
}


void BW_Py_getHooks( BW_Py_Hooks * hooks )
{
	*hooks = s_hooks;
}

// bwhooks.c
