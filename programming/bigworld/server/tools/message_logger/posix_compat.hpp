#ifndef MESSAGE_LOGGER_POSIX_COMPAT_HPP
#define MESSAGE_LOGGER_POSIX_COMPAT_HPP

// Windows shims for the POSIX surface this tool uses: directory enumeration
// (opendir/readdir/closedir/scandir), the file-removal and directory calls,
// the reentrant time functions and C getline. On POSIX the header is empty
// and the real system headers are included directly, so the tool keeps its
// existing POSIX include lines untouched.
//
// Everything lives at global scope, exactly where <dirent.h> and friends
// would declare it: user_segment.hpp exposes struct dirent to every TU in
// the tool the same way its <dirent.h> include does on POSIX.
#if defined( _WIN32 )

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <sys/stat.h>

#include <direct.h>	// _mkdir
#include <io.h>		// _unlink, _rmdir

// Lean windows.h first, same order as endpoint.hpp; Winsock2 supplies
// struct timeval for the gettimeofday shim below.
#include "cstdmf/cstdmf_windows.hpp"
#include <Winsock2.h>
#include <ws2tcpip.h>	// inet_pton, for the inet_aton shim below

#include <regex>	// std::regex, for the <regex.h> shims below

typedef intptr_t ssize_t;

// Windows struct stat carries only the file-type bits in st_mode, so the
// POSIX permission macros have no meaningful mapping. The tool's checks are
// "is this a directory" and "can I read/write it", which on Windows map to
// the always-granted defaults _S_IREAD/_S_IWRITE.
#ifndef S_ISDIR
	#define S_ISDIR( m ) ( ((m) & _S_IFMT) == _S_IFDIR )
#endif
#define S_IROTH _S_IREAD
#define S_IRWXU ( _S_IREAD | _S_IWRITE | _S_IEXEC )

// st_uid is 0 in Windows struct stat, so the tool's ownership check against
// the effective uid always passes.
inline int geteuid( void )
{
	return 0;
}

// Winsock has no inet_aton. inet_pton performs the same dotted-decimal
// parse with the same 0-on-failure contract the tool's address validation
// uses (hostnames.cpp's handleLine); normalised to exactly 1/0 because
// inet_pton also returns -1 for a bad address family.
inline int inet_aton( const char * cp, struct in_addr * pin )
{
	return inet_pton( AF_INET, cp, pin ) == 1;
}

struct dirent
{
	char d_name[ MAX_PATH + 1 ];
};

struct DIR
{
	HANDLE hFind;
	WIN32_FIND_DATAA data;
	struct dirent entry;
	int first;
};

inline DIR * opendir( const char * path )
{
	// FindFirstFile takes a pattern, not a directory: enumerate everything
	// in it. The trailing "\\*" form also rejects the root of empty drives
	// with GetLastError()==ERROR_ACCESS_DENIED, which matches opendir's
	// ENOENT-on-failure contract closely enough for this tool.
	char pattern[ MAX_PATH + 8 ];
	if (_snprintf( pattern, sizeof( pattern ), "%s\\*", path ) < 0)
	{
		return NULL;
	}

	DIR * dir = (DIR *)malloc( sizeof( DIR ) );
	if (dir == NULL)
	{
		return NULL;
	}

	dir->hFind = FindFirstFileA( pattern, &dir->data );
	if (dir->hFind == INVALID_HANDLE_VALUE)
	{
		free( dir );
		return NULL;
	}

	dir->first = 1;
	return dir;
}

inline struct dirent * readdir( DIR * dir )
{
	if (dir == NULL)
	{
		return NULL;
	}

	// The first entry is already sitting in dir->data from opendir's
	// FindFirstFile call; every later one needs FindNextFile.
	if (!dir->first && !FindNextFileA( dir->hFind, &dir->data ))
	{
		return NULL;
	}
	dir->first = 0;

	strncpy( dir->entry.d_name, dir->data.cFileName,
		sizeof( dir->entry.d_name ) - 1 );
	dir->entry.d_name[ sizeof( dir->entry.d_name ) - 1 ] = '\0';
	return &dir->entry;
}

inline int closedir( DIR * dir )
{
	if (dir == NULL)
	{
		return -1;
	}

	FindClose( dir->hFind );
	free( dir );
	return 0;
}

inline int scandir( const char * path, struct dirent *** namelist,
		int ( *filter )( const struct dirent * ),
		int ( *compar )( const struct dirent **, const struct dirent ** ) )
{
	// The comparator argument is carried for the signature only: every
	// caller in this tool sorts its results by segment start time instead
	// (see UserLogReader::loadSegments).
	DIR * dir = opendir( path );
	if (dir == NULL)
	{
		return -1;
	}

	struct dirent ** list = NULL;
	int count = 0;
	int capacity = 0;

	struct dirent * ent = NULL;
	while ((ent = readdir( dir )) != NULL)
	{
		if ((filter != NULL) && !filter( ent ))
		{
			continue;
		}

		if (count == capacity)
		{
			int newCapacity = (capacity == 0) ? 8 : capacity * 2;
			struct dirent ** newList = (struct dirent **)realloc( list,
				newCapacity * sizeof( struct dirent * ) );
			if (newList == NULL)
			{
				break;
			}

			list = newList;
			capacity = newCapacity;
		}

		struct dirent * copy = (struct dirent *)malloc(
			sizeof( struct dirent ) );
		if (copy == NULL)
		{
			break;
		}

		*copy = *ent;
		list[ count++ ] = copy;
	}

	closedir( dir );

	*namelist = list;
	return count;
}

inline int unlink( const char * path )
{
	return _unlink( path );
}

inline char * getcwd( char * buf, size_t size )
{
	return _getcwd( buf, int( size ) );
}

inline char * dirname( char * path )
{
	// POSIX dirname(3): mutates its argument and returns a pointer to it.
	// "a/b/c" -> "a/b", "a/b/" -> "a/b", "/name" -> "/", "name" -> ".",
	// "" -> ".". The config paths this tool feeds it are POSIX-form (the
	// server config tree uses '/' separators everywhere).
	char * last = strrchr( path, '/' );
	if (last == NULL)
	{
		strcpy( path, "." );
	}
	else if (last == path)
	{
		path[1] = '\0';
	}
	else
	{
		*last = '\0';
	}

	return path;
}

inline int rmdir( const char * path )
{
	return _rmdir( path );
}

inline int mkdir( const char * path, int /*mode*/ )
{
	// Windows has no directory permission mode; the tool only ever passes
	// 0777.
	return _mkdir( path );
}

inline int gettimeofday( struct timeval * tv, void * /*tz*/ )
{
	// FILETIME is 100ns units since 1601-01-01 UTC; shift the epoch to
	// 1970 (134774 days) and split to seconds/microseconds.
	const unsigned long long EPOCH_1601_TO_1970 = 116444736000000000ULL;

	FILETIME ft;
	GetSystemTimeAsFileTime( &ft );

	ULARGE_INTEGER large;
	large.LowPart = ft.dwLowDateTime;
	large.HighPart = ft.dwHighDateTime;

	unsigned long long now = large.QuadPart - EPOCH_1601_TO_1970;
	tv->tv_sec = long( now / 10000000ULL );
	tv->tv_usec = long( (now % 10000000ULL) / 10ULL );
	return 0;
}

inline struct tm * localtime_r( const time_t * timep, struct tm * result )
{
	// POSIX returns the result pointer; localtime_s takes it first and
	// returns an errno_t.
	localtime_s( result, timep );
	return result;
}

inline struct tm * gmtime_r( const time_t * timep, struct tm * result )
{
	gmtime_s( result, timep );
	return result;
}

inline ssize_t getline( char ** linePtr, size_t * n, FILE * stream )
{
	// POSIX getline: keeps the trailing newline, grows the buffer as
	// needed and allocates it when the caller passes NULL/0 - the tool
	// relies on that form.
	if ((linePtr == NULL) || (n == NULL) || (stream == NULL))
	{
		return -1;
	}

	if ((*linePtr == NULL) || (*n == 0))
	{
		*n = 128;
		*linePtr = (char *)malloc( *n );
		if (*linePtr == NULL)
		{
			*n = 0;
			return -1;
		}
	}

	size_t used = 0;
	int c = 0;
	while ((c = fgetc( stream )) != EOF)
	{
		if (used + 2 > *n)
		{
			size_t newN = *n * 2;
			char * newLine = (char *)realloc( *linePtr, newN );
			if (newLine == NULL)
			{
				return -1;
			}

			*linePtr = newLine;
			*n = newN;
		}

		(*linePtr)[ used++ ] = char( c );
		if (c == '\n')
		{
			break;
		}
	}

	if ((used == 0) && (c == EOF))
	{
		return -1;
	}

	(*linePtr)[ used ] = '\0';
	return ssize_t( used );
}


// <regex.h> shims over std::regex. The reader side (query_params) uses the
// POSIX API with REG_EXTENDED|REG_NOSUB(+REG_ICASE) and only ever asks
// "does this string match", so the full regmatch_t machinery is not needed;
// regexec's nmatch/pmatch arguments are accepted and ignored. REG_NEWLINE
// has no exact std::regex equivalent under the extended grammar - it only
// matters for multi-line subjects, which the tool never passes (each log
// message is a single line).
#define REG_EXTENDED	0x01
#define REG_ICASE		0x02
#define REG_NEWLINE		0x04
#define REG_NOSUB		0x08

#define REG_NOMATCH		1
#define REG_BADPAT		2

struct regex_t
{
	std::regex * re;
	char error[ 128 ];
};

inline int regcomp( regex_t * preg, const char * pattern, int cflags )
{
	std::regex::flag_type flags = std::regex::extended;
	if (cflags & REG_ICASE)
	{
		flags |= std::regex::icase;
	}

	preg->re = NULL;
	preg->error[ 0 ] = '\0';

	try
	{
		preg->re = new std::regex( pattern, flags );
		return 0;
	}
	catch ( const std::regex_error & e )
	{
		strncpy( preg->error, e.what(), sizeof( preg->error ) - 1 );
		preg->error[ sizeof( preg->error ) - 1 ] = '\0';
		return REG_BADPAT;
	}
}

inline int regexec( const regex_t * preg, const char * string,
		size_t /*nmatch*/, void * /*pmatch*/, int /*eflags*/ )
{
	if ((preg == NULL) || (preg->re == NULL))
	{
		return REG_NOMATCH;
	}

	try
	{
		return std::regex_search( string, *preg->re ) ? 0 : REG_NOMATCH;
	}
	catch ( ... )
	{
		return REG_NOMATCH;
	}
}

inline size_t regerror( int /*errcode*/, const regex_t * preg,
		char * errbuf, size_t errbufSize )
{
	const char * message = "invalid regular expression";
	if ((preg != NULL) && (preg->error[ 0 ] != '\0'))
	{
		message = preg->error;
	}

	if ((errbuf != NULL) && (errbufSize > 0))
	{
		strncpy( errbuf, message, errbufSize - 1 );
		errbuf[ errbufSize - 1 ] = '\0';
	}
	return strlen( message );
}

inline void regfree( regex_t * preg )
{
	delete preg->re;
	preg->re = NULL;
}

#endif // defined( _WIN32 )

#endif // MESSAGE_LOGGER_POSIX_COMPAT_HPP
