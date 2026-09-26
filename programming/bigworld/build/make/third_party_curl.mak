# This Makefile describes how to consume the cURL library.

# useCurl

# BIGWORLD_BEGIN(3.13 migration)
# cURL is now provided by vcpkg (see vcpkg.json / third_party_vcpkg.mak),
# built against the same vcpkg OpenSSL 3.x. The vendored 7.x configure
# build that used to live here was retired. The bw prefix on the staged
# archive is kept so we never accidentally link a system libcurl.
#
# CURL_BUILD_DIR is the vcpkg installed root; headers live at
# $(CURL_BUILD_DIR)/include (curl/curl.h) - the common footer adds the
# include path for useCurl components.
CURL_BUILD_DIR = $(VCPKG_INSTALLED)

BW_CURL_TARGET := bwcurl

curlLibDir := $(BW_INTERMEDIATE_DIR)/$(BW_PLATFORM_CONFIG)/lib
BW_CURLLIB := $(curlLibDir)/lib$(BW_CURL_TARGET).a
# BIGWORLD_END

# third_party_curl.mak
