// Coverage batch 28: the BWResource path/formatting, path-list access,
// section-census and file-type surfaces.
//
// Platform notes (these are behaviour, not assumptions):
//  - formatPath("") returns "" (bw_util.cpp formatPath early-returns on empty).
//  - getFilePath delegates to BWUtil::getFilePath, which uses dirname() on
//    non-Windows: "a/b/c.txt" -> "a/b", "c.txt" -> ".".
//  - getCurrentDirectory() is an unimplemented stub outside _WIN32 and returns
//    "" (bwresource.cpp getCurrentDirectory).
//  - addPath(path, -1) appends, never prepends: the -1 default is converted to
//    size_t before the range check, so atIndex becomes paths_.size().
//
// Not covered here, recorded in the migration ledger instead of being fixed:
//  - addSubPaths: `STRINGVECTOR newPaths( paths.size() )` pre-allocates
//    empty strings that end up polluting the path list.
//  - ensurePathExists / ensureAbsolutePathExists rewrite '/' to '\\' before
//    mkdir, so they only create directories on Windows.
#include "pch.hpp"

#include "resmgr/bwresource.hpp"
#include "resmgr/data_section_census.hpp"
#include "resmgr/resource_modification_listener.hpp"

#include <sys/stat.h>
#include <time.h>
#include <utime.h>
#include <unistd.h>

BW_BEGIN_NAMESPACE
BW_USE_NAMESPACE

namespace
{

/// Unique temp dir for this test process, removed by removeTempDir().
char g_tempDir[ BW_MAX_PATH + 1 ];

void makeTempDir()
{
	bw_snprintf( g_tempDir, ARRAY_SIZE( g_tempDir ),
		"/tmp/bwres_paths_%d", (int)getpid() );

	// Clear any leftover from a previous crashed run.
	BW::string removeCmd = "rm -rf \"";
	removeCmd += g_tempDir;
	removeCmd += "\"";
	int removeResult = system( removeCmd.c_str() );
	(void)removeResult;

	mkdir( g_tempDir, 0755 );
}

void removeTempDir()
{
	rmdir( g_tempDir );
}

/// Creates a sub directory inside the temp dir.
void makeTempSubDir( const char * name )
{
	BW::string subDir = g_tempDir;
	subDir += "/";
	subDir += name;
	mkdir( subDir.c_str(), 0755 );
}

void makeTempFile( const char * name )
{
	BW::string path = g_tempDir;
	path += "/";
	path += name;
	FILE * fp = fopen( path.c_str(), "wb" );
	if (fp)
	{
		fputs( "x", fp );
		fclose( fp );
	}
}

BW::string tempPath( const char * name )
{
	BW::string path = g_tempDir;
	path += "/";
	path += name;
	return path;
}

/// Backdates or advances a file's mtime so ordering checks are deterministic.
void setMtime( const BW::string & path, time_t time )
{
	struct utimbuf times;
	times.actime = time;
	times.modtime = time;
	utime( path.c_str(), &times );
}

/// Minimal listener for the modification-monitor family.
class TestModificationListener : public ResourceModificationListener
{
public:
	TestModificationListener() : numNotifications_( 0 ) {}

	virtual void onResourceModified( const BW::StringRef&,
			const BW::StringRef&, Action )
	{
		++numNotifications_;
	}

	int numNotifications_;
};

} // namespace


TEST_F( ResMgrUnitTestHarness, BWResource_getFilename )
{
	CHECK( this->isOK() );

	CHECK( BWResource::getFilename( "a/b/c.txt" ) == "c.txt" );
	CHECK( BWResource::getFilename( "c.txt" ) == "c.txt" );
	CHECK( BWResource::getFilename( "a\\b\\c.txt" ) == "c.txt" );
	CHECK( BWResource::getFilename( "" ) == "" );
	CHECK( BWResource::getFilename( "a/" ) == "" );
	CHECK( BWResource::getFilename( "/a/b/" ) == "" );
}


TEST_F( ResMgrUnitTestHarness, BWResource_getExtension )
{
	CHECK( this->isOK() );

	CHECK( BWResource::getExtension( "a/b.txt" ) == "txt" );
	CHECK( BWResource::getExtension( "noext" ) == "" );
	CHECK( BWResource::getExtension( ".hidden" ) == "hidden" );
	CHECK( BWResource::getExtension( "dir/.hidden" ) == "hidden" );
	CHECK( BWResource::getExtension( "a.b/c.txt" ) == "txt" );
	CHECK( BWResource::getExtension( "a.b/c" ) == "" );
	CHECK( BWResource::getExtension( "" ) == "" );
}


TEST_F( ResMgrUnitTestHarness, BWResource_removeExtension )
{
	CHECK( this->isOK() );

	CHECK( BWResource::removeExtension( "a/b.txt" ) == "a/b" );
	CHECK( BWResource::removeExtension( "noext" ) == "noext" );
	CHECK( BWResource::removeExtension( "a/b.c.d" ) == "a/b.c" );
	CHECK( BWResource::removeExtension( ".hidden" ) == "" );
	CHECK( BWResource::removeExtension( "dir/.hidden" ) == "dir/" );
	CHECK( BWResource::removeExtension( "" ) == "" );
}


TEST_F( ResMgrUnitTestHarness, BWResource_changeExtension )
{
	CHECK( this->isOK() );

	CHECK( BWResource::changeExtension( "a/b.txt", ".bak" ) == "a/b.bak" );
	CHECK( BWResource::changeExtension( "noext", ".bak" ) == "noext.bak" );
	CHECK( BWResource::changeExtension( "a/b.c.d", ".x" ) == "a/b.c.x" );
	CHECK( BWResource::changeExtension( "", ".bak" ) == ".bak" );
}


TEST_F( ResMgrUnitTestHarness, BWResource_pathIsRelative )
{
	CHECK( this->isOK() );

	CHECK( BWResource::pathIsRelative( "a/b/c.txt" ) );
	CHECK( !BWResource::pathIsRelative( "/a/b/c.txt" ) );
	CHECK( !BWResource::pathIsRelative( "\\a\\b" ) );
	CHECK( !BWResource::pathIsRelative( "C:\\a" ) );
}


TEST_F( ResMgrUnitTestHarness, BWResource_formatPath )
{
	CHECK( this->isOK() );

	CHECK( BWResource::formatPath( "a/b" ) == "a/b/" );
	CHECK( BWResource::formatPath( "a/b/" ) == "a/b/" );

	// An empty path is returned empty rather than being turned into "/".
	CHECK( BWResource::formatPath( "" ) == "" );
}


TEST_F( ResMgrUnitTestHarness, BWResource_getFilePath )
{
	CHECK( this->isOK() );

#ifdef _WIN32
	CHECK( BWResource::getFilePath( "a/b/c.txt" ) == "a/b/" );
	CHECK( BWResource::getFilePath( "a\\b\\c.txt" ) == "a\\b\\" );
	CHECK( BWResource::getFilePath( "c.txt" ) == "" );
#else
	// dirname() semantics: no trailing separator and "." for a bare name.
	CHECK( BWResource::getFilePath( "a/b/c.txt" ) == "a/b" );
	CHECK( BWResource::getFilePath( "c.txt" ) == "." );
	CHECK( BWResource::getFilePath( "/a/b/c.txt" ) == "/a/b" );
	CHECK( BWResource::getFilePath( "a\\b\\c.txt" ) == "." );
#endif
}


TEST_F( ResMgrUnitTestHarness, BWResource_getCurrentDirectory )
{
	CHECK( this->isOK() );

#ifdef _WIN32
	BW::string cwd = BWResource::getCurrentDirectory();
	CHECK( !cwd.empty() );
	CHECK( cwd[ cwd.size() - 1 ] == '/' );
#else
	// Unimplemented outside Windows: the TODO in getCurrentDirectory returns "".
	CHECK( BWResource::getCurrentDirectory().empty() );
#endif
}


TEST_F( ResMgrUnitTestHarness, BWResource_pathListAccessors )
{
	CHECK( this->isOK() );

	int numPaths = BWResource::getPathNum();
	CHECK( numPaths > 0 );

	// getDefaultPath returns the first path.
	CHECK( BWResource::getDefaultPath() == BWResource::getPath( 0 ) );

	// Out-of-range indices return an empty string.
	CHECK( BWResource::getPath( -1 ) == "" );
	CHECK( BWResource::getPath( numPaths ) == "" );
	CHECK( BWResource::getPath( numPaths + 100 ) == "" );

	// isValid is true when paths are loaded.
	CHECK( BWResource::isValid() );

	// getPathAsCommandLine quotes the paths.
	BW::string commandLine = BWResource::getPathAsCommandLine();
	CHECK( commandLine.find( "--res" ) != BW::string::npos );
	CHECK( commandLine[ commandLine.size() - 1 ] == '"' );
}


TEST_F( ResMgrUnitTestHarness, BWResource_pathBuffer )
{
	CHECK( this->isOK() );

	char buffer[ BW_MAX_PATH + 1 ];
	int numPaths = BWResource::getPathNum();

	CHECK( BWResource::instance().path( 0, buffer, ARRAY_SIZE( buffer ) ) );
	CHECK( strlen( buffer ) > 0 );

	// Out-of-range index returns false.
	CHECK( !BWResource::instance().path( numPaths, buffer, ARRAY_SIZE( buffer ) ) );
}


TEST_F( ResMgrUnitTestHarness, BWResource_addPath_delPath )
{
	CHECK( this->isOK() );

	makeTempDir();

	int numPaths = BWResource::getPathNum();

	// The default index appends.
	BWResource::addPath( g_tempDir );
	CHECK( BWResource::getPathNum() == numPaths + 1 );
	CHECK( BWResource::getPath( numPaths ) == g_tempDir );

	// An explicit index zero inserts at the front.
	BWResource::addPath( g_tempDir, 0 );
	CHECK( BWResource::getPathNum() == numPaths + 2 );
	CHECK( BWResource::getPath( 0 ) == g_tempDir );

	// The documented -1 default appends rather than prepending.
	BWResource::addPath( g_tempDir );
	CHECK( BWResource::getPathNum() == numPaths + 3 );
	CHECK( BWResource::getPath( numPaths + 2 ) == g_tempDir );

	// delPath removes by index, restoring the original list.
	BWResource::instance().delPath( 0 );
	CHECK( BWResource::getPathNum() == numPaths + 2 );
	BWResource::instance().delPath( numPaths + 1 );
	CHECK( BWResource::getPathNum() == numPaths + 1 );
	BWResource::instance().delPath( numPaths );
	CHECK( BWResource::getPathNum() == numPaths );

	removeTempDir();
}


TEST_F( ResMgrUnitTestHarness, BWResource_purge_purgeAll_save )
{
	CHECK( this->isOK() );

	makeTempDir();
	makeTempFile( "purge_me.xml" );

	// addPath so the resource id resolves against the temp directory.
	BWResource::addPath( g_tempDir );
	int numPaths = BWResource::getPathNum();

	// Open a section so it enters the cache and census.
	DataSectionPtr pSection = BWResource::openSection( "purge_me.xml" );
	CHECK( pSection.exists() );

	// purge removes the section from the cache and census.
	BWResource::instance().purge( "purge_me.xml" );
	CHECK( !DataSectionCensus::find( "purge_me.xml" ).exists() );

	// purgeAll clears the whole cache and census.
	BWResource::instance().purgeAll();
	CHECK( !DataSectionCensus::find( "purge_me.xml" ).exists() );

	// save on a resource that is not in the census returns false.
	CHECK( !BWResource::instance().save( "not_in_census.xml" ) );

	BWResource::instance().delPath( numPaths - 1 );

	removeTempDir();
}


TEST_F( ResMgrUnitTestHarness, BWResource_watchAccessFromCallingThread )
{
	CHECK( this->isOK() );

	CHECK( !BWResource::watchAccessFromCallingThread() );

	BWResource::watchAccessFromCallingThread( true );
	CHECK( BWResource::watchAccessFromCallingThread() );

	// The holder restores the previous state on scope exit.
	{
		BWResource::WatchAccessFromCallingThreadHolder holder( false );
		CHECK( !BWResource::watchAccessFromCallingThread() );
	}
	CHECK( BWResource::watchAccessFromCallingThread() );

	BWResource::watchAccessFromCallingThread( false );
	CHECK( !BWResource::watchAccessFromCallingThread() );

	// checkAccessFromCallingThread is safe to call in both states.
	BWResource::checkAccessFromCallingThread( "some/path", "test" );
	BWResource::watchAccessFromCallingThread( true );
	BWResource::checkAccessFromCallingThread( "some/path", "test" );
	BWResource::watchAccessFromCallingThread( false );
}


TEST_F( ResMgrUnitTestHarness, BWResource_modificationMonitor )
{
	CHECK( this->isOK() );

	BWResource & resource = BWResource::instance();

	TestModificationListener listener;
	resource.addModificationListener( &listener );
	resource.removeModificationListener( &listener );

	resource.enableModificationMonitor( true );
	resource.enableModificationMonitor( false );
	resource.flushModificationMonitor();

	CHECK( !resource.hasPendingModification( "no_such_file.xml" ) );

	// ignoreFileModification with a one-shot and a permanent action.
	resource.ignoreFileModification( "no_such_file.xml",
		ResourceModificationListener::ACTION_MODIFIED, true );
	resource.ignoreFileModification( "no_such_file.xml",
		ResourceModificationListener::ACTION_DELETED, false );

	CHECK( listener.numNotifications_ == 0 );
}


TEST_F( ResMgrUnitTestHarness, BWResource_fileTypeQueries )
{
	CHECK( this->isOK() );

	makeTempDir();
	makeTempFile( "real_file.xml" );
	makeTempSubDir( "real_dir" );

	// fileSystem() only resolves ids against the res path list.
	BWResource::addPath( g_tempDir );
	int numPaths = BWResource::getPathNum();

	CHECK( BWResource::isDir( "real_dir" ) );
	CHECK( !BWResource::isFile( "real_dir" ) );

	CHECK( BWResource::isFile( "real_file.xml" ) );
	CHECK( BWResource::fileExists( "real_file.xml" ) );

	// fileAbsolutelyExists goes through the native file system instead.
	CHECK( BWResource::fileAbsolutelyExists( tempPath( "real_file.xml" ) ) );
	CHECK( BWResource::fileAbsolutelyExists( tempPath( "real_dir" ) ) );

	CHECK( !BWResource::isFile( "missing.xml" ) );
	CHECK( !BWResource::fileExists( "missing.xml" ) );
	CHECK( !BWResource::fileAbsolutelyExists( tempPath( "missing.xml" ) ) );

	IFileSystem::FileInfo fi;
	CHECK( BWResource::getFileType( "real_dir", &fi ) ==
		IFileSystem::FT_DIRECTORY );
	CHECK( BWResource::getFileType( "real_file.xml", &fi ) ==
		IFileSystem::FT_FILE );

	BWResource::instance().delPath( numPaths - 1 );

	removeTempDir();
}


TEST_F( ResMgrUnitTestHarness, BWResource_modifiedTime_isFileOlder )
{
	CHECK( this->isOK() );

	makeTempDir();
	makeTempFile( "old.xml" );
	makeTempFile( "new.xml" );

	// Backdate old.xml so the comparison does not depend on wall-clock order.
	time_t now = time( NULL );
	setMtime( tempPath( "old.xml" ), now - 36000 );
	setMtime( tempPath( "new.xml" ), now );

	BWResource::addPath( g_tempDir );
	int numPaths = BWResource::getPathNum();

	uint64 oldTime = BWResource::modifiedTime( "old.xml" );
	CHECK( oldTime != 0 );

	uint64 newTime = BWResource::modifiedTime( "new.xml" );
	CHECK( newTime != 0 );
	CHECK( newTime > oldTime );

	// A missing file reports time 0.
	CHECK( BWResource::modifiedTime( "missing.xml" ) == 0 );

	// A file is never older than itself.
	CHECK( !BWResource::isFileOlder( "old.xml", "old.xml", 0 ) );

	// The zero-threshold comparison is the only portable one: on non-Windows
	// builds modifiedTime returns seconds (unix_file_system.cpp st_mtime) while
	// isFileOlder scales mustBeOlderThanSecs by 1e7, the Windows 100ns unit,
	// so any non-zero threshold is inert here. Recorded in the migration ledger.
	CHECK( BWResource::isFileOlder( "old.xml", "new.xml", 0 ) );
	CHECK( !BWResource::isFileOlder( "new.xml", "old.xml", 0 ) );

	BWResource::instance().delPath( numPaths - 1 );

	removeTempDir();
}


TEST_F( ResMgrUnitTestHarness, BWResource_resolveToAbsolutePath )
{
	CHECK( this->isOK() );

	makeTempDir();
	makeTempFile( "resolve_me.xml" );
	makeTempSubDir( "resolve_dir" );

	// An absolute path is resolved in place and reported as a file.
	BW::string absPath = tempPath( "resolve_me.xml" );
	CHECK( BWResource::resolveToAbsolutePath( absPath ) == IFileSystem::FT_FILE );
	CHECK( absPath == tempPath( "resolve_me.xml" ) );

	// A directory is reported as a directory.
	BW::string dirPath = tempPath( "resolve_dir" );
	CHECK( BWResource::resolveToAbsolutePath( dirPath ) ==
		IFileSystem::FT_DIRECTORY );

	// A missing absolute path reports not-found.
	BW::string missingPath = tempPath( "missing.xml" );
	CHECK( BWResource::resolveToAbsolutePath( missingPath ) ==
		IFileSystem::FT_NOT_FOUND );

	// A res-relative id is resolved against the path list.
	BWResource::addPath( g_tempDir );
	int numPaths = BWResource::getPathNum();

	BW::string relativePath = "resolve_me.xml";
	CHECK( BWResource::resolveToAbsolutePath( relativePath ) ==
		IFileSystem::FT_FILE );
	CHECK( relativePath.find( g_tempDir ) != BW::string::npos );

	BWResource::instance().delPath( numPaths - 1 );

	removeTempDir();
}


BW_END_NAMESPACE

// test_bwresource_paths.cpp
