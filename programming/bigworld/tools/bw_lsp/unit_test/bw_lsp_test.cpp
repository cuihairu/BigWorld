// tools/bw_lsp/unit_test/bw_lsp_test.cpp
//
// Pins the bw-lsp engine binary. The transport and the dispatch of the
// methods that need no resource tree run without the harness; the methods that
// read a tree run under the same EntityDefUnitTestHarness the entitydef tests
// use, so BWResource and Script are set up the way the engine expects.
//
// The headline acceptance for VSX-2 is the digest: the digest this tool
// answers with on the fantasydemo tree is byte-identical to what
// EntityDescriptionMap::parse produces, which is the same digest the server
// captures into BigWorld.schemaDigest() before any migrate (see
// server/cellapp/entity_type.cpp). addToMD5 feeds the local method index, not
// the range-assigned message id, and setExposedMsgID is NULL-safe, so the
// NULL ranges here do not change the digest.

#include "script/first_include.hpp"
#include "pch.hpp"

#include "bw_lsp.hpp"
#include "bw_lsp_rpc.hpp"

#include "cstdmf/bw_util.hpp"
#include "cstdmf/md5.hpp"
#include "entitydef/entity_description_map.hpp"
#include "resmgr/bwresource.hpp"

#include <json/json.h>

#include <cstring>

BW_BEGIN_NAMESPACE

namespace
{

/**
 *	Walks up from startDir looking for the fantasydemo res tree, following the
 *	same lookup the entitydef asset test uses.
 */
BW::string findFantasyDemoRes( const BW::string & startDir )
{
	BW::string dir = startDir;

	for (int level = 0; level < 10 && dir.length() > 1; ++level)
	{
		const BW::string candidate = dir + "/programming/fantasydemo/res";

		FILE * pFile = fopen( (candidate + "/scripts/entities.xml").c_str(),
			"rb" );
		if (pFile != NULL)
		{
			fclose( pFile );
			return candidate;
		}

		const size_t slash = dir.find_last_of( '/' );
		if (slash == BW::string::npos)
		{
			break;
		}
		dir = dir.substr( 0, slash );
	}

	return "";
}


/**
 *	The fantasydemo res root, resolved from the executable or the working
 *	directory. Cached.
 */
const BW::string & fantasyDemoResPath()
{
	static BW::string path;

	if (path.empty())
	{
		path = findFantasyDemoRes( BWUtil::executableDirectory().c_str() );
		if (path.empty())
		{
			path = findFantasyDemoRes( "." );
		}
	}
	return path;
}


/**
 *	Exposes the fantasydemo tree to BWResource for the test.
 */
bool addFantasyDemoPath()
{
	if (fantasyDemoResPath().empty())
	{
		return false;
	}
	BWResource::addPath( fantasyDemoResPath().c_str() );
	return true;
}

} // end anonymous namespace


/**
 *	One frame survives a write/read round trip on a memory FILE.
 */
TEST( BwLsp_FrameRoundTrip )
{
	const char * payload = "{\"jsonrpc\":\"2.0\",\"method\":\"initialize\"}";
	BW::string body = payload;

	FILE * pFile = tmpfile();
	CHECK( pFile != NULL );
	if (pFile == NULL)
	{
		return;
	}

	CHECK( bwLspWriteFrame( pFile, body ) );

	rewind( pFile );

	BW::string readBack;
	CHECK( bwLspReadFrame( pFile, readBack ) );
	CHECK_EQUAL( body, readBack );

	fclose( pFile );
}


/**
 *	A stream with no Content-Length header is not a frame.
 */
TEST( BwLsp_FrameRejectsHeaderless )
{
	FILE * pFile = tmpfile();
	CHECK( pFile != NULL );
	if (pFile == NULL)
	{
		return;
	}

	fputs( "this is not a frame", pFile );
	rewind( pFile );

	BW::string body;
	CHECK( !bwLspReadFrame( pFile, body ) );

	fclose( pFile );
}


/**
 *	initialize is answered with an (empty) capability set.
 */
TEST( BwLsp_DispatchInitialize )
{
	BwLspSession session;
	BW::string response;

	CHECK( bwLspHandleMessage( session,
		"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\"}",
		response ) );
	CHECK( !response.empty() );

	Json::Value parsed;
	Json::Reader reader;
	CHECK( reader.parse( response.c_str(), parsed ) );
	CHECK( parsed[ "result" ].isMember( "capabilities" ) );
}


/**
 *	Malformed JSON is a JSON-RPC parse error.
 */
TEST( BwLsp_DispatchBadJson )
{
	BwLspSession session;
	BW::string response;

	CHECK( bwLspHandleMessage( session, "{not json", response ) );
	CHECK( !response.empty() );

	Json::Value parsed;
	Json::Reader reader;
	CHECK( reader.parse( response.c_str(), parsed ) );
	CHECK_EQUAL( -32700, parsed[ "error" ][ "code" ].asInt() );
}


/**
 *	An unknown method is a JSON-RPC method-not-found error.
 */
TEST( BwLsp_DispatchUnknownMethod )
{
	BwLspSession session;
	BW::string response;

	CHECK( bwLspHandleMessage( session,
		"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"bw/notReal\"}",
		response ) );
	CHECK( !response.empty() );

	Json::Value parsed;
	Json::Reader reader;
	CHECK( reader.parse( response.c_str(), parsed ) );
	CHECK_EQUAL( -32601, parsed[ "error" ][ "code" ].asInt() );
}


/**
 *	shutdown replies null and then asks the loop to exit.
 */
TEST( BwLsp_DispatchShutdown )
{
	BwLspSession session;
	BW::string response;

	CHECK( !bwLspHandleMessage( session,
		"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"shutdown\"}",
		response ) );
	CHECK( !response.empty() );

	Json::Value parsed;
	Json::Reader reader;
	CHECK( reader.parse( response.c_str(), parsed ) );
	CHECK( parsed[ "result" ].isNull() );
}


/**
 *	Opening a --res path that is not a directory fails cleanly instead of
 *	letting BWResource's CRITICAL abort kill the process.
 */
TEST( BwLsp_OpenResPathsRejectsMissingDir )
{
	BwLspSession session;
	CHECK( !session.openResPaths( "/tmp/bw-lsp-does-not-exist-xyzzy" ) );
}


/**
 *	The digest the tool answers with is byte-identical to the parse-and-MD5
 *	digest the server computes for BigWorld.schemaDigest().
 */
TEST_F( EntityDefUnitTestHarness, BwLsp_SchemaDigestMatchesServerMap )
{
	CHECK( addFantasyDemoPath() );

	DataSectionPtr pEntities = BWResource::openSection( "scripts/entities.xml" );
	CHECK( pEntities );
	if (!pEntities)
	{
		return;
	}

	EntityDescriptionMap serverMap;
	CHECK( serverMap.parse( pEntities ) );

	MD5 md5;
	serverMap.addToMD5( md5 );
	MD5::Digest digest;
	md5.getDigest( digest );
	const BW::string serverDigest = digest.quote();

	BwLspSession session;
	CHECK( session.openResPaths( fantasyDemoResPath() ) );
	const BW::string toolDigest = session.schemaDigest();

	CHECK( !serverDigest.empty() );
	CHECK_EQUAL( serverDigest, toolDigest );
}


/**
 *	A well-formed def validates clean.
 */
TEST_F( EntityDefUnitTestHarness, BwLsp_ValidateGoodDef )
{
	CHECK( addFantasyDemoPath() );

	BwLspSession session;
	CHECK( session.openResPaths( fantasyDemoResPath() ) );

	const BW::string defPath = fantasyDemoResPath() +
		"/scripts/entity_defs/Avatar.def";

	CHECK( session.validateDef( defPath ).empty() );
}


/**
 *	A def outside any res tree yields a clear diagnostic rather than a crash or
 *	a misleading parse error.
 */
TEST_F( EntityDefUnitTestHarness, BwLsp_ValidateOutsideResTree )
{
	BwLspSession session;

	const BW::vector< BwLspDiagnostic > diagnostics =
		session.validateDef( "/tmp/no-res-tree/broken.def" );

	CHECK_EQUAL( (size_t)1, diagnostics.size() );
	if (diagnostics.size() == 1)
	{
		CHECK_EQUAL( BW::string( "bw-lsp/no-res-root" ),
			diagnostics[ 0 ].code_ );
	}
}


/**
 *	An entity type name resolves to its def file.
 */
TEST_F( EntityDefUnitTestHarness, BwLsp_DefinitionEntityType )
{
	CHECK( addFantasyDemoPath() );

	BwLspSession session;
	CHECK( session.openResPaths( fantasyDemoResPath() ) );

	BW::string absolutePath;
	unsigned int line = 0;
	unsigned int column = 0;

	CHECK( session.resolveDefinition( "Avatar", "",
		absolutePath, line, column ) );

	CHECK( absolutePath.find( "entity_defs/Avatar.def" ) != BW::string::npos );
}


BW_END_NAMESPACE

// bw_lsp_test.cpp
