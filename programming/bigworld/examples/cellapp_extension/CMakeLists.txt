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
TARGET_INCLUDE_DIRECTORIES( cellapp_extension PRIVATE ${BW_SOURCE_DIR}/server )

BW_PROJECT_CATEGORY( cellapp_extension "Examples" )
