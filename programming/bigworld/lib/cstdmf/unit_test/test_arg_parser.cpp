#include "pch.hpp"

#include "cstdmf/arg_parser.hpp"


BW_BEGIN_NAMESPACE

TEST( ArgParser_optionalArgs )
{
	ArgParser parser( "test_program" );

	parser.add( "name", "A name" );

	char arg0[] = "test_program";
	char arg1[] = "--name";
	char arg2[] = "fred";
	char * argv[] = { arg0, arg1, arg2, NULL };

	CHECK( parser.parse( 3, argv ) );
	CHECK( parser.isPresent( "name" ) );
	CHECK( !parser.isPresent( "nonExistent" ) );
	CHECK_EQUAL( BW::string( "fred" ), parser.get( "name", BW::string( "default" ) ) );
}


TEST( ArgParser_equalsSeparatedValue )
{
	ArgParser parser( "test_program" );

	parser.add( "count", "A count" );

	char arg0[] = "test_program";
	char arg1[] = "--count=42";
	char * argv[] = { arg0, arg1, NULL };

	CHECK( parser.parse( 2, argv ) );
	CHECK_EQUAL( 42u, parser.get( "count", 0u ) );
}


TEST( ArgParser_separateValue )
{
	ArgParser parser( "test_program" );

	parser.add( "count", "A count" );

	char arg0[] = "test_program";
	char arg1[] = "--count";
	char arg2[] = "7";
	char * argv[] = { arg0, arg1, arg2, NULL };

	CHECK( parser.parse( 3, argv ) );
	CHECK_EQUAL( 7u, parser.get( "count", 0u ) );
}


TEST( ArgParser_missingValuesUseDefaults )
{
	ArgParser parser( "test_program" );

	parser.add( "name", "A name" );
	parser.add( "count", "A count" );

	char arg0[] = "test_program";
	char * argv[] = { arg0, NULL };

	CHECK( parser.parse( 1, argv ) );

	CHECK_EQUAL( BW::string( "unknown" ), parser.get( "name", BW::string( "unknown" ) ) );
	CHECK_EQUAL( 5u, parser.get( "count", 5u ) );
}


TEST( ArgParser_flagWithNoValue )
{
	ArgParser parser( "test_program" );

	parser.add( "flag", "A flag" );

	char arg0[] = "test_program";
	char arg1[] = "--flag";
	char arg2[] = "--other";
	char * argv[] = { arg0, arg1, arg2, NULL };

	CHECK( parser.parse( 3, argv ) );

	// Present with no value returns true for bool gets.
	CHECK_EQUAL( true, parser.get( "flag", false ) );

	// String gets fall back to the default value.
	CHECK_EQUAL( BW::string( "default" ), parser.get( "flag", BW::string( "default" ) ) );
}


TEST( ArgParser_boolValues )
{
	ArgParser parser( "test_program" );

	parser.add( "yes", "A boolean" );
	parser.add( "no", "A boolean" );
	parser.add( "numbers", "A boolean" );

	char arg0[] = "test_program";
	char arg1[] = "--yes=true";
	char arg2[] = "--no=false";
	char arg3[] = "--numbers=1";
	char * argv[] = { arg0, arg1, arg2, arg3, NULL };

	CHECK( parser.parse( 4, argv ) );

	CHECK_EQUAL( true, parser.get( "yes", false ) );
	CHECK_EQUAL( false, parser.get( "no", true ) );

	// ArgParser::get(name, bool) only treats the literal string "true" as
	// true; anything else present is false. "1" is therefore false - the
	// documented way to set a boolean is --name=true (or just --name, which
	// means true because no value was given at all).
	CHECK_EQUAL( false, parser.get( "numbers", true ) );

	// Not present uses the default.
	CHECK_EQUAL( false, parser.get( "missing", false ) );
	CHECK_EQUAL( true, parser.get( "missing", true ) );
}


TEST( ArgParser_boolFlagWithoutValue )
{
	ArgParser parser( "test_program" );

	parser.add( "verbose", "A boolean flag" );

	char arg0[] = "test_program";
	char arg1[] = "--verbose";
	char * argv[] = { arg0, arg1, NULL };

	CHECK( parser.parse( 2, argv ) );

	// A present argument with no value at all means true.
	CHECK_EQUAL( true, parser.get( "verbose", false ) );
}


TEST( ArgParser_missingNonOptionalFails )
{
	ArgParser parser( "test_program" );

	parser.add( "required", "A required argument", false );

	char arg0[] = "test_program";
	char * argv[] = { arg0, NULL };

	// The non optional argument is not present.
	CHECK( !parser.parse( 1, argv ) );

	char arg1[] = "--required";
	char arg2[] = "value";
	char * argv2[] = { arg0, arg1, arg2, NULL };

	CHECK( parser.parse( 3, argv2 ) );
	CHECK_EQUAL( BW::string( "value" ),
		parser.get( "required", BW::string( "" ) ) );
}


TEST( ArgParser_helpFailsParsing )
{
	ArgParser parser( "test_program" );

	parser.add( "name", "A name" );

	char arg0[] = "test_program";
	char arg1[] = "--help";
	char * argv[] = { arg0, arg1, NULL };

	CHECK( !parser.parse( 2, argv ) );
}


TEST( ArgParser_unknownArgumentsIgnored )
{
	ArgParser parser( "test_program" );

	parser.add( "name", "A name" );

	char arg0[] = "test_program";
	char arg1[] = "--unknown";
	char arg2[] = "--name";
	char arg3[] = "wilma";
	char * argv[] = { arg0, arg1, arg2, arg3, NULL };

	CHECK( parser.parse( 4, argv ) );
	CHECK( !parser.isPresent( "unknown" ) );
	CHECK_EQUAL( BW::string( "wilma" ),
		parser.get( "name", BW::string( "" ) ) );
}


TEST( ArgParser_printUsage )
{
	ArgParser parser( "test_program" );

	parser.add( "name", "A name", false );
	parser.add( "count", "A count" );

	// Mostly to make sure it doesn't crash.
	parser.printUsage();
}

BW_END_NAMESPACE

// test_arg_parser.cpp
