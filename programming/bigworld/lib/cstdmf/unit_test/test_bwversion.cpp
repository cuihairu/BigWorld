#include "pch.hpp"

#include "cstdmf/bwversion.hpp"


BW_BEGIN_NAMESPACE

TEST( BWVersion_accessors )
{
	// The version numbers match the compile time constants.
	CHECK_EQUAL( (int)BW_VERSION_MAJOR, (int)BWVersion::majorNumber() );
	CHECK_EQUAL( (int)BW_VERSION_MINOR, (int)BWVersion::minorNumber() );
	CHECK_EQUAL( (int)BW_VERSION_PATCH, (int)BWVersion::patchNumber() );

	// The static initialiser should have generated the version string.
	CHECK( !BWVersion::versionString().empty() );
}


TEST( BWVersion_versionStringFormat )
{
	BW::stringstream expected;
	expected << BWVersion::majorNumber() << "." <<
		BWVersion::minorNumber() << "." << BWVersion::patchNumber();

	CHECK_EQUAL( expected.str(), BWVersion::versionString() );
}


TEST( BWVersion_settersUpdateString )
{
	uint16 origMajor = BWVersion::majorNumber();
	uint16 origMinor = BWVersion::minorNumber();
	uint16 origPatch = BWVersion::patchNumber();
	BW::string origString = BWVersion::versionString();

	// Change the version numbers and check the string is updated.
	BWVersion::majorNumber( origMajor + 1 );
	CHECK_EQUAL( origMajor + 1, BWVersion::majorNumber() );

	{
		BW::stringstream expected;
		expected << (origMajor + 1) << "." << origMinor << "." << origPatch;
		CHECK_EQUAL( expected.str(), BWVersion::versionString() );
	}

	BWVersion::minorNumber( origMinor + 2 );
	CHECK_EQUAL( origMinor + 2, BWVersion::minorNumber() );

	{
		BW::stringstream expected;
		expected << (origMajor + 1) << "." << (origMinor + 2) << "." << origPatch;
		CHECK_EQUAL( expected.str(), BWVersion::versionString() );
	}

	BWVersion::patchNumber( origPatch + 3 );
	CHECK_EQUAL( origPatch + 3, BWVersion::patchNumber() );

	{
		BW::stringstream expected;
		expected << (origMajor + 1) << "." << (origMinor + 2) << "." <<
			(origPatch + 3);
		CHECK_EQUAL( expected.str(), BWVersion::versionString() );
	}

	// Restore the original values so that other tests are unaffected.
	BWVersion::patchNumber( origPatch );
	BWVersion::minorNumber( origMinor );
	BWVersion::majorNumber( origMajor );

	CHECK_EQUAL( origString, BWVersion::versionString() );
}


TEST( BWVersion_setSameValueKeepsString )
{
	// Setting a number to its current value should not regenerate the string.
	uint16 origMajor = BWVersion::majorNumber();
	BW::string before = BWVersion::versionString();

	BWVersion::majorNumber( origMajor );

	CHECK_EQUAL( before, BWVersion::versionString() );
}

BW_END_NAMESPACE

// test_bwversion.cpp
