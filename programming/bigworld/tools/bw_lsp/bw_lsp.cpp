// bw_lsp.cpp

#include "bw_lsp.hpp"

#include "cstdmf/debug_filter.hpp"
#include "cstdmf/debug_message_callbacks.hpp"
#include "cstdmf/debug_message_priority.hpp"
#include "cstdmf/md5.hpp"
#include "cstdmf/stdmf.hpp"

#include "entitydef/constants.hpp"
#include "entitydef/entity_description.hpp"
#include "entitydef/entity_description_map.hpp"

#include "resmgr/bwresource.hpp"
#include "resmgr/file_system.hpp"
#include "resmgr/multi_file_system.hpp"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>

BW_BEGIN_NAMESPACE

// -----------------------------------------------------------------------------
// Section: text helpers
// -----------------------------------------------------------------------------

namespace
{

/**
 *	Reads a whole file into text. Returns false when it could not be opened.
 */
bool readWholeFile( const BW::string & path, BW::string & text )
{
	FILE * pFile = fopen( path.c_str(), "rb" );
	if (pFile == NULL)
	{
		return false;
	}

	fseek( pFile, 0, SEEK_END );
	const long size = ftell( pFile );
	fseek( pFile, 0, SEEK_SET );

	text.clear();
	if (size > 0)
	{
		text.reserve( (size_t)size );
		for (long i = 0; i < size; ++i)
		{
			text += (char)fgetc( pFile );
		}
	}
	fclose( pFile );
	return true;
}


/**
 *	Converts a byte offset into a zero-based line and column, the coordinate
 *	system the editor's range model uses.
 */
void offsetToLineColumn( const BW::string & text, size_t offset,
		unsigned int & line, unsigned int & column )
{
	line = 0;
	column = 0;
	if (offset > text.size())
	{
		offset = text.size();
	}
	for (size_t i = 0; i < offset; ++i)
	{
		if (text[ i ] == '\n')
		{
			++line;
			column = 0;
		}
		else
		{
			++column;
		}
	}
}


/**
 *	First occurrence of needle outside an XML comment. Def files are XML and
 *	carry comment headers, so a hit inside one would point the editor at a
 *	place the developer is not editing. Returns npos when there is none.
 */
size_t findOutsideComments( const BW::string & text, const BW::string & needle )
{
	const size_t npos = BW::string::npos;
	size_t pos = 0;
	size_t commentEnd = npos;

	while (pos < text.size())
	{
		if (commentEnd == npos)
		{
			const size_t commentStart = text.find( "<!--", pos );
			const size_t hit = text.find( needle, pos );

			if (commentStart == npos || (hit != npos && hit < commentStart))
			{
				return hit;
			}
			if (hit == npos)
			{
				return npos;
			}
			// The first hit is at or past a comment start: skip the comment.
			commentEnd = commentStart + 4;
			pos = commentEnd;
		}
		else
		{
			const size_t commentClose = text.find( "-->", pos );
			if (commentClose == npos)
			{
				return npos;
			}
			commentEnd = npos;
			pos = commentClose + 3;
		}
	}
	return npos;
}


/**
 *	True when name is a plausible identifier an engine message would name:
 *	letters and digits, starting with a letter or underscore.
 */
bool isPlausibleName( const BW::string & name )
{
	if (name.empty())
	{
		return false;
	}
	const char first = name[ 0 ];
	if (!isalpha( (unsigned char)first ) && first != '_')
	{
		return false;
	}
	for (size_t i = 0; i < name.size(); ++i)
	{
		if (!isalnum( (unsigned char)name[ i ] ) && name[ i ] != '_')
		{
			return false;
		}
	}
	return true;
}


/**
 *	Pulls the name an engine message complains about. The parser messages
 *	quote the type, property or section they could not resolve; the file holds
 *	that name somewhere, and that is where the editor needs to point. Returns
 *	false when the message carries no usable name.
 */
bool findComplainedName( const BW::string & message, BW::string & name )
{
	// Quoted first: "Could not find MetaDataType 'NOTATYPE'".
	const size_t firstQuote = message.find_first_of( "'\"" );
	if (firstQuote != BW::string::npos)
	{
		const size_t close = message.find_first_of( "'\"", firstQuote + 1 );
		if (close != BW::string::npos && close > firstQuote + 1)
		{
			name = message.substr( firstQuote + 1, close - firstQuote - 1 );
			if (isPlausibleName( name ))
			{
				return true;
			}
		}
	}

	// Otherwise the first upper-case identifier run in the message.
	size_t i = 0;
	while (i < message.size())
	{
		if (isupper( (unsigned char)message[ i ] ))
		{
			const size_t start = i;
			while (i < message.size() &&
					(isalnum( (unsigned char)message[ i ] ) ||
					 message[ i ] == '_'))
			{
				++i;
			}
			name = message.substr( start, i - start );
			if (isPlausibleName( name ))
			{
				return true;
			}
		}
		else
		{
			++i;
		}
	}
	return false;
}


/**
 *	The file stem, used to turn a def path into the entity name the engine
 *	resolves it by.
 */
BW::string fileStem( const BW::string & path )
{
	const size_t slash = path.find_last_of( "/\\" );
	const size_t start = (slash == BW::string::npos) ? 0 : slash + 1;
	const size_t dot = path.find_last_of( '.' );
	if (dot == BW::string::npos || dot < start)
	{
		return path.substr( start );
	}
	return path.substr( start, dot - start );
}


/**
 *	The path of the res root that carries this def file: everything up to the
 *	last /scripts/ on the way, since a res tree is laid out with its scripts
 *	directory one level down. Returns an empty string when the file does not
 *	look like it is inside a res tree.
 */
BW::string resRootOf( const BW::string & absoluteDefPath )
{
	const BW::string scriptsMarker = "/scripts/";
	const size_t marker = absoluteDefPath.find( scriptsMarker );
	if (marker == BW::string::npos)
	{
		return "";
	}
	return absoluteDefPath.substr( 0, marker );
}


/**
 *	True when path is an existing directory. BWResource::init and postAddPaths
 *	answer a path that is not a directory with a CRITICAL message that aborts
 *	the process, which is the engine's own behaviour; a tool that takes a path
 *	on its command line wants to report that as a clean failure instead, so it
 *	checks here first.
 */
bool isDirectory( const BW::string & path )
{
	return NativeFileSystem::getAbsoluteFileType( path ) ==
		IFileSystem::FT_DIRECTORY;
}


/**
 *	A finding about the tree as a whole rather than a spot in a file, so it
 *	carries no range. The editor presents these as a list.
 */
BwLspDiagnostic makeTreeFinding( unsigned int severity,
	const BW::string & code, const BW::string & message )
{
	BwLspDiagnostic d;
	d.severity_ = severity;
	d.code_ = code;
	d.message_ = message;
	return d;
}

} // end anonymous namespace


// -----------------------------------------------------------------------------
// Section: BwLspMessageCapture
// -----------------------------------------------------------------------------

/**
 *	A DebugMessageCallback that keeps the engine's own messages while a def is
 *	parsed. handleMessage returns true so the message is consumed rather than
 *	printed to the console a second time: the caller decides how much of it to
 *	surface, which keeps the tool's output to just the findings.
 */
class BwLspMessageCapture::Callback : public DebugMessageCallback
{
public:
	explicit Callback( BW::vector< BW::string > & messages ) :
		messages_( messages ) {}

	bool handleMessage( DebugMessagePriority messagePriority,
		const char * pCategory, DebugMessageSource messageSource,
		const LogMetaData & metaData, const char * pFormat, va_list argPtr )
	{
		(void)pCategory;
		(void)messageSource;
		(void)metaData;

		char buffer[ 4096 ];
		va_list copy;
		bw_va_copy( copy, argPtr );
		vsnprintf( buffer, sizeof( buffer ), pFormat, copy );
		va_end( copy );
		buffer[ sizeof( buffer ) - 1 ] = '\0';

		messages_.push_back( buffer );

		// Consume: the caller turns these into findings. Not handling them
		// would print the same line to the console and also attach the error
		// backtrace, neither of which helps the editor.
		return true;
	}

private:
	BW::vector< BW::string > & messages_;
};


BwLspMessageCapture::BwLspMessageCapture() :
	pCallback_( NULL ),
	oldThreshold_( MESSAGE_PRIORITY_DEBUG )
{
}


void BwLspMessageCapture::begin()
{
	DebugFilter & filter = DebugFilter::instance();

	// Make sure the parse messages reach us even if the process default would
	// have filtered them, and remember what to restore.
	oldThreshold_ = filter.filterThreshold();
	filter.filterThreshold( MESSAGE_PRIORITY_TRACE );

	pCallback_ = new Callback( messages_ );
	filter.addMessageCallback( pCallback_, /*ignoreIfMsgLoggingIsDisabled*/ false );
}


void BwLspMessageCapture::end()
{
	DebugFilter & filter = DebugFilter::instance();
	if (pCallback_ != NULL)
	{
		filter.deleteMessageCallback( pCallback_ );
		delete pCallback_;
		pCallback_ = NULL;
	}
	filter.filterThreshold( oldThreshold_ );
}


// -----------------------------------------------------------------------------
// Section: BwLspSession
// -----------------------------------------------------------------------------

BwLspSession::BwLspSession() :
	entitiesXmlLoaded_( false )
{
}


BW::string BwLspSession::findResTree( const BW::string & startDir )
{
	BW::string dir = startDir;

	for (int level = 0; level < 10 && dir.length() > 1; ++level)
	{
		const BW::string candidate = dir + "/scripts/entities.xml";

		FILE * pFile = fopen( candidate.c_str(), "rb" );
		if (pFile != NULL)
		{
			fclose( pFile );
			return dir;
		}

		const size_t slash = dir.find_last_of( "/\\" );
		if (slash == BW::string::npos)
		{
			break;
		}
		dir = dir.substr( 0, slash );
	}

	return "";
}


bool BwLspSession::openResPaths( const BW::string & resPaths )
{
	BW::vector< BW::string > paths;

	size_t pos = 0;
	while (pos <= resPaths.size())
	{
		const size_t colon = resPaths.find( ':', pos );
		const BW::string path = resPaths.substr( pos,
			(colon == BW::string::npos) ? BW::string::npos : colon - pos );

		if (!path.empty())
		{
			paths.push_back( path );
		}
		if (colon == BW::string::npos)
		{
			break;
		}
		pos = colon + 1;
	}

	if (paths.empty())
	{
		return false;
	}

	// Checked before anything is handed to BWResource, so a mistyped --res
	// path is a clean false the caller can report (the CLI prints "could not
	// open --res paths") rather than the engine's CRITICAL abort.
	for (size_t i = 0; i < paths.size(); ++i)
	{
		if (!isDirectory( paths[ i ] ))
		{
			return false;
		}
	}

	if (BWResource::pInstance() == NULL)
	{
		// First exposure in this process: BWResource needs its full init so the
		// file system is built (addPath alone records a path but does not create
		// the MultiFileSystem openSection walks). A caller that already built a
		// BWResource (the unit-test harness does) lands in the addPath branch
		// instead and just extends the existing search path.
		BWResource::init( paths );
		for (size_t i = 0; i < paths.size(); ++i)
		{
			if (std::find( resRoots_.begin(), resRoots_.end(), paths[ i ] ) ==
				resRoots_.end())
			{
				resRoots_.push_back( paths[ i ] );
			}
		}
	}
	else
	{
		for (size_t i = 0; i < paths.size(); ++i)
		{
			if (std::find( resRoots_.begin(), resRoots_.end(), paths[ i ] ) ==
				resRoots_.end())
			{
				BWResource::addPath( paths[ i ].c_str() );
				resRoots_.push_back( paths[ i ] );
			}
		}
	}

	return true;
}


bool BwLspSession::openEntitiesXml()
{
	if (entitiesXmlLoaded_)
	{
		return true;
	}

	if (BWResource::pInstance() == NULL)
	{
		// No tree was opened. BWResource::openSection would answer this with a
		// singleton assertion that kills the process; a tool answers it as a
		// failed open instead.
		return false;
	}

	DataSectionPtr pEntities = BWResource::openSection(
		EntityDef::Constants::entitiesFile() );
	if (!pEntities)
	{
		return false;
	}

	if (!map_.parse( pEntities ))
	{
		return false;
	}

	entitiesXmlLoaded_ = true;
	return true;
}


bool BwLspSession::ensureResTreeFor( const BW::string & absoluteDefPath )
{
	const BW::string resRoot = resRootOf( absoluteDefPath );
	if (resRoot.empty())
	{
		// Not inside a scripts/ layout: nothing sensible to add.
		return false;
	}

	for (size_t i = 0; i < resRoots_.size(); ++i)
	{
		if (resRoots_[ i ] == resRoot)
		{
			return true;
		}
	}

	BWResource::addPath( resRoot.c_str() );
	resRoots_.push_back( resRoot );
	return true;
}


BW::string BwLspSession::schemaDigest()
{
	if (!openEntitiesXml())
	{
		return "";
	}

	// This is the same digest the server captures in EntityType::init (see
	// server/cellapp/entity_type.cpp) before any migrate: MD5 over the parsed
	// description map. The message ranges are not passed, which does not
	// change the digest: addToMD5 feeds the local method index, not the
	// range-assigned message id.
	MD5 md5;
	map_.addToMD5( md5 );

	MD5::Digest digest;
	md5.getDigest( digest );
	return digest.quote();
}


BW::vector< BwLspDiagnostic > BwLspSession::validateDef(
	const BW::string & absoluteDefPath )
{
	BW::vector< BwLspDiagnostic > diagnostics;

	if (!ensureResTreeFor( absoluteDefPath ))
	{
		BwLspDiagnostic d;
		d.severity_ = 2;
		d.code_ = "bw-lsp/no-res-root";
		d.message_ = "No res root found for this def file: nothing above it "
			"carries scripts/entities.xml.";
		diagnostics.push_back( d );
		return diagnostics;
	}

	const BW::string name = fileStem( absoluteDefPath );

	BW::string raw;
	const bool haveRaw = readWholeFile( absoluteDefPath, raw );

	BwLspMessageCapture capture;
	capture.begin();

	EntityDescription description;
	const bool parsed = description.parse( name, "", NULL );

	capture.end();

	if (parsed)
	{
		return diagnostics;
	}

	const BW::vector< BW::string > & messages = capture.messages();
	if (!haveRaw || messages.empty())
	{
		BwLspDiagnostic d;
		d.severity_ = 1;
		d.code_ = "bw-lsp/parse-failed";
		d.message_ = "The def file did not parse.";
		diagnostics.push_back( d );
		return diagnostics;
	}

	for (size_t i = 0; i < messages.size(); ++i)
	{
		const BW::string & message = messages[ i ];

		BwLspDiagnostic d;
		d.severity_ = 1;
		d.code_ = "bw-lsp/parse-failed";
		d.message_ = message;

		// Position the finding on the name the message complains about, so the
		// squiggle lands where the developer needs to look rather than at the
		// top of the file. The position is presentation, not validation: the
		// validation is the parse above.
		BW::string complained;
		if (findComplainedName( message, complained ))
		{
			size_t offset = findOutsideComments( raw, complained );
			if (offset == BW::string::npos)
			{
				offset = findOutsideComments( raw,
					BW::string( "<" ) + complained );
			}
			if (offset != BW::string::npos)
			{
				offsetToLineColumn( raw, offset,
					d.line_, d.column_ );
				offsetToLineColumn( raw, offset + complained.size(),
					d.endLine_, d.endColumn_ );
				if (d.endLine_ == d.line_ && d.endColumn_ <= d.column_)
				{
					d.endColumn_ = d.column_ + 1;
				}
			}
			else
			{
				d.endLine_ = 0;
				d.endColumn_ = 0;
			}
		}

		diagnostics.push_back( d );
	}

	return diagnostics;
}


bool BwLspSession::resolveDefinition( const BW::string & symbol,
	const BW::string & fromDefPath,
	BW::string & absolutePath,
	unsigned int & line, unsigned int & column )
{
	// An entity type name resolves to its def file.
	if (openEntitiesXml())
	{
		EntityTypeID index = 0;
		if (map_.nameToIndex( symbol, index ))
		{
			for (size_t i = 0; i < resRoots_.size(); ++i)
			{
				const BW::string candidate = resRoots_[ i ] +
					"/" + EntityDef::Constants::entitiesDefsPath() +
					"/" + symbol + ".def";

				FILE * pFile = fopen( candidate.c_str(), "rb" );
				if (pFile != NULL)
				{
					fclose( pFile );
					absolutePath = candidate;
					line = 0;
					column = 0;
					return true;
				}
			}
		}
	}

	// A type alias resolves to its definition in alias.xml.
	DataSectionPtr pAliases = BWResource::openSection(
		EntityDef::Constants::aliasesFile() );
	if (pAliases)
	{
		DataSectionPtr pAlias = pAliases->openSection( symbol );
		if (pAlias)
		{
			for (size_t i = 0; i < resRoots_.size(); ++i)
			{
				const BW::string candidate = resRoots_[ i ] +
					"/" + EntityDef::Constants::aliasesFile();

				BW::string raw;
				if (readWholeFile( candidate, raw ))
				{
					const size_t offset = findOutsideComments( raw,
						BW::string( "<" ) + symbol );
					if (offset != BW::string::npos)
					{
						offsetToLineColumn( raw, offset, line, column );
						absolutePath = candidate;
						return true;
					}
				}
			}
		}
	}

	// A property or method name inside the def the request came from resolves
	// to itself: it is defined where it is written.
	if (!fromDefPath.empty())
	{
		BW::string raw;
		if (readWholeFile( fromDefPath, raw ))
		{
			const size_t offset = findOutsideComments( raw,
				BW::string( "<" ) + symbol );
			if (offset != BW::string::npos)
			{
				offsetToLineColumn( raw, offset, line, column );
				absolutePath = fromDefPath;
				return true;
			}
		}
	}

	return false;
}


BW::vector< BwLspDiagnostic > BwLspSession::crossReference()
{
	BW::vector< BwLspDiagnostic > findings;

	if (BWResource::pInstance() == NULL)
	{
		findings.push_back( makeTreeFinding( 1, "bw-lsp/no-res-root",
			"No res tree is open: open one with --res before "
			"cross-referencing entities.xml against the def files." ) );
		return findings;
	}

	if (!openEntitiesXml())
	{
		findings.push_back( makeTreeFinding( 1, "bw-lsp/entities-unreadable",
			"entities.xml could not be parsed, so the def files cannot be "
			"cross-referenced against it." ) );
		return findings;
	}

	// The def files that exist. readDirectory walks every res root, so the
	// check sees the whole tree even when the session was opened on one root
	// and the defs live in another. The declared names come from the parsed
	// description map (the same list the server builds its entity types from)
	// rather than by walking entities.xml a second way: one implementation,
	// one list.
	MultiFileSystem * pFileSystem = BWResource::instance().fileSystem();
	IFileSystem::Directory dir;
	pFileSystem->readDirectory( dir, EntityDef::Constants::entitiesDefsPath() );

	BW::vector< BW::string > defFiles;
	for (size_t i = 0; i < dir.size(); ++i)
	{
		const BW::string & entry = dir[ i ];
		if (entry.size() > 4 && entry.substr( entry.size() - 4 ) == ".def")
		{
			defFiles.push_back( entry );
		}
	}

	// A def file nothing declares: the server never loads it, and it is
	// usually a typo in entities.xml or a def left behind by a rename. The
	// converse -- a declared entity type with no def file -- is not checked
	// here because it fails the entities.xml parse above, which surfaces as
	// the entities-unreadable finding (and as an empty schemaDigest); this
	// branch is the one thing no other surface reports.
	for (size_t i = 0; i < defFiles.size(); ++i)
	{
		EntityTypeID index = 0;
		if (!map_.nameToIndex( fileStem( defFiles[ i ] ), index ))
		{
			findings.push_back( makeTreeFinding( 2, "bw-lsp/orphan-def",
				"Def file '" + defFiles[ i ] + "' is not declared in "
				"entities.xml." ) );
		}
	}

	return findings;
}

BW_END_NAMESPACE

// bw_lsp.cpp
