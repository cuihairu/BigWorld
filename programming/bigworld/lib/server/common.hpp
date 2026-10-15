#ifndef COMMON_HPP
#define COMMON_HPP

#include "cstdmf/bwversion.hpp"
#include "network/basictypes.hpp"

#if defined( _WIN32 )
// Compile-compat only: the tree's server code (CellApp::onSignalled,
// DBAppMgr::onSignalled, table_synchroniser, the SignalProcessor name
// table) references the POSIX signal family, while MSVC's <signal.h> only
// knows seven of them. These keep their POSIX Linux values so the name
// table in signal_processor.cpp stays shared. None of these numbers is
// ever DELIVERED by Windows - registering one with signal() fails with
// SIG_ERR - so handlers registered against them are inert until a real
// delivery mechanism is designed for Windows.
#include <signal.h>
#define SIGHUP 1
#define SIGQUIT 3
#define SIGTRAP 5
#define SIGKILL 9
#define SIGUSR1 10
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGSTKFLT 16
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGURG 23
#define SIGXCPU 24
#define SIGXFSZ 25
#define SIGVTALRM 26
#define SIGPROF 27
#define SIGWINCH 28
#define SIGIO 29
#define SIGPWR 30
#define SIGSYS 31
#endif // defined( _WIN32 )


BW_BEGIN_NAMESPACE

// -----------------------------------------------------------------------------
// Section: Default values
// -----------------------------------------------------------------------------

/// Should we enable content updating (will updater be run)?
const bool DEFAULT_UPDATER_ENABLED = false;

/// This is the frequency at which the game runs. Packets are sent to the client
/// at this rate.
const long DEFAULT_GAME_UPDATE_HERTZ = 10;

/// This is the bandwidth to clients in bytes per second.
const long DEFAULT_BANDWIDTH_TO_CLIENT = 256;

/// This is the default amount of CPU load that is attempted to be offloaded
/// each balancing iteration.
const float DEFAULT_MAX_CPU_OFFLOAD = 0.02f;

enum ShutDownStage
{
	// This means 'not shutting down' and is what shutdown state variables are
	// typically initialised to.
	SHUTDOWN_NONE,

	// This means please start your phase of a controlled shutdown (which may
	// involve requesting other processes to shutdown)
	SHUTDOWN_REQUEST,

	// This is used to inform a parent process that is waiting for you to shut
	// down about the stage of the shutdown you're up to.
	SHUTDOWN_INFORM,

	// This is part of the BaseApp shutdown sequence
	SHUTDOWN_DISCONNECT_PROXIES,

	// This means please shut down this process (i.e. don't propagate anything
	// to dependent processes, just shut down now now NOW!).
	SHUTDOWN_PERFORM,

	// This is only understood by the BaseAppMgr and means please trigger a
	// controlled shutdown from the most 'senior' process that currently exists,
	// i.e. try the LoginApp first, then the DBApp, then the BaseAppMgr.
	SHUTDOWN_TRIGGER
};


/**
 *	Used with updating the logon record for a persistent entity.
 */
enum UpdateAutoLoad
{
	/** Don't modify the auto load state. */
	UPDATE_AUTO_LOAD_RETAIN,
	/** Set the auto-load state to true. */
	UPDATE_AUTO_LOAD_TRUE,
	/** Set the auto-load state to false. */
	UPDATE_AUTO_LOAD_FALSE
};

BW_END_NAMESPACE

#endif // COMMON_HPP
