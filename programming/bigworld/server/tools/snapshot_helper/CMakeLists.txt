CMAKE_MINIMUM_REQUIRED( VERSION 2.8 )
PROJECT( snapshot_helper )

INCLUDE( BWStandardProject )

# A Linux administration tool through and through: it shells LVM
# (lvcreate/lvremove, /usr/sbin/fsfreeze) and getpwuid/waitpid, and needs the
# MySQL headers (mysql/mysql.h, C1083 in the root superproject build, run
# 39016799505). The make chain never built it off Linux; skip the same way
# consolidate_dbs skips without MySQL rather than port the machinery.
IF( BW_PLATFORM_WINDOWS )
	MESSAGE( STATUS "snapshot_helper is Linux-only. Skipping ${PROJECT_NAME} build." )
	RETURN()
ENDIF()

SET( ALL_SRCS
	snapshot_helper.cpp
	snapshot_constants.hpp
)
SOURCE_GROUP( "" FILES ${ALL_SRCS} )

BW_ADD_EXECUTABLE( snapshot_helper ${ALL_SRCS} )


BW_TARGET_LINK_LIBRARIES( snapshot_helper
	build
	server
)

BW_PROJECT_CATEGORY( snapshot_helper "Tools" )
