# Standalone CMake sample for lib/network (Linux server profile).
#
# NOT part of the Windows CMake superproject (the per-module CMakeLists.txt
# under lib/ that uses BWStandardProject stays the Windows track). This sample
# is the Linux CMake-track counterpart of the make chain:
#   make counterpart : lib/network/Makefile.rules (libnetwork) + unit_test/ (network_test)
#                      + lib/connection/Makefile.rules (libconnection — a network_test
#                      link dependency that has no unit_test of its own)
#   flags/defines    : copied verbatim from `make -n -B bw-run-test-network` output
#   dependencies     : unit_test_lib + CppUnitLite2 + cstdmf + math + resmgr + connection
#                      built here from source so the test binary links exactly what
#                      make links (-lunit_test_lib -lCppUnitLite2 -lnetwork -lconnection
#                      -lresmgr -lmath -lcstdmf -lzip -lbwssl -lbwcrypto -ldl -lm -lpthread;
#                      bwssl/bwcrypto are the vcpkg OpenSSL staged by third_party_vcpkg.mak,
#                      mirrored here as OpenSSL::SSL/OpenSSL::Crypto)
#
# This sample is the bottom-up next step after cmake/samples/resmgr: it adds
# connection (network's link dependency), the OpenSSL link, and the network
# unit-test surface (52 TUs). entitydef can then be layered on top the same way.
#
# One-command build + test: scripts/build-network-cmake.sh
# Manual configure:
#   cmake -S programming/bigworld/cmake/samples/network \
#     -B programming/bigworld/build/cmake-network \
#     -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
#   cmake --build programming/bigworld/build/cmake-network -j
#   ctest --test-dir programming/bigworld/build/cmake-network --output-on-failure
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

project(bigworld_network_sample LANGUAGES CXX)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND NOT DEFINED VCPKG_TOOLCHAIN)
	message(FATAL_ERROR
		"This sample expects the vcpkg toolchain (manifest mode). Configure with:\n"
		"  -DCMAKE_TOOLCHAIN_FILE=\$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake\n"
		"or run scripts/build-network-cmake.sh")
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
# same directory's include path to every TU.
if(NOT DEFINED VCPKG_INSTALLED_DIR)
	set(VCPKG_INSTALLED_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed")
endif()
include_directories("${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")

# zlib + openssl: make links vcpkg zlib staged as libzip.a (third_party_vcpkg.mak)
# and vcpkg OpenSSL staged as libbwssl.a/libbwcrypto.a.
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)

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
target_compile_definitions(math PRIVATE
	BWENTITY_DLL_EXPORT
	CSTDMF_IMPORT
)
target_compile_definitions(cstdmf PRIVATE CSTDMF_EXPORT)

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
target_compile_definitions(network PRIVATE
	BWENTITY_DLL_EXPORT
	CSTDMF_IMPORT
)
# lib/network/Makefile.rules: cxxFlags_$(binName) += -Wno-float-equal
# (bsd_snprintf.cpp compares floats with ==; cancels the global -Wfloat-equal)
target_compile_options(network PRIVATE -Wno-float-equal)
target_link_libraries(network PRIVATE ZLIB::ZLIB OpenSSL::SSL OpenSSL::Crypto)

# --- connection (lib/connection/Makefile.rules, 26 TUs, Linux server profile)
# network's link dependency; has no unit_test of its own, so it appears here
# for the first time. client_interface.cpp (Windows/MFC client connection) and
# pch.cpp are not in the make cxxSource; mirrored here.
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
target_compile_definitions(connection PRIVATE
	BWENTITY_DLL_EXPORT
	CSTDMF_IMPORT
)
# connection's sources reference network symbols (DeferredBundle,
# CompressionIStream); make resolves this with --start-group (-lnetwork
# -lconnection), mirrored in the network_test link line below.
target_link_libraries(connection PRIVATE network OpenSSL::SSL OpenSSL::Crypto)

# --- network_test (lib/network/unit_test/Makefile.rules cxxSource, 52 TUs) --
# pch.cpp is not in the make cxxSource (the pch is applied by the make build
# itself); every test TU includes pch.hpp directly, so it is not listed here.
add_executable(network_test
	${BW_LIB_DIR}/network/unit_test/main.cpp
	${BW_LIB_DIR}/network/unit_test/common_interface.cpp
	${BW_LIB_DIR}/network/unit_test/packet_generator.cpp
	${BW_LIB_DIR}/network/unit_test/test_auto_switch.cpp
	${BW_LIB_DIR}/network/unit_test/test_avatar_filter_helper.cpp
	${BW_LIB_DIR}/network/unit_test/test_baseapp_death.cpp
	${BW_LIB_DIR}/network/unit_test/test_channel.cpp
	${BW_LIB_DIR}/network/unit_test/test_channel_version.cpp
	${BW_LIB_DIR}/network/unit_test/test_compresslength.cpp
	${BW_LIB_DIR}/network/unit_test/test_config.cpp
	${BW_LIB_DIR}/network/unit_test/test_data_download.cpp
	${BW_LIB_DIR}/network/unit_test/test_exposed_message_range.cpp
	${BW_LIB_DIR}/network/unit_test/test_elliptic_curve.cpp
	${BW_LIB_DIR}/network/unit_test/test_encryption_filter.cpp
	${BW_LIB_DIR}/network/unit_test/test_event_poller.cpp
	${BW_LIB_DIR}/network/unit_test/test_file_stream.cpp
	${BW_LIB_DIR}/network/unit_test/test_filter_helper.cpp
	${BW_LIB_DIR}/network/unit_test/test_flood.cpp
	${BW_LIB_DIR}/network/unit_test/test_fragment.cpp
	${BW_LIB_DIR}/network/unit_test/test_interface.cpp
	${BW_LIB_DIR}/network/unit_test/test_log_on_params.cpp
	${BW_LIB_DIR}/network/unit_test/test_login_challenge.cpp
	${BW_LIB_DIR}/network/unit_test/test_login_challenge_task.cpp
	${BW_LIB_DIR}/network/unit_test/test_login_request_protocol.cpp
	${BW_LIB_DIR}/network/unit_test/test_machine_guard.cpp
	${BW_LIB_DIR}/network/unit_test/test_mangle.cpp
	${BW_LIB_DIR}/network/unit_test/test_movement_filter.cpp
	${BW_LIB_DIR}/network/unit_test/test_msgtypes.cpp
	${BW_LIB_DIR}/network/unit_test/test_netmask.cpp
	${BW_LIB_DIR}/network/unit_test/test_networkstats.cpp
	${BW_LIB_DIR}/network/unit_test/test_overflow.cpp
	${BW_LIB_DIR}/network/unit_test/test_receive_window.cpp
	${BW_LIB_DIR}/network/unit_test/test_reliable.cpp
	${BW_LIB_DIR}/network/unit_test/test_replay_header.cpp
	${BW_LIB_DIR}/network/unit_test/test_replay_tick_loader.cpp
	${BW_LIB_DIR}/network/unit_test/test_network_checksum_schemes.cpp
	${BW_LIB_DIR}/network/unit_test/test_request_failure.cpp
	${BW_LIB_DIR}/network/unit_test/test_server_finder.cpp
	${BW_LIB_DIR}/network/unit_test/test_snprintf.cpp
	${BW_LIB_DIR}/network/unit_test/test_stream.cpp
	${BW_LIB_DIR}/network/unit_test/test_symmetric_block_cipher.cpp
	${BW_LIB_DIR}/network/unit_test/test_tcp_bundle.cpp
	${BW_LIB_DIR}/network/unit_test/test_zip_stream.cpp
	${BW_LIB_DIR}/network/unit_test/test_tcp_channels.cpp
	${BW_LIB_DIR}/network/unit_test/test_threadsafety.cpp
	${BW_LIB_DIR}/network/unit_test/test_websockets.cpp
	${BW_LIB_DIR}/network/unit_test/test_login_challenge_factories.cpp
	${BW_LIB_DIR}/network/unit_test/test_cuckoo_cycle_login_challenge.cpp
	${BW_LIB_DIR}/network/unit_test/test_replay_metadata.cpp
	${BW_LIB_DIR}/network/unit_test/test_piggybacks_longer_than_normal.cpp
	${BW_LIB_DIR}/network/unit_test/test_virtual_clock.cpp
	${BW_LIB_DIR}/network/unit_test/test_event_stall.cpp
)
target_compile_definitions(network_test PRIVATE CSTDMF_IMPORT)
# BWT-25539: network's unit_test profile disables -Wfloat-equal
target_compile_options(network_test PRIVATE -Wno-float-equal)

# make links the unit-test binaries with -Wl,--start-group ... -Wl,--end-group
# around the static libs (connection references network symbols, network
# references resmgr symbols — single-pass ordering cannot resolve both).
# CMake passes dash-prefixed items through verbatim, so the group is spelled
# out here exactly as make spells it out.
target_link_libraries(network_test PRIVATE
	-Wl,--start-group
	unit_test_lib
	CppUnitLite2
	network
	connection
	resmgr
	math
	cstdmf
	ZLIB::ZLIB
	OpenSSL::SSL
	OpenSSL::Crypto
	-Wl,--end-group
	dl
	m
	pthread
)
# make links the unit-test binaries with -rdynamic -Wl,-export-dynamic
# (backtrace() symbol names in callstack.cpp depend on it)
target_link_options(network_test PRIVATE -rdynamic -Wl,--export-dynamic)

get_filename_component(BW_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
enable_testing()
add_test(NAME network_test
	COMMAND network_test --root "${BW_REPO_ROOT}"
	WORKING_DIRECTORY "${BW_LIB_DIR}/network/unit_test"
)
