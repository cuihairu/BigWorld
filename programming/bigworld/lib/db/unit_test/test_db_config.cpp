#include "CppUnitLite2/src/CppUnitLite2.h"

#include "resmgr/xml_section.hpp"
#include "server/bwconfig.hpp"

#include "db/db_config.hpp"

#include <sstream>


BW_BEGIN_NAMESPACE

namespace DBConfig
{
	/**
	 *	Defined in db_config.cpp.  The blocks that report on the whole
	 *	configuration - isGood() and maxCommitPeriodInTicks() - reach the
	 *	top-level object through this file-scope pointer, which only
	 *	get() assigns; the fixture below publishes its own block through
	 *	it so those paths can be exercised without the singleton.
	 */
	extern TopLevelConfigBlock * pTopLevelConfig;
}

namespace // (anonymous)
{

/**
 *	A complete, valid configuration: every option the database blocks
 *	declare is present, so init() succeeds and no obsolete name is hit.
 */
const char * FULL_CONFIG =
	"<root>"
	"  <gameUpdateHertz>10</gameUpdateHertz>"
	"  <db>"
	"    <type>mysql</type>"
	"    <mysql>"
	"      <host>db.example.com</host>"
	"      <port>3307</port>"
	"      <username>bwuser</username>"
	"      <password>secret</password>"
	"      <databaseName>bwdb</databaseName>"
	"      <numConnections>12</numConnections>"
	"      <secureAuth>true</secureAuth>"
	"      <syncTablesToDefs>true</syncTablesToDefs>"
	"      <maxSpaceDataSize>4096</maxSpaceDataSize>"
	"      <unicodeString>"
	"        <characterSet>latin1</characterSet>"
	"        <collation>latin1_general_ci</collation>"
	"      </unicodeString>"
	"    </mysql>"
	"    <xml>"
	"      <archivePeriod>1800.5</archivePeriod>"
	"      <numArchives>7</numArchives>"
	"      <savePeriod>45.5</savePeriod>"
	"    </xml>"
	"    <secondaryDB>"
	"      <enable>false</enable>"
	"      <maxCommitPeriod>2.5</maxCommitPeriod>"
	"      <directory>server/db/alt</directory>"
	"      <consolidation>"
	"        <directory>/tmp/alt</directory>"
	"      </consolidation>"
	"    </secondaryDB>"
	"  </db>"
	"</root>";


/**
 *	Installs the given XML as the only file BWConfig searches, replacing
 *	whatever was installed before, and hands back its root section so a
 *	test can add sections or overwrite single options before the
 *	configuration blocks are built from it.
 */
DataSectionPtr installConfig( const char * pXML )
{
	std::istringstream stream( pXML );
	DataSectionPtr pRoot = XMLSection::createFromStream( "root", stream );

	BWConfig::Container container;
	container.push_back(
		BWConfig::ConfigElement( pRoot, BW::string( "unit-test.xml" ) ) );

	// hijack() parses no command line of its own; with an argument count
	// of zero its loop never looks at the (absent) argv.
	BWConfig::hijack( container, /* argc */ 0, /* argv */ NULL );

	return pRoot;
}


/**
 *	Builds a top-level configuration block from whatever configuration is
 *	installed at the time of construction, and publishes it through
 *	db_config.cpp's pTopLevelConfig for as long as the fixture lives.  The
 *	previous value of that pointer is put back on destruction so tests
 *	stay independent of each other and of the singleton.
 */
class TopLevelFixture
{
public:
	TopLevelFixture() :
		pPrevious_( DBConfig::pTopLevelConfig )
	{
		// block_ is declared last, so it is built - and reads the
		// configuration - before the pointer is published.
		DBConfig::pTopLevelConfig = &block_;
	}

	~TopLevelFixture()
	{
		DBConfig::pTopLevelConfig = pPrevious_;
	}

	const DBConfig::TopLevelConfigBlock &	block() const	{ return block_; }
	const DBConfig::DBConfigBlock &		db() const	{ return block_.db; }
	bool							isGood() const	{ return block_.isGood(); }

private:
	TopLevelFixture( const TopLevelFixture & );
	TopLevelFixture & operator=( const TopLevelFixture & );

	DBConfig::TopLevelConfigBlock * pPrevious_;
	DBConfig::TopLevelConfigBlock block_;
};

} // end namespace (anonymous)


// Every option the database blocks declare is read from the
// configuration, at the path its block hierarchy implies, and the whole
// set is reported as good.
TEST( DBConfig_readsEveryOption )
{
	installConfig( FULL_CONFIG );

	TopLevelFixture fixture;
	CHECK( fixture.isGood() );

	CHECK_EQUAL( 10u, fixture.block().updateHertz() );

	CHECK_EQUAL( BW::string( "mysql" ), fixture.db().type() );

	CHECK_EQUAL( BW::string( "db.example.com" ), fixture.db().mysql.host() );
	CHECK_EQUAL( 3307u, fixture.db().mysql.port() );
	CHECK_EQUAL( BW::string( "bwuser" ), fixture.db().mysql.username() );
	CHECK_EQUAL( BW::string( "secret" ), fixture.db().mysql.password() );
	CHECK_EQUAL( BW::string( "bwdb" ), fixture.db().mysql.databaseName() );
	CHECK_EQUAL( 12u, fixture.db().mysql.numConnections() );
	CHECK( fixture.db().mysql.secureAuth() );
	CHECK( fixture.db().mysql.syncTablesToDefs() );
	CHECK_EQUAL( 4096u, fixture.db().mysql.maxSpaceDataSize() );
	CHECK_EQUAL( BW::string( "latin1" ),
		fixture.db().mysql.unicodeString.characterSet() );
	CHECK_EQUAL( BW::string( "latin1_general_ci" ),
		fixture.db().mysql.unicodeString.collation() );

	// Floats go through CHECK_CLOSE: the build treats -Wfloat-equal as an
	// error, and these come back from a decimal-to-binary conversion.
	CHECK_CLOSE( 1800.5f, fixture.db().xml.archivePeriod(), 0.0001f );
	CHECK_EQUAL( 7u, fixture.db().xml.numArchives() );
	CHECK_CLOSE( 45.5f, fixture.db().xml.savePeriod(), 0.0001f );

	CHECK( !fixture.db().secondaryDB.enable() );
	CHECK_CLOSE( 2.5f, fixture.db().secondaryDB.maxCommitPeriod(), 0.0001f );
	CHECK_EQUAL( BW::string( "server/db/alt" ),
		fixture.db().secondaryDB.directory() );
	CHECK_EQUAL( BW::string( "/tmp/alt" ),
		fixture.db().secondaryDB.consolidation.directory() );

	// A top-level block with an empty name contributes no path element,
	// so its own options sit directly under the document root.
	CHECK_EQUAL( BW::string( "gameUpdateHertz" ),
		fixture.block().updateHertz.path() );
	CHECK_EQUAL( BW::string( "db/type" ), fixture.db().type.path() );
	CHECK_EQUAL( BW::string( "db/mysql/port" ), fixture.db().mysql.port.path() );
	CHECK_EQUAL( BW::string( "db/secondaryDB/consolidation/directory" ),
		fixture.db().secondaryDB.consolidation.directory.path() );
}


// A configuration that supplies nothing at all is an error - but the
// options keep the defaults they were constructed with, because
// BWConfig::update() leaves a value alone when its section is missing.
TEST( DBConfig_missingOptionsAreAnError )
{
	installConfig( "<root />" );

	TopLevelFixture fixture;
	CHECK( !fixture.isGood() );

	CHECK_EQUAL( 10u, fixture.block().updateHertz() );
	CHECK_EQUAL( BW::string( "xml" ), fixture.db().type() );
	CHECK_EQUAL( BW::string( "localhost" ), fixture.db().mysql.host() );
	CHECK_EQUAL( BW::string( "bigworld" ), fixture.db().mysql.databaseName() );
	CHECK_EQUAL( 5u, fixture.db().mysql.numConnections() );
	CHECK_EQUAL( 2048u, fixture.db().mysql.maxSpaceDataSize() );
	CHECK_EQUAL( BW::string( "utf8" ),
		fixture.db().mysql.unicodeString.characterSet() );
	CHECK_CLOSE( 3600.f, fixture.db().xml.archivePeriod(), 0.0001f );
	CHECK_EQUAL( 5u, fixture.db().xml.numArchives() );
	CHECK( fixture.db().secondaryDB.enable() );
	CHECK_CLOSE( 5.0f, fixture.db().secondaryDB.maxCommitPeriod(), 0.0001f );
	CHECK_EQUAL( BW::string( "server/db/secondary" ),
		fixture.db().secondaryDB.directory() );
	CHECK_EQUAL( BW::string( "/tmp/" ),
		fixture.db().secondaryDB.consolidation.directory() );

	// The paths are assigned before the value is looked up, so they are
	// still meaningful even when the lookup failed.
	CHECK_EQUAL( BW::string( "db/mysql/port" ), fixture.db().mysql.port.path() );
}


// postInit() rejects a MySQL port above the 16-bit range and an empty
// database name; the largest 16-bit port is still accepted.
TEST( DBConfig_mysqlPostInitValidation )
{
	{
		DataSectionPtr pRoot = installConfig( FULL_CONFIG );
		pRoot->writeString( "db/mysql/port", "65535" );

		TopLevelFixture fixture;
		CHECK( fixture.isGood() );
		CHECK_EQUAL( 65535u, fixture.db().mysql.port() );
	}

	{
		DataSectionPtr pRoot = installConfig( FULL_CONFIG );
		pRoot->writeString( "db/mysql/port", "65536" );

		TopLevelFixture fixture;
		CHECK( !fixture.isGood() );
	}

	{
		DataSectionPtr pRoot = installConfig( FULL_CONFIG );
		pRoot->writeString( "db/mysql/databaseName", "" );

		TopLevelFixture fixture;
		CHECK( !fixture.isGood() );
	}
}


// postInit() repairs the two options that must not stay at zero: a zero
// connection count is raised to one (with a warning), and a zero maximum
// space data size is raised to one silently.  Neither is an error.
TEST( DBConfig_mysqlPostInitClampsZeroValues )
{
	{
		DataSectionPtr pRoot = installConfig( FULL_CONFIG );
		pRoot->writeString( "db/mysql/numConnections", "0" );
		pRoot->writeString( "db/mysql/maxSpaceDataSize", "0" );

		TopLevelFixture fixture;
		CHECK( fixture.isGood() );
		CHECK_EQUAL( 1u, fixture.db().mysql.numConnections() );
		CHECK_EQUAL( 1u, fixture.db().mysql.maxSpaceDataSize() );
	}
}


// Options and sections that moved to a different block are reported as
// errors wherever they turn up, and so is the <dbMgr> section the
// top-level block is only there to detect.
TEST( DBConfig_obsoleteNamesAreRejected )
{
	{
		DataSectionPtr pRoot = installConfig( FULL_CONFIG );
		pRoot->openSection( "dbMgr", /* makeNewSection */ true );

		TopLevelFixture fixture;
		CHECK( !fixture.isGood() );
	}

	// MySQL options inside <db> itself rather than <db/mysql>.
	{
		DataSectionPtr pRoot = installConfig( FULL_CONFIG );
		pRoot->openSection( "db/host", /* makeNewSection */ true );

		TopLevelFixture fixture;
		CHECK( !fixture.isGood() );
	}

	// The same MySQL options in <dbApp>.
	{
		DataSectionPtr pRoot = installConfig( FULL_CONFIG );
		pRoot->openSection( "dbApp/port", /* makeNewSection */ true );

		TopLevelFixture fixture;
		CHECK( !fixture.isGood() );
	}

	// Secondary database options left behind in <baseApp>.
	{
		DataSectionPtr pRoot = installConfig( FULL_CONFIG );
		pRoot->openSection( "baseApp/secondaryDB", /* makeNewSection */ true );

		TopLevelFixture fixture;
		CHECK( !fixture.isGood() );
	}
}


// secondsToTicks() multiplies by the configured update rate, rounds to
// the nearest tick, and never returns less than the caller's lower bound.
TEST( DBConfig_secondsToTicks )
{
	installConfig( FULL_CONFIG );

	TopLevelFixture fixture;

	CHECK_EQUAL( 50u, fixture.block().secondsToTicks( 5.0f, 1U ) );
	// 0.25s at 10Hz is 2.5 ticks, which rounds up to 3.
	CHECK_EQUAL( 3u, fixture.block().secondsToTicks( 0.25f, 1U ) );
	// 0.14s is 1.4 ticks and rounds down to 1.
	CHECK_EQUAL( 1u, fixture.block().secondsToTicks( 0.14f, 1U ) );
	// A period that rounds to zero ticks is held at the lower bound...
	CHECK_EQUAL( 1u, fixture.block().secondsToTicks( 0.0f, 1U ) );
	// ... and a larger lower bound wins over the computed value.
	CHECK_EQUAL( 7u, fixture.block().secondsToTicks( 0.0f, 7U ) );
	CHECK_EQUAL( 10000000u, fixture.block().secondsToTicks( 1.0e6f, 1U ) );

	// The rate comes from the configuration, so a different file gives a
	// different conversion.
	{
		DataSectionPtr pRoot = installConfig( FULL_CONFIG );
		pRoot->writeString( "gameUpdateHertz", "20" );

		TopLevelFixture doubleRate;
		CHECK_EQUAL( 20u, doubleRate.block().updateHertz() );
		CHECK_EQUAL( 50u, doubleRate.block().secondsToTicks( 2.5f, 1U ) );
	}
}


// maxCommitPeriodInTicks() is the maxCommitPeriod option in ticks,
// converted through the top-level rate, with a lower bound of one tick.
TEST( DBConfig_maxCommitPeriodInTicks )
{
	DataSectionPtr pRoot = installConfig( FULL_CONFIG );

	{
		TopLevelFixture fixture;
		CHECK_EQUAL( 25u, fixture.db().secondaryDB.maxCommitPeriodInTicks() );
	}

	// Zero seconds converts to no ticks at all, so the lower bound applies.
	pRoot->writeString( "db/secondaryDB/maxCommitPeriod", "0" );

	{
		TopLevelFixture fixture;
		CHECK_EQUAL( 1u, fixture.db().secondaryDB.maxCommitPeriodInTicks() );
	}
}


// get() is the accessor the rest of the engine uses: it builds the
// top-level block once, keeps it alive so its obsolete-name checks run,
// and publishes it through pTopLevelConfig.  This test is the only caller
// of get() in the binary, so the singleton is built from the
// configuration installed here.
TEST( DBConfig_getBuildsTheSingleton )
{
	installConfig( FULL_CONFIG );

	const DBConfig::DBConfigBlock & config = DBConfig::get();

	CHECK( config.isGood() );
	CHECK_EQUAL( BW::string( "mysql" ), config.type() );
	CHECK_EQUAL( 3307u, config.mysql.port() );

	// The singleton is kept, and it is the block behind pTopLevelConfig.
	CHECK( DBConfig::pTopLevelConfig != NULL );
	CHECK( &config == &( DBConfig::pTopLevelConfig->db ) );

	// A second call hands back the same object rather than rebuilding it.
	CHECK( &config == &( DBConfig::get() ) );
}

BW_END_NAMESPACE

// test_db_config.cpp
