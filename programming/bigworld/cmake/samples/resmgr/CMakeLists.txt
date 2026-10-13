# Standalone CMake sample for lib/resmgr (Linux server profile).
#
# NOT part of the Windows CMake superproject (the per-module CMakeLists.txt
# under lib/ that uses BWStandardProject stays the Windows track). This sample
# is the Linux CMake-track counterpart of the make chain:
#   make counterpart : lib/resmgr/Makefile.rules (libresmgr) + unit_test/ (resmgr_test)
#   flags/defines    : copied verbatim from `make -n -B bw-run-test-resmgr` output
#   dependencies     : unit_test_lib + CppUnitLite2 are built here from source
#                      so the test binary links exactly what make links
#                      (-lunit_test_lib -lCppUnitLite2 -lresmgr -lcstdmf -lmath -lzip -lm -lpthread)
#
# This sample is the bottom-up next step after cmake/samples/cstdmf: it reuses
# the exact same helper/cstdmf source lists and adds math, zip (vcpkg zlib) and
# resmgr. connection/network/entitydef can then be layered on top the same way.
#
# One-command build + test: scripts/build-resmgr-cmake.sh
# Manual configure:
#   cmake -S programming/bigworld/cmake/samples/resmgr \
#     -B programming/bigworld/build/cmake-resmgr \
#     -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
#   cmake --build programming/bigworld/build/cmake-resmgr -j
#   ctest --test-dir programming/bigworld/build/cmake-resmgr --output-on-failure
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

project(bigworld_resmgr_sample LANGUAGES CXX)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND NOT DEFINED VCPKG_TOOLCHAIN)
	message(FATAL_ERROR
		"This sample expects the vcpkg toolchain (manifest mode). Configure with:\n"
		"  -DCMAKE_TOOLCHAIN_FILE=\$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake\n"
		"or run scripts/build-resmgr-cmake.sh")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
	message(FATAL_ERROR "gcc >= 14 required (C++23), found ${CMAKE_CXX_COMPILER_VERSION}")
endif()

# --- Flags copied from the make hybrid server profile (-O3 -g, C++23) --------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF) # make profile does not use LTO

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

# zlib: make links vcpkg zlib staged as libzip.a (third_party_vcpkg.mak).
find_package(ZLIB REQUIRED)

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
# base_resmgr_unit_test_harness.cpp references BWResource, so this object only
# resolves when the final link has resmgr in its group (as make does with
# --start-group). It is not pulled in by consumers that never use the harness
# (same reason cmake/samples/cstdmf builds without resmgr).
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
	${BW_LIB_DIR}/cstdmf/bw_util_linux.cpp
	${BW_LIB_DIR}/cstdmf/callstack.cpp
	${BW_LIB_DIR}/cstdmf/callstack_linux.cpp
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

# --- math (lib/math/Makefile.rules, 12 TUs, Linux server profile) -----------
# Windows/macosx/other TUs (blend_transform, convex_hull, frustum_hull,
# loose_octree, loose_octree_logging, oriented_bbox, perlin_noise, polygon,
# polyhedron, simplex_noise, sphere, pch) are not in the make cxxSource;
# mirrored here.
add_library(math STATIC
	${BW_LIB_DIR}/math/boundbox.cpp
	${BW_LIB_DIR}/math/ema.cpp
	${BW_LIB_DIR}/math/linear_lut.cpp
	${BW_LIB_DIR}/math/math_extra.cpp
	${BW_LIB_DIR}/math/matrix.cpp
	${BW_LIB_DIR}/math/planeeq.cpp
	${BW_LIB_DIR}/math/portal2d.cpp
	${BW_LIB_DIR}/math/quat.cpp
	${BW_LIB_DIR}/math/vector2.cpp
	${BW_LIB_DIR}/math/vector3.cpp
	${BW_LIB_DIR}/math/vector4.cpp
	${BW_LIB_DIR}/math/stat_watcher_creator.cpp
)

# --- resmgr (lib/resmgr/Makefile.rules, 25 TUs, Linux server profile) -------
# win_file_streamer/win_file_system/resource_modification_listener/
# filename_case_checker/hierarchical_config/pch are outside the make list;
# mirrored here.
add_library(resmgr STATIC
	${BW_LIB_DIR}/resmgr/access_monitor.cpp
	${BW_LIB_DIR}/resmgr/auto_config.cpp
	${BW_LIB_DIR}/resmgr/bdiff.cpp
	${BW_LIB_DIR}/resmgr/binary_block.cpp
	${BW_LIB_DIR}/resmgr/bin_section.cpp
	${BW_LIB_DIR}/resmgr/bundiff.cpp
	${BW_LIB_DIR}/resmgr/bwresource.cpp
	${BW_LIB_DIR}/resmgr/dataresource.cpp
	${BW_LIB_DIR}/resmgr/datasection.cpp
	${BW_LIB_DIR}/resmgr/data_section_cache.cpp
	${BW_LIB_DIR}/resmgr/data_section_census.cpp
	${BW_LIB_DIR}/resmgr/dir_section.cpp
	${BW_LIB_DIR}/resmgr/multi_file_system.cpp
	${BW_LIB_DIR}/resmgr/packed_section.cpp
	${BW_LIB_DIR}/resmgr/primitive_file.cpp
	${BW_LIB_DIR}/resmgr/resource_file_path.cpp
	${BW_LIB_DIR}/resmgr/sanitise_helper.cpp
	${BW_LIB_DIR}/resmgr/string_provider.cpp
	${BW_LIB_DIR}/resmgr/unix_file_system.cpp
	${BW_LIB_DIR}/resmgr/file_handle_streamer.cpp
	${BW_LIB_DIR}/resmgr/resmgr_test_utils.cpp
	${BW_LIB_DIR}/resmgr/xml_section.cpp
	${BW_LIB_DIR}/resmgr/xml_special_chars.cpp
	${BW_LIB_DIR}/resmgr/zip_file_system.cpp
	${BW_LIB_DIR}/resmgr/zip_section.cpp
)
target_compile_definitions(resmgr PRIVATE
	BWENTITY_DLL_EXPORT
	HAVE_ZIP
	CSTDMF_IMPORT
)

# exportFor = bwentity for resmgr/math; consumers import with BWENTITY_DLL_IMPORT
target_compile_definitions(math PRIVATE
	BWENTITY_DLL_EXPORT
	CSTDMF_IMPORT
)
target_compile_definitions(cstdmf PRIVATE CSTDMF_EXPORT)

# --- resmgr_test (lib/resmgr/unit_test/Makefile.rules cxxSource, 11 TUs) ----
# pch.cpp is not in the make cxxSource (the pch is applied by the make build
# itself); every test TU includes pch.hpp directly, so it is not listed here.
add_executable(resmgr_test
	${BW_LIB_DIR}/resmgr/unit_test/main.cpp
	${BW_LIB_DIR}/resmgr/unit_test/test_bdiff.cpp
	${BW_LIB_DIR}/resmgr/unit_test/test_bwresource.cpp
	${BW_LIB_DIR}/resmgr/unit_test/test_bwresource_paths.cpp
	${BW_LIB_DIR}/resmgr/unit_test/test_data_resource.cpp
	${BW_LIB_DIR}/resmgr/unit_test/test_datasection.cpp
	${BW_LIB_DIR}/resmgr/unit_test/test_file_system.cpp
	${BW_LIB_DIR}/resmgr/unit_test/test_packed_section.cpp
	${BW_LIB_DIR}/resmgr/unit_test/test_path_resource.cpp
	${BW_LIB_DIR}/resmgr/unit_test/test_xml_section.cpp
	${BW_LIB_DIR}/resmgr/unit_test/test_xml_special_chars.cpp
	${BW_LIB_DIR}/resmgr/unit_test/test_zip_section.cpp
)
target_compile_definitions(resmgr_test PRIVATE
	BWENTITY_DLL_IMPORT
	CSTDMF_IMPORT
)

target_link_libraries(resmgr PRIVATE ZLIB::ZLIB)
target_link_libraries(resmgr_test PRIVATE
	unit_test_lib
	CppUnitLite2
	resmgr
	cstdmf
	math
	ZLIB::ZLIB
	m
	pthread
)
# make links the unit-test binaries with -rdynamic -Wl,-export-dynamic
# (backtrace() symbol names in callstack.cpp depend on it)
target_link_options(resmgr_test PRIVATE -rdynamic -Wl,--export-dynamic)

get_filename_component(BW_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
enable_testing()
add_test(NAME resmgr_test
	COMMAND resmgr_test --root "${BW_REPO_ROOT}"
	WORKING_DIRECTORY "${BW_LIB_DIR}/resmgr/unit_test"
)
