// test_entity_description.cpp
//
// Coverage batch 29: the .def XML loading surface of entity_description.cpp.
//
// The file was registered as unreachable on the grounds that it needed "the
// EntityType / EntityDef XML loader suite". That does not hold: EntityDescription
// has a public default constructor and EntityDescription::parse needs only
// BWResource to open a .def section - entity_description.cpp never references
// EntityType. The fixture .def files are written into a pid-unique temp tree
// and exposed to BWResource with addPath(), so no entity_defs tree needs to
// live in the repository. The harness (TEST_F fixture) constructs a fresh
// BWResource per test, so addPath runs in every test while the files are
// written only once.
//
// Parsing is driven through the public entry points only (parse, parseService,
// supersede, addSectionToStream, addSectionToDictionary, readStreamToDict,
// hasComponent, checkExposedMethods), which reach the whole protected/private
// tree underneath (parseInterface -> parseProperties, parseMethods,
// parseTempProperties, parseComponents).

#include "pch.hpp"

#include "test_harness.hpp"

#include "entitydef/constants.hpp"
#include "entitydef/entity_description.hpp"
#include "entitydef/method_description.hpp"
#include "entitydef/script_data_source.hpp"

#include "resmgr/bwresource.hpp"
#include "resmgr/multi_file_system.hpp"
#include "resmgr/xml_section.hpp"

#include "cstdmf/bw_string.hpp"
#include "cstdmf/bw_util.hpp"
#include "cstdmf/md5.hpp"
#include "network/exposed_message_range.hpp"

#include <sys/stat.h>
#include <unistd.h>


BW_BEGIN_NAMESPACE

namespace
{

const char FIXTURE_DIR[] = "scripts/entity_defs";
const char SERVICES_DIR[] = "scripts/service_defs";
const char COMPONENTS_DIR[] = "scripts/component_defs";
const char INTERFACES_DIR[] = "scripts/entity_defs/interfaces";
const char CELL_SCRIPTS_DIR[] = "scripts/cell";
const char BASE_SCRIPTS_DIR[] = "scripts/base";
const char CLIENT_SCRIPTS_DIR[] = "scripts/client";


char g_fixtureRoot[ BW_MAX_PATH + 1 ];


/**
 *	Recursively creates the directories under a path.
 */
void makeDirs( const char * path )
{
	BW::string partial( path );

	for (BW::string::size_type pos = partial.find( '/', 1 );
		 pos != BW::string::npos; pos = partial.find( '/', pos + 1 ))
	{
		char partialPath[ BW_MAX_PATH + 1 ];
		strncpy( partialPath, partial.substr( 0, pos ).c_str(),
			sizeof( partialPath ) - 1 );
		partialPath[ sizeof( partialPath ) - 1 ] = '\0';

		struct stat st;
		if (stat( partialPath, &st ) != 0)
		{
			mkdir( partialPath, 0755 );
		}
	}

	struct stat st;
	if (stat( path, &st ) != 0)
	{
		mkdir( path, 0755 );
	}
}


/**
 *	Writes a fixture file under the temp tree, creating directories as needed.
 */
void writeFixtureFile( const BW::string & relPath, const char * content )
{
	BW::string path = g_fixtureRoot;
	path += "/";
	path += relPath.c_str();

	BW::string parent = path.substr( 0, path.rfind( '/' ) );
	makeDirs( parent.c_str() );

	FILE * pFile = fopen( path.c_str(), "wb" );
	MF_VERIFY( pFile != NULL );

	fputs( content, pFile );
	fclose( pFile );
}


/**
 *	Writes a non-empty python script so the HasScriptOrTagDistributionDecider
 *	finds a script for the named entity.
 */
void writeEntityScripts( const char * entityName )
{
	const char content[] = "# fixture entity script\n";

	writeFixtureFile(
		BW::string( CELL_SCRIPTS_DIR ) + "/" + entityName + ".py", content );
	writeFixtureFile(
		BW::string( BASE_SCRIPTS_DIR ) + "/" + entityName + ".py", content );
	writeFixtureFile(
		BW::string( CLIENT_SCRIPTS_DIR ) + "/" + entityName + ".py", content );
}


/**
 *	Installs the fixture tree and exposes it to BWResource. The harness
 *	constructor makes a fresh BWResource instance for every test, so addPath
 *	has to run each time; the fixture tree itself is created once per process.
 */
void installFixtures()
{
	static bool filesWritten = false;

	if (!filesWritten)
	{
		bw_snprintf( g_fixtureRoot, ARRAY_SIZE( g_fixtureRoot ),
			"/tmp/bwentitydef_%d", (int)getpid() );

		BW::string removeCmd = "rm -rf \"";
		removeCmd += g_fixtureRoot;
		removeCmd += "\"";
		int removeResult = system( removeCmd.c_str() );
		(void)removeResult;

		makeDirs( g_fixtureRoot );

		filesWritten = true;
	}

	BWResource::addPath( g_fixtureRoot );
}


/**
 *	Fills both schema digests of a description: the full entity digest
 *	(addToMD5) and the persistent-property digest
 *	(addPersistentPropertiesToMD5). The two cover different member subsets,
 *	which the MD5 section below pins down through before/after fixtures.
 */
void digestsOf( const EntityDescription & description,
	MD5::Digest & full, MD5::Digest & persistent )
{
	MD5 md5;

	description.addToMD5( md5 );
	full = MD5::Digest( md5 );

	MD5 persistentMd5;

	description.addPersistentPropertiesToMD5( persistentMd5 );
	persistent = MD5::Digest( persistentMd5 );
}


/**
 *	Writes a def, parses it and fills both schema digests. Returns the parse
 *	result so callers can CHECK it.
 */
bool parseAndDigest( const char * entityName, const char * defXml,
	MD5::Digest & full, MD5::Digest & persistent )
{
	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/" + entityName + ".def",
		defXml );

	EntityDescription description;
	const bool parsed = description.parse( entityName, "", NULL );

	if (parsed)
	{
		digestsOf( description, full, persistent );
	}

	return parsed;
}


/**
 *	Re-parses a def whose bytes were replaced since the previous parse.
 *	BWResource::openSection answers from DataSectionCensus before the
 *	filesystem, so the rewritten def has to be evicted first.
 */
void reparseAndDigest( const char * entityName, const char * defXml,
	MD5::Digest & full, MD5::Digest & persistent )
{
	BWResource::instance().purgeAll();

	const bool parsed = parseAndDigest( entityName, defXml,
		full, persistent );
	MF_VERIFY( parsed );
}


} // end anonymous namespace


/**
 *	A .def file written to the process-unique temp tree is reachable through
 *	BWResource and parses, so the rest of this suite can drive
 *	EntityDescription::parse without an entity_defs tree in the repository.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_parseMinimal )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29Probe.def",
		"<root/>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch29Probe", "", NULL ) );
	CHECK_EQUAL( 0, strcmp( "batch29Probe", description.name().c_str() ) );
	CHECK_EQUAL( 0, description.propertyCount() );
	CHECK_EQUAL( false, description.isService() );
	CHECK_EQUAL( false, description.hasComponents() );

	// With no decider the distribution flags keep their constructor defaults:
	// an entity is assumed to live everywhere.
	CHECK_EQUAL( true, description.canBeOnCell() );
	CHECK_EQUAL( true, description.canBeOnBase() );
	CHECK_EQUAL( true, description.canBeOnClient() );
}


/**
 *	The scalar entity flags of a def land where the description's accessors
 *	read them: Persistent, ExplicitDatabaseID and AppealRadius.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_parseScalarFlags )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29Full.def",
		"<root>\n"
		"	<Persistent>false</Persistent>\n"
		"	<ExplicitDatabaseID>true</ExplicitDatabaseID>\n"
		"	<AppealRadius>3.5</AppealRadius>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch29Full", "", NULL ) );
	CHECK_EQUAL( 0, strcmp( "batch29Full", description.name().c_str() ) );
	CHECK_EQUAL( 0, description.propertyCount() );
	CHECK_EQUAL( false, description.isPersistent() );
	CHECK_EQUAL( true, description.forceExplicitDBID() );
	CHECK( almostEqual( description.appealRadius(), 3.5f ) );
	CHECK_EQUAL( false, description.shouldSendDetailedPosition() );
	CHECK_EQUAL( false, description.isManualAoI() );
}


/**
 *	A missing .def file fails the parse and leaves the description empty.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_parseMissingDefFails )
{
	installFixtures();

	EntityDescription description;

	CHECK( !description.parse( "batch29Nonexistent", "",
		NULL ) );
	CHECK_EQUAL( 0, description.propertyCount() );
}


/**
 *	Properties parse with their flags: CELL_PUBLIC is cell data and ghosted,
 *	BASE is base data, ALL_CLIENTS is other-client data with an event stamp.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_parseProperties )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29Props.def",
		"<root>\n"
		"	<Properties>\n"
		"		<hp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"		</hp>\n"
		"		<owner>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>BASE</Flags>\n"
		"			<Persistent>true</Persistent>\n"
		"		</owner>\n"
		"		<nick>\n"
		"			<Type>STRING</Type>\n"
		"			<Flags>ALL_CLIENTS</Flags>\n"
		"		</nick>\n"
		"	</Properties>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch29Props", "", NULL ) );
	CHECK_EQUAL( 3, description.propertyCount() );

	DataDescription * pHp = description.property( 0 );
	CHECK( pHp != NULL );
	CHECK_EQUAL( 0, strcmp( "hp", pHp->name().c_str() ) );
	CHECK( pHp->isGhostedData() );
	CHECK( !pHp->isBaseData() );

	DataDescription * pOwner = description.property( 1 );
	CHECK( pOwner != NULL );
	CHECK_EQUAL( 0, strcmp( "owner", pOwner->name().c_str() ) );
	CHECK( pOwner->isBaseData() );
	CHECK( !pOwner->isGhostedData() );
	CHECK( pOwner->isPersistent() );

	DataDescription * pNick = description.property( 2 );
	CHECK( pNick != NULL );
	CHECK_EQUAL( 0, strcmp( "nick", pNick->name().c_str() ) );
	CHECK( pNick->isOtherClientData() );
	// Other-client data is event stamped, and the stamp index is allocated in
	// parse order.
	CHECK_EQUAL( 0, pNick->eventStampIndex() );
}


/**
 *	An invalid property flags string fails the whole parse.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_parseBadPropertyFails )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29BadFlags.def",
		"<root>\n"
		"	<Properties>\n"
		"		<hp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>NOT_A_REAL_FLAG</Flags>\n"
		"		</hp>\n"
		"	</Properties>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( !description.parse( "batch29BadFlags", "", NULL ) );
	CHECK_EQUAL( 0, description.propertyCount() );
}


/**
 *	Methods parse into their per-component lists, and exposed methods are
 *	counted separately.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_parseMethods )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29Methods.def",
		"<root>\n"
		"	<BaseMethods>\n"
		"		<basePing>\n"
		"			<Arg>INT32</Arg>\n"
		"		</basePing>\n"
		"		<baseExposed>\n"
		"			<Exposed/>\n"
		"		</baseExposed>\n"
		"	</BaseMethods>\n"
		"	<CellMethods>\n"
		"		<cellTick/>\n"
		"	</CellMethods>\n"
		"	<ClientMethods>\n"
		"		<clientNotify>\n"
		"			<Arg>FLOAT32</Arg>\n"
		"		</clientNotify>\n"
		"	</ClientMethods>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch29Methods", "", NULL ) );

	CHECK_EQUAL( 2, description.base().size() );
	CHECK_EQUAL( 0, strcmp( "basePing",
		description.base().internalMethod( 0 )->name().c_str() ) );
	// baseExposed carries an empty <Exposed/> tag.
	CHECK_EQUAL( 1, description.base().exposedSize() );

	CHECK_EQUAL( 1, description.cell().size() );
	CHECK_EQUAL( 0, strcmp( "cellTick",
		description.cell().internalMethod( 0 )->name().c_str() ) );

	CHECK_EQUAL( 1, description.client().size() );
	CHECK_EQUAL( 0, strcmp( "clientNotify",
		description.client().internalMethod( 0 )->name().c_str() ) );
}


/**
 *	A Parent tag parses the parent def into the same description first, so the
 *	child inherits its properties.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_parseParentInheritance )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29Parent.def",
		"<root>\n"
		"	<Properties>\n"
		"		<baseHp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"		</baseHp>\n"
		"	</Properties>\n"
		"</root>\n" );

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29Child.def",
		"<root>\n"
		"	<Parent>batch29Parent</Parent>\n"
		"	<Properties>\n"
		"		<childHp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"		</childHp>\n"
		"	</Properties>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch29Child", "", NULL ) );
	CHECK_EQUAL( 2, description.propertyCount() );
	CHECK_EQUAL( 0, strcmp( "baseHp",
		description.property( 0 )->name().c_str() ) );
	CHECK_EQUAL( 0, strcmp( "childHp",
		description.property( 1 )->name().c_str() ) );
}


/**
 *	A Parent naming an unparsable def fails the child's parse too.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_parseMissingParentFails )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29Orphan.def",
		"<root>\n"
		"	<Parent>batch29NoSuchParent</Parent>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( !description.parse( "batch29Orphan", "", NULL ) );
}


/**
 *	Volatile priorities parse (and are squared by VolatileInfo::asPriority),
	and the appeal radius information branch is reached without incident.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_parseVolatileInfo )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29Volatile.def",
		"<root>\n"
		"	<Volatile>\n"
		"		<position>2</position>\n"
		"		<yaw>2</yaw>\n"
		"		<pitch>1</pitch>\n"
		"		<roll>0</roll>\n"
		"	</Volatile>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch29Volatile", "", NULL ) );

	const VolatileInfo & volatileInfo = description.volatileInfo();
	CHECK( volatileInfo.shouldSendPosition() );
	// Priorities arrive as value * value (VolatileInfo::asPriority).
	CHECK( almostEqual( 4.f, volatileInfo.positionPriority() ) );
	CHECK( almostEqual( 4.f, volatileInfo.yawPriority() ) );

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29NoVolatile.def",
		"<root>\n"
		"	<Volatile>\n"
		"		<yaw>1</yaw>\n"
		"		<pitch>2</pitch>\n"
		"	</Volatile>\n"
		"</root>\n" );

	EntityDescription invalid;

	// yaw < pitch is rejected by VolatileInfo::parse.
	CHECK( !invalid.parse( "batch29NoVolatile", "", NULL ) );
}


/**
 *	LoD levels parse and other-client properties resolve their DetailLevel
 *	label against them; a property without a DetailLevel tag is outer level,
 *	which a decider-driven parse converts to the outermost level index.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_parseLoDLevels )
{
	installFixtures();

	writeEntityScripts( "batch29Lod" );

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29Lod.def",
		"<root>\n"
		"	<LoDLevels>\n"
		"		<Level>100.0\n"
		"			<label>near</label>\n"
		"		</Level>\n"
		"	</LoDLevels>\n"
		"	<Properties>\n"
		"		<nick>\n"
		"			<Type>STRING</Type>\n"
		"			<Flags>ALL_CLIENTS</Flags>\n"
		"			<DetailLevel>near</DetailLevel>\n"
		"		</nick>\n"
		"		<farNick>\n"
		"			<Type>STRING</Type>\n"
		"			<Flags>OTHER_CLIENTS</Flags>\n"
		"		</farNick>\n"
		"	</Properties>\n"
		"</root>\n" );

	EntityDescription description;
	HasScriptOrTagDistributionDecider decider;

	CHECK( description.parse( "batch29Lod", "", &decider ) );

	CHECK_EQUAL( 2, description.lodLevels().size() );
	CHECK_EQUAL( 2, description.propertyCount() );

	DataDescription * pNick = description.property( 0 );
	CHECK( pNick != NULL );
	// 'near' is the inner level, index 0 after the levels are sorted.
	CHECK_EQUAL( 0, pNick->detailLevel() );

	// With a decider the distribution flags come from the scripts the fixture
	// wrote, so all three components are available.
	CHECK_EQUAL( true, description.canBeOnCell() );
	CHECK_EQUAL( true, description.canBeOnBase() );
	CHECK_EQUAL( true, description.canBeOnClient() );
	// canBeOnClient fills an empty client name with the entity name.
	CHECK_EQUAL( true, description.isClientType() );
}


/**
 *	A DetailLevel label that matches no LoD level fails the parse.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_parseBadDetailLevelFails )
{
	installFixtures();

	writeEntityScripts( "batch29BadLod" );

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29BadLod.def",
		"<root>\n"
		"	<Properties>\n"
		"		<nick>\n"
		"			<Type>STRING</Type>\n"
		"			<Flags>ALL_CLIENTS</Flags>\n"
		"			<DetailLevel>nonexistent</DetailLevel>\n"
		"		</nick>\n"
		"	</Properties>\n"
		"</root>\n" );

	EntityDescription description;
	HasScriptOrTagDistributionDecider decider;

	CHECK( !description.parse( "batch29BadLod", "", &decider ) );
}


/**
 *	Distribution tags override script-existence checks, and an entity without
 *	a cell distribution keeps its cell data only as a warning.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_parseDistributionTags )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29Dist.def",
		"<root>\n"
		"	<ClientName>batch29DistClient</ClientName>\n"
		"	<Distribution>\n"
		"		<Cell>false</Cell>\n"
		"		<Base>true</Base>\n"
		"		<Client>true</Client>\n"
		"	</Distribution>\n"
		"	<Properties>\n"
		"		<hp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"		</hp>\n"
		"	</Properties>\n"
		"</root>\n" );

	EntityDescription description;
	HasScriptOrTagDistributionDecider decider;

	CHECK( description.parse( "batch29Dist", "", &decider ) );

	CHECK_EQUAL( false, description.canBeOnCell() );
	CHECK_EQUAL( true, description.canBeOnBase() );
	CHECK_EQUAL( true, description.canBeOnClient() );
	// The ClientName tag survives because the entity can be on the client.
	CHECK_EQUAL( 0, strcmp( "batch29DistClient",
		description.clientName().c_str() ) );
	CHECK_EQUAL( false, description.isClientType() );
	// The cell property is still parsed, only warned about.
	CHECK_EQUAL( 1, description.propertyCount() );
}


/**
 *	Client methods on an entity that cannot be on the client fail the parse.
 */
TEST_F( EntityDefUnitTestHarness,
	EntityDescription_parseClientMethodsWithoutClientFails )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29NoClient.def",
		"<root>\n"
		"	<Distribution>\n"
		"		<Client>false</Client>\n"
		"	</Distribution>\n"
		"	<ClientMethods>\n"
		"		<clientNotify/>\n"
		"	</ClientMethods>\n"
		"</root>\n" );

	EntityDescription description;
	HasScriptOrTagDistributionDecider decider;

	CHECK( !description.parse( "batch29NoClient", "",
		&decider ) );
}


/**
 *	With no scripts anywhere the decider keeps only the clientless, baseless
 *	and cellless variant of every flag, and the client name is cleared.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_parseDeciderNoScripts )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29NoScripts.def",
		"<root>\n"
		"	<ClientName>batch29NoScriptsClient</ClientName>\n"
		"	<Properties>\n"
		"		<hp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"		</hp>\n"
		"	</Properties>\n"
		"	</root>\n" );

	EntityDescription description;
	HasScriptOrTagDistributionDecider decider;

	CHECK( description.parse( "batch29NoScripts", "",
		&decider ) );

	CHECK_EQUAL( false, description.canBeOnCell() );
	CHECK_EQUAL( false, description.canBeOnBase() );
	CHECK_EQUAL( false, description.canBeOnClient() );
	// A clientless entity has no client name.
	CHECK_EQUAL( 0u, description.clientName().size() );
}


/**
 *	TempProperties parse without disturbing the persistent property list.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_parseTempProperties )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29Temp.def",
		"<root>\n"
		"	<TempProperties>\n"
		"		<scratch>\n"
		"			<Type>UINT16</Type>\n"
		"		</scratch>\n"
		"	</TempProperties>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch29Temp", "", NULL ) );
	// Temp properties are recorded by name only, they do not become
	// persistent properties.
	CHECK_EQUAL( 0, description.propertyCount() );
}


/**
 *	Components parse from scripts/component_defs, are recorded by name and
 *	contribute their properties to the entity.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_parseComponents )
{
	installFixtures();

	writeFixtureFile( BW::string( COMPONENTS_DIR ) + "/batch29Comp.def",
		"<root>\n"
		"	<Properties>\n"
		"		<compProp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"		</compProp>\n"
		"	</Properties>\n"
		"</root>\n" );

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29HasComp.def",
		"<root>\n"
		"	<Components>\n"
		"		<batch29Comp/>\n"
		"	</Components>\n"
		"	<Properties>\n"
		"		<hp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"		</hp>\n"
		"	</Properties>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch29HasComp", "", NULL ) );
	CHECK_EQUAL( true, description.hasComponents() );
	CHECK_EQUAL( true, description.hasComponent( "batch29Comp" ) );
	CHECK_EQUAL( false, description.hasComponent( "noSuchComponent" ) );

	// The entity's own properties come first, then the component properties.
	CHECK_EQUAL( 2, description.propertyCount() );
	CHECK_EQUAL( 0, strcmp( "hp",
		description.property( 0 )->name().c_str() ) );

	DataDescription * pCompProp = description.property( 1 );
	CHECK( pCompProp != NULL );
	CHECK_EQUAL( 0, strcmp( "compProp", pCompProp->name().c_str() ) );
	CHECK( pCompProp->isComponentised() );
	CHECK_EQUAL( 0, strcmp( "batch29Comp",
		pCompProp->componentName().c_str() ) );
}


/**
 *	A component naming a def file that does not exist fails the parse.
 */
TEST_F( EntityDefUnitTestHarness,
	EntityDescription_parseComponentsMissingDefFails )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29GhostComp.def",
		"<root>\n"
		"	<Components>\n"
		"		<batch29NoComponent/>\n"
		"	</Components>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( !description.parse( "batch29GhostComp", "", NULL ) );
}


/**
 *	Two components with the same alias in one entity fail the parse.
 */
TEST_F( EntityDefUnitTestHarness,
	EntityDescription_parseComponentsDuplicateFails )
{
	installFixtures();

	writeFixtureFile( BW::string( COMPONENTS_DIR ) + "/batch29Comp.def",
		"<root>\n"
		"	<Properties>\n"
		"		<compProp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"		</compProp>\n"
		"	</Properties>\n"
		"</root>\n" );

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29DupComp.def",
		"<root>\n"
		"	<Components>\n"
		"		<batch29Comp/>\n"
		"		<batch29Comp/>\n"
		"	</Components>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( !description.parse( "batch29DupComp", "", NULL ) );
}


/**
 *	Services parse from scripts/service_defs with Methods instead of the
 *	per-component method lists, never carry properties and are never
 *	persistent.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_parseService )
{
	installFixtures();

	writeFixtureFile( BW::string( SERVICES_DIR ) + "/batch29Svc.def",
		"<root>\n"
		"	<Persistent>true</Persistent>\n"
		"	<Properties>\n"
		"		<hp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"		</hp>\n"
		"	</Properties>\n"
		"	<Methods>\n"
		"		<serviceTick/>\n"
		"	</Methods>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parseService( "batch29Svc", "", NULL ) );
	CHECK_EQUAL( true, description.isService() );
	// Services do not support properties and are forced non-persistent.
	CHECK_EQUAL( 0, description.propertyCount() );
	CHECK_EQUAL( false, description.isPersistent() );
	// Service methods land in the base list (isService_ maps Methods to the
	// base method list).
	CHECK_EQUAL( 1, description.base().size() );
	CHECK_EQUAL( 0, strcmp( "serviceTick",
		description.base().internalMethod( 0 )->name().c_str() ) );
}


/**
 *	A service that cannot run on a base fails the parse: the service script is
 *	what implements it.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_parseServiceNoBaseFails )
{
	installFixtures();

	writeFixtureFile( BW::string( SERVICES_DIR ) + "/batch29NoBaseSvc.def",
		"<root>\n"
		"	<Methods>\n"
		"		<serviceTick/>\n"
		"	</Methods>\n"
		"</root>\n" );

	EntityDescription description;
	HasScriptOrTagDistributionDecider decider;

	// No service script fixture exists, so the decider says no base.
	CHECK( !description.parseService( "batch29NoBaseSvc", "",
		&decider ) );
}


/**
 *	An Implements section pulls properties in from the shared interfaces
 *	directory.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_parseImplementsInterface )
{
	installFixtures();

	writeFixtureFile( BW::string( INTERFACES_DIR ) + "/batch29Iface.def",
		"<root>\n"
		"	<Properties>\n"
		"		<ifaceProp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"		</ifaceProp>\n"
		"	</Properties>\n"
		"</root>\n" );

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29Impl.def",
		"<root>\n"
		"	<Implements>\n"
		"		<Interface>batch29Iface</Interface>\n"
		"	</Implements>\n"
		"	<Properties>\n"
		"		<hp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"		</hp>\n"
		"	</Properties>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch29Impl", "", NULL ) );
	CHECK_EQUAL( 2, description.propertyCount() );
	CHECK_EQUAL( 0, strcmp( "ifaceProp",
		description.property( 0 )->name().c_str() ) );
}


/**
 *	An exposed base method is only legal on a proxy entity.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_checkExposedMethodsProxy )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29Exposed.def",
		"<root>\n"
		"	<BaseMethods>\n"
		"		<baseExposed>\n"
		"			<Exposed/>\n"
		"		</baseExposed>\n"
		"	</BaseMethods>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch29Exposed", "", NULL ) );
	CHECK_EQUAL( 1, description.base().exposedSize() );

	// Non-proxy entities may not expose base methods at all.
	CHECK( !description.checkExposedMethods( false ) );
	// A proxy may expose them with the default flag.
	CHECK( description.checkExposedMethods( true ) );
}


/**
 *	A cell method exposed with the default flag is only legal on non-proxy
 *	entities; proxies need an explicit OWN_CLIENT or ALL_CLIENTS flag.
 */
TEST_F( EntityDefUnitTestHarness,
	EntityDescription_checkExposedCellMethodsNonProxy )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29CellExp.def",
		"<root>\n"
		"	<CellMethods>\n"
		"		<cellExposed>\n"
		"			<Exposed/>\n"
		"		</cellExposed>\n"
		"	</CellMethods>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch29CellExp", "", NULL ) );
	CHECK_EQUAL( 1, description.cell().exposedSize() );

	// Default exposure is fine for a non-proxy entity.
	CHECK( description.checkExposedMethods( false ) );
	// A proxy needs explicit OWN_CLIENT/ALL_CLIENTS flags.
	CHECK( !description.checkExposedMethods( true ) );

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29CellOwn.def",
		"<root>\n"
		"	<CellMethods>\n"
		"		<cellOwnClient>\n"
		"			<Exposed>OWN_CLIENT</Exposed>\n"
		"		</cellOwnClient>\n"
		"	</CellMethods>\n"
		"</root>\n" );

	EntityDescription ownClient;

	CHECK( ownClient.parse( "batch29CellOwn", "", NULL ) );
	// OWN_CLIENT exposure is invalid for a non-proxy entity.
	CHECK( !ownClient.checkExposedMethods( false ) );
	CHECK( ownClient.checkExposedMethods( true ) );
}


/**
 *	The static StreamContentType-to-data-domains mapping.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_getDataDomains )
{
	CHECK_EQUAL( EntityDescription::BASE_DATA,
		EntityDescription::getDataDomains( BASE_ENTITY_BACKUP ) );
	CHECK_EQUAL( EntityDescription::BASE_DATA,
		EntityDescription::getDataDomains( BASE_ENTITY_OFFLOAD ) );
	CHECK_EQUAL( EntityDescription::CELL_DATA,
		EntityDescription::getDataDomains( CELL_CREATION ) );
	CHECK_EQUAL(
		EntityDescription::CELL_DATA |
			EntityDescription::ONLY_OTHER_CLIENT_DATA,
		EntityDescription::getDataDomains( CELL_GHOST_CREATION ) );
	CHECK_EQUAL( EntityDescription::FROM_BASE_TO_CLIENT_DATA,
		EntityDescription::getDataDomains( CLIENT_PLAYER_CREATION ) );
	CHECK_EQUAL(
		EntityDescription::BASE_DATA |
			EntityDescription::ONLY_PERSISTENT_DATA,
		EntityDescription::getDataDomains( DATABASE_ENTITY ) );
}


/**
 *	The pass-selection statics: base-only data is streamed in pass 0 and only
 *	when the domains include BASE_DATA.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_shouldSkipPass )
{
	const int BASE_DOMAINS = EntityDescription::EXACT_MATCH |
		EntityDescription::BASE_DATA;

	CHECK( !EntityDescription::shouldSkipPass( 0, BASE_DOMAINS ) );
	CHECK( EntityDescription::shouldSkipPass( 1, BASE_DOMAINS ) );
	CHECK( EntityDescription::shouldSkipPass( 2, BASE_DOMAINS ) );
	CHECK( EntityDescription::shouldSkipPass( 3, BASE_DOMAINS ) );

	// With EXACT_MATCH a domain set runs only the pass whose jumper pattern
	// matches it exactly.
	const int PASS_DOMAINS[4] =
	{
		EntityDescription::EXACT_MATCH | EntityDescription::BASE_DATA,
		EntityDescription::EXACT_MATCH | EntityDescription::BASE_DATA |
			EntityDescription::CLIENT_DATA,
		EntityDescription::EXACT_MATCH | EntityDescription::CELL_DATA |
			EntityDescription::CLIENT_DATA,
		EntityDescription::EXACT_MATCH | EntityDescription::CELL_DATA
	};

	for (int want = 0; want < 4; want++)
	{
		for (int pass = 0; pass < 4; pass++)
		{
			bool skip = EntityDescription::shouldSkipPass( pass,
				PASS_DOMAINS[ want ] );

			CHECK_EQUAL( pass != want, skip );
		}
	}

	// Without EXACT_MATCH a pass runs when any of its flag bits is set.
	const int CELL_ONLY = EntityDescription::CELL_DATA;

	CHECK( EntityDescription::shouldSkipPass( 0, CELL_ONLY ) );
	CHECK( EntityDescription::shouldSkipPass( 1, CELL_ONLY ) );
	CHECK( !EntityDescription::shouldSkipPass( 2, CELL_ONLY ) );
	CHECK( !EntityDescription::shouldSkipPass( 3, CELL_ONLY ) );
}


/**
 *	The pass-content statics: a BASE property is considered in the base
 *	passes, and ONLY_PERSISTENT_DATA drops non-persistent properties.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_shouldConsiderData )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29Consider.def",
		"<root>\n"
		"	<Properties>\n"
		"		<owner>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>BASE</Flags>\n"
		"			<Persistent>true</Persistent>\n"
		"		</owner>\n"
		"		<scratch>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>BASE</Flags>\n"
		"		</scratch>\n"
		"	</Properties>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch29Consider", "", NULL ) );
	CHECK_EQUAL( 2, description.propertyCount() );

	const DataDescription * pOwner = description.property( 0 );
	const DataDescription * pScratch = description.property( 1 );

	const int BASE_DOMAINS = EntityDescription::EXACT_MATCH |
		EntityDescription::BASE_DATA;

	// Pass 0 is base-and-not-client.
	CHECK( EntityDescription::shouldConsiderData( 0, pOwner, BASE_DOMAINS ) );
	CHECK( EntityDescription::shouldConsiderData( 0, pScratch,
		BASE_DOMAINS ) );

	// The persistent-only filter keeps the persistent property and drops the
	// volatile one.
	const int PERSISTENT_ONLY = BASE_DOMAINS |
		EntityDescription::ONLY_PERSISTENT_DATA;

	CHECK( EntityDescription::shouldConsiderData( 0, pOwner,
		PERSISTENT_ONLY ) );
	CHECK( !EntityDescription::shouldConsiderData( 0, pScratch,
		PERSISTENT_ONLY ) );
}


/**
 *	Property values stream from a value section to a binary stream and read
 *	back into a Python dict with the same values.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_streamRoundTrip )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29Stream.def",
		"<root>\n"
		"	<Properties>\n"
		"		<hp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>ALL_CLIENTS</Flags>\n"
		"		</hp>\n"
		"	</Properties>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch29Stream", "", NULL ) );

	BW::stringstream valueXML;
	valueXML << "<root>\n	<hp>77</hp>\n</root>\n";

	XMLSectionPtr pValues = XMLSection::createFromStream( "",
		valueXML );
	CHECK( pValues != NULL );

	const int domains = EntityDescription::FROM_CELL_TO_CLIENT_DATA;

	MemoryOStream os;
	CHECK( description.addSectionToStream( pValues, os, domains ) );
	CHECK( os.size() > 0 );

	ScriptDict dict = ScriptDict::create();
	MemoryIStream is( os.data(), os.size() );
	CHECK( description.readStreamToDict( is, domains, dict ) );

	ScriptObject pHp = dict.getItem( "hp", ScriptErrorPrint() );
	CHECK( pHp != NULL );
	CHECK( ScriptInt::check( pHp ) );
	CHECK_EQUAL( 77, ScriptInt( pHp.get() ).asLong() );
}


/**
 *	Property values also go straight from a value section into a Python dict.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_addSectionToDictionary )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29Dict.def",
		"<root>\n"
		"	<Properties>\n"
		"		<hp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>ALL_CLIENTS</Flags>\n"
		"		</hp>\n"
		"	</Properties>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch29Dict", "", NULL ) );

	BW::stringstream valueXML;
	valueXML << "<root>\n	<hp>42</hp>\n</root>\n";

	XMLSectionPtr pValues = XMLSection::createFromStream( "",
		valueXML );
	CHECK( pValues != NULL );

	ScriptDict dict = ScriptDict::create();

	CHECK( description.addSectionToDictionary( pValues, dict,
		EntityDescription::FROM_CELL_TO_CLIENT_DATA ) );

	ScriptObject pHp = dict.getItem( "hp", ScriptErrorPrint() );
	CHECK( pHp != NULL );
	CHECK( ScriptInt::check( pHp ) );
	CHECK_EQUAL( 42, ScriptInt( pHp.get() ).asLong() );
}


/**
 *	Supersede hands the call to the base and cell method lists and warns for
	any other component, leaving the parsed method lists intact.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_supersedeSmoke )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch29Supersede.def",
		"<root>\n"
		"	<BaseMethods>\n"
		"		<basePing/>\n"
		"	</BaseMethods>\n"
		"	<CellMethods>\n"
		"		<cellTick/>\n"
		"	</CellMethods>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch29Supersede", "", NULL ) );
	CHECK_EQUAL( 1, description.base().size() );
	CHECK_EQUAL( 1, description.cell().size() );

	description.supersede( MethodDescription::BASE );
	description.supersede( MethodDescription::CELL );
	// Any other component only warns.
	description.supersede( MethodDescription::CLIENT );

	CHECK_EQUAL( 1, description.base().size() );
	CHECK_EQUAL( 1, description.cell().size() );
}


/**
 *	Reparsing identical def bytes yields identical digests on both surfaces:
 *	nothing pointer- or parse-order dependent may leak into a schema id.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_md5DeterministicAcrossReparse )
{
	installFixtures();

	const char xml[] =
		"<root>\n"
		"	<Properties>\n"
		"		<hp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"		</hp>\n"
		"	</Properties>\n"
		"	<BaseMethods>\n"
		"		<basePing>\n"
		"			<Arg>INT32</Arg>\n"
		"		</basePing>\n"
		"	</BaseMethods>\n"
		"</root>\n";

	MD5::Digest fullA, persistentA;

	CHECK( parseAndDigest( "batch31Md5Same", xml, fullA, persistentA ) );

	MD5::Digest fullB, persistentB;

	reparseAndDigest( "batch31Md5Same", xml, fullB, persistentB );

	CHECK( fullA == fullB );
	CHECK( persistentA == persistentB );
}


/**
 *	CELL_PUBLIC is neither client-server data nor persistent, so it feeds
 *	neither digest: even replacing its whole type must not move either id.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_md5IgnoresCellPublicProperty )
{
	installFixtures();

	const char beforeXml[] =
		"<root>\n"
		"	<Properties>\n"
		"		<hp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"		</hp>\n"
		"	</Properties>\n"
		"</root>\n";

	const char afterXml[] =
		"<root>\n"
		"	<Properties>\n"
		"		<hp>\n"
		"			<Type>FLOAT32</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"		</hp>\n"
		"	</Properties>\n"
		"</root>\n";

	MD5::Digest fullBefore, persistentBefore;

	CHECK( parseAndDigest( "batch31Md5Scope", beforeXml,
		fullBefore, persistentBefore ) );

	MD5::Digest fullAfter, persistentAfter;

	reparseAndDigest( "batch31Md5Scope", afterXml, fullAfter, persistentAfter );

	CHECK( fullBefore == fullAfter );
	CHECK( persistentBefore == persistentAfter );
}


/**
 *	A client-server property enters the full digest only: adding one moves
 *	addToMD5 while addPersistentPropertiesToMD5 stays put.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_md5TracksClientServerProperty )
{
	installFixtures();

	const char beforeXml[] =
		"<root>\n"
		"	<Properties>\n"
		"		<hp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"		</hp>\n"
		"	</Properties>\n"
		"</root>\n";

	const char afterXml[] =
		"<root>\n"
		"	<Properties>\n"
		"		<hp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"		</hp>\n"
		"		<nick>\n"
		"			<Type>STRING</Type>\n"
		"			<Flags>ALL_CLIENTS</Flags>\n"
		"		</nick>\n"
		"	</Properties>\n"
		"</root>\n";

	MD5::Digest fullBefore, persistentBefore;

	CHECK( parseAndDigest( "batch31Md5CServer", beforeXml,
		fullBefore, persistentBefore ) );

	MD5::Digest fullAfter, persistentAfter;

	reparseAndDigest( "batch31Md5CServer", afterXml, fullAfter, persistentAfter );

	CHECK( fullBefore != fullAfter );
	CHECK( persistentBefore == persistentAfter );
}


/**
 *	A BASE + Persistent property enters the persistent digest only: adding
 *	one moves addPersistentPropertiesToMD5 while addToMD5 stays put.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_persistentMd5TracksServerOnlyProperty )
{
	installFixtures();

	const char beforeXml[] =
		"<root>\n"
		"	<Properties>\n"
		"		<hp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"		</hp>\n"
		"	</Properties>\n"
		"</root>\n";

	const char afterXml[] =
		"<root>\n"
		"	<Properties>\n"
		"		<hp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>BASE</Flags>\n"
		"			<Persistent>true</Persistent>\n"
		"		</hp>\n"
		"	</Properties>\n"
		"</root>\n";

	MD5::Digest fullBefore, persistentBefore;

	CHECK( parseAndDigest( "batch31Md5ServOnly", beforeXml,
		fullBefore, persistentBefore ) );

	MD5::Digest fullAfter, persistentAfter;

	reparseAndDigest( "batch31Md5ServOnly", afterXml, fullAfter, persistentAfter );

	CHECK( fullBefore == fullAfter );
	CHECK( persistentBefore != persistentAfter );
}


/**
 *	The persistent digest has no gate of its own beyond isPersistent(): a
 *	def with only client-server properties digests exactly like an empty one.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_persistentMd5SkipsNonPersistentProperties )
{
	installFixtures();

	const char beforeXml[] = "<root/>\n";

	const char afterXml[] =
		"<root>\n"
		"	<Properties>\n"
		"		<nick>\n"
		"			<Type>STRING</Type>\n"
		"			<Flags>ALL_CLIENTS</Flags>\n"
		"		</nick>\n"
		"	</Properties>\n"
		"</root>\n";

	MD5::Digest fullBefore, persistentBefore;

	CHECK( parseAndDigest( "batch31Md5NonPersist", beforeXml,
		fullBefore, persistentBefore ) );

	MD5::Digest fullAfter, persistentAfter;

	reparseAndDigest( "batch31Md5NonPersist", afterXml,
		fullAfter, persistentAfter );

	CHECK( fullBefore != fullAfter );
	CHECK( persistentBefore == persistentAfter );
}


/**
 *	Base methods without <Exposed/> are invisible to addToMD5: a def with an
 *	unexposed base method digests exactly like one without any base method.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_md5SkipsNonExposedBaseMethod )
{
	installFixtures();

	const char beforeXml[] =
		"<root>\n"
		"	<BaseMethods>\n"
		"		<basePing>\n"
		"			<Arg>INT32</Arg>\n"
		"		</basePing>\n"
		"	</BaseMethods>\n"
		"</root>\n";

	const char afterXml[] = "<root/>\n";

	MD5::Digest fullBefore, persistentBefore;

	CHECK( parseAndDigest( "batch31Md5UnexpBase", beforeXml,
		fullBefore, persistentBefore ) );

	MD5::Digest fullAfter, persistentAfter;

	reparseAndDigest( "batch31Md5UnexpBase", afterXml,
		fullAfter, persistentAfter );

	CHECK( fullBefore == fullAfter );
	CHECK( persistentBefore == persistentAfter );
}


/**
 *	Exposing a base method pulls it into addToMD5, so the digest moves even
 *	though the method set itself did not change.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_md5TracksExposedBaseMethod )
{
	installFixtures();

	const char beforeXml[] =
		"<root>\n"
		"	<BaseMethods>\n"
		"		<basePing>\n"
		"			<Arg>INT32</Arg>\n"
		"		</basePing>\n"
		"	</BaseMethods>\n"
		"</root>\n";

	const char afterXml[] =
		"<root>\n"
		"	<BaseMethods>\n"
		"		<basePing>\n"
		"			<Arg>INT32</Arg>\n"
		"			<Exposed/>\n"
		"		</basePing>\n"
		"	</BaseMethods>\n"
		"</root>\n";

	MD5::Digest fullBefore, persistentBefore;

	CHECK( parseAndDigest( "batch31Md5ExpBase", beforeXml,
		fullBefore, persistentBefore ) );

	MD5::Digest fullAfter, persistentAfter;

	reparseAndDigest( "batch31Md5ExpBase", afterXml, fullAfter, persistentAfter );

	CHECK( fullBefore != fullAfter );
	CHECK( persistentBefore == persistentAfter );
}


/**
 *	Cell methods sit behind the same exposure gate as base methods: an
 *	unexposed cell method does not move the digest, an exposed one does.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_md5CellMethodExposureGate )
{
	installFixtures();

	const char beforeXml[] =
		"<root>\n"
		"	<CellMethods>\n"
		"		<cellTick>\n"
		"			<Arg>UINT32</Arg>\n"
		"		</cellTick>\n"
		"	</CellMethods>\n"
		"</root>\n";

	const char afterXml[] =
		"<root>\n"
		"	<CellMethods>\n"
		"		<cellTick>\n"
		"			<Arg>UINT32</Arg>\n"
		"			<Exposed/>\n"
		"		</cellTick>\n"
		"	</CellMethods>\n"
		"</root>\n";

	MD5::Digest fullBefore, persistentBefore;

	CHECK( parseAndDigest( "batch31Md5CellGate", beforeXml,
		fullBefore, persistentBefore ) );

	MD5::Digest fullAfter, persistentAfter;

	reparseAndDigest( "batch31Md5CellGate", afterXml,
		fullAfter, persistentAfter );

	CHECK( fullBefore != fullAfter );
	CHECK( persistentBefore == persistentAfter );
}


/**
 *	Client methods carry no exposure gate - <Exposed/> is not even legal
 *	there - so a plain client method moves the digest as soon as it exists.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_md5ClientMethodAlwaysTracked )
{
	installFixtures();

	const char beforeXml[] = "<root/>\n";

	const char afterXml[] =
		"<root>\n"
		"	<ClientMethods>\n"
		"		<clientNotify>\n"
		"			<Arg>FLOAT32</Arg>\n"
		"		</clientNotify>\n"
		"	</ClientMethods>\n"
		"</root>\n";

	MD5::Digest fullBefore, persistentBefore;

	CHECK( parseAndDigest( "batch31Md5ClientAlways", beforeXml,
		fullBefore, persistentBefore ) );

	MD5::Digest fullAfter, persistentAfter;

	reparseAndDigest( "batch31Md5ClientAlways", afterXml,
		fullAfter, persistentAfter );

	CHECK( fullBefore != fullAfter );
	CHECK( persistentBefore == persistentAfter );
}


/**
 *	Exposed methods digest with their position counter, so swapping two
 *	exposed base methods changes the schema id even though the method set
 *	is identical.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_md5ExposedMethodOrderSensitivity )
{
	installFixtures();

	const char beforeXml[] =
		"<root>\n"
		"	<BaseMethods>\n"
		"		<basePing>\n"
		"			<Arg>INT32</Arg>\n"
		"			<Exposed/>\n"
		"		</basePing>\n"
		"		<baseHeal>\n"
		"			<Arg>UINT32</Arg>\n"
		"			<Exposed/>\n"
		"		</baseHeal>\n"
		"	</BaseMethods>\n"
		"</root>\n";

	const char afterXml[] =
		"<root>\n"
		"	<BaseMethods>\n"
		"		<baseHeal>\n"
		"			<Arg>UINT32</Arg>\n"
		"			<Exposed/>\n"
		"		</baseHeal>\n"
		"		<basePing>\n"
		"			<Arg>INT32</Arg>\n"
		"			<Exposed/>\n"
		"		</basePing>\n"
		"	</BaseMethods>\n"
		"</root>\n";

	MD5::Digest fullBefore, persistentBefore;

	CHECK( parseAndDigest( "batch31Md5MethOrder", beforeXml,
		fullBefore, persistentBefore ) );

	MD5::Digest fullAfter, persistentAfter;

	reparseAndDigest( "batch31Md5MethOrder", afterXml,
		fullAfter, persistentAfter );

	CHECK( fullBefore != fullAfter );
	CHECK( persistentBefore == persistentAfter );
}


/**
 *	Client-server properties digest with their clientServerFullIndex, which
 *	parseProperties numbers in def order, so swapping two client-server
 *	properties changes the schema id. Their order is also member order in
 *	addToMD5 itself. The persistent digest, which has no index, stays put.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_md5ClientServerPropertyOrderSensitivity )
{
	installFixtures();

	const char beforeXml[] =
		"<root>\n"
		"	<Properties>\n"
		"		<nick>\n"
		"			<Type>STRING</Type>\n"
		"			<Flags>ALL_CLIENTS</Flags>\n"
		"		</nick>\n"
		"		<score>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>ALL_CLIENTS</Flags>\n"
		"		</score>\n"
		"	</Properties>\n"
		"</root>\n";

	const char afterXml[] =
		"<root>\n"
		"	<Properties>\n"
		"		<score>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>ALL_CLIENTS</Flags>\n"
		"		</score>\n"
		"		<nick>\n"
		"			<Type>STRING</Type>\n"
		"			<Flags>ALL_CLIENTS</Flags>\n"
		"		</nick>\n"
		"	</Properties>\n"
		"</root>\n";

	MD5::Digest fullBefore, persistentBefore;

	CHECK( parseAndDigest( "batch31Md5PropOrder", beforeXml,
		fullBefore, persistentBefore ) );

	MD5::Digest fullAfter, persistentAfter;

	reparseAndDigest( "batch31Md5PropOrder", afterXml,
		fullAfter, persistentAfter );

	CHECK( fullBefore != fullAfter );
	CHECK( persistentBefore == persistentAfter );
}


/**
 *	Property values stream from a Python dict through addDictionaryToStream,
 *	including the per-pass data sizes, and read back with the same values.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_addDictionaryToStreamRoundTrip )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch32DictStream.def",
		"<root>\n"
		"	<Properties>\n"
		"		<hp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"		</hp>\n"
		"		<owner>\n"
		"			<Type>UINT32</Type>\n"
		"			<Flags>BASE</Flags>\n"
		"		</owner>\n"
		"		<nick>\n"
		"			<Type>STRING</Type>\n"
		"			<Flags>ALL_CLIENTS</Flags>\n"
		"		</nick>\n"
		"	</Properties>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch32DictStream", "", NULL ) );

	ScriptDict map = ScriptDict::create();
	CHECK( map.setItem( "hp", ScriptInt::create( 5 ), ScriptErrorPrint() ) );
	CHECK( map.setItem( "owner", ScriptInt::create( 7 ),
		ScriptErrorPrint() ) );
	CHECK( map.setItem( "nick", ScriptString::create( "bob" ),
		ScriptErrorPrint() ) );

	const int domains = EntityDescription::BASE_DATA |
		EntityDescription::CELL_DATA | EntityDescription::CLIENT_DATA;

	// There are four passes (base, base+client, cell+client, cell). owner
	// lands in pass 0, nick in pass 2, hp in pass 3, and no property has the
	// base-and-client combination, so pass 1 contributes nothing.
	int32 sizes[ 4 ];
	MemoryOStream os;

	CHECK( description.addDictionaryToStream( map, os, domains, sizes, 4 ) );
	CHECK( os.size() > 0 );

	CHECK_EQUAL( 4, sizes[ 0 ] );
	CHECK_EQUAL( 0, sizes[ 1 ] );
	CHECK_EQUAL( 4, sizes[ 2 ] );
	CHECK_EQUAL( 2, sizes[ 3 ] );

	int32 total = 0;

	for (int i = 0; i < 4; ++i)
	{
		CHECK( sizes[ i ] >= 0 );
		total += sizes[ i ];
	}

	CHECK_EQUAL( os.size(), size_t( total ) );

	ScriptDict dict = ScriptDict::create();
	MemoryIStream is( os.data(), os.size() );
	CHECK( description.readStreamToDict( is, domains, dict ) );
	CHECK_EQUAL( 0, is.remainingLength() );

	ScriptObject pHp = dict.getItem( "hp", ScriptErrorPrint() );
	CHECK( pHp.exists() );
	CHECK( ScriptInt::check( pHp ) );
	CHECK_EQUAL( 5, ScriptInt( pHp.get() ).asLong() );

	ScriptObject pOwner = dict.getItem( "owner", ScriptErrorPrint() );
	CHECK( pOwner.exists() );
	CHECK( ScriptInt::check( pOwner ) );
	CHECK_EQUAL( 7, ScriptInt( pOwner.get() ).asLong() );

	ScriptObject pNick = dict.getItem( "nick", ScriptErrorPrint() );
	CHECK( pNick.exists() );
	CHECK( ScriptString::check( pNick ) );
	CHECK( strcmp( "bob", ScriptString( pNick.get() ).c_str() ) == 0 );
}


/**
 *	A dict that misses keys streams their defaults, a value of the wrong
 *	Python type fails the stream, and a null mapping is rejected up front.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_addDictionaryToStreamDefaultsAndInvalid )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch32DictDefaults.def",
		"<root>\n"
		"	<Properties>\n"
		"		<hp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"		</hp>\n"
		"		<owner>\n"
		"			<Type>UINT32</Type>\n"
		"			<Flags>BASE</Flags>\n"
		"		</owner>\n"
		"		<nick>\n"
		"			<Type>STRING</Type>\n"
		"			<Flags>ALL_CLIENTS</Flags>\n"
		"		</nick>\n"
		"	</Properties>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch32DictDefaults", "", NULL ) );

	const int domains = EntityDescription::BASE_DATA |
		EntityDescription::CELL_DATA | EntityDescription::CLIENT_DATA;

	// Only hp has a value: owner and nick stream their defaults.
	ScriptDict map = ScriptDict::create();
	CHECK( map.setItem( "hp", ScriptInt::create( 5 ), ScriptErrorPrint() ) );

	MemoryOStream os;
	CHECK( description.addDictionaryToStream( map, os, domains ) );

	ScriptDict dict = ScriptDict::create();
	MemoryIStream is( os.data(), os.size() );
	CHECK( description.readStreamToDict( is, domains, dict ) );

	ScriptObject pHp = dict.getItem( "hp", ScriptErrorPrint() );
	CHECK( pHp.exists() );
	CHECK_EQUAL( 5, ScriptInt( pHp.get() ).asLong() );

	ScriptObject pOwner = dict.getItem( "owner", ScriptErrorPrint() );
	CHECK( pOwner.exists() );
	CHECK_EQUAL( 0, ScriptInt( pOwner.get() ).asLong() );

	ScriptObject pNick = dict.getItem( "nick", ScriptErrorPrint() );
	CHECK( pNick.exists() );
	CHECK( ScriptString::check( pNick ) );
	CHECK( strcmp( "", ScriptString( pNick.get() ).c_str() ) == 0 );

	// A value of the wrong Python type fails the stream and is replaced by
	// the default while doing so.
	ScriptDict wrong = ScriptDict::create();
	CHECK( wrong.setItem( "hp", ScriptString::create( "oops" ),
		ScriptErrorPrint() ) );

	MemoryOStream wrongStream;
	CHECK( !description.addDictionaryToStream( wrong, wrongStream, domains ) );

	// A null mapping is rejected before anything is written.
	ScriptMapping invalid;
	MemoryOStream invalidStream;
	CHECK( !description.addDictionaryToStream( invalid, invalidStream,
		domains ) );
}


/**
 *	Property values stream from Python instance attributes through
 *	addAttributesToStream and read back with the same values. An object
 *	missing an attribute fails the stream, as does a null object.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_addAttributesToStreamRoundTrip )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch32Attrs.def",
		"<root>\n"
		"	<Properties>\n"
		"		<hp>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"		</hp>\n"
		"		<owner>\n"
		"			<Type>UINT32</Type>\n"
		"			<Flags>BASE</Flags>\n"
		"		</owner>\n"
		"		<nick>\n"
		"			<Type>STRING</Type>\n"
		"			<Flags>ALL_CLIENTS</Flags>\n"
		"		</nick>\n"
		"	</Properties>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch32Attrs", "", NULL ) );

	// PyRun_String with Py_file_input returns None, so define the class and
	// its attribute values with exec and fetch the instance with eval.
	PyObject * pGlobals = PyDict_New();
	PyObject * pBuiltins = PyImport_AddModule( "builtins" );
	PyDict_SetItemString( pGlobals, "__builtins__",
		PyModule_GetDict( pBuiltins ) );

	PyRun_String(
		"class _BWBatch32Attrs(object):\n"
		"    pass\n"
		"a = _BWBatch32Attrs()\n"
		"a.hp = 5\n"
		"a.owner = 7\n"
		"a.nick = 'bob'\n"
		"b = _BWBatch32Attrs()\n"
		"b.hp = 5\n",
		Py_file_input, pGlobals, pGlobals );
	PyErr_Clear();

	ScriptObject full( PyRun_String( "a\n", Py_eval_input, pGlobals,
		pGlobals ), ScriptObject::STEAL_REFERENCE );
	CHECK( full.exists() );
	CHECK( full.get() != Py_None );

	ScriptObject partial( PyRun_String( "b\n", Py_eval_input, pGlobals,
		pGlobals ), ScriptObject::STEAL_REFERENCE );
	CHECK( partial.exists() );

	Py_DECREF( pGlobals );
	PyErr_Clear();

	const int domains = EntityDescription::BASE_DATA |
		EntityDescription::CELL_DATA | EntityDescription::CLIENT_DATA;

	MemoryOStream os;
	CHECK( description.addAttributesToStream( full, os, domains ) );
	CHECK( os.size() > 0 );

	ScriptDict dict = ScriptDict::create();
	MemoryIStream is( os.data(), os.size() );
	CHECK( description.readStreamToDict( is, domains, dict ) );

	ScriptObject pHp = dict.getItem( "hp", ScriptErrorPrint() );
	CHECK( pHp.exists() );
	CHECK_EQUAL( 5, ScriptInt( pHp.get() ).asLong() );

	ScriptObject pNick = dict.getItem( "nick", ScriptErrorPrint() );
	CHECK( pNick.exists() );
	CHECK( ScriptString::check( pNick ) );
	CHECK( strcmp( "bob", ScriptString( pNick.get() ).c_str() ) == 0 );

	// The attribute visitor treats a missing attribute as an error, unlike
	// the map visitor which falls back to the default value.
	MemoryOStream partialStream;
	CHECK( !description.addAttributesToStream( partial, partialStream,
		domains ) );

	// A null object is rejected up front.
	MemoryOStream nullStream;
	CHECK( !description.addAttributesToStream( ScriptObject(), nullStream,
		domains ) );
}


/**
 *	A value section streams out and reads back into a fresh section with the
 *	same property children.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_readStreamToSection )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch32Section.def",
		"<root>\n"
		"	<Properties>\n"
		"		<nick>\n"
		"			<Type>STRING</Type>\n"
		"			<Flags>ALL_CLIENTS</Flags>\n"
		"		</nick>\n"
		"		<secret>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>OWN_CLIENT</Flags>\n"
		"		</secret>\n"
		"	</Properties>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch32Section", "", NULL ) );

	BW::stringstream valueXML;
	valueXML << "<root>\n	<nick>bob</nick>\n	<secret>9</secret>\n</root>\n";

	XMLSectionPtr pValues = XMLSection::createFromStream( "", valueXML );
	CHECK( pValues != NULL );

	const int domains = EntityDescription::FROM_CELL_TO_CLIENT_DATA;

	MemoryOStream os;
	CHECK( description.addSectionToStream( pValues, os, domains ) );
	CHECK( os.size() > 0 );

	BW::stringstream emptyXML;
	emptyXML << "<root/>\n";
	XMLSectionPtr pOut = XMLSection::createFromStream( "root", emptyXML );
	CHECK( pOut != NULL );

	MemoryIStream is( os.data(), os.size() );
	CHECK( description.readStreamToSection( is, domains, pOut ) );

	CHECK_EQUAL( 0, strcmp( "bob",
		pOut->readString( "nick" ).c_str() ) );
	CHECK_EQUAL( 9, pOut->readInt( "secret", /* default */ 0 ) );
}


/**
 *	Tagged client streams carry a count and per-item property indices: the
 *	reader resolves each index through clientServerProperty and fills the
 *	dict, covering other-client and own-client data alike.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_readTaggedClientStreamToDict )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch32Tagged.def",
		"<root>\n"
		"	<Properties>\n"
		"		<nick>\n"
		"			<Type>STRING</Type>\n"
		"			<Flags>ALL_CLIENTS</Flags>\n"
		"		</nick>\n"
		"		<score>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>ALL_CLIENTS</Flags>\n"
		"		</score>\n"
		"		<secret>\n"
		"			<Type>UINT16</Type>\n"
		"			<Flags>OWN_CLIENT</Flags>\n"
		"		</secret>\n"
		"	</Properties>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch32Tagged", "", NULL ) );
	CHECK_EQUAL( 3, int( description.clientServerPropertyCount() ) );

	// Each item's value part is one property's addToStream output, prefixed
	// by its client-server property index.
	// The client-server list is sorted by stream size: fixed-size
	// properties first (stable within one size), then variable-length ones.
	CHECK( strcmp( "score",
		description.clientServerProperty( 0 )->name().c_str() ) == 0 );
	CHECK( strcmp( "secret",
		description.clientServerProperty( 1 )->name().c_str() ) == 0 );
	CHECK( strcmp( "nick",
		description.clientServerProperty( 2 )->name().c_str() ) == 0 );

	// Each item's value part is one property's addToStream output, prefixed
	// by its client-server property index.
	const DataDescription * pScoreDesc = description.clientServerProperty( 0 );
	ScriptDataSource scoreSource( ScriptInt::create( 5 ) );
	MemoryOStream scoreStream;
	CHECK( pScoreDesc->addToStream( scoreSource, scoreStream, false ) );

	const DataDescription * pSecretDesc = description.clientServerProperty( 1 );
	ScriptDataSource secretSource( ScriptInt::create( 9 ) );
	MemoryOStream secretStream;
	CHECK( pSecretDesc->addToStream( secretSource, secretStream, false ) );

	const DataDescription * pNickDesc = description.clientServerProperty( 2 );
	ScriptDataSource nickSource( ScriptString::create( "bob" ) );
	MemoryOStream nickStream;
	CHECK( pNickDesc->addToStream( nickSource, nickStream,
		/* isPersistentOnly */ false ) );

	MemoryOStream os;
	const uint8 count = 3;
	os.addBlob( &count, sizeof( count ) );
	const uint8 scoreIndex = 0;
	os.addBlob( &scoreIndex, sizeof( scoreIndex ) );
	os.addBlob( scoreStream.data(), scoreStream.size() );
	const uint8 secretIndex = 1;
	os.addBlob( &secretIndex, sizeof( secretIndex ) );
	os.addBlob( secretStream.data(), secretStream.size() );
	const uint8 nickIndex = 2;
	os.addBlob( &nickIndex, sizeof( nickIndex ) );
	os.addBlob( nickStream.data(), nickStream.size() );
	os.rewind();

	ScriptDict dict = ScriptDict::create();
	CHECK( description.readTaggedClientStreamToDict( os, dict ) );

	ScriptObject pNickValue = dict.getItem( "nick", ScriptErrorPrint() );
	CHECK( pNickValue.exists() );
	CHECK( ScriptString::check( pNickValue ) );
	CHECK( strcmp( "bob", ScriptString( pNickValue.get() ).c_str() ) == 0 );

	ScriptObject pScoreValue = dict.getItem( "score", ScriptErrorPrint() );
	CHECK( pScoreValue.exists() );
	CHECK_EQUAL( 5, ScriptInt( pScoreValue.get() ).asLong() );

	ScriptObject pSecretValue = dict.getItem( "secret", ScriptErrorPrint() );
	CHECK( pSecretValue.exists() );
	CHECK_EQUAL( 9, ScriptInt( pSecretValue.get() ).asLong() );
}


/**
 *	The property accessors resolve the Identifier property, temp properties
 *	and the client-server property list.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_propertyAccessors )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch32Access.def",
		"<root>\n"
		"	<Properties>\n"
		"		<id>\n"
		"			<Type>UINT32</Type>\n"
		"			<Flags>CELL_PUBLIC</Flags>\n"
		"			<Identifier>true</Identifier>\n"
		"			<Persistent>true</Persistent>\n"
		"		</id>\n"
		"		<nick>\n"
		"			<Type>STRING</Type>\n"
		"			<Flags>ALL_CLIENTS</Flags>\n"
		"		</nick>\n"
		"	</Properties>\n"
		"	<TempProperties>\n"
		"		<scratch/>\n"
		"	</TempProperties>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch32Access", "", NULL ) );
	CHECK_EQUAL( 2, description.propertyCount() );

	const DataDescription * pId = description.pIdentifier();
	CHECK( pId != NULL );
	CHECK( strcmp( "id", pId->name().c_str() ) == 0 );

	CHECK_EQUAL( true, description.isTempProperty( "scratch" ) );
	CHECK_EQUAL( false, description.isTempProperty( "nick" ) );

	CHECK_EQUAL( 1, description.clientServerPropertyCount() );
	CHECK( strcmp( "nick",
		description.clientServerProperty( 0 )->name().c_str() ) == 0 );

	// Without an Identifier property the lookup yields NULL.
	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch32AccessNoId.def",
		"<root>\n"
		"	<Properties>\n"
		"		<nick>\n"
		"			<Type>STRING</Type>\n"
		"			<Flags>ALL_CLIENTS</Flags>\n"
		"		</nick>\n"
		"	</Properties>\n"
		"</root>\n" );

	EntityDescription plain;
	CHECK( plain.parse( "batch32AccessNoId", "", NULL ) );
	CHECK( plain.pIdentifier() == NULL );
}


/**
 *	setExposedMsgIDs hands each exposed method the message id its position in
 *	the range dictates, and checkMethods verifies a Python class implements
 *	every non-componentised method.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_setExposedMsgIDsAndCheckMethods )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch32MsgIDs.def",
		"<root>\n"
		"	<BaseMethods>\n"
		"		<basePing/>\n"
		"		<baseHeal>\n"
		"			<Exposed/>\n"
		"		</baseHeal>\n"
		"	</BaseMethods>\n"
		"	<CellMethods>\n"
		"		<cellTick/>\n"
		"	</CellMethods>\n"
		"	<ClientMethods>\n"
		"		<clientNotify/>\n"
		"	</ClientMethods>\n"
		"</root>\n" );

	EntityDescription description;

	CHECK( description.parse( "batch32MsgIDs", "", NULL ) );

	ExposedMethodMessageRange range( 10, 19 );
	description.setExposedMsgIDs( 100, &range, 100, &range, 100, &range );

	CHECK_EQUAL( 1, description.base().exposedSize() );
	CHECK_EQUAL( 0, description.cell().exposedSize() );

	// baseHeal is the only exposed method, so it takes the first slot.
	CHECK_EQUAL( 10,
		int( description.base().exposedMethod( 0 )->exposedMsgID() ) );

	// basePing is not exposed and keeps the wrapped sentinel id.
	CHECK_EQUAL( 255,
		int( description.base().internalMethod( 0 )->exposedMsgID() ) );

	// checkMethods wants a Python class implementing basePing and baseHeal.
	PyObject * pGlobals = PyDict_New();
	PyObject * pBuiltins = PyImport_AddModule( "builtins" );
	PyDict_SetItemString( pGlobals, "__builtins__",
		PyModule_GetDict( pBuiltins ) );

	PyRun_String(
		"class _BWBatch32OnlyPing(object):\n"
		"    def basePing(self):\n"
		"        pass\n"
		"class _BWBatch32Both(object):\n"
		"    def basePing(self):\n"
		"        pass\n"
		"    def baseHeal(self):\n"
		"        pass\n",
		Py_file_input, pGlobals, pGlobals );
	PyErr_Clear();

	ScriptObject onlyPing( PyRun_String( "_BWBatch32OnlyPing\n",
		Py_eval_input, pGlobals, pGlobals ), ScriptObject::STEAL_REFERENCE );
	CHECK( onlyPing.exists() );

	ScriptObject both( PyRun_String( "_BWBatch32Both\n",
		Py_eval_input, pGlobals, pGlobals ), ScriptObject::STEAL_REFERENCE );
	CHECK( both.exists() );

	Py_DECREF( pGlobals );
	PyErr_Clear();

	CHECK( !description.checkMethods( description.base().internalMethods(),
		onlyPing, /* warnOnMissing */ false ) );
	CHECK( description.checkMethods( description.base().internalMethods(),
		both, false ) );
}


/**
 *	Supersede renames every cell method with an old_ prefix: lookups by the
 *	original name break, while index-based access (internal and exposed)
 *	still resolves to the same objects. Other component lists are untouched.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_supersedeRenamesCellMethods )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch33Supersede.def",
		"<root>\n"
		"	<CellMethods>\n"
		"		<cellTick/>\n"
		"		<cellHeal>\n"
		"			<Exposed/>\n"
		"		</cellHeal>\n"
		"	</CellMethods>\n"
		"	<BaseMethods>\n"
		"		<basePing>\n"
		"			<Exposed/>\n"
		"		</basePing>\n"
		"	</BaseMethods>\n"
		"	<ClientMethods>\n"
		"		<clientNotify/>\n"
		"	</ClientMethods>\n"
		"</root>\n" );

	EntityDescription description;
	CHECK( description.parse( "batch33Supersede", "", NULL ) );

	CHECK( description.cell().find( "cellTick" ) != NULL );
	CHECK_EQUAL( 2, description.cell().size() );

	description.supersede( MethodDescription::CELL );

	// Name lookups break for the original names ...
	CHECK( description.cell().find( "cellTick" ) == NULL );
	CHECK( description.cell().find( "cellHeal" ) == NULL );
	// ... and resolve under the old_ prefix to the same objects.
	const MethodDescription * pOldTick =
		description.cell().find( "old_cellTick" );
	CHECK( pOldTick != NULL );
	CHECK( pOldTick == description.cell().internalMethod( 0 ) );
	CHECK( pOldTick->name() == "old_cellTick" );

	// Exposed (msgID) dispatch keeps its index mapping after the rename.
	const MethodDescription * pOldHeal =
		description.cell().exposedMethod( 0 );
	CHECK( pOldHeal != NULL );
	CHECK( pOldHeal->name() == "old_cellHeal" );

	// Only the cell list was touched.
	CHECK( description.base().find( "basePing" ) != NULL );
	CHECK( description.client().find( "clientNotify" ) != NULL );
}


/**
 *	Supersede with the base component renames only the base list and leaves
 *	the cell and client lists resolvable under their original names.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_supersedeRenamesBaseMethods )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch33SupersedeBase.def",
		"<root>\n"
		"	<CellMethods>\n"
		"		<cellTick/>\n"
		"	</CellMethods>\n"
		"	<BaseMethods>\n"
		"		<basePing>\n"
		"			<Exposed/>\n"
		"		</basePing>\n"
		"	</BaseMethods>\n"
		"	<ClientMethods>\n"
		"		<clientNotify/>\n"
		"	</ClientMethods>\n"
		"</root>\n" );

	EntityDescription description;
	CHECK( description.parse( "batch33SupersedeBase", "", NULL ) );

	description.supersede( MethodDescription::BASE );

	CHECK( description.base().find( "basePing" ) == NULL );
	CHECK( description.base().find( "old_basePing" ) != NULL );
	CHECK( description.base().find( "old_basePing" ) ==
		description.base().internalMethod( 0 ) );
	CHECK( description.base().exposedMethod( 0 )->name() == "old_basePing" );

	// Cell and client lists keep their original names.
	CHECK( description.cell().find( "cellTick" ) != NULL );
	CHECK( description.client().find( "clientNotify" ) != NULL );
}


/**
 *	Each supersede call renames again: the old_ prefix stacks. This is why
 *	EntityType::old guards against re-recording an old type.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_supersedeTwiceStacksPrefixes )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch33SupersedeTwice.def",
		"<root>\n"
		"	<CellMethods>\n"
		"		<cellTick/>\n"
		"	</CellMethods>\n"
		"</root>\n" );

	EntityDescription description;
	CHECK( description.parse( "batch33SupersedeTwice", "", NULL ) );

	description.supersede( MethodDescription::CELL );
	CHECK( description.cell().find( "old_cellTick" ) != NULL );

	description.supersede( MethodDescription::CELL );

	CHECK( description.cell().find( "old_cellTick" ) == NULL );
	CHECK( description.cell().find( "old_old_cellTick" ) != NULL );
	CHECK( description.cell().size() == 1 );
}


/**
 *	A superseded description no longer digests the same: the rename feeds
 *	addToMD5 through every exposed method name, so a schema digest must be
 *	captured before supersede (the reload chain supersedes old descriptions
 *	during Entity::migrate and never undoes it).
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_supersedeChangesMd5 )
{
	installFixtures();

	writeFixtureFile( BW::string( FIXTURE_DIR ) + "/batch33SupersedeMd5.def",
		"<root>\n"
		"	<CellMethods>\n"
		"		<cellHeal>\n"
		"			<Exposed/>\n"
		"		</cellHeal>\n"
		"	</CellMethods>\n"
		"</root>\n" );

	EntityDescription description;
	CHECK( description.parse( "batch33SupersedeMd5", "", NULL ) );

	MD5::Digest beforeFull, beforePersistent;
	digestsOf( description, beforeFull, beforePersistent );

	description.supersede( MethodDescription::CELL );

	MD5::Digest afterFull, afterPersistent;
	digestsOf( description, afterFull, afterPersistent );

	// The exposed cell method was renamed, so the full digest moved.
	CHECK( beforeFull != afterFull );

	// No properties: the persistent digest covers the entity name only.
	CHECK( beforePersistent == afterPersistent );
}


#if ENABLE_WATCHERS
/**
 *	The description exposes a watcher tree over its method lists.
 */
TEST_F( EntityDefUnitTestHarness, EntityDescription_pWatcherSmoke )
{
	CHECK( EntityDescription::pWatcher() != NULL );
}
#endif // ENABLE_WATCHERS


BW_END_NAMESPACE

// test_entity_description.cpp
