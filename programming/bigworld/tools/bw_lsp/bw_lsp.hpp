// bw_lsp.hpp
//
// The engine side of the BigWorld VS Code extension.
//
// Def files, entities.xml and the schema digest are owned by
// programming/bigworld/lib/entitydef. This is the only place the editor side
// talks to that code, so the parsing and validation logic keeps one home and
// one version: nothing here is re-implemented in the extension. The unit
// tests under unit_test/ pin the behaviour below.

#ifndef BW_LSP_HPP
#define BW_LSP_HPP

#include "cstdmf/bw_namespace.hpp"
#include "cstdmf/bw_string.hpp"
#include "cstdmf/bw_vector.hpp"
#include "cstdmf/debug_message_priority.hpp"

#include "entitydef/entity_description_map.hpp"

BW_BEGIN_NAMESPACE

class BwLspSession;
class DebugMessageCallback;
class LogMetaData;

/**
 *	A finding in a def file. Positions are zero-based so they travel straight
 *	into the editor's range model; severity follows the editor's convention
 *	(1 error, 2 warning, 3 information, 4 hint).
 */
struct BwLspDiagnostic
{
	unsigned int line_;
	unsigned int endLine_;
	unsigned int column_;
	unsigned int endColumn_;
	unsigned int severity_;
	BW::string code_;
	BW::string message_;

	BwLspDiagnostic() :
		line_( 0 ), endLine_( 0 ),
		column_( 0 ), endColumn_( 0 ),
		severity_( 1 ) {}
};

/**
 *	Collects what the engine reports while it parses a def.
 *
 *	The parser reports its findings through the engine's own message system
 *	instead of returning them, and they are the text a developer expects to
 *	read: which type could not be found, which property is missing a section.
 *	Capturing them here turns them into findings instead of log lines.
 */
class BwLspMessageCapture
{
public:
	BwLspMessageCapture();

	/// Installs the capture into the engine's message system.
	void begin();
	/// Removes the capture again.
	void end();

	const BW::vector< BW::string > & messages() const	{ return messages_; }

private:
	class Callback;

	BW::vector< BW::string > messages_;
	Callback * pCallback_;
	DebugMessagePriority oldThreshold_;
};

/**
 *	The engine's view of one BigWorld resource tree.
 *
 *	One session per resource tree. The tree is opened once and reused for
 *	every question the editor asks about it.
 */
class BwLspSession
{
public:
	BwLspSession();

	/**
	 *	Walks up from startDir looking for a directory that carries
	 *	scripts/entities.xml, and returns that directory (the res root).
	 *	Returns an empty path when no ancestor has one.
	 */
	static BW::string findResTree( const BW::string & startDir );

	/**
	 *	Exposes a resource tree. Takes the colon-separated res root list the
	 *	engine's own --res argument uses.
	 */
	bool openResPaths( const BW::string & resPaths );

	/**
	 *	The schema digest of the opened tree: MD5 over the parsed
	 *	entities.xml description map. This is byte-identical to what a server
	 *	reports as BigWorld.schemaDigest() on the same tree, because it walks
	 *	the same addToMD5() the EntityType::init digest uses. Returns an empty
	 *	string when the tree could not be opened or parsed.
	 */
	BW::string schemaDigest();

	/**
	 *	Validates one def file. Returns nothing when the engine accepts it,
	 *	otherwise the engine's own findings, located in the file.
	 */
	BW::vector< BwLspDiagnostic > validateDef(
		const BW::string & absoluteDefPath );

	/**
	 *	Resolves a name to a file position. An entity type name points at its
	 *	def file, a type alias at alias.xml, and a property name at the
	 *	property inside the def the request came from. Returns false when the
	 *	name is not one of these.
	 */
	bool resolveDefinition( const BW::string & symbol,
		const BW::string & fromDefPath,
		BW::string & absolutePath,
		unsigned int & line, unsigned int & column );

private:
	bool ensureResTreeFor( const BW::string & absoluteDefPath );
	bool openEntitiesXml();

	bool entitiesXmlLoaded_;
	EntityDescriptionMap map_;
	BW::vector< BW::string > resRoots_;
};

BW_END_NAMESPACE

#endif // BW_LSP_HPP
