CMAKE_MINIMUM_REQUIRED( VERSION 2.8 )
PROJECT( clear_auto_load )

INCLUDE( BWStandardProject )


# The MySQL client is a system dependency (make chain: SYSTEM_HAS_MYSQL,
# third_party_mysql.mak - the tool is skipped when the development package
# is missing, same ifneq guard as its Makefile.rules).
FIND_PATH( BW_MYSQL_INCLUDE_DIR mysql/mysql.h )
MARK_AS_ADVANCED( BW_MYSQL_INCLUDE_DIR )
IF( NOT BW_MYSQL_INCLUDE_DIR )
	MESSAGE( STATUS "MySQL development headers not found. Skipping ${PROJECT_NAME} build." )
	RETURN()
ENDIF()
SET( ALL_SRCS
	clear_auto_load.cpp
	clear_auto_load.hpp
	main.cpp
)
SOURCE_GROUP( "" FILES ${ALL_SRCS} )

BW_ADD_EXECUTABLE( clear_auto_load ${ALL_SRCS} )

BW_TARGET_LINK_LIBRARIES( clear_auto_load
	build
	chunk
	db_storage
	db_storage_mysql
	entitydef
	pyscript
	script
	server
)

BW_PROJECT_CATEGORY( clear_auto_load "Tools" )
