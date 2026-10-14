# Fails unless the built libpython3.13.a carries the BWHooks symbols that
# lib/pyscript/script.cpp links against. Those symbols are the reason the
# vendored interpreter cannot be replaced by vcpkg's python3 port; if they
# disappear the entitydef CMake track loses its Python.
#
# Run as:
#   cmake -DBW_PYTHON_ARCHIVE=<path-to-libpython3.13.a> -P check_symbols.cmake

if(NOT DEFINED BW_PYTHON_ARCHIVE OR NOT EXISTS "${BW_PYTHON_ARCHIVE}")
	message(FATAL_ERROR "check_symbols: libpython3.13.a not built at ${BW_PYTHON_ARCHIVE}")
endif()

set(required_symbols
	BW_Py_malloc
	BW_Py_free
	BW_Py_calloc
	BW_Py_realloc
	BW_Py_getHooks
	BW_Py_setHooks
	BW_Py_memoryTrackingIgnoreBegin
	BW_Py_memoryTrackingIgnoreEnd
)

# --defined-only already limits the output to symbols this archive defines, so
# a whole-word match is the whole check; nm output is "0000... T BW_Py_malloc".
set(missing "")
foreach(symbol ${required_symbols})
	execute_process(
		COMMAND sh -c
			"nm --defined-only '${BW_PYTHON_ARCHIVE}' | grep -q -w '${symbol}'"
		RESULT_VARIABLE match_result
		ERROR_QUIET OUTPUT_QUIET
	)
	if(NOT match_result EQUAL 0)
		list(APPEND missing ${symbol})
	endif()
endforeach()

if(missing)
	message(FATAL_ERROR
		"check_symbols: BWHooks symbols missing from ${BW_PYTHON_ARCHIVE}: ${missing}")
endif()

list(LENGTH required_symbols count)
message(STATUS "check_symbols: all ${count} BWHooks symbols present in libpython3.13.a")
