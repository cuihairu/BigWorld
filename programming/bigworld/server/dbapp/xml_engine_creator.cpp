#include "db_engine_creator.hpp"

#include "dbapp_config.hpp"

#include "db_storage_xml/xml_database.hpp"

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
 *
 */
class XMLEngineCreator : public DatabaseEngineCreator
{
public:
	/**
	 *	Constructor.
	 */
	XMLEngineCreator() :
		DatabaseEngineCreator( "xml" )
	{ }


	/*
	 *	Override from DatabaseEngineCreator.
	 */
	IDatabase * createImpl( Mercury::NetworkInterface & interface,
							Mercury::EventDispatcher & dispatcher ) const
	{
		IDatabase * pDatabase = new XMLDatabase();

		if (pDatabase && DBAppConfig::isProduction())
		{
			ERROR_MSG(
				"The XML database is suitable for demonstrations and "
				"evaluations only.\n"
				"Please use the MySQL database for serious development and "
				"production systems.\n"
				"See the Server Operations Guide for instructions on how to "
				"switch to the MySQL database.\n" );
		}

		return pDatabase;
	}
};

namespace // (anonymous)
{

XMLEngineCreator staticInitialiser;

} // end namespace (anonymous)

BW_END_NAMESPACE

// xml_engine_creator.cpp

#if defined( _WIN32 )
#pragma pop_macro( "interface" )
#endif
