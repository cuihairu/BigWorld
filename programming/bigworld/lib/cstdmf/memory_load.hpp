#ifndef __CSTDMF_MEMORY_LOAD_HPP__
#define __CSTDMF_MEMORY_LOAD_HPP__

#ifdef WIN32

// Self-sufficient windows include: stdmf.hpp does not pull <windows.h> and
// this header needs DWORDLONG. In the VS-project builds pch.hpp (which does
// include cstdmf_windows.hpp) always precedes it, but the root superproject
// turns PCH_SUPPORT off (BWConfiguration_server.cmake), pch.hpp compiles out
// empty, and memory_load.cpp then dies on every MemSize declaration
// (C4430/C2146 x121, run 38990281409).
#include "cstdmf_windows.hpp"

#include "stdmf.hpp"

BW_BEGIN_NAMESPACE

/**
 *	Runtime memory information
 */
namespace Memory
{
	typedef DWORDLONG MemSize;
	/**
	*	returns maximum available memory
	*/
	CSTDMF_DLL MemSize	maxAvailableMemory();
	/**
	*	returns how much memory is currently in use
	*/
	CSTDMF_DLL MemSize	usedMemory();
	/**
	*	returns current memory load [ 0; 100 ]
	*/
	CSTDMF_DLL float	memoryLoad();
	/**
	*	returns largest available memory block size
	*	expensive 
	*/
	CSTDMF_DLL MemSize	largestBlockSize();
	/**
	*	returns amount of available VA space
	*/
	CSTDMF_DLL MemSize	availableVA();

} // namespace Memory

BW_END_NAMESPACE

#endif // WIN32

#endif // __CSTDMF_MEMORY_LOAD_HPP__
