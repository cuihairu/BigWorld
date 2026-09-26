# This Makefile describes how to consume the OpenSSL library.

# useOpenSSL

# BIGWORLD_BEGIN(3.13 migration)
# OpenSSL is now provided by vcpkg (see vcpkg.json / third_party_vcpkg.mak).
# The vendored 1.0.0d source build that used to live in this file was
# retired: it cannot compile under gcc >= 14 (C23 bool keyword) and is below
# CPython 3.13's minimum (1.1.1) for _ssl/_hashlib. The library names on the
# BigWorld link lines are preserved: libbwssl.a / libbwcrypto.a are staged
# copies of the vcpkg libssl.a / libcrypto.a.
#
# The variable names below are kept for the consumers:
#   OPENSSL_BUILD_DIR  - root containing include/ and lib/ (vcpkg installed
#                        tree); also used as Python's --with-openssl root.
#   BW_SSL_LIB / BW_CRYPTO_LIB - staged archives in $(BW_LIBDIR).
OPENSSL_BUILD_DIR := $(VCPKG_INSTALLED)

BW_OPENSSL_CRYPTO_TARGET := bwcrypto
BW_OPENSSL_SSL_TARGET    := bwssl

sslLibDir := $(BW_INTERMEDIATE_DIR)/$(BW_PLATFORM_CONFIG)/lib
BW_SSL_LIB    := $(sslLibDir)/lib$(BW_OPENSSL_SSL_TARGET).a
BW_CRYPTO_LIB := $(sslLibDir)/lib$(BW_OPENSSL_CRYPTO_TARGET).a
# BIGWORLD_END

# third_party_openssl.mak
