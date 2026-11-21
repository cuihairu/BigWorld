/**
 *	@internal
 *	@file
 */

#ifndef STACK_TRACKER__HPP
#define STACK_TRACKER__HPP

#include "cstdmf/config.hpp"

#if ENABLE_STACK_TRACKER

#include "cstdmf/bw_string.hpp"
#include "cstdmf/bw_util.hpp"
#include "cstdmf/concurrency.hpp"
#include "cstdmf/debug.hpp"
#include "cstdmf/stdmf.hpp"

#include <string.h>

// Using a C assert here, because MF_ASSERT calls things that can cause another
// push/pop to happen, causing this to assert again, ad infinitum.
#include <cassert>

BW_BEGIN_NAMESPACE

extern bool g_copyStackInfo;

// Declare g_copyStackInfo when cstdmf is built as a DLL
#define DECLARE_COPY_STACK_INFO( isPlugin ) \
	bool g_copyStackInfo = (isPlugin);

class StackTracker
{
private:
	enum
	{
		THREAD_STACK_DEPTH	= 1024,		// The maximum stack size for a thread
		ANNOTATION_BUFFER_SIZE = 4096,	// The size of the stack annotation buffer
		MAX_ANNOTATION_STACK_ENTRIES = 256
	};

public:

	enum
	{
		/// Fixed storage for an item's name and file. The tracker owns these
		/// strings: a caller may pass pointers into memory it releases right
		/// after the push (a Python frame's code object is DECREF'd as soon
		/// as the push returns).
		STRING_SIZE = 128
	};

	struct StackItem
	{
		char			name[ STRING_SIZE ];	// function name
		char			file[ STRING_SIZE ];	// file name
		uint			line:31;	// line number
		bool			temp:1;		// flags if the name and file came from
									// temporary memory
		const char*		annotation;	// This is always a temp string and must be copied
	};

	inline static void push( const char * name, const char * file=NULL, uint line=0 )
	{
		push( name, file, line, g_copyStackInfo );
	}
	CSTDMF_DLL static void push( const char * name, const char * file, uint line, bool temp );
	CSTDMF_DLL static void pop();
	CSTDMF_DLL static uint stackSize();

	// 0 == top of stack, stackSize-1 == bottom
	CSTDMF_DLL static StackItem& getStackItem(uint idx);

	CSTDMF_DLL static void buildReport(char* buffer, size_t len);

#if defined( _DEBUG )
	static uint getMaxStackPos();
#endif

	static void pushAnnotation( const char* annotation );
	static void popAnnotation();

private:
	static THREADLOCAL(StackItem)	stack_[THREAD_STACK_DEPTH];
	static THREADLOCAL(uint)		stackPos_;
#if defined( _DEBUG )
	static THREADLOCAL(uint)		maxStackPos_;
#endif
	static THREADLOCAL(char*)		nextAnnotationPtr_;
	static THREADLOCAL(char)		annotationBuffer_[ANNOTATION_BUFFER_SIZE];

	struct AnnotationEntry
	{
		char*		annotation;
		uint32		stackPos;
	};
	static THREADLOCAL(AnnotationEntry*)	nextAnnotationEntryPtr_;
	static THREADLOCAL(AnnotationEntry)		annotationEntries_[MAX_ANNOTATION_STACK_ENTRIES];
	static THREADLOCAL(uint32)				numAnnotationsToSkipPop_;
};

class ScopedStackTrack
{
public:
	inline ScopedStackTrack( const char * name, const char * file = NULL,
		uint line = 0 )
	{
		StackTracker::push( name, file, line );
	}

	inline ~ScopedStackTrack()
	{
		StackTracker::pop();
	}
};

class ScopedStackTrackAnnotation
{
public:
	inline ScopedStackTrackAnnotation( const char * annotation )
	{
		StackTracker::pushAnnotation( annotation );
	}

	inline ~ScopedStackTrackAnnotation()
	{
		StackTracker::popAnnotation();
	}
};

#include "stack_tracker.ipp"

BW_END_NAMESPACE

#else
#define DECLARE_COPY_STACK_INFO( isPlugin)
#endif // #if ENABLE_STACK_TRACKER

#endif
