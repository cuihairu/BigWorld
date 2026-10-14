# Standalone CMake sample for lib/entitydef (Linux server profile).
#
# NOT part of the Windows CMake superproject (the per-module CMakeLists.txt
# under lib/ that uses BWStandardProject stays the Windows track). This sample
# is the Linux CMake-track counterpart of the make chain:
#   make counterpart : lib/entitydef/Makefile.rules (libentitydef) +
#                      unit_test/ (entitydef_test) + every library in its
#                      dependsOn (pyscript script chunk scene terrain moo
#                      physics2 network resmgr zip math cstdmf) + connection
#                      (network's link dependency)
#   flags/defines    : copied verbatim from `make -n -B bw-run-test-entitydef`
#                      output and platform_common.mak/platform_el7.mak
#   dependencies     : unit_test_lib + CppUnitLite2 + the BW libs above built
#                      here from source + vcpkg zlib/libpng/OpenSSL
#                      (linked as ZLIB::ZLIB/PNG::PNG/OpenSSL::SSL/Crypto,
#                      mirroring make's libzip.a/libpng.a/bwssl/bwcrypto
#                      staging in third_party_vcpkg.mak) + the vendored CPython
#                      from cmake/samples/python (BWPython::Python)
#
# This is the layer the network sample's header reserved: it adds the Python
# leg. entitydef_test links pyscript, which calls the BWHooks symbols
# (BW_Py_malloc/free/calloc/realloc/setHooks) that only the vendored
# third_party/python build provides (Python/bwhooks.c in libpython3.13.a); see
# cmake/samples/python for why vcpkg's python3 port cannot be used here.
#
# One-command build + test: scripts/build-entitydef-cmake.sh
# Manual configure:
#   cmake -S programming/bigworld/cmake/samples/entitydef \
#     -B programming/bigworld/build/cmake-entitydef \
#     -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
#   cmake --build programming/bigworld/build/cmake-entitydef -j
#   ctest --test-dir programming/bigworld/build/cmake-entitydef --output-on-failure
#
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

project(bigworld_entitydef_sample LANGUAGES CXX C)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND NOT DEFINED VCPKG_TOOLCHAIN)
	message(FATAL_ERROR
		"This sample expects the vcpkg toolchain (manifest mode). Configure with:\n"
		"  -DCMAKE_TOOLCHAIN_FILE=\$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake\n"
		"or run scripts/build-entitydef-cmake.sh")
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
	USE_OPENSSL
)

# vcpkg manifest-mode installs into <build>/vcpkg_installed; make adds the
# same directory's include path to every TU (moo's png.cpp and network's
# zip_stream.cpp read <png.h>/<zlib.h> from there).
if(NOT DEFINED VCPKG_INSTALLED_DIR)
	set(VCPKG_INSTALLED_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed")
endif()
include_directories("${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")

find_package(ZLIB REQUIRED)
find_package(PNG REQUIRED)
find_package(OpenSSL REQUIRED)

set(BW_LIB_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../lib")
set(BW_THIRD_PARTY_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../third_party")
include_directories("${BW_LIB_DIR}" "${BW_THIRD_PARTY_DIR}")

# --- vendored CPython (BWHooks symbols for pyscript) -------------------------
# Builds libpython3.13.a via configure+make and exposes BWPython::Python
# (includes + archive + SCRIPT_PYTHON). Included as a subdirectory so this
# sample builds standalone; cmake/samples/python/CMakeLists.txt skips its own
# project() when included this way.
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../python" "${CMAKE_BINARY_DIR}/bwpython-sample")

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

# --- unit_test_lib (lib/unit_test_lib/Makefile.rules, 4 TUs) -----------------
add_library(unit_test_lib STATIC
	${BW_LIB_DIR}/unit_test_lib/unit_test.cpp
	${BW_LIB_DIR}/unit_test_lib/multi_proc_test_case.cpp
	${BW_LIB_DIR}/unit_test_lib/TestResultBWOut.cpp
	${BW_LIB_DIR}/unit_test_lib/base_resmgr_unit_test_harness.cpp
)
target_include_directories(unit_test_lib PUBLIC
	"${BW_THIRD_PARTY_DIR}/CppUnitLite2/src")

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
target_compile_definitions(cstdmf PRIVATE CSTDMF_EXPORT)

# --- math (lib/math/Makefile.rules, 12 TUs, Linux server profile) -----------
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
target_compile_definitions(math PRIVATE BWENTITY_DLL_EXPORT CSTDMF_IMPORT)

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
target_compile_definitions(resmgr PRIVATE BWENTITY_DLL_EXPORT CSTDMF_IMPORT)
target_link_libraries(resmgr PRIVATE cstdmf ZLIB::ZLIB)

# --- network (lib/network/Makefile.rules, 78 TUs, Linux server profile) ------
# net_360.cpp (Xbox 360) and pch.cpp are not in the make cxxSource; mirrored
# here.
add_library(network STATIC
	${BW_LIB_DIR}/network/address_resolver.cpp
	${BW_LIB_DIR}/network/basictypes.cpp
	${BW_LIB_DIR}/network/blocking_reply_handler.cpp
	${BW_LIB_DIR}/network/bsd_snprintf.cpp
	${BW_LIB_DIR}/network/buffered_tcp_endpoint.cpp
	${BW_LIB_DIR}/network/bundle.cpp
	${BW_LIB_DIR}/network/bundle_sending_map.cpp
	${BW_LIB_DIR}/network/bw_message_forwarder.cpp
	${BW_LIB_DIR}/network/channel.cpp
	${BW_LIB_DIR}/network/channel_map.cpp
	${BW_LIB_DIR}/network/channel_owner.cpp
	${BW_LIB_DIR}/network/compression_stream.cpp
	${BW_LIB_DIR}/network/condemned_channels.cpp
	${BW_LIB_DIR}/network/deferred_bundle.cpp
	${BW_LIB_DIR}/network/delayed_channels.cpp
	${BW_LIB_DIR}/network/elliptic_curve_checksum_scheme.cpp
	${BW_LIB_DIR}/network/encryption_filter.cpp
	${BW_LIB_DIR}/network/encryption_stream_filter.cpp
	${BW_LIB_DIR}/network/endpoint.cpp
	${BW_LIB_DIR}/network/error_reporter.cpp
	${BW_LIB_DIR}/network/event_dispatcher.cpp
	${BW_LIB_DIR}/network/event_poller.cpp
	${BW_LIB_DIR}/network/exposed_message_range.cpp
	${BW_LIB_DIR}/network/file_stream.cpp
	${BW_LIB_DIR}/network/forwarding_reply_handler.cpp
	${BW_LIB_DIR}/network/forwarding_string_handler.cpp
	${BW_LIB_DIR}/network/fragmented_bundle.cpp
	${BW_LIB_DIR}/network/frequent_tasks.cpp
	${BW_LIB_DIR}/network/http_messages.cpp
	${BW_LIB_DIR}/network/interface_element.cpp
	${BW_LIB_DIR}/network/interface_minder.cpp
	${BW_LIB_DIR}/network/interface_table.cpp
	${BW_LIB_DIR}/network/irregular_channels.cpp
	${BW_LIB_DIR}/network/keepalive_channels.cpp
	${BW_LIB_DIR}/network/logger_endpoint.cpp
	${BW_LIB_DIR}/network/logger_message_forwarder.cpp
	${BW_LIB_DIR}/network/machine_guard.cpp
	${BW_LIB_DIR}/network/machine_guard_response_checker.cpp
	${BW_LIB_DIR}/network/machined_utils.cpp
	${BW_LIB_DIR}/network/monitored_channels.cpp
	${BW_LIB_DIR}/network/network_interface.cpp
	${BW_LIB_DIR}/network/network_stats.cpp
	${BW_LIB_DIR}/network/network_utils.cpp
	${BW_LIB_DIR}/network/netmask.cpp
	${BW_LIB_DIR}/network/once_off_packet.cpp
	${BW_LIB_DIR}/network/packet.cpp
	${BW_LIB_DIR}/network/packet_filter.cpp
	${BW_LIB_DIR}/network/packet_receiver.cpp
	${BW_LIB_DIR}/network/packet_receiver_stats.cpp
	${BW_LIB_DIR}/network/packet_sender.cpp
	${BW_LIB_DIR}/network/packet_loss_parameters.cpp
	${BW_LIB_DIR}/network/process_socket_stats_helper.cpp
	${BW_LIB_DIR}/network/public_key_cipher.cpp
	${BW_LIB_DIR}/network/recently_dead_channels.cpp
	${BW_LIB_DIR}/network/request.cpp
	${BW_LIB_DIR}/network/request_manager.cpp
	${BW_LIB_DIR}/network/rescheduled_sender.cpp
	${BW_LIB_DIR}/network/sending_stats.cpp
	${BW_LIB_DIR}/network/sha_checksum_scheme.cpp
	${BW_LIB_DIR}/network/signed_checksum_scheme.cpp
	${BW_LIB_DIR}/network/symmetric_block_cipher.cpp
	${BW_LIB_DIR}/network/space_data_mapping.cpp
	${BW_LIB_DIR}/network/space_data_mappings.cpp
	${BW_LIB_DIR}/network/tcp_bundle.cpp
	${BW_LIB_DIR}/network/tcp_bundle_processor.cpp
	${BW_LIB_DIR}/network/tcp_connection_opener.cpp
	${BW_LIB_DIR}/network/tcp_channel.cpp
	${BW_LIB_DIR}/network/tcp_server.cpp
	${BW_LIB_DIR}/network/udp_bundle.cpp
	${BW_LIB_DIR}/network/udp_bundle_processor.cpp
	${BW_LIB_DIR}/network/udp_channel.cpp
	${BW_LIB_DIR}/network/unacked_packet.cpp
	${BW_LIB_DIR}/network/unpacked_message_header.cpp
	${BW_LIB_DIR}/network/watcher_connection.cpp
	${BW_LIB_DIR}/network/watcher_nub.cpp
	${BW_LIB_DIR}/network/watcher_packet_handler.cpp
	${BW_LIB_DIR}/network/websocket_stream_filter.cpp
	${BW_LIB_DIR}/network/zip_stream.cpp
)
target_compile_definitions(network PRIVATE BWENTITY_DLL_EXPORT CSTDMF_IMPORT)
target_compile_options(network PRIVATE -Wno-float-equal)
target_link_libraries(network PRIVATE cstdmf math resmgr ZLIB::ZLIB OpenSSL::SSL OpenSSL::Crypto)

# --- connection (lib/connection/Makefile.rules, 26 TUs, Linux server profile)
# network's link dependency; has no unit_test of its own. client_interface.cpp
# (Windows/MFC) and pch.cpp are not in the make cxxSource; mirrored here.
add_library(connection STATIC
	${BW_LIB_DIR}/connection/avatar_filter.cpp
	${BW_LIB_DIR}/connection/avatar_filter_helper.cpp
	${BW_LIB_DIR}/connection/baseapp_login_request_protocol.cpp
	${BW_LIB_DIR}/connection/condemned_interfaces.cpp
	${BW_LIB_DIR}/connection/cuckoo_cycle_login_challenge_factory.cpp
	${BW_LIB_DIR}/connection/data_download.cpp
	${BW_LIB_DIR}/connection/filter_helper.cpp
	${BW_LIB_DIR}/connection/log_on_params.cpp
	${BW_LIB_DIR}/connection/login_challenge_factory.cpp
	${BW_LIB_DIR}/connection/login_challenge_task.cpp
	${BW_LIB_DIR}/connection/login_handler.cpp
	${BW_LIB_DIR}/connection/login_interface.cpp
	${BW_LIB_DIR}/connection/login_request.cpp
	${BW_LIB_DIR}/connection/login_request_protocol.cpp
	${BW_LIB_DIR}/connection/login_request_transport.cpp
	${BW_LIB_DIR}/connection/loginapp_login_request_protocol.cpp
	${BW_LIB_DIR}/connection/movement_filter.cpp
	${BW_LIB_DIR}/connection/replay_checksum_scheme.cpp
	${BW_LIB_DIR}/connection/replay_controller.cpp
	${BW_LIB_DIR}/connection/replay_data_file_reader.cpp
	${BW_LIB_DIR}/connection/replay_header.cpp
	${BW_LIB_DIR}/connection/replay_metadata.cpp
	${BW_LIB_DIR}/connection/replay_tick_loader.cpp
	${BW_LIB_DIR}/connection/server_connection.cpp
	${BW_LIB_DIR}/connection/server_finder.cpp
	${BW_LIB_DIR}/connection/smart_server_connection.cpp
)
target_compile_definitions(connection PRIVATE BWENTITY_DLL_EXPORT CSTDMF_IMPORT)
target_link_libraries(connection PRIVATE network OpenSSL::SSL OpenSSL::Crypto)

# --- physics2 (lib/physics2/Makefile.rules, 7 TUs) ---------------------------
add_library(physics2 STATIC
	${BW_LIB_DIR}/physics2/bsp.cpp
	${BW_LIB_DIR}/physics2/hulltree.cpp
	${BW_LIB_DIR}/physics2/material_kinds.cpp
	${BW_LIB_DIR}/physics2/quad_tree.cpp
	${BW_LIB_DIR}/physics2/worldpoly.cpp
	${BW_LIB_DIR}/physics2/worldtri.cpp
	${BW_LIB_DIR}/physics2/sweep_helpers.cpp
)
target_compile_definitions(physics2 PRIVATE CSTDMF_IMPORT)

# --- moo (lib/moo/Makefile.rules, 2 TUs; usePNG → vcpkg libpng) --------------
add_library(moo STATIC
	${BW_LIB_DIR}/moo/node.cpp
	${BW_LIB_DIR}/moo/png.cpp
)
target_compile_definitions(moo PRIVATE CSTDMF_IMPORT)
target_link_libraries(moo PRIVATE cstdmf PNG::PNG)

# --- terrain (lib/terrain/Makefile.rules, 18 TUs, Linux server profile) ------
add_library(terrain STATIC
	${BW_LIB_DIR}/terrain/base_terrain_block.cpp
	${BW_LIB_DIR}/terrain/height_map_compress.cpp
	${BW_LIB_DIR}/terrain/manager.cpp
	${BW_LIB_DIR}/terrain/terrain_block_cache.cpp
	${BW_LIB_DIR}/terrain/terrain_collision_callback.cpp
	${BW_LIB_DIR}/terrain/terrain_ray_collision_callback.cpp
	${BW_LIB_DIR}/terrain/terrain_prism_collision_callback.cpp
	${BW_LIB_DIR}/terrain/terrain_finder.cpp
	${BW_LIB_DIR}/terrain/terrain_height_map.cpp
	${BW_LIB_DIR}/terrain/terrain_settings.cpp
	${BW_LIB_DIR}/terrain/dominant_texture_map.cpp
	${BW_LIB_DIR}/terrain/terrain2/common_terrain_block2.cpp
	${BW_LIB_DIR}/terrain/terrain2/dominant_texture_map2.cpp
	${BW_LIB_DIR}/terrain/terrain2/terrain_quad_tree_cell.cpp
	${BW_LIB_DIR}/terrain/terrain2/terrain_hole_map2.cpp
	${BW_LIB_DIR}/terrain/terrain2/terrain_height_map2.cpp
	${BW_LIB_DIR}/terrain/terrain2/terrain_block2.cpp
	${BW_LIB_DIR}/terrain/terrain2/terrain_texture_layer2.cpp
)
target_compile_definitions(terrain PRIVATE CSTDMF_IMPORT)
target_link_libraries(terrain PRIVATE cstdmf math resmgr)

# --- scene (lib/scene/Makefile.rules, 14 TUs; pch IS in the make list here) --
add_library(scene STATIC
	${BW_LIB_DIR}/scene/collision_operation.cpp
	${BW_LIB_DIR}/scene/collision_scene_view.cpp
	${BW_LIB_DIR}/scene/draw_object_operation.cpp
	${BW_LIB_DIR}/scene/draw_operation.cpp
	${BW_LIB_DIR}/scene/intersection_set.cpp
	${BW_LIB_DIR}/scene/intersect_scene_view.cpp
	${BW_LIB_DIR}/scene/pch.cpp
	${BW_LIB_DIR}/scene/scene.cpp
	${BW_LIB_DIR}/scene/scene_listener.cpp
	${BW_LIB_DIR}/scene/scene_object.cpp
	${BW_LIB_DIR}/scene/scene_object_flags.cpp
	${BW_LIB_DIR}/scene/scene_provider.cpp
	${BW_LIB_DIR}/scene/scene_type_system.cpp
	${BW_LIB_DIR}/scene/tick_scene_view.cpp
)
target_compile_definitions(scene PRIVATE CSTDMF_IMPORT)
target_link_libraries(scene PRIVATE cstdmf math)

# --- chunk (lib/chunk/Makefile.rules, 34 TUs, Linux server profile) ----------
add_library(chunk STATIC
	${BW_LIB_DIR}/chunk/base_chunk_space.cpp
	${BW_LIB_DIR}/chunk/base_chunk_tree.cpp
	${BW_LIB_DIR}/chunk/chunk.cpp
	${BW_LIB_DIR}/chunk/chunk_boundary.cpp
	${BW_LIB_DIR}/chunk/chunk_cache.cpp
	${BW_LIB_DIR}/chunk/chunk_exit_portal.cpp
	${BW_LIB_DIR}/chunk/chunk_grid_size.cpp
	${BW_LIB_DIR}/chunk/chunk_item.cpp
	${BW_LIB_DIR}/chunk/chunk_link.cpp
	${BW_LIB_DIR}/chunk/chunk_loader.cpp
	${BW_LIB_DIR}/chunk/chunk_model_obstacle.cpp
	${BW_LIB_DIR}/chunk/chunk_model_vlo.cpp
	${BW_LIB_DIR}/chunk/chunk_model_vlo_ref.cpp
	${BW_LIB_DIR}/chunk/chunk_obstacle.cpp
	${BW_LIB_DIR}/chunk/chunk_overlapper.cpp
	${BW_LIB_DIR}/chunk/chunk_space.cpp
	${BW_LIB_DIR}/chunk/chunk_stationnode.cpp
	${BW_LIB_DIR}/chunk/chunk_terrain_obstacle.cpp
	${BW_LIB_DIR}/chunk/chunk_user_data_object.cpp
	${BW_LIB_DIR}/chunk/chunk_vlo.cpp
	${BW_LIB_DIR}/chunk/chunk_vlo_obstacle.cpp
	${BW_LIB_DIR}/chunk/geometry_mapping.cpp
	${BW_LIB_DIR}/chunk/portal_chunk_item.cpp
	${BW_LIB_DIR}/chunk/server_chunk_manager.cpp
	${BW_LIB_DIR}/chunk/server_chunk_model.cpp
	${BW_LIB_DIR}/chunk/server_chunk_space.cpp
	${BW_LIB_DIR}/chunk/server_chunk_terrain.cpp
	${BW_LIB_DIR}/chunk/server_chunk_tree.cpp
	${BW_LIB_DIR}/chunk/server_chunk_water.cpp
	${BW_LIB_DIR}/chunk/server_super_model.cpp
	${BW_LIB_DIR}/chunk/station_graph.cpp
	${BW_LIB_DIR}/chunk/user_data_object.cpp
	${BW_LIB_DIR}/chunk/user_data_object_type.cpp
	${BW_LIB_DIR}/chunk/user_data_object_link_data_type.cpp
)
target_compile_definitions(chunk PRIVATE CSTDMF_IMPORT)
target_link_libraries(chunk PRIVATE cstdmf math resmgr BWPython::Python)

# --- script (lib/script/Makefile.rules, 4 TUs) -------------------------------
add_library(script STATIC
	${BW_LIB_DIR}/script/init_time_job.cpp
	${BW_LIB_DIR}/script/py_script_object.cpp
	${BW_LIB_DIR}/script/py_script_output_writer.cpp
	${BW_LIB_DIR}/script/script_output_hook.cpp
)
target_compile_definitions(script PRIVATE BWENTITY_DLL_EXPORT CSTDMF_IMPORT)
target_link_libraries(script PRIVATE cstdmf BWPython::Python)

# --- pyscript (lib/pyscript/Makefile.rules, 18 TUs) --------------------------
add_library(pyscript STATIC
	${BW_LIB_DIR}/pyscript/personality.cpp
	${BW_LIB_DIR}/pyscript/pickler.cpp
	${BW_LIB_DIR}/pyscript/py_data_section.cpp
	${BW_LIB_DIR}/pyscript/py_traceback.cpp
	${BW_LIB_DIR}/pyscript/py_to_stl.cpp
	${BW_LIB_DIR}/pyscript/pyobject_plus.cpp
	${BW_LIB_DIR}/pyscript/py_factory_method_link.cpp
	${BW_LIB_DIR}/pyscript/py_import_paths.cpp
	${BW_LIB_DIR}/pyscript/py_logging.cpp
	${BW_LIB_DIR}/pyscript/python_input_substituter.cpp
	${BW_LIB_DIR}/pyscript/pywatcher.cpp
	${BW_LIB_DIR}/pyscript/res_mgr_script.cpp
	${BW_LIB_DIR}/pyscript/resource_table.cpp
	${BW_LIB_DIR}/pyscript/py_debug_message_file_logger.cpp
	${BW_LIB_DIR}/pyscript/script.cpp
	${BW_LIB_DIR}/pyscript/script_events.cpp
	${BW_LIB_DIR}/pyscript/script_math.cpp
	${BW_LIB_DIR}/pyscript/stl_to_py.cpp
)
target_compile_definitions(pyscript PRIVATE BWENTITY_DLL_EXPORT CSTDMF_IMPORT)
target_compile_options(pyscript PRIVATE -Wno-float-equal)
target_link_libraries(pyscript PRIVATE cstdmf math resmgr BWPython::Python)

# --- entitydef (lib/entitydef/Makefile.rules, 55 TUs) ------------------------
add_library(entitydef STATIC
	${BW_LIB_DIR}/entitydef/base_user_data_object_description.cpp
	${BW_LIB_DIR}/entitydef/data_description.cpp
	${BW_LIB_DIR}/entitydef/data_lod_level.cpp
	${BW_LIB_DIR}/entitydef/data_type.cpp
	${BW_LIB_DIR}/entitydef/data_types.cpp
	${BW_LIB_DIR}/entitydef/entity_delegate_helpers.cpp
	${BW_LIB_DIR}/entitydef/entity_description.cpp
	${BW_LIB_DIR}/entitydef/entity_description_debug.cpp
	${BW_LIB_DIR}/entitydef/entity_description_map.cpp
	${BW_LIB_DIR}/entitydef/entity_member_stats.cpp
	${BW_LIB_DIR}/entitydef/entity_method_descriptions.cpp
	${BW_LIB_DIR}/entitydef/mailbox_base.cpp
	${BW_LIB_DIR}/entitydef/meta_data_type.cpp
	${BW_LIB_DIR}/entitydef/member_description.cpp
	${BW_LIB_DIR}/entitydef/method_args.cpp
	${BW_LIB_DIR}/entitydef/method_description.cpp
	${BW_LIB_DIR}/entitydef/property_change.cpp
	${BW_LIB_DIR}/entitydef/property_change_reader.cpp
	${BW_LIB_DIR}/entitydef/property_event_stamps.cpp
	${BW_LIB_DIR}/entitydef/property_owner.cpp
	${BW_LIB_DIR}/entitydef/py_deferred.cpp
	${BW_LIB_DIR}/entitydef/py_volatile_info.cpp
	${BW_LIB_DIR}/entitydef/remote_entity_method.cpp
	${BW_LIB_DIR}/entitydef/return_values_handler.cpp
	${BW_LIB_DIR}/entitydef/script_data_sink.cpp
	${BW_LIB_DIR}/entitydef/script_data_source.cpp
	${BW_LIB_DIR}/entitydef/single_type_data_sinks.cpp
	${BW_LIB_DIR}/entitydef/single_type_data_sources.cpp
	${BW_LIB_DIR}/entitydef/user_data_object_description.cpp
	${BW_LIB_DIR}/entitydef/user_data_object_description_map.cpp
	${BW_LIB_DIR}/entitydef/volatile_info.cpp
	${BW_LIB_DIR}/entitydef/data_instances/array_data_instance.cpp
	${BW_LIB_DIR}/entitydef/data_instances/class_data_instance.cpp
	${BW_LIB_DIR}/entitydef/data_instances/fixed_dict_data_instance.cpp
	${BW_LIB_DIR}/entitydef/data_instances/intermediate_property_owner.cpp
	${BW_LIB_DIR}/entitydef/data_types/array_data_type.cpp
	${BW_LIB_DIR}/entitydef/data_types/blob_data_type.cpp
	${BW_LIB_DIR}/entitydef/data_types/class_data_type.cpp
	${BW_LIB_DIR}/entitydef/data_types/class_meta_data_type.cpp
	${BW_LIB_DIR}/entitydef/data_types/dictionary_data_type.cpp
	${BW_LIB_DIR}/entitydef/data_types/fixed_dict_data_type.cpp
	${BW_LIB_DIR}/entitydef/data_types/fixed_dict_meta_data_type.cpp
	${BW_LIB_DIR}/entitydef/data_types/float_data_types.cpp
	${BW_LIB_DIR}/entitydef/data_types/integer_data_type.cpp
	${BW_LIB_DIR}/entitydef/data_types/long_integer_data_type.cpp
	${BW_LIB_DIR}/entitydef/data_types/mailbox_data_type.cpp
	${BW_LIB_DIR}/entitydef/data_types/python_data_type.cpp
	${BW_LIB_DIR}/entitydef/data_types/sequence_data_type.cpp
	${BW_LIB_DIR}/entitydef/data_types/sequence_meta_data_type.cpp
	${BW_LIB_DIR}/entitydef/data_types/string_data_type.cpp
	${BW_LIB_DIR}/entitydef/data_types/tuple_data_type.cpp
	${BW_LIB_DIR}/entitydef/data_types/unicode_string_data_type.cpp
	${BW_LIB_DIR}/entitydef/data_types/user_data_type.cpp
	${BW_LIB_DIR}/entitydef/data_types/user_meta_data_type.cpp
	${BW_LIB_DIR}/entitydef/data_types/vector_data_types.cpp
)
target_compile_definitions(entitydef PRIVATE BWENTITY_DLL_EXPORT CSTDMF_IMPORT)
target_link_libraries(entitydef PRIVATE
	cstdmf math resmgr network connection physics2 moo terrain scene chunk script pyscript
	BWPython::Python ZLIB::ZLIB PNG::PNG)

# --- entitydef_test (lib/entitydef/unit_test/Makefile.rules cxxSource, 16 TUs)
add_executable(entitydef_test
	${BW_LIB_DIR}/entitydef/unit_test/main.cpp
	${BW_LIB_DIR}/entitydef/unit_test/test_entity_description.cpp
	${BW_LIB_DIR}/entitydef/unit_test/test_datatype_const_iterator.cpp
	${BW_LIB_DIR}/entitydef/unit_test/test_datatype_persistent_only_const_iterator.cpp
	${BW_LIB_DIR}/entitydef/unit_test/unittest_mailbox.cpp
	${BW_LIB_DIR}/entitydef/unit_test/test_datasection.cpp
	${BW_LIB_DIR}/entitydef/unit_test/test_method_description.cpp
	${BW_LIB_DIR}/entitydef/unit_test/test_method_description_gaps.cpp
	${BW_LIB_DIR}/entitydef/unit_test/test_script_data_sink.cpp
	${BW_LIB_DIR}/entitydef/unit_test/test_script_data_source.cpp
	${BW_LIB_DIR}/entitydef/unit_test/test_single_data_sink.cpp
	${BW_LIB_DIR}/entitydef/unit_test/test_single_data_source.cpp
	${BW_LIB_DIR}/entitydef/unit_test/test_stream.cpp
	${BW_LIB_DIR}/entitydef/unit_test/test_conversion.cpp
	${BW_LIB_DIR}/entitydef/unit_test/test_streamsizes.cpp
	${BW_LIB_DIR}/entitydef/unit_test/integer_range_checker.cpp
)
target_compile_definitions(entitydef_test PRIVATE CSTDMF_IMPORT)

# make links the unit-test binaries with -Wl,--start-group ... -Wl,--end-group
# around the static libs (chunk references entitydef vtables, connection
# references network symbols, network references resmgr symbols — single-pass
# ordering cannot resolve all of them).
target_link_libraries(entitydef_test PRIVATE
	-Wl,--start-group
	unit_test_lib
	CppUnitLite2
	entitydef
	script
	pyscript
	chunk
	scene
	terrain
	moo
	physics2
	network
	connection
	resmgr
	math
	cstdmf
	ZLIB::ZLIB
	PNG::PNG
	OpenSSL::SSL
	OpenSSL::Crypto
	-Wl,--end-group
	BWPython::Python
	dl
	m
	pthread
	util
)
# make links the unit-test binaries with -rdynamic -Wl,-export-dynamic
# (backtrace() symbol names in callstack.cpp depend on it)
target_link_options(entitydef_test PRIVATE -rdynamic -Wl,--export-dynamic)

get_filename_component(BW_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
enable_testing()
add_test(NAME entitydef_test
	COMMAND entitydef_test --root "${BW_REPO_ROOT}"
	WORKING_DIRECTORY "${BW_LIB_DIR}/entitydef/unit_test"
)
