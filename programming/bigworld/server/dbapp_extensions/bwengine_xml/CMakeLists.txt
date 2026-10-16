CMAKE_MINIMUM_REQUIRED( VERSION 2.8 )
PROJECT( bwengine_xml )

INCLUDE( BWStandardProject )
INCLUDE( BWStandardLibrary )

SET( ALL_SRCS
	xml_engine_creator.cpp
)
SOURCE_GROUP( "" FILES ${ALL_SRCS} )

BW_ADD_LIBRARY( bwengine_xml MODULE
	${ALL_SRCS}
)

# MODULE (DLL on MSVC) targets resolve every symbol at link time, and the
# creator's references (XMLDatabase, DatabaseEngineCreator base, LogMsg,
# bw_new/bw_delete) live in the storage libs and cstdmf - the make chain gets
# these from dependsOn = db_storage_xml plus the standard component libs.
# Mirrors the dbapp executable's link set minus the db lib itself.
BW_TARGET_LINK_LIBRARIES( bwengine_xml
	db_storage
	db_storage_xml
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
)

BW_PROJECT_CATEGORY( bwengine_xml "DBApp Extensions" )
