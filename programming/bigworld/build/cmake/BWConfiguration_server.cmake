ADD_DEFINITIONS( -DMF_SERVER )

# START_MSG and the service banners print the config name through
# MF_CONFIG. The make chain defines it as a string
# (build/make/Makefile:210, -DMF_CONFIG="$(BW_CONFIG)"); without this
# define the header fallback `#define MF_CONFIG Unknown` names an enum
# member no Windows TU has (C2065 'Unknown' at bwservice.hpp:55, run
# 38999627798). Hybrid is the root server build's canonical configuration.
ADD_DEFINITIONS( -DMF_CONFIG="Hybrid" )

# Add commands for start, stop, status
BW_TARGET_LINUX_COMMAND( start_server 
	"python ${BW_LINUX_DIR}/../tools/server/control_cluster.py start ${BW_LINUX_HOST}" )
BW_TARGET_LINUX_COMMAND( stop_server 
	"python ${BW_LINUX_DIR}/../tools/server/control_cluster.py stop" )
BW_TARGET_LINUX_COMMAND( display_server 
	"python ${BW_LINUX_DIR}/../tools/server/control_cluster.py display" )

IF( BW_UNIT_TESTS_ENABLED )
	MESSAGE( STATUS "Unit tests enabled for server." )
	ENABLE_TESTING()
	
	SET( BW_SERVER_UNIT_TEST_LIBRARIES
		CppUnitLite2		third_party/CppUnitLite2
		unit_test_lib		lib/unit_test_lib
		)

	SET( BW_SERVER_UNIT_TEST_BINARIES
		cstdmf_unit_test		lib/cstdmf/unit_test
		db_unit_test			lib/db/unit_test
		entitydef_unit_test		lib/entitydef/unit_test
		math_unit_test			lib/math/unit_test
		network_unit_test		lib/network/unit_test
		physics2_unit_test		lib/physics2/unit_test
		pyscript_unit_test		lib/pyscript/unit_test
		resmgr_unit_test		lib/resmgr/unit_test
		scene_unit_test			lib/scene/unit_test
		server_unit_test		lib/server/unit_test
		script_unit_test		lib/script/unit_test
		terrain_unit_test		lib/terrain/unit_test
		unit_test_lib_unit_test	lib/unit_test_lib/unit_test
		baseapp_unit_test		server/baseapp/unit_test
		baseappmgr_unit_test	server/baseappmgr/unit_test
		cellapp_unit_test		server/cellapp/unit_test
		cellappmgr_unit_test	server/cellappmgr/unit_test
		dbapp_unit_test			server/dbapp/unit_test
		dbappmgr_unit_test		server/dbappmgr/unit_test
		loginapp_unit_test		server/loginapp/unit_test
		reviver_unit_test		server/reviver/unit_test
		)
ENDIF()

SET( BW_LIBRARY_PROJECTS
	# BigWorld Standard libraries
	network 			lib/network
	resmgr 				lib/resmgr
	math 				lib/math
	cstdmf				lib/cstdmf
	connection_model	lib/connection_model
	memhook				lib/memhook
	build				lib/build

	# All servers
	server				lib/server

	# DBMgr
	db					lib/db
	db_storage			lib/db_storage

	# DBMgr MySQL support
	bwengine_mysql		server/dbapp_extensions/bwengine_mysql
	db_storage_mysql	lib/db_storage_mysql

	# DBMgr XML database support
	bwengine_xml		server/dbapp_extensions/bwengine_xml
	db_storage_xml		lib/db_storage_xml

	# BaseApp
	sqlite				lib/sqlite

	# BaseApp + DBMgr + LoginApp
	connection			lib/connection

	# CellApp 
	waypoint			lib/waypoint
	terrain				lib/terrain
	physics2			lib/physics2
	moo					lib/moo
	chunk_loading		lib/chunk_loading
	
	# BaseApp + CellApp
	delegate_interface	lib/delegate_interface
	entitydef_script	lib/entitydef_script
	scene				lib/scene

	# CellApp
	# Server-mode speedtree is collision-cache only (speedtree_collision.cpp,
	# the make chain's whole lib); chunk's server_chunk_tree.obj references
	# getBSPTree unguarded (LNK2019 in the root superproject build, run
	# 39006994918).
	speedtree			lib/speedtree

	# BaseApp + CellApp + DBMgr
	entitydef			lib/entitydef
	pyscript			lib/pyscript
	chunk				lib/chunk
	urlrequest			lib/urlrequest
	script				lib/script

	# Third party
	libpython			third_party/python
	# lib/resmgr links re2 (lib/resmgr/CMakeLists.txt:149) and every other
	# configuration lists it here, so the vendored tree - which ships its own
	# repo-tailored CMakeLists and builds on MSVC as-is - was simply missing
	# from the server list: bwengine_xml's link then died with LNK1181 on
	# re2.lib (run 38883400603).
	re2					third_party/re2
	# cstdmf links nedalloc on Windows (server or not) for the same reason
	# every other configuration lists it: allocator.cpp's SystemAllocator is
	# BW::NedAllocator there.
	nedalloc			third_party/nedalloc
	
	# Unit test librarys
	${BW_SERVER_UNIT_TEST_LIBRARIES}
)

IF( BW_SPEEDTREE_SUPPORT )
	SET( BW_LIBRARY_PROJECTS ${BW_LIBRARY_PROJECTS}
		speedtree			lib/speedtree
	)
ENDIF()

# zlib/libpng come from vcpkg when a vcpkg toolchain file is in play (the
# find runs again - and the imported targets zip/libpng get built - in
# BWStandardProject.cmake, which every library project includes). The vendored
# trees are only added here as the fallback while they exist, so their library
# projects never clash with the vcpkg imported targets of the same name.
FIND_PACKAGE( ZLIB QUIET )
IF( NOT ZLIB_FOUND )
	SET( BW_LIBRARY_PROJECTS ${BW_LIBRARY_PROJECTS}
		zip					third_party/zip
	)
ENDIF()
FIND_PACKAGE( PNG QUIET )
IF( NOT PNG_FOUND )
	SET( BW_LIBRARY_PROJECTS ${BW_LIBRARY_PROJECTS}
		libpng				third_party/png/projects/visualc71
	)
ENDIF()

SET( BW_BINARY_PROJECTS
	baseapp				server/baseapp
	baseappmgr			server/baseappmgr
	cellapp				server/cellapp
	cellappmgr			server/cellappmgr
	dbapp				server/dbapp
	dbappmgr			server/dbappmgr
	loginapp			server/loginapp
	reviver				server/reviver
	
	# Tools
	bwmachined			server/tools/bwmachined
	bots				server/tools/bots
	clear_auto_load		server/tools/clear_auto_load
	consolidate_dbs		server/tools/consolidate_dbs
	message_logger		server/tools/message_logger
	_bw_profile			server/tools/bw_profile
	snapshot_helper		server/tools/snapshot_helper
	sync_db				server/tools/sync_db
	transfer_db			server/tools/transfer_db

	# examples/cellapp_extension imports the host's classes (Entity,
	# Controller, EntityType) out of the running cellapp executable. A Linux
	# shared module binds those to the exe's exports at load time; an MSVC
	# exe exports nothing unless told to, so the module has nothing to bind
	# to - 53 LNK2019/2001 in the superproject stage-1 build (run
	# 39439040925). Exporting the host interface is a design item of its own
	# (todo.md R3); until it lands the example stays out of the Windows
	# build, where nothing consumes it.
	IF( NOT MSVC )
		SET( BW_BINARY_PROJECTS ${BW_BINARY_PROJECTS}
			cellapp_extension	examples/cellapp_extension
		)
	ENDIF()

	${BW_SERVER_UNIT_TEST_BINARIES}
)

SET( BW_PCH_SUPPORT OFF )
SET( BW_FMOD_SUPPORT OFF )
SET( BW_SCALEFORM_SUPPORT OFF )
SET( BW_UMBRA_SUPPORT OFF )
