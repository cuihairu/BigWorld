CMAKE_MINIMUM_REQUIRED( VERSION 2.8 )
PROJECT( bwengine_mysql )

INCLUDE( BWStandardProject )
INCLUDE( BWStandardLibrary )

# The MySQL client is a system dependency (make chain: SYSTEM_HAS_MYSQL,
# third_party_mysql.mak - the component is skipped when the development
# package is missing). Windows build hosts carry no MySQL dev package, so the
# engine and its storage library are skipped there exactly like the make
# chain skips them.
FIND_PATH( BW_MYSQL_INCLUDE_DIR mysql/mysql.h )
MARK_AS_ADVANCED( BW_MYSQL_INCLUDE_DIR )
IF( NOT BW_MYSQL_INCLUDE_DIR )
	MESSAGE( STATUS "MySQL development headers not found. Skipping bwengine_mysql build." )
	RETURN()
ENDIF()

SET( ALL_SRCS
	mysql_engine_creator.cpp
)
SOURCE_GROUP( "" FILES ${ALL_SRCS} )

BW_ADD_LIBRARY( bwengine_mysql MODULE
	${ALL_SRCS}
)

# MODULE (DLL on MSVC) targets resolve every symbol at link time, and the
# creator's references (createMySqlDatabase, DatabaseEngineCreator base,
# LogMsg, bw_new/bw_delete) live in the mysql storage lib and cstdmf - the
# make chain gets these from dependsOn = db_storage_mysql plus the standard
# component libs. Mirrors the dbapp executable's link set minus the db lib.
BW_TARGET_LINK_LIBRARIES( bwengine_mysql
	db_storage
	db_storage_mysql
	server
	chunk
	entitydef
	pyscript
	script
	connection
	urlrequest
	network
	resmgr
	math
	cstdmf
	zip
	${BW_MYSQL_LIBRARY}
)

BW_PROJECT_CATEGORY( bwengine_mysql "DBApp Extensions" )
