CMAKE_MINIMUM_REQUIRED( VERSION 2.8 )
PROJECT( cellapp_extension )

INCLUDE( BWStandardProject )

SET( ALL_SRCS
	bounce_grenade_extra.cpp
	bounce_grenade_extra.hpp
	debug_extension.cpp
	dump_controllers_extra.cpp
	dump_controllers_extra.hpp
	egcontroller.cpp
	egcontroller.hpp
	egextra.cpp
	egextra.hpp
	speed_controller.cpp
	speed_controller.hpp
	speed_extra.cpp
	speed_extra.hpp
)
SOURCE_GROUP( "" FILES ${ALL_SRCS} )

BW_ADD_LIBRARY( cellapp_extension MODULE ${ALL_SRCS} )

# MODULE (DLL on MSVC) targets resolve every symbol at link time, and the
# example's references span the Python C-API (PyType_Type, PyErr_*, via the
# libpython import target), pyscript (Script::setData/getData,
# PyObjectPlus::s_type_), cstdmf (bw_debugMunge, watchers) and cellapp itself
# (Entity, Controller, Singleton<CellApp>). The make chain relies on the
# default BW_DEPENDS_ON set plus load-time resolution; a MODULE target
# carries neither here, so it needs the explicit list. Mirrors the cellapp
# executable's link set plus libpython, same shape as the bwengine_* modules.
BW_TARGET_LINK_LIBRARIES( cellapp_extension
	db
	server
	script
	pyscript
	entitydef
	entitydef_script
	waypoint
	chunk_loading
	chunk
	scene
	terrain
	physics2
	moo
	libpng
	delegate_interface
	urlrequest
	network
	resmgr
	math
	cstdmf
	zip
	libpython
)

# The example's TUs compile against cellapp/entitydef headers that reach
# <Python.h> -> pyconfig.h, which pythoncore only generates during the
# bwpython313 ExternalProject build. Unlike the modules that link libpython
# (and inherit its add_dependencies on bwpython313), a MODULE target has no
# such link, so without this its compilation races the ExternalProject and
# every TU died on C1083 'pyconfig.h' in the root superproject build.
IF( TARGET bwpython313 )
	ADD_DEPENDENCIES( cellapp_extension bwpython313 )
ENDIF()

# The example extends cellapp and compiles against its headers
# ("cellapp/controller.hpp" et al), which live in the server tree - the same
# include the cellapp module itself grants its own TUs
# (server/cellapp/CMakeLists.txt). The legacy VS project carried this
# additional include dir; without it every TU fails with
# "'cellapp/controller.hpp': No such file or directory".
# The source root itself is the other cellapp include (make track:
# -I$(BW_ABS_SRC) in server/cellapp/Makefile.rules) - bounce_grenade_extra.cpp
# reaches "common/closest_triangle.hpp" through it (C1083 in the root
# superproject build).
TARGET_INCLUDE_DIRECTORIES( cellapp_extension PRIVATE
	${BW_SOURCE_DIR}
	${BW_SOURCE_DIR}/server )

BW_PROJECT_CATEGORY( cellapp_extension "Examples" )
