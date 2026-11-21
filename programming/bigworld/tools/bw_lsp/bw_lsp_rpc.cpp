// bw_lsp_rpc.cpp

#include "bw_lsp_rpc.hpp"

#include "bw_lsp.hpp"

#include <json/json.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

BW_BEGIN_NAMESPACE

namespace
{

/**
 *	Reads a string member as a BW string. jsoncpp answers in std strings and
 *	the two use different allocators, so the conversion is explicit here rather
 *	than left to an implicit one that only exists one way.
 */
BW::string toBw( const Json::Value & value )
{
	const std::string s = value.asString();
	return BW::string( s.data(), s.size() );
}


/**
 *	Stores a std JSON string into the BW string that carries the frame body.
 */
void writeBody( BW::string & out, const std::string & json )
{
	out.assign( json.data(), json.size() );
}


/**
 *	Case-insensitive prefix test, so "content-length:" is accepted in any
 *	letter case the way the LSP base protocol allows.
 */
bool startsWithIgnoreCase( const char * text, const char * prefix )
{
	while (*prefix != '\0')
	{
		if (tolower( (unsigned char)*text ) != tolower( (unsigned char)*prefix ))
		{
			return false;
		}
		++text;
		++prefix;
	}
	return true;
}


/**
 *	Decodes the percent-escapes a URI carries into the path the engine reads.
 */
BW::string percentDecode( const BW::string & uri )
{
	BW::string out;
	out.reserve( uri.size() );
	for (size_t i = 0; i < uri.size(); ++i)
	{
		if (uri[ i ] == '%' && i + 2 < uri.size() &&
			isxdigit( (unsigned char)uri[ i + 1 ] ) &&
			isxdigit( (unsigned char)uri[ i + 2 ] ))
		{
			const char hex[ 3 ] = { uri[ i + 1 ], uri[ i + 2 ], '\0' };
			out += (char)strtoul( hex, NULL, 16 );
			i += 2;
		}
		else
		{
			out += uri[ i ];
		}
	}
	return out;
}


/**
 *	file:///a/b → /a/b.
 */
BW::string uriToPath( const BW::string & uri )
{
	if (uri.compare( 0, 7, "file://" ) == 0)
	{
		return percentDecode( uri.substr( 7 ) );
	}
	return percentDecode( uri );
}


/**
 *	/a/b → file:///a/b.
 */
BW::string pathToUri( const BW::string & path )
{
	return BW::string( "file://" ) + path;
}


/**
 *	Fills an array member with the engine's findings in the shape the editor
 *	side unpacks: a range, a severity and a code, all zero-based and in the
 *	editor's own units.
 */
void diagnosticsToJson( const BW::vector< BwLspDiagnostic > & diagnostics,
	Json::Value & out )
{
	out = Json::Value( Json::arrayValue );
	for (size_t i = 0; i < diagnostics.size(); ++i)
	{
		const BwLspDiagnostic & d = diagnostics[ i ];
		Json::Value j;
		j[ "range" ][ "start" ][ "line" ] = d.line_;
		j[ "range" ][ "start" ][ "character" ] = d.column_;
		j[ "range" ][ "end" ][ "line" ] = d.endLine_;
		j[ "range" ][ "end" ][ "character" ] = d.endColumn_;
		j[ "severity" ] = d.severity_;
		j[ "code" ] = d.code_.c_str();
		j[ "message" ] = d.message_.c_str();
		out.append( j );
	}
}

} // end anonymous namespace


bool bwLspReadFrame( FILE * in, BW::string & body )
{
	body.clear();

	size_t contentLength = 0;
	bool sawLength = false;

	// Headers are lines of "Name: value" until a blank line.
	char line[ 256 ];
	while (fgets( line, sizeof( line ), in ) != NULL)
	{
		if (line[ 0 ] == '\n' || (line[ 0 ] == '\r' && line[ 1 ] == '\n'))
		{
			break;
		}

		size_t len = strlen( line );
		while (len > 0 &&
			(line[ len - 1 ] == '\n' || line[ len - 1 ] == '\r'))
		{
			line[ --len ] = '\0';
		}

		if (startsWithIgnoreCase( line, "Content-Length:" ))
		{
			sawLength = true;
			contentLength = (size_t)strtoul( line + 15, NULL, 10 );
		}
	}

	if (!sawLength)
	{
		return false;
	}

	body.reserve( contentLength );
	char c;
	for (size_t i = 0; i < contentLength; ++i)
	{
		if (fread( &c, 1, 1, in ) != 1)
		{
			return false;
		}
		body += c;
	}
	return true;
}


bool bwLspWriteFrame( FILE * out, const BW::string & body )
{
	fprintf( out, "Content-Length: %d\r\n\r\n", (int)body.size() );
	fwrite( body.data(), 1, body.size(), out );
	fflush( out );
	return true;
}


bool bwLspHandleMessage( BwLspSession & session, const BW::string & messageBody,
	BW::string & responseBody )
{
	responseBody.clear();

	Json::Value request;
	Json::Reader reader;
	if (!reader.parse( messageBody.data(),
			messageBody.data() + messageBody.size(), request ) ||
		!request.isObject())
	{
		Json::Value response;
		response[ "jsonrpc" ] = "2.0";
		response[ "id" ] = Json::nullValue;
		response[ "error" ][ "code" ] = -32700;
		response[ "error" ][ "message" ] = "parse error";
		writeBody( responseBody, Json::FastWriter().write( response ) );
		return true;
	}

	// The method name is compared against literals only; keeping it a std
	// string avoids a round trip through the BW allocator for the comparison
	// and for the error message below.
	const std::string method = request[ "method" ].asString();
	const bool isNotification = !request.isMember( "id" );
	const Json::Value id = request[ "id" ];

	Json::Value result;
	Json::Value error;
	bool hasError = false;
	bool exitRequested = false;

	if (method == "initialize")
	{
		result[ "capabilities" ] = Json::Value( Json::objectValue );
	}
	else if (method == "shutdown")
	{
		result = Json::Value( Json::nullValue );
		exitRequested = true;
	}
	else if (method == "exit")
	{
		// A notification: nothing is written back.
		return false;
	}
	else if (method == "bw/schemaDigest")
	{
		result[ "digest" ] = session.schemaDigest().c_str();
	}
	else if (method == "bw/validate")
	{
		const BW::string uri = toBw( request[ "params" ][ "uri" ] );
		const BW::vector< BwLspDiagnostic > diagnostics =
			session.validateDef( uriToPath( uri ) );

		diagnosticsToJson( diagnostics, result[ "diagnostics" ] );
	}
	else if (method == "bw/definition")
	{
		const BW::string symbol = toBw( request[ "params" ][ "symbol" ] );
		const BW::string fromUri = toBw( request[ "params" ][ "uri" ] );

		BW::string absolutePath;
		unsigned int line = 0;
		unsigned int column = 0;

		if (session.resolveDefinition( symbol, uriToPath( fromUri ),
				absolutePath, line, column ))
		{
			result[ "found" ] = true;
			result[ "uri" ] = pathToUri( absolutePath ).c_str();
			result[ "range" ][ "start" ][ "line" ] = line;
			result[ "range" ][ "start" ][ "character" ] = column;
			result[ "range" ][ "end" ][ "line" ] = line;
			result[ "range" ][ "end" ][ "character" ] = column + 1;
		}
		else
		{
			result[ "found" ] = false;
		}
	}
	else if (method == "bw/crossReference")
	{
		const BW::vector< BwLspDiagnostic > findings =
			session.crossReference();

		diagnosticsToJson( findings, result[ "findings" ] );
	}
	else
	{
		hasError = true;
		error[ "code" ] = -32601;
		error[ "message" ] = "method not found: " + method;
	}

	if (isNotification)
	{
		return !exitRequested;
	}

	Json::Value response;
	response[ "jsonrpc" ] = "2.0";
	response[ "id" ] = id;
	if (hasError)
	{
		response[ "error" ] = error;
	}
	else
	{
		response[ "result" ] = result;
	}
	writeBody( responseBody, Json::FastWriter().write( response ) );
	return !exitRequested;
}


void bwLspServe( BwLspSession & session )
{
	BW::string body;
	BW::string response;

	while (bwLspReadFrame( stdin, body ))
	{
		// A shutdown request still gets its reply; the loop only stops after
		// the reply has been written.
		const bool keepGoing = bwLspHandleMessage( session, body, response );
		if (!response.empty())
		{
			bwLspWriteFrame( stdout, response );
		}
		if (!keepGoing)
		{
			break;
		}
	}
}

BW_END_NAMESPACE

// bw_lsp_rpc.cpp
