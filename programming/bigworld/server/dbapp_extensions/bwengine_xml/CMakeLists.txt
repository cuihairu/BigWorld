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
# libpython is linked directly rather than relying on pyscript's INTERFACE
# entry: every static lib in the set (pyscript, script, entitydef, chunk)
# references the Python C-API, and a MODULE target must carry the interpreter
# import archive itself (LNK2001 on _Py_NoneStruct/PyType_Type without it).
# db is here for the same host-resolution reason: db_storage_xml's
# xml_database.obj calls DBConfig::get(), defined in lib/db/db_config.cpp -
# the make chain's .so resolves it at load time from the dbapp process, a
# MODULE target needs the definition at link (LNK2019, run 38993504232).
BW_TARGET_LINK_LIBRARIES( bwengine_xml
	db
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
	libpython
)

BW_PROJECT_CATEGORY( bwengine_xml "DBApp Extensions" )
