# This Makefile integrates the vcpkg package manager for the third-party
# libraries that are no longer vendored under third_party/ (see
# vcpkg.json at the BigWorld source root and docs/vcpkg-migration.md).
#
# It provides:
#   * one `vcpkg_install` stamp target that materialises the manifest into
#     $(VCPKG_INSTALL_ROOT)/<triplet> (headers + static libs),
#   * per-package staging rules that copy the vcpkg static libraries into
#     $(BW_LIBDIR) under the names the BigWorld link lines have always used
#     (bwssl/bwcrypto/bwcurl/jsoncpp/zip/png/sqlite3), so component
#     Makefile.rules and the thirdPartyDependsOn chain keep working
#     unchanged.
#
# Requirements:
#   * vcpkg must be installed and VCPKG_ROOT must point at it (exported in
#     the environment or passed on the make command line).

# The vcpkg executable
VCPKG_ROOT ?= $(if $(VCPKG_ROOT_ENV),$(VCPKG_ROOT_ENV),)
ifeq ($(strip $(VCPKG_ROOT)),)
VCPKG_ROOT := $(shell command -v vcpkg >/dev/null 2>&1 && dirname "$$(command -v vcpkg)")
endif
ifeq ($(strip $(VCPKG_ROOT)),)
$(error VCPKG_ROOT is not set. Install vcpkg (https://github.com/microsoft/vcpkg) and export VCPKG_ROOT=<vcpkg checkout>)
endif
VCPKG_EXE := $(VCPKG_ROOT)/vcpkg

# Triplet: static, release-only Linux libraries (see build/vcpkg-triplets/).
# NOTE: the overlay dir deliberately does NOT live under build/vcpkg/, which
# .gitignore excludes; the triplet is a checked-in build input, so a fresh
# clone has to be able to see it.
BW_VCPKG_TRIPLET ?= x64-linux-bw
BW_VCPKG_OVERLAY_TRIPLETS := $(BW_ABS_SRC)/build/vcpkg-triplets

# Where the manifest gets installed. Kept under the third-party build dir so
# BW_LOCAL_DIR relocations apply; only libraries/headers are consumed from
# here, nothing is executed, so noexec mounts are fine.
VCPKG_INSTALL_ROOT := $(BW_THIRD_PARTY_BUILD_DIR)/vcpkg_installed
VCPKG_INSTALLED    := $(VCPKG_INSTALL_ROOT)/$(BW_VCPKG_TRIPLET)

# Single include root shared by every vcpkg-provided package.
BW_VCPKG_INCLUDE := $(VCPKG_INSTALLED)/include

# NOTE: BW_LIBDIR is only defined later (per-component, in
# common_footer_config.mak); compute the staging directory the same way the
# other third_party_*.mak files do.
bwVcpkgLibDir := $(BW_INTERMEDIATE_DIR)/$(BW_PLATFORM_CONFIG)/lib

vcpkgInstallStamp := $(BW_THIRD_PARTY_BUILD_DIR)/vcpkg-install.stamp

.PHONY: vcpkg_install clean_vcpkg_install
vcpkg_install: $(vcpkgInstallStamp)

# The manifest and the triplet are the inputs; vcpkg itself is incremental
# and a no-op when the installed tree already satisfies the manifest.
$(vcpkgInstallStamp): $(BW_ABS_SRC)/vcpkg.json $(BW_VCPKG_OVERLAY_TRIPLETS)/$(BW_VCPKG_TRIPLET).cmake
	@mkdir -p $(BW_THIRD_PARTY_BUILD_DIR)
	$(VCPKG_EXE) install \
		--triplet $(BW_VCPKG_TRIPLET) \
		--overlay-triplets=$(BW_VCPKG_OVERLAY_TRIPLETS) \
		--x-manifest-root=$(BW_ABS_SRC) \
		--x-install-root=$(VCPKG_INSTALL_ROOT) \
		--clean-after-build
	@touch $@

clean_vcpkg_install:
	-@$(RM) -rf $(VCPKG_INSTALL_ROOT) $(vcpkgInstallStamp)

BW_CLEAN_HOOKS += clean_vcpkg_install

#
# Per-package staging into $(BW_LIBDIR).
#
# Every staged library depends on the vcpkg install stamp; the copy source
# paths are the vcpkg canonical artefact names.
#
define BW_VCPKG_STAGE_LIBRARY
$$(bwVcpkgLibDir)/lib$(1).a: $(3) | $$(bwVcpkgLibDir)
	$$(bwCommand_createCopy)

.PHONY: lib$(2)
lib$(2): $$(bwVcpkgLibDir)/lib$(1).a
endef

# <staged name> <target name> <vcpkg source>
BW_VCPKG_PACKAGES := \
	bwssl:bwssl:libssl.a \
	bwcrypto:bwcrypto:libcrypto.a \
	bwcurl:bwcurl:libcurl.a \
	jsoncpp:jsoncpp:libjsoncpp.a \
	zip:zip:libz.a \
	png:png:libpng16.a \
	sqlite3:sqlite3:libsqlite3.a

$(foreach pkg,$(BW_VCPKG_PACKAGES),$(eval $(call BW_VCPKG_STAGE_LIBRARY,$(word 1,$(subst :, ,$(pkg))),$(word 2,$(subst :, ,$(pkg))),$(VCPKG_INSTALLED)/lib/$(word 3,$(subst :, ,$(pkg))))))

# Make the staged libraries depend on the install having completed
$(foreach pkg,$(BW_VCPKG_PACKAGES),$(eval $(VCPKG_INSTALLED)/lib/$(word 3,$(subst :, ,$(pkg))): $(vcpkgInstallStamp)))

BW_THIRD_PARTY += $(foreach pkg,$(BW_VCPKG_PACKAGES),lib$(word 2,$(subst :, ,$(pkg))))

# third_party_vcpkg.mak
