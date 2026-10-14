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

#include "resmgr/bwresource.hpp"
#include "resmgr/multi_file_system.hpp"
#include "resmgr/xml_section.hpp"

#include "cstdmf/bw_string.hpp"
#include "cstdmf/bw_util.hpp"

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
