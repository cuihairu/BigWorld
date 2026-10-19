#include "pch.hpp"

#include "cstdmf/config.hpp"
#include "cstdmf/callstack.hpp"
#include "cstdmf/debug.hpp"
#include "cstdmf/log_msg.hpp"

// Pins for the ENABLE_* feature macros in cstdmf/config.hpp.
//
// config.hpp evaluates every defined() predicate in a real #if line and
// keeps the macro bodies themselves free of defined(): the MSVC
// traditional preprocessor mis-evaluates defined() that appears through
// macro expansion (it read MF_SERVER as undefined inside the old
// ENABLE_RELOAD_MODEL body, pulling client moo headers into every server
// TU on the Windows root build; see probe #12, run 38852284641). These
// asserts pin the server-build contract on every compiler.

#if defined( MF_SERVER )
// Model reloading is a client feature: FORCE_ENABLE_RELOAD_MODEL cannot
// turn it on in server builds.
static_assert( ENABLE_RELOAD_MODEL == 0,
	"ENABLE_RELOAD_MODEL must be 0 in server builds" );

// Hitch detection rides the client profiler and is compiled out on server.
static_assert( ENABLE_HITCH_DETECTION == 0,
	"ENABLE_HITCH_DETECTION must be 0 in server builds" );

// Pool statistics are client-only diagnostics.
static_assert( ENABLE_FIXED_SIZED_POOL_STATISTICS == 0,
	"ENABLE_FIXED_SIZED_POOL_STATISTICS must be 0 in server builds" );

#if !defined( ENABLE_MEMTRACKER ) && !defined( ENABLE_PROTECTED_ALLOCATOR )
// With no tracking forced on, a server build compiles its allocators
// without debug instrumentation.
static_assert( ENABLE_MEMORY_DEBUG == 0,
	"ENABLE_MEMORY_DEBUG must be 0 in plain server builds" );
static_assert( ENABLE_ALLOCATOR_STATISTICS == 0,
	"ENABLE_ALLOCATOR_STATISTICS must be 0 in plain server builds" );
#endif
#endif // defined( MF_SERVER )

// Derived flags must stay consistent with their inputs.
static_assert( ENABLE_SLOT_TRACKER ==
		(ENABLE_MEMORY_DEBUG || ENABLE_ALLOCATOR_STATISTICS),
	"ENABLE_SLOT_TRACKER must mirror its inputs" );
static_assert( ENABLE_SMARTPOINTER_TRACKING ==
		(ENABLE_MEMORY_DEBUG && FORCE_ENABLE_SMARTPOINTER_TRACKING),
	"ENABLE_SMARTPOINTER_TRACKING must mirror its inputs" );

// The rest of cstdmf followed config.hpp's lead: their defined() predicates
// moved out of the macro bodies into real #if lines, so each is now a
// plain 0/1. Being usable as a C++ expression IS the contract - a
// reintroduced defined() in a body fails to compile on GCC and silently
// mis-evaluates on MSVC's traditional preprocessor.
static_assert( BW_ENABLE_STACKTRACE == 0 || BW_ENABLE_STACKTRACE == 1,
	"BW_ENABLE_STACKTRACE must be a plain 0/1 value" );
static_assert( BW_BUILD_SUPPORTS_DEV_ASSERT == 0 ||
		BW_BUILD_SUPPORTS_DEV_ASSERT == 1,
	"BW_BUILD_SUPPORTS_DEV_ASSERT must be a plain 0/1 value" );
static_assert( BW_UNIX_BACKTRACE == 0 || BW_UNIX_BACKTRACE == 1,
	"BW_UNIX_BACKTRACE must be a plain 0/1 value" );

#if defined( unix ) && !defined( __ANDROID__ )
static_assert( BW_UNIX_BACKTRACE == 1,
	"BW_UNIX_BACKTRACE must be on for unix builds" );
#endif

#if defined( MF_SERVER )
// Server builds keep MF_ASSERT_DEV: it is the production-environment
// assertion the server links against.
static_assert( BW_BUILD_SUPPORTS_DEV_ASSERT == 1,
	"MF_SERVER builds must support MF_ASSERT_DEV" );
#endif

#if !BW_ENABLE_STACKTRACE && !defined( BWENTITY_DLL )
// Callstack tracing rides the debug allocators; without them there is
// nothing to trace.
static_assert( ENABLE_MEMORY_DEBUG == 0,
	"BW_ENABLE_STACKTRACE is off only without memory debug" );
#endif

BW_BEGIN_NAMESPACE

TEST( Config_valuesPinned )
{
	// Runtime mirror of the compile-time pins above so the values surface
	// in the test output on every platform.
#if defined( MF_SERVER )
	CHECK_EQUAL( 0, ENABLE_RELOAD_MODEL );
	CHECK_EQUAL( 0, ENABLE_HITCH_DETECTION );
	CHECK_EQUAL( 0, ENABLE_FIXED_SIZED_POOL_STATISTICS );
	CHECK_EQUAL( 1, BW_BUILD_SUPPORTS_DEV_ASSERT );
#endif
	CHECK( (ENABLE_SLOT_TRACKER != 0) ==
		((ENABLE_MEMORY_DEBUG != 0) || (ENABLE_ALLOCATOR_STATISTICS != 0)) );
	CHECK( BW_ENABLE_STACKTRACE == 0 || BW_ENABLE_STACKTRACE == 1 );
	CHECK( BW_BUILD_SUPPORTS_DEV_ASSERT == 0 ||
		BW_BUILD_SUPPORTS_DEV_ASSERT == 1 );
	CHECK( BW_UNIX_BACKTRACE == 0 || BW_UNIX_BACKTRACE == 1 );
}

BW_END_NAMESPACE

// test_config_values.cpp
