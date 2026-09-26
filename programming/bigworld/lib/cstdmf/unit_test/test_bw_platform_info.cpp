#include "pch.hpp"

#include "cstdmf/bw_platform_info.hpp"

#include <sys/stat.h>


BW_BEGIN_NAMESPACE

namespace // anonymous
{

	bool platformStartsWith( const BW::string & str, const BW::string & prefix )
	{
		return str.find( prefix ) == 0;
	}


	bool fileExists( const char * path )
	{
		struct stat statBuf;
		return stat( path, &statBuf ) == 0;
	}

} // namespace anonymous


TEST( PlatformInfo_str )
{
	const BW::string & platformName = PlatformInfo::str();

	// The platform name is a non empty cached string.
	CHECK( !platformName.empty() );

	// Repeated calls return the same cached string.
	CHECK( platformName == PlatformInfo::str() );
}


TEST( PlatformInfo_buildStr )
{
	const BW::string & buildName = PlatformInfo::buildStr();

	CHECK( !buildName.empty() );

	// Repeated calls return the same cached string.
	CHECK( buildName == PlatformInfo::buildStr() );

	// CentOS and RHEL platform names are condensed to 'el' for builds.
	if (platformStartsWith( PlatformInfo::str(), "centos" ) ||
		platformStartsWith( PlatformInfo::str(), "rhel" ))
	{
		CHECK( platformStartsWith( buildName, "el" ) );
	}
}


TEST( PlatformInfo_buildStrMatchesPlatform )
{
	// On this platform the build string is either the platform string or
	// an condensed enterprise Linux variant of it.
	const BW::string & platformName = PlatformInfo::str();
	const BW::string & buildName = PlatformInfo::buildStr();

	if (!platformStartsWith( platformName, "centos" ) &&
		!platformStartsWith( platformName, "rhel" ))
	{
		CHECK( buildName == platformName );
	}
}


// BIGWORLD_BEGIN(3.13 migration)
// Debian-family hosts must report the same platform name as
// build/make/platform_info.py does for the build, so that the
// lib-dynload-<platform> resource directory built by the makefiles is the
// one Script::init() puts on sys.path.
// BIGWORLD_END
TEST( PlatformInfo_debianEquivalentPlatform )
{
	const bool hasRedhatRelease = fileExists( "/etc/redhat-release" );
	const bool hasDebianVersion = fileExists( "/etc/debian_version" );

	if (!hasRedhatRelease && hasDebianVersion)
	{
		CHECK_EQUAL( BW::string( "el7" ), PlatformInfo::str() );
	}
	else
	{
		// The fallback never applies when a RedHat release file is present.
		CHECK( hasRedhatRelease || !hasDebianVersion );
	}
}

BW_END_NAMESPACE

// test_bw_platform_info.cpp
