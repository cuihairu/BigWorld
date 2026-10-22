# Standalone CMake sample for lib/cstdmf (Linux server profile).
#
# NOT part of the Windows CMake superproject (the per-module CMakeLists.txt
# under lib/ that uses BWStandardProject stays the Windows track). This sample
# is the Linux CMake-track counterpart of the make chain:
#   make counterpart : lib/cstdmf/Makefile.rules (libcstdmf) + unit_test/ (cstdmf_test)
#   flags/defines    : copied verbatim from `make -n unit_test_cstdmf` output
#   dependencies     : unit_test_lib + CppUnitLite2 are built here from source
#                      so the test binary links exactly what make links
#                      (-lunit_test_lib -lCppUnitLite2 -lcstdmf -lm -lpthread)
#
# One-command build + test: scripts/build-cstdmf-cmake.sh
# Manual configure:
#   cmake -S programming/bigworld/cmake/samples/cstdmf \
#     -B programming/bigworld/build/cmake-cstdmf \
#     -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
#   cmake --build programming/bigworld/build/cmake-cstdmf -j
#   ctest --test-dir programming/bigworld/build/cmake-cstdmf --output-on-failure
#
# Dependencies come from the shared manifest (programming/bigworld/vcpkg.json,
# pinned builtin-baseline); first configure installs them into the build dir
# via vcpkg manifest mode, afterwards the binary cache makes it a no-op.
# Make stays the primary entry point; keep the source lists below in sync with
# Makefile.rules when adding files.

cmake_minimum_required(VERSION 3.20)

# --- vcpkg manifest-mode defaults (must be set before project()) -------------
if(NOT DEFINED VCPKG_MANIFEST_DIR)
	set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
endif()
if(NOT DEFINED VCPKG_OVERLAY_TRIPLETS)
	set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_LIST_DIR}/../../../build/vcpkg-triplets")
endif()
if(NOT DEFINED VCPKG_TARGET_TRIPLET AND UNIX AND NOT APPLE)
	set(VCPKG_TARGET_TRIPLET "x64-linux-bw")
endif()

project(bigworld_cstdmf_sample LANGUAGES CXX)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND NOT DEFINED VCPKG_TOOLCHAIN)
	message(FATAL_ERROR
		"This sample expects the vcpkg toolchain (manifest mode). Configure with:\n"
		"  -DCMAKE_TOOLCHAIN_FILE=\$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake\n"
		"or run scripts/build-cstdmf-cmake.sh")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
	message(FATAL_ERROR "gcc >= 14 required (C++23), found ${CMAKE_CXX_COMPILER_VERSION}")
endif()

# --- Flags copied from the make hybrid server profile (-O3 -g, C++23) --------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF) # make profile does not use LTO

if(MSVC)
	add_compile_options(
		/MP /W3 /Zi /WX-
	)
else()
	add_compile_options(
		-m64 -msse3 -pipe
		-Wall -Wextra -Werror
		-Wfloat-equal
		-Wno-deprecated -Wno-uninitialized -Wno-char-subscripts
		-Wno-non-virtual-dtor -Wno-write-strings
		-Wno-unused-parameter
		-Wno-error=expansion-to-defined
		-Wno-error=deprecated-declarations
		-Wno-error=deprecated-copy
		-fno-strict-aliasing
		-g -O3
	)
endif()

add_compile_definitions(
	MF_SERVER
	MF_CONFIG="hybrid"
	BW_BUILD_PLATFORM="el7"
	BWENTITY_DLL_IMPORT
	CODE_INLINE
	MF_USE_ASSERTS
	_HYBRID
)

# vcpkg manifest-mode installs into <build>/vcpkg_installed; make adds the
# same directory's include path to every TU.
if(NOT DEFINED VCPKG_INSTALLED_DIR)
	set(VCPKG_INSTALLED_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed")
endif()
include_directories("${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")

set(BW_LIB_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../lib")
set(BW_THIRD_PARTY_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../third_party")
include_directories("${BW_LIB_DIR}" "${BW_THIRD_PARTY_DIR}")

# --- CppUnitLite2 (third_party/CppUnitLite2/src/Makefile.rules, 7 TUs) ------
add_library(CppUnitLite2 STATIC
	${BW_THIRD_PARTY_DIR}/CppUnitLite2/src/ExceptionHandler.cpp
	${BW_THIRD_PARTY_DIR}/CppUnitLite2/src/Failure.cpp
	${BW_THIRD_PARTY_DIR}/CppUnitLite2/src/Test.cpp
	${BW_THIRD_PARTY_DIR}/CppUnitLite2/src/TestException.cpp
	${BW_THIRD_PARTY_DIR}/CppUnitLite2/src/TestRegistry.cpp
	${BW_THIRD_PARTY_DIR}/CppUnitLite2/src/TestResult.cpp
	${BW_THIRD_PARTY_DIR}/CppUnitLite2/src/TestResultStdErr.cpp
)
# Linux/SignalHandler.cpp and Win32/TestResultDebugOut.cpp are not in the
# make cxxSource either; do not "fix" that here.

# --- unit_test_lib (lib/unit_test_lib/Makefile.rules, 4 TUs) -----------------
add_library(unit_test_lib STATIC
	${BW_LIB_DIR}/unit_test_lib/unit_test.cpp
	${BW_LIB_DIR}/unit_test_lib/multi_proc_test_case.cpp
	${BW_LIB_DIR}/unit_test_lib/TestResultBWOut.cpp
	${BW_LIB_DIR}/unit_test_lib/base_resmgr_unit_test_harness.cpp
)

# --- cstdmf (lib/cstdmf/Makefile.rules, 57 TUs, Linux server profile) --------
# Keep in sync with Makefile.rules: windows/macosx TUs (bw_util_windows,
# bw_util_macosx, callstack_windows) and files outside the make list
# (bw_hash64, command_line, date_time_utils, debug_exception_filter,
# external_profiler, main_loop_task, memory_load, ned_allocator, message_box,
# critical_message_box, launch_windows_process, pch) are intentionally absent.
add_library(cstdmf STATIC
	${BW_LIB_DIR}/cstdmf/allocator.cpp
	${BW_LIB_DIR}/cstdmf/ansi_allocator.cpp
	${BW_LIB_DIR}/cstdmf/protected_allocator.cpp
	${BW_LIB_DIR}/cstdmf/bw_safe_allocatable.cpp
	${BW_LIB_DIR}/cstdmf/arg_parser.cpp
	${BW_LIB_DIR}/cstdmf/background_file_writer.cpp
	${BW_LIB_DIR}/cstdmf/base64.cpp
	${BW_LIB_DIR}/cstdmf/bgtask_manager.cpp
	${BW_LIB_DIR}/cstdmf/binaryfile.cpp
	${BW_LIB_DIR}/cstdmf/bit_reader.cpp
	${BW_LIB_DIR}/cstdmf/bit_writer.cpp
	${BW_LIB_DIR}/cstdmf/blob_or_null.cpp
	${BW_LIB_DIR}/cstdmf/binary_stream.cpp
	${BW_LIB_DIR}/cstdmf/bwrandom.cpp
	${BW_LIB_DIR}/cstdmf/bwversion.cpp
	${BW_LIB_DIR}/cstdmf/bw_hash.cpp
	${BW_LIB_DIR}/cstdmf/bw_memory.cpp
	${BW_LIB_DIR}/cstdmf/bw_platform_info.cpp
	${BW_LIB_DIR}/cstdmf/bw_string_ref.cpp
	${BW_LIB_DIR}/cstdmf/bw_util.cpp
	$<$<PLATFORM_ID:Linux>:${BW_LIB_DIR}/cstdmf/bw_util_linux.cpp>
	$<$<PLATFORM_ID:Windows>:${BW_LIB_DIR}/cstdmf/bw_util_windows.cpp>
	${BW_LIB_DIR}/cstdmf/callstack.cpp
	$<$<PLATFORM_ID:Linux>:${BW_LIB_DIR}/cstdmf/callstack_linux.cpp>
	$<$<PLATFORM_ID:Windows>:${BW_LIB_DIR}/cstdmf/callstack_windows.cpp>
	${BW_LIB_DIR}/cstdmf/checksum_stream.cpp
	${BW_LIB_DIR}/cstdmf/concurrency.cpp
	${BW_LIB_DIR}/cstdmf/cpuinfo.cpp
	${BW_LIB_DIR}/cstdmf/critical_error_handler.cpp
	${BW_LIB_DIR}/cstdmf/cstdmf_init.cpp
	${BW_LIB_DIR}/cstdmf/debug.cpp
	${BW_LIB_DIR}/cstdmf/debug_filter.cpp
	${BW_LIB_DIR}/cstdmf/debug_message_file_logger.cpp
	${BW_LIB_DIR}/cstdmf/debug_message_source.cpp
	${BW_LIB_DIR}/cstdmf/dogwatch.cpp
	${BW_LIB_DIR}/cstdmf/dprintf.cpp
	${BW_LIB_DIR}/cstdmf/grow_only_pod_allocator.cpp
	${BW_LIB_DIR}/cstdmf/fini_job.cpp
	${BW_LIB_DIR}/cstdmf/fixed_sized_allocator.cpp
	${BW_LIB_DIR}/cstdmf/intrusive_object.cpp
	${BW_LIB_DIR}/cstdmf/locale.cpp
	${BW_LIB_DIR}/cstdmf/log_meta_data.cpp
	${BW_LIB_DIR}/cstdmf/log_msg.cpp
	${BW_LIB_DIR}/cstdmf/md5.cpp
	${BW_LIB_DIR}/cstdmf/memory_debug.cpp
	${BW_LIB_DIR}/cstdmf/memory_stream.cpp
	${BW_LIB_DIR}/cstdmf/polymorphic_watcher.cpp
	${BW_LIB_DIR}/cstdmf/profile.cpp
	${BW_LIB_DIR}/cstdmf/profiler.cpp
	${BW_LIB_DIR}/cstdmf/slot_tracker.cpp
	${BW_LIB_DIR}/cstdmf/stack_tracker.cpp
	${BW_LIB_DIR}/cstdmf/string_utils.cpp
	${BW_LIB_DIR}/cstdmf/string_builder.cpp
	${BW_LIB_DIR}/cstdmf/task_watcher.cpp
	${BW_LIB_DIR}/cstdmf/time_queue.cpp
	${BW_LIB_DIR}/cstdmf/timestamp.cpp
	${BW_LIB_DIR}/cstdmf/unique_id.cpp
	${BW_LIB_DIR}/cstdmf/watcher.cpp
	${BW_LIB_DIR}/cstdmf/watcher_path_request.cpp
)
target_compile_definitions(cstdmf PRIVATE CSTDMF_EXPORT) # exportFor = cstdmf

# consumers get CSTDMF_IMPORT like the make build (explicit-instantiation
# declarations; see cstdmf_dll.hpp)
target_compile_definitions(CppUnitLite2 PRIVATE CSTDMF_IMPORT)
target_compile_definitions(unit_test_lib PRIVATE CSTDMF_IMPORT)

# --- cstdmf_test (lib/cstdmf/unit_test/Makefile.rules cxxSource, 49 TUs) -----
# test_object_pool.cpp exists on disk but is NOT in the make cxxSource (does
# not compile: object_pool.hpp:343 needs LookUpTable::reserve(), see the
# comment in unit_test/Makefile.rules); mirrored here.
add_executable(cstdmf_test
	${BW_LIB_DIR}/cstdmf/unit_test/main.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_aligned_allocate.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_allocate.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_ansi_allocator.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_arg_parser.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_asserts.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_background_file_writer.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_base64.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_bgtasks.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_binary_file.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_bit_reader.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_blob_or_null.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_bw_containers.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_bw_hash.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_bw_platform_info.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_bwrandom.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_bw_safe_allocatable.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_bw_snprintf.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_bw_string_ref.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_bw_util.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_bwversion.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_checksum_stream.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_circular_queue.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_concurrency.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_cpuinfo.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_debug_message_source.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_debug.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_dependency_graph.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_dogwatch.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_fixed_sized_allocator.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_float.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_grow_only_pod_allocator.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_log_meta_data.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_log_msg.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_md5.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_memory_debug.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_profiler.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_smartpointer.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_static_array.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_string_builder.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_stringmap.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_string_utils_extra.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_string_utils.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_time_queue.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_timestamp.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_unique_id.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_watcher_gaps.cpp
	${BW_LIB_DIR}/cstdmf/unit_test/test_watcher.cpp
)
target_compile_definitions(cstdmf_test PRIVATE CSTDMF_IMPORT)
if(NOT MSVC)
	target_compile_options(cstdmf_test PRIVATE -Wno-float-equal) # unit_test profile
endif()

target_link_libraries(cstdmf_test PRIVATE
	unit_test_lib
	CppUnitLite2
	cstdmf
)
if(NOT WIN32)
	target_link_libraries(cstdmf_test PRIVATE m pthread)
	target_link_options(cstdmf_test PRIVATE -rdynamic -Wl,--export-dynamic)
endif()

get_filename_component(BW_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
enable_testing()
add_test(NAME cstdmf_test
	COMMAND cstdmf_test --root "${BW_REPO_ROOT}"
	WORKING_DIRECTORY "${BW_LIB_DIR}/cstdmf/unit_test"
)
