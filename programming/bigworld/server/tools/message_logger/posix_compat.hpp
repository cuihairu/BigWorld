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

#include <direct.h>	// _mkdir
#include <io.h>		// _unlink, _rmdir

// Lean windows.h first, same order as endpoint.hpp; Winsock2 supplies
// struct timeval for the gettimeofday shim below.
#include "cstdmf/cstdmf_windows.hpp"
#include <Winsock2.h>

typedef intptr_t ssize_t;

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

#endif // defined( _WIN32 )

#endif // MESSAGE_LOGGER_POSIX_COMPAT_HPP
