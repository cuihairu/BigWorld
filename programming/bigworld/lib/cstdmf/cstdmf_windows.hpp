#ifndef CSTDMF_WINDOWS_HPP
#define CSTDMF_WINDOWS_HPP

#if defined( _WIN32 ) && !defined( _XBOX ) && !defined( _XBOX360 )
// The root superproject already passes this on the command line
// (BWStandardProject.cmake), so the bare #define collides with the flag and
// every TU that reaches this header died on C4005 macro redefinition under
// /WX. Guarded instead of removed because the module-level MSVC gate
// compiles without the flag and relies on this define.
#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
#endif
	#include <windows.h>
#endif

#endif // CSTDMF_WINDOWS_HPP
