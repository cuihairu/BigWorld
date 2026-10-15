#ifndef LIB__DBAPP_MYSQL__TABLE_SYNCHRONISER_HPP
#define LIB__DBAPP_MYSQL__TABLE_SYNCHRONISER_HPP

#include "cstdmf/bw_namespace.hpp"

// pid_t; MSVC has no <unistd.h> (C1083 in the root superproject build) but
// its CRT headers provide pid_t.
#if !defined( _WIN32 )
#include <unistd.h>
#endif // !defined( _WIN32 )

#include "cstdmf/bw_string.hpp"


BW_BEGIN_NAMESPACE

namespace Mercury
{
	class EventDispatcher;
} // end namespace Mercury


/**
 *	Class to run sync_db.
 */
class TableSynchroniser
{
public:
	/**
	 *	Constructor.
	 */
	TableSynchroniser():
		pid_( -1 ),
		running_( false ),
		status_( 0 )
	{}

	~TableSynchroniser();

	bool run( Mercury::EventDispatcher & dispatcher );

	void onProcessExited( int status );

	void abort();

	bool isFinished() const
		{ return pid_ != -1 && running_ == false; }

	bool wasSuccessful() const
	{ 
		return this->didExit() && 
			this->exitCode() == 0; 
	}

	bool didExit() const;
	int exitCode() const;

	bool wasSignalled() const;
	int signal() const;

private:
	void execSyncDB( const BW::string & syncdbPath );

	pid_t pid_;

	bool running_;
	int status_;
};

BW_END_NAMESPACE

#endif // LIB__DBAPP_MYSQL__TABLE_SYNCHRONISER_HPP
