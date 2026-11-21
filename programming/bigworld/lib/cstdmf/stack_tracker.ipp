/**
 *	@file
 */
 
// -----------------------------------------------------------------------------
// Section: StackTracker
// -----------------------------------------------------------------------------

inline void StackTracker::push(const char* name, const char*file, uint line, bool temp)
{
	assert( stackPos_ < THREAD_STACK_DEPTH );
	StackItem& stackPos = stack_[stackPos_];
	// Zero the whole item first: the raw bytes of an item are hashed and
	// compared wholesale by the memory-tracking callstack dedup, so padding
	// bytes must be deterministic.
	memset( &stackPos, 0, sizeof( stackPos ) );
	// The tracker owns these strings (the item stores copies), so the caller
	// may pass pointers into memory it releases straight after this call.
	// The temp flag only records where they came from.
	// Copy n-1 bytes at most and keep the last byte NUL: GCC flags a plain
	// strncpy( dst, src, n-1 ) as possible truncation (-Wstringop-truncation)
	// under -Werror even though the memset above guarantees termination.
	strncat( stackPos.name, (name != NULL) ? name : "",
		ARRAY_SIZE( stackPos.name ) - 1 );
	strncat( stackPos.file, (file != NULL) ? file : "",
		ARRAY_SIZE( stackPos.file ) - 1 );
	stackPos.line = line;
	stackPos.temp = temp;
	stackPos.annotation = 0;
	++stackPos_;
	
#ifdef _DEBUG
	if (stackPos_ > maxStackPos_)
	{
		maxStackPos_ = stackPos_;
	}
#endif
}

inline void StackTracker::pop()
{
	assert( stackPos_ > 0 );
	--stackPos_;
}

inline uint StackTracker::stackSize()
{
	return stackPos_;
}

// 0 == top of stack, stackSize-1 == bottom
inline StackTracker::StackItem& StackTracker::getStackItem(uint idx)
{
	assert( idx < stackPos_ );
	return stack_[ stackPos_-1 - idx ];
}
