// tools/bw_lsp/unit_test/main.cpp

#include "script/first_include.hpp"
#include "pch.hpp"

#include "cstdmf/debug_filter.hpp"
#include "entitydef/data_type.hpp"
#include "entitydef/meta_data_type.hpp"
#include "entitydef/unit_test/test_harness.hpp"
#include "pyscript/script.hpp"
#include "unit_test_lib/unit_test.hpp"


BW_BEGIN_NAMESPACE

// Force the entitydef dependency objects into the link, exactly as the
// lib/entitydef unit test main does (see lib/entitydef/unit_test/main.cpp).
extern int Math_token;
extern int ResMgr_token;
extern int PyScript_token;
extern int ChunkUserDataObject_token;
extern int PyUserDataObject_token;
extern int UserDataObjectDescriptionMap_Token;

int s_tokens =
	Math_token |
	ResMgr_token |
	PyScript_token |
	ChunkUserDataObject_token |
	PyUserDataObject_token |
	UserDataObjectDescriptionMap_Token;

BW_END_NAMESPACE


BW_USE_NAMESPACE

int main( int argc, char* argv[] )
{
	BW_SYSTEMSTAGE_MAIN();

	Allocator::setCrashOnLeak( true );

	// Saved for the harness (it needs the res args to build its own BWResource).
	EntityDefUnitTestHarness::s_cmdArgC = argc;
	EntityDefUnitTestHarness::s_cmdArgV = argv;

	const int result = BWUnitTest::runTest( "bw_lsp", argc, argv );

	Allocator::setCrashOnLeak( false );
	Script::fini();
	DataType::clearStaticsForReload();
	MetaDataType::fini();
	DebugFilter::fini();

	return result;
}

// main.cpp
