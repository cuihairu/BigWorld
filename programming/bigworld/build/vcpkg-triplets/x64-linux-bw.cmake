# BigWorld vcpkg triplet for Linux server builds.
#
# Static libraries, release-only (the BigWorld Make chain links every
# third-party dependency statically and only ever builds Release/Hybrid
# configurations). Kept in-tree so the build is reproducible without a
# global vcpkg customization.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
