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
	// Bounded copy with explicit truncation: both strncpy and strncat trip
	// GCC's -Wstringop-truncation under -Werror when the source can actually
	// exceed the buffer (142-byte names vs 128). The memset above zeroed the
	// item and guarantees termination, so copy at most size-1 bytes + NUL.
	{
		const size_t nameLen = (name != NULL) ? strlen( name ) : 0;
		const size_t fileLen = (file != NULL) ? strlen( file ) : 0;
		const size_t nameCap = ARRAY_SIZE( stackPos.name ) - 1;
		const size_t fileCap = ARRAY_SIZE( stackPos.file ) - 1;
		memcpy( stackPos.name, (name != NULL) ? name : "",
			nameLen < nameCap ? nameLen : nameCap );
		memcpy( stackPos.file, (file != NULL) ? file : "",
			fileLen < fileCap ? fileLen : fileCap );
	}
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
