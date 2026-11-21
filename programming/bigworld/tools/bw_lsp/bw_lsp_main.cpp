// bw_lsp_main.cpp
//
// The command line face of the bw-lsp engine binary. Everything it prints is
// produced by the same JSON-RPC dispatcher the serve loop uses, so a command
// line run and the extension see exactly the same answers.

#include "bw_lsp.hpp"
#include "bw_lsp_rpc.hpp"

#include "cstdmf/bw_namespace.hpp"
#include "cstdmf/bw_vector.hpp"

#include <json/json.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

BW_BEGIN_NAMESPACE

namespace
{

/**
 *	Prints how to use the tool.
 */
void printUsage()
{
	fprintf( stderr,
		"usage:\n"
		"  bw-lsp serve --res <paths>\n"
		"  bw-lsp schema-digest --res <paths>\n"
		"  bw-lsp validate --res <paths> <def file>\n"
		"  bw-lsp definition --res <paths> <symbol> [--from <def file>]\n"
		"  bw-lsp cross-ref --res <paths>\n"
		"\n"
		"<paths> is the colon separated res root list the engine itself takes\n"
		"(the directory that carries scripts/entities.xml).\n" );
}


/**
 *	Runs one JSON-RPC request through the dispatcher and prints its result to
 *	stdout. Returns false when the dispatcher answered with an error.
 */
bool runOneShot( BwLspSession & session, const BW::string & method,
	const Json::Value & params )
{
	Json::Value request;
	request[ "jsonrpc" ] = "2.0";
	request[ "id" ] = 1;
	request[ "method" ] = method.c_str();
	request[ "params" ] = params;

	const std::string bodyStd = Json::FastWriter().write( request );
	const BW::string body( bodyStd.data(), bodyStd.size() );

	BW::string responseBody;
	bwLspHandleMessage( session, body, responseBody );

	Json::Value response;
	Json::Reader reader;
	if (!reader.parse( responseBody.c_str(), response ))
	{
		fprintf( stderr, "bw-lsp: internal: unparseable reply\n" );
		return false;
	}

	if (response.isMember( "error" ))
	{
		fprintf( stderr, "bw-lsp: %s\n",
			response[ "error" ][ "message" ].asCString() );
		return false;
	}

	printf( "%s", Json::FastWriter().write( response[ "result" ] ).c_str() );
	return true;
}

} // end anonymous namespace

BW_END_NAMESPACE


BW_USE_NAMESPACE

int main( int argc, char * argv[] )
{
	if (argc < 2)
	{
		printUsage();
		return 2;
	}

	const BW::string command = argv[ 1 ];
	BW::string resPaths;
	BW::string fromPath;
	BW::vector< BW::string > loose;

	for (int i = 2; i < argc; ++i)
	{
		const BW::string arg = argv[ i ];

		if (arg.compare( 0, 5, "--res" ) == 0)
		{
			if (arg.size() > 5 && arg[ 5 ] == '=')
			{
				resPaths = arg.substr( 6 );
			}
			else if (i + 1 < argc)
			{
				resPaths = argv[ ++i ];
			}
		}
		else if (arg == "--from" && i + 1 < argc)
		{
			fromPath = argv[ ++i ];
		}
		else
		{
			loose.push_back( arg );
		}
	}

	BwLspSession session;

	// The res tree is opened before the command runs, so every command (and
	// the serve loop below) sees the same tree.
	if (!resPaths.empty() && !session.openResPaths( resPaths ))
	{
		fprintf( stderr, "bw-lsp: could not open --res paths\n" );
		return 2;
	}

	if (command == "serve")
	{
		bwLspServe( session );
		return 0;
	}

	if (command == "schema-digest")
	{
		return runOneShot( session, "bw/schemaDigest", Json::Value() ) ? 0 : 1;
	}

	if (command == "validate")
	{
		if (loose.empty())
		{
			printUsage();
			return 2;
		}

		Json::Value params;
		params[ "uri" ] = (BW::string( "file://" ) + loose[ 0 ]).c_str();
		return runOneShot( session, "bw/validate", params ) ? 0 : 1;
	}

	if (command == "definition")
	{
		if (loose.empty())
		{
			printUsage();
			return 2;
		}

		Json::Value params;
		params[ "symbol" ] = loose[ 0 ].c_str();
		if (!fromPath.empty())
		{
			params[ "uri" ] = (BW::string( "file://" ) + fromPath).c_str();
		}
		return runOneShot( session, "bw/definition", params ) ? 0 : 1;
	}

	if (command == "cross-ref")
	{
		return runOneShot( session, "bw/crossReference", Json::Value() ) ? 0 : 1;
	}

	printUsage();
	return 2;
}

// bw_lsp_main.cpp
