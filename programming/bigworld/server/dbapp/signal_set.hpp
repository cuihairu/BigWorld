#ifndef SIGNAL_SET_HPP
#define SIGNAL_SET_HPP

#include "cstdmf/debug.hpp"

#include <signal.h>

#if !defined( _WIN32 )

// POSIX only, same as lib/server/signal_set.hpp, which this file mirrors: the
// sigset_t members and sig* calls need POSIX, and the MSVC CRT's <signal.h>
// offers neither (MSVC C4430, C2065, C3861 in the root superproject build).

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

	void set( int signal )
	{
		MF_VERIFY( sigaddset( &signals_, signal ) == 0 );
		isClear_ = false;
	}
	void clear()
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

#endif // !defined( _WIN32 )

#endif // SIGNAL_SET_HPP
