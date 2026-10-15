#ifndef SIGNAL_SET_HPP
#define SIGNAL_SET_HPP

#include "cstdmf/debug.hpp"

#include <signal.h>

#if !defined( _WIN32 )

// POSIX only. Everything below is built on sigset_t and the sig* family,
// none of which the MSVC CRT provides: <signal.h> there only offers signal(),
// raise() and sig_atomic_t, so the sigset_t members were assumed to be int and
// every sig* call was an unknown identifier (MSVC C4430, C2065, C3861 in the
// root superproject build). This is the same POSIX-only-where-there-is-no-
// Windows-equivalent split dprintf.hpp uses for struct timeval.
//
// Not ported here: pthread_sigmask is what gives Blocker its meaning, and
// Windows has no per-thread signal mask to replace it, so a Windows Blocker
// would need a decision about what "blocking" should mean rather than a
// translation of the call. The only users are lib/server/signal_processor and
// lib/db_storage_mysql/table_synchroniser, which stay POSIX-only for now.

BW_BEGIN_NAMESPACE

namespace Signal
{

/**
 * 	This class is a thin wrapper around sigset_t. Has dodgy isClear() feature
 * 	because sigisemptyset() is non-standard function.
 */
class Set
{
public:
	enum InitState
	{
		EMPTY, FULL
	};

	Set( InitState state = EMPTY ) : isClear_( state == EMPTY )
	{
		if (state == EMPTY)
		{
			sigemptyset( &signals_ );
		}
		else
		{
			sigfillset( &signals_ );
		}
	}

	void clear( int signal )
	{
		MF_VERIFY( sigdelset( &signals_, signal ) == 0 );
		isClear_ = sigisemptyset( &signals_ );
	}

	void set( int signal )
	{
		MF_VERIFY( sigaddset( &signals_, signal ) == 0 );
		isClear_ = false;
	}
	void clearAll()
	{
		MF_VERIFY( sigemptyset( &signals_ ) == 0 );
		isClear_ = true;
	}

	bool isClear() const			{ return isClear_; }
	bool isSet( int signal ) const
	{
		return sigismember( &signals_, signal ) ? true : false;
	}

	operator const sigset_t*() const 	{ return &signals_; }

private:
	sigset_t	signals_;
	bool		isClear_;
};

/**
 * 	This class blocks and unblocks the specified signals when it is
 * 	created and destroyed.
 */
class Blocker
{
public:
	Blocker( const Set& blockSignals )
	{
		MF_VERIFY( pthread_sigmask( SIG_SETMASK, blockSignals, &oldSignals_ )
				== 0 );
	}
	~Blocker()
	{
		MF_VERIFY( pthread_sigmask( SIG_SETMASK, &oldSignals_, NULL ) == 0 );
	}

private:
	sigset_t	oldSignals_;
};

}	// end namespace Signal

BW_END_NAMESPACE

#else // defined( _WIN32 )

// Windows compile-compat Set: the POSIX section above needs sigset_t, so
// here Set keeps its interface over a plain bitmap instead. The signal
// numbers (including the POSIX-only ones lib/server/common.hpp defines for
// MSVC) are register-and-flag only - Windows never delivers them - and
// Blocker stays POSIX-only per the decision note above.

#include "server/common.hpp"

BW_BEGIN_NAMESPACE

namespace Signal
{

/**
 * 	This class keeps the POSIX Set's interface over a fixed bitmap. There is
 * 	no operator sigset_t* because there is no sigset_t to hand out.
 */
class Set
{
public:
	enum InitState
	{
		EMPTY, FULL
	};

	enum { NUM_SIGNALS = 32 };

	Set( InitState state = EMPTY )
	{
		this->clearAll();

		if (state == FULL)
		{
			for (int signal = 1; signal < NUM_SIGNALS; ++signal)
			{
				signals_[ signal ] = true;
			}

			isClear_ = false;
		}
	}

	void clear( int signal )
	{
		if (this->inRange( signal ))
		{
			signals_[ signal ] = false;
		}
	}

	void set( int signal )
	{
		if (this->inRange( signal ))
		{
			signals_[ signal ] = true;
			isClear_ = false;
		}
	}

	void clearAll()
	{
		for (int i = 0; i < NUM_SIGNALS; ++i)
		{
			signals_[ i ] = false;
		}

		isClear_ = true;
	}

	bool isClear() const			{ return isClear_; }
	bool isSet( int signal ) const
	{
		return this->inRange( signal ) && signals_[ signal ];
	}

private:
	static bool inRange( int signal )
	{
		return (1 <= signal) && (signal < NUM_SIGNALS);
	}

	bool	signals_[ NUM_SIGNALS ];
	bool	isClear_;
};

}	// end namespace Signal

BW_END_NAMESPACE

#endif // !defined( _WIN32 )

#endif // SIGNAL_SET_HPP
