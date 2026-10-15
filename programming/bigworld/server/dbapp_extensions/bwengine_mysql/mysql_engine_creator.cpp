#include "cstdmf/bw_namespace.hpp"

#include "db_storage/db_engine_creator.hpp"

#include "db_storage_mysql/mysql_database_creation.hpp"

#if defined( _WIN32 )
// MSVC's SDK leaves `#define interface struct` (rpcndr.h via d3d9.h / shlobj /
// shlwapi / d3d10) live for the rest of the TU, which rewrites the
// `interface`-named declarations in this file into syntax errors (C2332/C2027
// in the root superproject build). Off for this file only; the pop below
// restores it for whatever SDK COM header comes later in the TU.
#pragma push_macro( "interface" )
#undef interface
#endif


BW_BEGIN_NAMESPACE

/**
 *	MySql database engine creator.
 */
class MySqlEngineCreator : public DatabaseEngineCreator
{
public:
	/**
	 *	Constructor.
	 */
	MySqlEngineCreator() :
		DatabaseEngineCreator( "mysql" )
	{ }


	/*
	 *	Override from DatabaseEngineCreator
	 */
	IDatabase * createImpl( DatabaseEngineData & dbEngineData ) const
	{
		return createMySqlDatabase( dbEngineData.interface(),
									dbEngineData.dispatcher() );
	}
};

namespace // (anonymous)
{

MySqlEngineCreator staticInitialiser;

} // end namespace (anonymous)

BW_END_NAMESPACE

// mysql_engine_creator.cpp

#if defined( _WIN32 )
#pragma pop_macro( "interface" )
#endif
