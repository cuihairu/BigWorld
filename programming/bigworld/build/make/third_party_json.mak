# This Makefile describes how to consume the jsoncpp library.

# useJSON

# BIGWORLD_BEGIN(3.13 migration)
# jsoncpp is now provided by vcpkg (see vcpkg.json / third_party_vcpkg.mak)
# instead of the vendored 0.7.0 sources. The staged archive keeps the
# historical target name (jsoncpp) and the vcpkg installed root carries the
# headers, so the useJSON mapping in common_footer_config.mak is unchanged.
JSON_DIR := $(VCPKG_INSTALLED)

BW_JSON_TARGET := jsoncpp
# BIGWORLD_END

# third_party_json.mak
