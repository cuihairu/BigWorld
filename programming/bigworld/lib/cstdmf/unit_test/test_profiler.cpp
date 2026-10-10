#include "pch.hpp"

#include "cstdmf/profiler.hpp"
#include "cstdmf/watcher.hpp"
#include "cstdmf/concurrency.hpp"

#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

BW_BEGIN_NAMESPACE

// -----------------------------------------------------------------------------
// Section: batch 23 helpers
// -----------------------------------------------------------------------------
// Coverage batch 23 for cstdmf/profiler.cpp. The whole file drives the global
// g_profiler exactly once through init()/fini(): init has no idempotence guard
// (re-init would re-register the profiler/* watchers and leak buffers), so the
// first test inits with a small 64KB/8-thread setup and the LAST test calls
// fini(). This TU is appended at the end of Makefile.rules cxxSource, and
// TestRegistry keeps insertion order, so every test here runs after all other
// cstdmf tests and nothing else touches the profiler watchers after fini()
// (fini does not unregister them).
// -----------------------------------------------------------------------------

namespace
{
// Every processed frame must carry >= 2 entries (processEntries discards a
// whole frame when entryCount <= 1 via the currentFrameIdentifier == -1 quirk).
// After the first getStatistics() call the BW_GUARD_PROFILER inside it keeps
// two entries flowing per frame anyway; this helper is for explicit frames.
void addScopedPair( const char* name )
{
	g_profiler.addEntry( name, Profiler::EVENT_START, 0 );
	g_profiler.addEntry( name, Profiler::EVENT_END, 0 );
}

// getStatistics renders the current view into ProfileLines; tests assert on
// the joined text. Note: the BW_GUARD_PROFILER(getStatistics) inside adds
// entries to the *fill* buffer, they are processed by the next tick.
BW::string statsToText()
{
	BW::vector<Profiler::ProfileLine> lines;
	g_profiler.getStatistics( &lines );

	BW::string joined;
	for (BW::vector<Profiler::ProfileLine>::size_type i = 0;
			i < lines.size(); ++i)
	{
		joined += lines[ i ].text_;
	}

	return joined;
}

bool contains( const BW::string& haystack, const char* needle )
{
	return haystack.find( needle ) != BW::string::npos;
}

// Position of needle in haystack (BW::string::npos if absent); used for
// relative line-order assertions.
size_t indexOf( const BW::string& haystack, const char* needle )
{
	return haystack.find( needle );
}

// Colour of the first hierarchical line whose text contains needle (0 if none).
uint32 colourOfLineContaining( const BW::vector<Profiler::ProfileLine>& lines,
	const char* needle )
{
	for (BW::vector<Profiler::ProfileLine>::size_type i = 0;
			i < lines.size(); ++i)
	{
		if (lines[ i ].text_.find( needle ) != BW::string::npos)
		{
			return lines[ i ].colour_;
		}
	}

	return 0;
}

struct Batch23WorkerArgs
{
	bwThreadID tid_;
};

// Runs on a SimpleThread: registers itself like a real game thread would, then
// records a burst of entries. tls_ThreadIndex_ starts at -1 on foreign
// threads, so addThisThread() must run before any addEntry from this thread.
void batch23WorkerFunc( void* arg )
{
	Batch23WorkerArgs* args = static_cast< Batch23WorkerArgs* >( arg );

	g_profiler.addThisThread( "Batch23Worker" );

	for (int i = 0; i < 4000; ++i)
	{
		g_profiler.addEntry( "Batch23WorkerEntry", Profiler::EVENT_START, 0 );
		g_profiler.addEntry( "Batch23WorkerEntry", Profiler::EVENT_END, 0 );
	}

	args->tid_ = OurThreadID();
}

// Buffer sizing replicated from Profiler::init so the display-entry overflow
// test can hit the arm exactly without risking the raw-entry MF_ASSERT.
const int BATCH23_MEMORY_SIZE = 64 * 1024;

int batch23MaxDisplayEntries()
{
	const int num = int( BATCH23_MEMORY_SIZE /
		(sizeof( Profiler::ProfileEntry ) * 2 +
			sizeof( Profiler::DisplayEntry ) / 2) );
	return num / 2;
}

// Leaves a json dump file open with dumping disarmed so the next processed
// frame takes the "no dump requested but a file is live" stopJsonDump arm.
// Only callable while forceJsonDump is on: the two processing ticks here run
// as background tasks once force is off.
void setForceJsonDumpOffWithOpenFile()
{
	g_profiler.setForceJsonDump( false );   // restores the parked dump count
	g_profiler.setJsonDumpCount( 2 );
	Watcher::rootWatcher().setFromString( NULL,
		"profiler/dumpProfile", "true" );   // jsonDumpIndex_ = 0, doJsonDump_
	g_profiler.tick();                      // opens the stream (ACTIVE)
	Watcher::rootWatcher().setFromString( NULL,
		"profiler/dumpProfile", "false" );  // disarm while file still open
	g_profiler.tick();                      // takes the stopJsonDump arm
}

} // anonymous namespace


// -----------------------------------------------------------------------------
// Section: tests
// -----------------------------------------------------------------------------

/// init + the full watcher surface registered by init.
TEST( Profiler_lifecycle_init_watchers )
{
	// Pre-init: threads_ is NULL until init, addThisThread must early-out.
	g_profiler.addThisThread( "Batch23PreInit" );

	const int maxThreads = 8;
	CHECK( g_profiler.init( BATCH23_MEMORY_SIZE, maxThreads ) );

	CHECK_EQUAL( BW::string( "MainThread" ),
		BW::string( g_profiler.getThreadName( 0 ) ) );
	CHECK( g_profiler.getThreadVisibility( 0 ) );

	// watcher resolution through the root.
	CHECK( getWatcher( "profiler/enabled" ) != NULL );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	// enabled: inactive until the first tick after an enable.
	CHECK( Watcher::rootWatcher().getAsString( NULL, "profiler/enabled",
		result, desc, mode ) );
	CHECK_EQUAL( BW::string( "false" ), result );
	CHECK( Watcher::rootWatcher().setFromString( NULL,
		"profiler/enabled", "true" ) );
	CHECK( Watcher::rootWatcher().getAsString( NULL, "profiler/enabled",
		result, desc, mode ) );
	CHECK_EQUAL( BW::string( "true" ), result );

	// dumpFrameCount / dumpFrameIndex (RW int / RO int).
	CHECK( Watcher::rootWatcher().getAsString( NULL,
		"profiler/dumpFrameCount", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "1" ), result );
	CHECK( Watcher::rootWatcher().setFromString( NULL,
		"profiler/dumpFrameCount", "5" ) );
	CHECK( Watcher::rootWatcher().getAsString( NULL,
		"profiler/dumpFrameCount", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "5" ), result );
	CHECK( Watcher::rootWatcher().setFromString( NULL,
		"profiler/dumpFrameCount", "1" ) );

	CHECK( !Watcher::rootWatcher().setFromString( NULL,
		"profiler/dumpFrameIndex", "9" ) );

	// dumpProfile: setter resets the index and drives doJsonDump_.
	CHECK( Watcher::rootWatcher().setFromString( NULL,
		"profiler/dumpProfile", "true" ) );
	CHECK( Watcher::rootWatcher().getAsString( NULL, "profiler/dumpProfile",
		result, desc, mode ) );
	CHECK_EQUAL( BW::string( "true" ), result );
	CHECK( Watcher::rootWatcher().setFromString( NULL,
		"profiler/dumpProfile", "false" ) );

	// sortMode: init defaults to SORT_BY_TIME; unknown names silently no-op.
	CHECK( Watcher::rootWatcher().getAsString( NULL, "profiler/sortMode",
		result, desc, mode ) );
	CHECK_EQUAL( BW::string( "SORT_BY_TIME" ), result );
	CHECK( Watcher::rootWatcher().setFromString( NULL, "profiler/sortMode",
		"SORT_BY_NUMCALLS" ) );
	CHECK( Watcher::rootWatcher().getAsString( NULL, "profiler/sortMode",
		result, desc, mode ) );
	CHECK_EQUAL( BW::string( "SORT_BY_NUMCALLS" ), result );
	CHECK( Watcher::rootWatcher().setFromString( NULL, "profiler/sortMode",
		"BOGUS_SORT_MODE" ) );
	CHECK( Watcher::rootWatcher().getAsString( NULL, "profiler/sortMode",
		result, desc, mode ) );
	CHECK_EQUAL( BW::string( "SORT_BY_NUMCALLS" ), result );

	// exclusive: RW bool.
	CHECK( Watcher::rootWatcher().getAsString( NULL, "profiler/exclusive",
		result, desc, mode ) );
	CHECK_EQUAL( BW::string( "false" ), result );
	CHECK( Watcher::rootWatcher().setFromString( NULL,
		"profiler/exclusive", "true" ) );
	CHECK( Watcher::rootWatcher().getAsString( NULL, "profiler/exclusive",
		result, desc, mode ) );
	CHECK_EQUAL( BW::string( "true" ), result );
	CHECK( Watcher::rootWatcher().setFromString( NULL,
		"profiler/exclusive", "false" ) );

	// dumpState: RO int of JsonDumpState (INACTIVE == 0).
	CHECK( Watcher::rootWatcher().getAsString( NULL, "profiler/dumpState",
		result, desc, mode ) );
	CHECK_EQUAL( BW::string( "0" ), result );

	// dumpFilePath: SafeWatcher over a read-only string, empty until a dump.
	CHECK( Watcher::rootWatcher().getAsString( NULL,
		"profiler/dumpFilePath", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "" ), result );
	CHECK( !Watcher::rootWatcher().setFromString( NULL,
		"profiler/dumpFilePath", "/somewhere/evil" ) );

	// categories / currentCategory.
	CHECK( Watcher::rootWatcher().getAsString( NULL,
		"profiler/controls/categories", result, desc, mode ) );
	// MF_SERVER build: no GPU category, map key order gives C++ < Python < All.
	CHECK_EQUAL( BW::string( "C++;Python;All" ), result );
	CHECK( !Watcher::rootWatcher().setFromString( NULL,
		"profiler/controls/categories", "Nope" ) );
	CHECK( Watcher::rootWatcher().getAsString( NULL,
		"profiler/controls/currentCategory", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "All" ), result );

	// statistics: ProfileLinesWatcher sequence; visitChildren refreshes the
	// lines via getStatistics. Index children are 2-digit strings.
	CHECK( Watcher::rootWatcher().getAsString( NULL,
		"profiler/statistics/00", result, desc, mode ) );
	CHECK( contains( result, "Profile data" ) );
	CHECK( Watcher::rootWatcher().getAsString( NULL,
		"profiler/statistics/02", result, desc, mode ) );
	CHECK( contains( result, "Inclusive" ) );
	// The bare directory path is not readable as a string.
	CHECK( !Watcher::rootWatcher().getAsString( NULL,
		"profiler/statistics", result, desc, mode ) );
}


/// Scope/counter entry macros and the synchronous (forceJsonDump) processing
/// path, verified through getStatistics' flat view.
TEST( Profiler_basic_entries_statistics )
{
	// Dump files must land outside the repo; default dir is the executable
	// directory (the game/ tree) which we must not pollute.
	mkdir( "/tmp/pf_batch23", 0755 );
	g_profiler.setJsonDumpDir( "/tmp/pf_batch23" );

	// forceJsonDump makes tick() process entries synchronously -> every
	// assertion below is deterministic (no background-task race). Side
	// effect: jsonDumpCount_ is parked at 15 and each processed frame
	// writes/truncates a dump file under /tmp/pf_batch23.
	g_profiler.setForceJsonDump( true );

	// First tick after enable: arming only (entryCount == 0), no processing.
	g_profiler.tick();
	CHECK_EQUAL( 0, g_profiler.getFrameNumber() );

	{
		PROFILER_SCOPED( Batch23Root );
		{
			PROFILER_SCOPED( Batch23Leaf );
		}
		PROFILER_COUNTER( Batch23Counter, 42 );

		{
			PROFILER_IDLE_SCOPED( Batch23Idle );
		}
		{
			// FILE_PROFILE_ENABLED is defined, so this is a ScopedProfiler.
			PROFILE_FILE_SCOPED( Batch23File );
		}
		{
			// Dynamic strings are interned through the profiler's string set.
			ScopedDynamicStringProfiler dynamicScoped( "Batch23Dynamic" );
		}
		{
			PROFILER_LEVEL_COUNTER( Batch23Level );
			PROFILER_LEVEL_COUNTER_INC( Batch23Level, 3 );
			PROFILER_LEVEL_COUNTER_DEC( Batch23Level, 1 );
		}

		// A non-default category entry; visible while filtering on "All".
		g_profiler.addEntry( "Batch23PythonScoped", Profiler::EVENT_START,
			0, Profiler::CATEGORY_PYTHON );
		g_profiler.addEntry( "Batch23PythonScoped", Profiler::EVENT_END,
			0, Profiler::CATEGORY_PYTHON );
	}

	g_profiler.tick();
	CHECK_EQUAL( 1, g_profiler.getFrameNumber() );

	BW::string stats = statsToText();
	// Mode header was left at SORT_BY_NUMCALLS by the lifecycle test.
	CHECK( contains( stats, "Profile data - " ) );
	CHECK( contains( stats, "Sorted By Number of Calls" ) );
	CHECK( contains( stats, "(Inclusive):" ) );
	CHECK( contains( stats, "Current Filter: All" ) );
	CHECK( contains( stats, "0: MainThread" ) );
	CHECK( contains( stats, "Batch23Root" ) );
	CHECK( contains( stats, "Batch23Leaf" ) );
	CHECK( contains( stats, "Batch23Counter" ) );
	CHECK( contains( stats, "Batch23Idle" ) );
	CHECK( contains( stats, "Batch23Dynamic" ) );
	CHECK( contains( stats, "Batch23PythonScoped" ) );
	// Footer block.
	CHECK( contains( stats, "Time spent profiling" ) );
	CHECK( contains( stats, "profile entries" ) );
	CHECK( contains( stats, "display entries" ) );
}


/// Mode switching: drives all four live sort comparators, the CPU_GPU thread
/// visibility store/restore arms and the "Invalid Sort!" default arm.
TEST( Profiler_sort_modes )
{
	// Entries for the TIME-mode ordering assertion: Beta must cost more than
	// Alpha, and Alpha is called three times.
	{
		for (int i = 0; i < 3; ++i)
		{
			PROFILER_SCOPED( Batch23Alpha );
		}
		{
			PROFILER_SCOPED( Batch23Beta );
			volatile int spinner = 0;
			for (int i = 0; i < 2000; ++i)
			{
				spinner += i;
			}
		}
	}

	g_profiler.setProfileMode( Profiler::SORT_BY_TIME, false );
	g_profiler.tick();

	BW::string stats = statsToText();
	CHECK( contains( stats, "Sorted By Time" ) );
	CHECK( contains( stats, "Batch23Alpha" ) );
	CHECK( contains( stats, "Batch23Beta" ) );
	// SortByCostliest: the costlier Beta is listed first.
	CHECK( indexOf( stats, "Batch23Beta" ) < indexOf( stats, "Batch23Alpha" ) );

	g_profiler.setProfileMode( Profiler::SORT_BY_NUMCALLS, false );
	g_profiler.tick();
	CHECK( contains( statsToText(), "Sorted By Number of Calls" ) );

	// SORT_BY_NAME via the watcher (SortByName comparator).
	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;
	CHECK( Watcher::rootWatcher().setFromString( NULL, "profiler/sortMode",
		"SORT_BY_NAME" ) );
	CHECK( Watcher::rootWatcher().getAsString( NULL, "profiler/sortMode",
		result, desc, mode ) );
	CHECK_EQUAL( BW::string( "SORT_BY_NAME" ), result );
	g_profiler.tick();
	stats = statsToText();
	CHECK( contains( stats, "Sorted By Name" ) );
	// Alphabetical: Alpha line precedes Beta line.
	CHECK( indexOf( stats, "Batch23Alpha" ) < indexOf( stats, "Batch23Beta" ) );

	// CPU_GPU: stores thread visibility (keeping only MainThread/GPU visible),
	// then restores it on the way out.
	CHECK( g_profiler.getThreadVisibility( 0 ) );
	g_profiler.setProfileMode( Profiler::CPU_GPU, false );
	CHECK( g_profiler.getThreadVisibility( 0 ) );
	g_profiler.tick();
	stats = statsToText();
	CHECK( contains( stats, "CPU and GPU" ) );
	// watcherGetSortMode has no CPU_GPU string -> empty.
	CHECK( Watcher::rootWatcher().getAsString( NULL, "profiler/sortMode",
		result, desc, mode ) );
	CHECK_EQUAL( BW::string( "" ), result );
	g_profiler.setProfileMode( Profiler::SORT_BY_TIME, false );
	CHECK( g_profiler.getThreadVisibility( 0 ) );

	// GRAPHS falls into the getStatistics default arm.
	g_profiler.setProfileMode( Profiler::GRAPHS, false );
	g_profiler.tick();
	CHECK( contains( statsToText(), "Invalid Sort!" ) );
	CHECK( Watcher::rootWatcher().getAsString( NULL, "profiler/sortMode",
		result, desc, mode ) );
	CHECK_EQUAL( BW::string( "" ), result );

	// Restore via the watcher.
	CHECK( Watcher::rootWatcher().setFromString( NULL, "profiler/sortMode",
		"SORT_BY_TIME" ) );
	CHECK( Watcher::rootWatcher().getAsString( NULL, "profiler/sortMode",
		result, desc, mode ) );
	CHECK_EQUAL( BW::string( "SORT_BY_TIME" ), result );

	// Drive the controls watcher entries (next/prev entry, toggle graph) —
	// their setters forward to the hierarchy navigation calls.
	CHECK( Watcher::rootWatcher().setFromString( NULL,
		"profiler/controls/nextEntry", "true" ) );
	CHECK( Watcher::rootWatcher().setFromString( NULL,
		"profiler/controls/prevEntry", "true" ) );
	CHECK( Watcher::rootWatcher().setFromString( NULL,
		"profiler/controls/toggleEntry", "true" ) );

	// The dummy getters on the controls entries always read false.
	CHECK( Watcher::rootWatcher().getAsString( NULL,
		"profiler/controls/nextEntry", result, desc, mode ) );
	CHECK_EQUAL( BW::string( "false" ), result );
}


/// Category cycling (wrap arms), set-by-name (hit + miss) and the frame-level
/// category filter applied in processEntries.
TEST( Profiler_categories )
{
	CHECK_EQUAL( BW::string( "C++;Python;All" ), g_profiler.getCategories() );
	CHECK_EQUAL( BW::string( "All" ), g_profiler.getCurrentCategory() );

	// nextCategory wraps at the end (All -> C++), then walks on.
	g_profiler.nextCategory();
	CHECK_EQUAL( BW::string( "C++" ), g_profiler.getCurrentCategory() );
	g_profiler.nextCategory();
	CHECK_EQUAL( BW::string( "Python" ), g_profiler.getCurrentCategory() );
	g_profiler.nextCategory();
	CHECK_EQUAL( BW::string( "All" ), g_profiler.getCurrentCategory() );

	// prevCategory wraps at the beginning (All -> Python), then walks back.
	g_profiler.prevCategory();
	CHECK_EQUAL( BW::string( "Python" ), g_profiler.getCurrentCategory() );
	g_profiler.prevCategory();
	CHECK_EQUAL( BW::string( "C++" ), g_profiler.getCurrentCategory() );
	g_profiler.prevCategory();
	CHECK_EQUAL( BW::string( "All" ), g_profiler.getCurrentCategory() );

	// Filter to Python: the C++ entries of this frame must be dropped by the
	// finalEventCategory_ mask check in processEntries.
	g_profiler.setCurrentCategory( "Python" );
	CHECK_EQUAL( BW::string( "Python" ), g_profiler.getCurrentCategory() );
	addScopedPair( "Batch23CppFiltered" );
	g_profiler.tick();

	BW::string stats = statsToText();
	CHECK( !contains( stats, "0: MainThread" ) );
	CHECK( !contains( stats, "Batch23CppFiltered" ) );

	// Unknown category: warning, selection unchanged.
	g_profiler.setCurrentCategory( "Batch23NoSuchCategory" );
	CHECK_EQUAL( BW::string( "Python" ), g_profiler.getCurrentCategory() );

	// Drive the category controls watchers for their coverage.
	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;
	CHECK( Watcher::rootWatcher().setFromString( NULL,
		"profiler/controls/nextCategory", "true" ) );
	CHECK( Watcher::rootWatcher().setFromString( NULL,
		"profiler/controls/prevCategory", "true" ) );
	CHECK( Watcher::rootWatcher().getAsString( NULL,
		"profiler/controls/currentCategory", result, desc, mode ) );

	// Back to All: entries flow again (the guard-profiler entries from the
	// stats calls above provide this frame's data).
	g_profiler.setCurrentCategory( "All" );
	CHECK_EQUAL( BW::string( "All" ), g_profiler.getCurrentCategory() );
	g_profiler.tick();
	stats = statsToText();
	CHECK( contains( stats, "0: MainThread" ) );
}


/// HIERARCHICAL view: stats branch, navigation (toggle-then-move), graph
/// toggling with symmetric close, colour flags, controls watchers.
TEST( Profiler_hierarchical )
{
	g_profiler.setProfileMode( Profiler::HIERARCHICAL, true );
	CHECK_EQUAL( BW::string( "HIERARCHICAL" ), g_profiler.watcherGetSortMode() );

	{
		PROFILER_SCOPED( Batch23HRoot );
		{
			PROFILER_SCOPED( Batch23HLeaf );
		}
	}
	g_profiler.tick();

	BW::vector<Profiler::ProfileLine> lines;
	g_profiler.getStatistics( &lines );
	BW::string stats;
	for (size_t i = 0; i < lines.size(); ++i)
	{
		stats += lines[ i ].text_;
	}

	CHECK( contains( stats, "Hierarchical Profile data" ) );
	CHECK( contains( stats, "(Inclusive)" ) );
	// MF_SERVER marks the current entry with '*' (root is current here).
	CHECK( contains( stats, "*All Threads" ) );
	CHECK( contains( stats, "MainThread(1)" ) );

	// Nothing is visible yet: next() from the root has nowhere to go.
	CHECK_EQUAL( 0, g_profiler.nextHierEntry() );

	// Expose the thread level, descend.
	g_profiler.toggleHierChildren();
	CHECK_EQUAL( 1, g_profiler.nextHierEntry() );
	// currentHierEntry_ is now the MainThread entry -> '*' moved.
	g_profiler.getStatistics( &lines );
	CHECK( (colourOfLineContaining( lines, "MainThread" ) &
		HierEntry::HIGHLIGHTED) != 0 );

	// Expose the function level, descend to it.
	g_profiler.toggleHierChildren();
	CHECK_EQUAL( 1, g_profiler.nextHierEntry() );
	// Leaf is still hidden: no further descent.
	CHECK_EQUAL( 0, g_profiler.nextHierEntry() );

	// Walk back up: HRoot -> MainThread -> root, then stop at the root.
	CHECK_EQUAL( 1, g_profiler.prevHierEntry() );
	CHECK_EQUAL( 1, g_profiler.prevHierEntry() );
	CHECK_EQUAL( 0, g_profiler.prevHierEntry() );

	// Graph the current entry (root): allocates its time array. Recursive
	// toggle only reaches *visible* children, the leaf stays hidden.
	g_profiler.toggleGraph( true );

	// Another hierarchical frame refreshes the hierarchy and records the
	// graphed entry.
	addScopedPair( "Batch23HRoot" );
	g_profiler.tick();
	CHECK( !g_profiler.getHierGraphs().empty() );

	g_profiler.getStatistics( &lines );
	CHECK( (colourOfLineContaining( lines, "All Threads" ) &
		HierEntry::GRAPHED) != 0 );

	// Symmetric close: HierarchyManager frees its pool without calling the
	// HierEntry destructors, so a live timeArray_ here would leak past fini()
	// and trip the crash-on-leak check.
	g_profiler.toggleGraph( false );

	// Leave the mode; later categories/hierarchy resets reuse pool entries.
	g_profiler.setProfileMode( Profiler::SORT_BY_TIME, false );
}


/// Display-entry buffer overflow arm (deliberately unbalanced frame), the
/// MAX_STACK_DEPTH arms on both the START and END sides, the depth-0 END arm
/// and the end-of-frame clamp for unclosed entries.
TEST( Profiler_display_overflow_depth )
{
	// Replicate init()'s sizing so the overflow lands inside the raw-entry
	// budget: 63 nested unclosed STARTs + (maxDisplay + 1 - 63) balanced
	// sibling pairs costs num - 61 raw entries and fills the display buffer
	// exactly once past its limit.
	const int maxDisplayEntries = batch23MaxDisplayEntries();
	const int nested = 63;
	const int pairs = maxDisplayEntries + 1 - nested;

	for (int i = 0; i < nested; ++i)
	{
		g_profiler.addEntry( "Batch23Over", Profiler::EVENT_START, 0 );
	}

	for (int i = 0; i < pairs; ++i)
	{
		g_profiler.addEntry( "Batch23Over", Profiler::EVENT_START, 0 );
		g_profiler.addEntry( "Batch23Over", Profiler::EVENT_END, 0 );
	}

	g_profiler.tick();

	// The overflow arm sets the sticky error string; getErrorString reads it.
	CHECK( contains( g_profiler.getErrorString(), "Too many display entries" ) );

	// START-side depth arm: 64 pushes fill the stack, the rest are counted
	// and skipped. The ENDs unwind: 7 hit the END-side depth arm, 3 pop.
	for (int i = 0; i < 70; ++i)
	{
		g_profiler.addEntry( "Batch23Deep", Profiler::EVENT_START, 0 );
	}
	for (int i = 0; i < 10; ++i)
	{
		g_profiler.addEntry( "Batch23Deep", Profiler::EVENT_END, 0 );
	}
	// Remainder is clamped to the end-of-frame timestamp (unclosed arm).
	g_profiler.tick();

	// END with an empty stack is skipped (depth-0 arm).
	g_profiler.addEntry( "Batch23Orphan", Profiler::EVENT_END, 0 );
	{
		PROFILER_SCOPED( Batch23Tail );
	}
	g_profiler.tick();

	BW::string stats = statsToText();
	CHECK( contains( stats, "0: MainThread" ) );
	CHECK( contains( stats, "Batch23Tail" ) );
}


/// JSON dump: streaming with the close-old-file arm, the completed single
/// dump, and the watcher-driven stop arm. Under forceJsonDump the jsonDump
/// machinery runs on EVERY processed frame, so jsonDumpIndex_ must be reset
/// to 0 before each scenario - watcherSetDumpProfile does exactly that
/// (jsonDumpIndex_ = 0; doJsonDump_ = value).
TEST( Profiler_json_dump_success )
{
	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;

	// Two-frame stream, count = 2: frame 1 opens the file and goes ACTIVE.
	g_profiler.setJsonDumpCount( 2 );
	CHECK( Watcher::rootWatcher().setFromString( NULL,
		"profiler/dumpProfile", "true" ) );
	g_profiler.tick();
	CHECK_EQUAL( Profiler::JSON_DUMPING_ACTIVE, g_profiler.getJsonDumpState() );
	CHECK( Watcher::rootWatcher().getAsString( NULL, "profiler/dumpState",
		result, desc, mode ) );
	CHECK_EQUAL( BW::string( "1" ), result );

	// Re-arm while the file is still open: the index reset to 0 makes the
	// next jsonDump take the close-old-file arm before opening a new one.
	CHECK( Watcher::rootWatcher().setFromString( NULL,
		"profiler/dumpProfile", "true" ) );
	g_profiler.tick();
	CHECK_EQUAL( Profiler::JSON_DUMPING_ACTIVE, g_profiler.getJsonDumpState() );

	// Frame 2 reaches index == count: stopJsonDump closes the stream.
	{
		PROFILER_SCOPED( Batch23J );
		PROFILER_COUNTER( Batch23JC, 7 );
	}
	g_profiler.tick();
	CHECK_EQUAL( Profiler::JSON_DUMPING_COMPLETED,
		g_profiler.getJsonDumpState() );

	// Single-frame dump: index 1 == count 1 opens, writes and closes.
	g_profiler.setJsonDumpCount( 1 );
	CHECK( Watcher::rootWatcher().setFromString( NULL,
		"profiler/dumpProfile", "true" ) );
	{
		PROFILER_SCOPED( Batch23J );
	}
	g_profiler.tick();
	CHECK_EQUAL( Profiler::JSON_DUMPING_COMPLETED,
		g_profiler.getJsonDumpState() );
	CHECK( Watcher::rootWatcher().getAsString( NULL, "profiler/dumpState",
		result, desc, mode ) );
	CHECK_EQUAL( BW::string( "2" ), result );

	// The path is published both through the getter and the SafeWatcher.
	BW::string outputPath = g_profiler.getJsonOutputFilePath();
	CHECK( contains( outputPath, "/tmp/pf_batch23" ) );
	CHECK( contains( outputPath, ".json" ) );
	CHECK( Watcher::rootWatcher().getAsString( NULL,
		"profiler/dumpFilePath", result, desc, mode ) );
	CHECK_EQUAL( outputPath, result );

	// File content: chrome-trace envelope with the recorded entry names.
	FILE* dumpFile = fopen( outputPath.c_str(), "r" );
	CHECK( dumpFile != NULL );
	if (dumpFile)
	{
		char buffer[ 16384 ];
		size_t got = fread( buffer, 1, sizeof( buffer ) - 1, dumpFile );
		buffer[ got ] = '\0';
		fclose( dumpFile );

		BW::string content( buffer );
		CHECK( contains( content, "[" ) );
		CHECK( contains( content, "]" ) );
		CHECK( contains( content, "thread_name" ) );
		CHECK( contains( content, "MainThread" ) );
		CHECK( contains( content, "Batch23J" ) );
		CHECK( contains( content, "\"ph\":\"C\"" ) );
	}

	// Finally the unforced stop arm: with forceJsonDump off and doJsonDump_
	// false, a still-open file is closed by the next processed frame (which
	// now runs as a background task - hence the wait before asserting).
	setForceJsonDumpOffWithOpenFile();
	usleep( 200 * 1000 );
	CHECK_EQUAL( Profiler::JSON_DUMPING_COMPLETED,
		g_profiler.getJsonDumpState() );

	// Back to synchronous processing for the remaining tests.
	g_profiler.setForceJsonDump( true );
}


/// JSON dump failure: an unusable dump directory drives createNewJsonDumpFile
/// to NULL and the FAILED state.
TEST( Profiler_json_dump_failure )
{
	g_profiler.setJsonDumpDir( "/nonexistent_batch23_dir/subdir" );
	g_profiler.setJsonDumpCount( 1 );
	// Re-arm through the watcher: resets jsonDumpIndex_ to 0 so this frame's
	// jsonDump deterministically attempts a fresh file.
	CHECK( Watcher::rootWatcher().setFromString( NULL,
		"profiler/dumpProfile", "true" ) );
	{
		PROFILER_SCOPED( Batch23JFail );
	}
	g_profiler.tick();

	CHECK_EQUAL( Profiler::JSON_DUMPING_FAILED,
		g_profiler.getJsonDumpState() );

	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;
	CHECK( Watcher::rootWatcher().getAsString( NULL, "profiler/dumpState",
		result, desc, mode ) );
	CHECK_EQUAL( BW::string( "3" ), result );

	// Last successful path is still published by the getter.
	CHECK( contains( g_profiler.getJsonOutputFilePath(), "/tmp/pf_batch23" ) );

	// Restore for any later dump.
	g_profiler.setJsonDumpDir( "/tmp/pf_batch23" );
}


/// CSV history: open (thread-name substring lookup), the setProfileMode
/// force-to-SORT_BY_NAME arm, the getStatistics CSV guard, the output hook,
/// close-on-demand and the open-failure arm.
TEST( Profiler_csv_output )
{
	BW::string msg;
	CHECK( g_profiler.setNewHistory( "/tmp/pf_batch23/out.csv", "Main",
		&msg ) );
	CHECK( contains( msg, "MainThread" ) );
	CHECK( contains( msg, "Opened file" ) );

	// Profiler::setNewHistory forces SORT_BY_NAME.
	BW::string result;
	BW::string desc;
	Watcher::Mode mode = Watcher::WT_INVALID;
	CHECK( Watcher::rootWatcher().getAsString( NULL, "profiler/sortMode",
		result, desc, mode ) );
	CHECK_EQUAL( BW::string( "SORT_BY_NAME" ), result );

	// While CSV runs, setProfileMode is forced back (exclusive times + name
	// sort + enabled), whatever was requested.
	g_profiler.setProfileMode( Profiler::SORT_BY_TIME, false );
	CHECK( Watcher::rootWatcher().getAsString( NULL, "profiler/sortMode",
		result, desc, mode ) );
	CHECK_EQUAL( BW::string( "SORT_BY_NAME" ), result );

	// Statistics are withheld while CSV is running: the view is replaced by
	// a single guard line (the footer block below it is unconditional).
	{
		BW::vector<Profiler::ProfileLine> lines;
		g_profiler.getStatistics( &lines );
		CHECK( lines.size() >= 1 );
		if (lines.size() >= 1)
		{
			CHECK( contains( lines[ 0 ].text_,
				"Profile data not available while profiling to CSV file" ) );
			CHECK( !contains( lines[ 0 ].text_, "Sorted" ) );
		}
	}

	// Processed frames feed addProfileData through the outputReport hook.
	{
		PROFILER_SCOPED( Batch23Csv );
	}
	g_profiler.tick();
	{
		PROFILER_SCOPED( Batch23Csv );
	}
	g_profiler.tick();

	// Close flushes the history through the background task, then closes.
	g_profiler.closeHistory();

	FILE* csvFile = fopen( "/tmp/pf_batch23/out.csv", "r" );
	CHECK( csvFile != NULL );
	if (csvFile)
	{
		char buffer[ 8192 ];
		size_t got = fread( buffer, 1, sizeof( buffer ) - 1, csvFile );
		buffer[ got ] = '\0';
		fclose( csvFile );

		BW::string content( buffer );
		CHECK( contains( content, "Batch23" ) );
	}

	// Closing again with no file is a no-op.
	g_profiler.closeHistory();

	// An unopenable history file fails gracefully.
	BW::string failMsg;
	CHECK( !g_profiler.setNewHistory( "/proc/batch23_no_such_dir/x.csv",
		"Main", &failMsg ) );
	CHECK( contains( failMsg, "Unable to open file" ) );

	// Back to the flat time-sorted view for the remaining tests.
	g_profiler.setProfileMode( Profiler::SORT_BY_TIME, false );
}


/// Thread registration, duplicate/erase arms, visibility, delayed vs immediate
/// removal and a real second thread recording entries.
TEST( Profiler_threads_visibility )
{
	const uint mainThreadId = OurThreadID();

	// Duplicate registration of MainThread: re-tls, un-marks any pending
	// removal, warns and returns.
	g_profiler.addThisThread( "MainThread" );
	CHECK_EQUAL( BW::string( "MainThread" ),
		BW::string( g_profiler.getThreadName( 0 ) ) );

	// Mark MainThread for delayed removal, then cancel that by re-adding:
	// the following processed frame must keep it.
	g_profiler.removeThread( mainThreadId );
	g_profiler.addThisThread( "MainThread" );
	addScopedPair( "Batch23ThreadProbe" );
	g_profiler.tick();
	CHECK_EQUAL( BW::string( "MainThread" ),
		BW::string( g_profiler.getThreadName( 0 ) ) );

	// Visibility toggles remove a thread section from the statistics.
	g_profiler.setThreadVisibility( 0, false );
	CHECK( !g_profiler.getThreadVisibility( 0 ) );
	g_profiler.tick();
	CHECK( !contains( statsToText(), "0: MainThread" ) );
	g_profiler.setThreadVisibility( 0, true );
	CHECK( g_profiler.getThreadVisibility( 0 ) );

	// A real worker thread: registers itself, records entries, exits.
	Batch23WorkerArgs workerArgs;
	workerArgs.tid_ = 0;
	SimpleThread* worker = new SimpleThread( &batch23WorkerFunc,
		&workerArgs, "Batch23WorkerThread" );
	usleep( 300 * 1000 );
	bw_safe_delete( worker ); // joins

	CHECK( workerArgs.tid_ != 0 );
	// Slot 0 is MainThread and the profiler's own background thread ("Profiler
	// (n)") registers itself on startThreads, so locate the worker's slot by
	// name instead of assuming an index.
	int workerSlot = -1;
	for (int i = 1; i < 8; ++i)
	{
		const char* slotName = g_profiler.getThreadName( i );
		fprintf( stderr, "BATCH23SLOT %d: %s\n", i, slotName ? slotName : "(null)" );
		if (slotName && BW::string( slotName ) == "Batch23Worker")
		{
			workerSlot = i;
			break;
		}
	}
	fprintf( stderr, "BATCH23TID %u\n", (unsigned)workerArgs.tid_ );
	CHECK( workerSlot > 0 );

	// One more frame so the worker's entries reach the statistics.
	g_profiler.tick();
	BW::string stats = statsToText();
	CHECK( contains( stats, ": Batch23Worker" ) );

	// Delayed removal: the id is parked until the next processed frame.
	g_profiler.removeThread( workerArgs.tid_ );
	{
		PROFILER_SCOPED( Batch23ThreadProbe );
	}
	g_profiler.tick();
	// getThreadName MF_ASSERTs on negative indexes - only safe to call with a
	// valid slot.
	if (workerSlot > 0)
	{
		CHECK( g_profiler.getThreadName( workerSlot ) == NULL );
	}

	// Immediate removal arm (profiler is active here, so use the no-match
	// path via a bogus id; internalRemoveThread returns without change).
	// Delayed-vs-immediate switch itself is exercised by enable(false) below.
	g_profiler.removeThread( 0xf0000001u );
	CHECK_EQUAL( BW::string( "MainThread" ),
		BW::string( g_profiler.getThreadName( 0 ) ) );

	// With the profiler disabled, removal is immediate.
	g_profiler.enable( false );
	g_profiler.removeThread( 0xf0000002u );
	g_profiler.enable( true );
	CHECK_EQUAL( BW::string( "MainThread" ),
		BW::string( g_profiler.getThreadName( 0 ) ) );
}


/// freeze()/hitchFreeze() gating: frozen frames skip display building and
/// frame counting but still service the json dump machinery.
TEST( Profiler_freeze_hitch )
{
	int frameBefore = g_profiler.getFrameNumber();

	// freeze(true, false): no dump flag is raised by this call.
	g_profiler.freeze( true, false );
	{
		PROFILER_SCOPED( Batch23Frozen );
	}
	g_profiler.tick();
	CHECK_EQUAL( frameBefore, g_profiler.getFrameNumber() );

	// Unfreeze: frames advance again (a processed frame needs fresh entries).
	g_profiler.freeze( false, false );
	{
		PROFILER_SCOPED( Batch23Unfrozen );
	}
	g_profiler.tick();
	CHECK_EQUAL( frameBefore + 1, g_profiler.getFrameNumber() );

	// freeze(true, true) raises doJsonDump_ for the frozen frame.
	g_profiler.freeze( true, true );
	{
		PROFILER_SCOPED( Batch23Frozen );
	}
	g_profiler.tick();
	CHECK_EQUAL( frameBefore + 1, g_profiler.getFrameNumber() );
	g_profiler.freeze( false, false );
	{
		PROFILER_SCOPED( Batch23Unfrozen );
	}
	g_profiler.tick();
	CHECK_EQUAL( frameBefore + 2, g_profiler.getFrameNumber() );

	// hitchFreeze drives freeze() with dump = (hitch != previous && hitch):
	// false->true raises a dump, true->false clears without one.
	CHECK( !g_profiler.hitchFreeze() );
	g_profiler.allowHitchFreezing( true );
	CHECK( g_profiler.allowHitchFreezing() );
	g_profiler.hitchFreeze( true );
	CHECK( g_profiler.hitchFreeze() );
	g_profiler.hitchFreeze( false );
	CHECK( !g_profiler.hitchFreeze() );
	g_profiler.allowHitchFreezing( false );
	CHECK( !g_profiler.allowHitchFreezing() );

	{
		PROFILER_SCOPED( Batch23Unfrozen );
	}
	g_profiler.tick();
	CHECK_EQUAL( frameBefore + 3, g_profiler.getFrameNumber() );
}


/// ProfileLine stream operators.
TEST( Profiler_profileline_streams )
{
	Profiler::ProfileLine line( 0x00ff00ff, "Batch23LineText" );

	std::ostringstream oss;
	oss << line;
	CHECK( oss.str().find( "Batch23LineText" ) != std::string::npos );

	Profiler::ProfileLine parsed( "placeholder" );
	std::istringstream iss( "Batch23ParsedText" );
	iss >> parsed;
	CHECK( parsed.text_.find( "Batch23ParsedText" ) != BW::string::npos );
}


/// Must stay the LAST profiler test: fini() frees the buffers and clears
/// threads_ but does not unregister the profiler/* watchers, so nothing may
/// touch those paths afterwards. fini() must run exactly once (the init
/// semaphore balance breaks on a second pull).
TEST( Profiler_fini_guards_last )
{
	// Stop the forced synchronous processing and restore the dump count.
	g_profiler.setForceJsonDump( false );

	// CSV was closed by the CSV test; hierarchy graphs were toggled off by
	// the hierarchical test; json dumps all completed or failed cleanly.
	g_profiler.fini();

	// Post-fini guards: threads_ is NULL, both calls early-out safely.
	// (getThreadName and the profiler/* watchers are NOT safe after fini.)
	g_profiler.addThisThread( "Batch23PostFini" );
	g_profiler.removeThread( 0xf0000003u );
}


BW_END_NAMESPACE

// test_profiler.cpp
