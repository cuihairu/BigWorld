# This Makefile describes how to build the various Python modules we
# bundle with the BigWorld res tree (Python 3.13).
#
# The Python 2 trees (Twisted 11 / SQLAlchemy 0.6 / Zope 2.11 / Pympler 0.3 /
# pika 0.9 / msgpack 0.4 / oursql 0.9) were replaced with Py3-compatible
# releases. The full set is proven to import cleanly under the embedded
# interpreter by scripts/python-modules-smoke-import.py.
#
# Oursql is a Py2 C extension with no Py3 port (see todo.md); its tarball is
# kept for the record but its build target is disabled until a Py3 MySQL
# connector is chosen.
#
# Each package below is a single rule: extract the vendored tarball, copy the
# importable directory into a res-tree site-packages directory, and leave a
# .pth marker. The package's __init__.py is the install stamp, so a partial
# install re-runs the whole rule rather than reusing a stale extraction.

# Module source directory
sourcePythonModulesDir := $(BW_ABS_SRC)/third_party/python_modules

# Intermediate tree directories
bwSitePackages := site-packages
bwCommonSiteDir := $(BW_SUFFIX_RES_BIGWORLD)/scripts/common/$(bwSitePackages)
bwServerCommonSiteDir := $(BW_SUFFIX_RES_BIGWORLD)/scripts/server_common/$(bwSitePackages)
bwIntermediateCommonSiteDir := $(BW_INSTALL_DIR)/$(bwCommonSiteDir)
bwIntermediateServerCommonSiteDir := $(BW_INSTALL_DIR)/$(bwServerCommonSiteDir)

$(bwIntermediateCommonSiteDir) $(bwIntermediateServerCommonSiteDir):
	@mkdir -p $@

.PHONY: clean_python_sitePackages
clean_python_sitePackages:
	-@$(RM) -rf $(bwIntermediateCommonSiteDir) $(bwIntermediateServerCommonSiteDir)

BW_CLEAN_HOOKS += clean_python_sitePackages

#
# --- common site-packages -------------------------------------------------

# setuptools (provides pkg_resources, required by zope.interface's namespace)
setuptoolsTarball := $(sourcePythonModulesDir)/setuptools-75.3.0.tar.gz
setuptoolsExtract := $(BW_THIRD_PARTY_BUILD_DIR)/setuptools-75.3.0
setuptoolsStamp := $(bwIntermediateCommonSiteDir)/pkg_resources/__init__.py

$(setuptoolsStamp): $(setuptoolsTarball) | $(bwIntermediateCommonSiteDir)
	@rm -rf $(setuptoolsExtract)
	@mkdir -p $(setuptoolsExtract)
	@tar -xzf $< --strip-components=1 -C $(setuptoolsExtract)
	@cp -r $(setuptoolsExtract)/pkg_resources $(bwIntermediateCommonSiteDir)/
	@cp -r $(setuptoolsExtract)/setuptools $(bwIntermediateCommonSiteDir)/
	@cp -r $(setuptoolsExtract)/_distutils_hack $(bwIntermediateCommonSiteDir)/
	@cp $(setuptoolsExtract)/distutils-precedence.pth $(bwIntermediateCommonSiteDir)/ 2>/dev/null || true
	@echo setuptools-75.3.0 > $(bwIntermediateCommonSiteDir)/setuptools-75.3.0.pth

.PHONY: python_setuptools
python_setuptools: $(setuptoolsStamp)
BW_MISC += python_setuptools

# typing_extensions
typing_extensionsTarball := $(sourcePythonModulesDir)/typing_extensions-4.12.2.tar.gz
typing_extensionsExtract := $(BW_THIRD_PARTY_BUILD_DIR)/typing_extensions-4.12.2
typing_extensionsStamp := $(bwIntermediateCommonSiteDir)/typing_extensions.py

$(typing_extensionsStamp): $(typing_extensionsTarball) | $(bwIntermediateCommonSiteDir)
	@rm -rf $(typing_extensionsExtract)
	@mkdir -p $(typing_extensionsExtract)
	@tar -xzf $< --strip-components=1 -C $(typing_extensionsExtract)
	@cp $(typing_extensionsExtract)/src/typing_extensions.py $(bwIntermediateCommonSiteDir)/
	@echo typing_extensions-4.12.2 > $(bwIntermediateCommonSiteDir)/typing_extensions-4.12.2.pth

.PHONY: python_typing_extensions
python_typing_extensions: $(typing_extensionsStamp)
BW_MISC += python_typing_extensions

# attrs (provides attr/attrs namespaces; used by twisted/hyperlink)
attrsTarball := $(sourcePythonModulesDir)/attrs-24.2.0.tar.gz
attrsExtract := $(BW_THIRD_PARTY_BUILD_DIR)/attrs-24.2.0
attrsStamp := $(bwIntermediateCommonSiteDir)/attr/__init__.py

$(attrsStamp): $(attrsTarball) | $(bwIntermediateCommonSiteDir)
	@rm -rf $(attrsExtract)
	@mkdir -p $(attrsExtract)
	@tar -xzf $< --strip-components=1 -C $(attrsExtract)
	@cp -r $(attrsExtract)/src/attr $(bwIntermediateCommonSiteDir)/
	@echo attrs-24.2.0 > $(bwIntermediateCommonSiteDir)/attrs-24.2.0.pth

.PHONY: python_attrs
python_attrs: $(attrsStamp)
BW_MISC += python_attrs

# constantly
constantlyTarball := $(sourcePythonModulesDir)/constantly-23.10.4.tar.gz
constantlyExtract := $(BW_THIRD_PARTY_BUILD_DIR)/constantly-23.10.4
constantlyStamp := $(bwIntermediateCommonSiteDir)/constantly/__init__.py

$(constantlyStamp): $(constantlyTarball) | $(bwIntermediateCommonSiteDir)
	@rm -rf $(constantlyExtract)
	@mkdir -p $(constantlyExtract)
	@tar -xzf $< --strip-components=1 -C $(constantlyExtract)
	@cp -r $(constantlyExtract)/constantly $(bwIntermediateCommonSiteDir)/
	@echo constantly-23.10.4 > $(bwIntermediateCommonSiteDir)/constantly-23.10.4.pth

.PHONY: python_constantly
python_constantly: $(constantlyStamp)
BW_MISC += python_constantly

# incremental
incrementalTarball := $(sourcePythonModulesDir)/incremental-24.7.0.tar.gz
incrementalExtract := $(BW_THIRD_PARTY_BUILD_DIR)/incremental-24.7.0
incrementalStamp := $(bwIntermediateCommonSiteDir)/incremental/__init__.py

$(incrementalStamp): $(incrementalTarball) | $(bwIntermediateCommonSiteDir)
	@rm -rf $(incrementalExtract)
	@mkdir -p $(incrementalExtract)
	@tar -xzf $< --strip-components=1 -C $(incrementalExtract)
	@cp -r $(incrementalExtract)/src/incremental $(bwIntermediateCommonSiteDir)/
	@echo incremental-24.7.0 > $(bwIntermediateCommonSiteDir)/incremental-24.7.0.pth

.PHONY: python_incremental
python_incremental: $(incrementalStamp)
BW_MISC += python_incremental

# Automat
automatTarball := $(sourcePythonModulesDir)/automat-24.8.1.tar.gz
automatExtract := $(BW_THIRD_PARTY_BUILD_DIR)/automat-24.8.1
automatStamp := $(bwIntermediateCommonSiteDir)/automat/__init__.py

$(automatStamp): $(automatTarball) | $(bwIntermediateCommonSiteDir)
	@rm -rf $(automatExtract)
	@mkdir -p $(automatExtract)
	@tar -xzf $< --strip-components=1 -C $(automatExtract)
	@cp -r $(automatExtract)/src/automat $(bwIntermediateCommonSiteDir)/
	@echo automat-24.8.1 > $(bwIntermediateCommonSiteDir)/automat-24.8.1.pth

.PHONY: python_automat
python_automat: $(automatStamp)
BW_MISC += python_automat

# idna (used by hyperlink)
idnaTarball := $(sourcePythonModulesDir)/idna-3.10.tar.gz
idnaExtract := $(BW_THIRD_PARTY_BUILD_DIR)/idna-3.10
idnaStamp := $(bwIntermediateCommonSiteDir)/idna/__init__.py

$(idnaStamp): $(idnaTarball) | $(bwIntermediateCommonSiteDir)
	@rm -rf $(idnaExtract)
	@mkdir -p $(idnaExtract)
	@tar -xzf $< --strip-components=1 -C $(idnaExtract)
	@cp -r $(idnaExtract)/idna $(bwIntermediateCommonSiteDir)/
	@echo idna-3.10 > $(bwIntermediateCommonSiteDir)/idna-3.10.pth

.PHONY: python_idna
python_idna: $(idnaStamp)
BW_MISC += python_idna

# hyperlink (used by twisted)
hyperlinkTarball := $(sourcePythonModulesDir)/hyperlink-21.0.0.tar.gz
hyperlinkExtract := $(BW_THIRD_PARTY_BUILD_DIR)/hyperlink-21.0.0
hyperlinkStamp := $(bwIntermediateCommonSiteDir)/hyperlink/__init__.py

$(hyperlinkStamp): $(hyperlinkTarball) | $(bwIntermediateCommonSiteDir)
	@rm -rf $(hyperlinkExtract)
	@mkdir -p $(hyperlinkExtract)
	@tar -xzf $< --strip-components=1 -C $(hyperlinkExtract)
	@cp -r $(hyperlinkExtract)/src/hyperlink $(bwIntermediateCommonSiteDir)/
	@echo hyperlink-21.0.0 > $(bwIntermediateCommonSiteDir)/hyperlink-21.0.0.pth

.PHONY: python_hyperlink
python_hyperlink: $(hyperlinkStamp)
BW_MISC += python_hyperlink

# Pympler
pymplerTarball := $(sourcePythonModulesDir)/Pympler-1.0.tar.gz
pymplerExtract := $(BW_THIRD_PARTY_BUILD_DIR)/Pympler-1.0
pymplerStamp := $(bwIntermediateCommonSiteDir)/pympler/__init__.py

$(pymplerStamp): $(pymplerTarball) | $(bwIntermediateCommonSiteDir)
	@rm -rf $(pymplerExtract)
	@mkdir -p $(pymplerExtract)
	@tar -xzf $< --strip-components=1 -C $(pymplerExtract)
	@cp -r $(pymplerExtract)/pympler $(bwIntermediateCommonSiteDir)/
	@echo Pympler-1.0 > $(bwIntermediateCommonSiteDir)/Pympler-1.0.pth

.PHONY: python_pympler
python_pympler: $(pymplerStamp)
BW_MISC += python_pympler

#
# --- server_common site-packages ------------------------------------------

# pika
pikaTarball := $(sourcePythonModulesDir)/pika-1.3.2.tar.gz
pikaExtract := $(BW_THIRD_PARTY_BUILD_DIR)/pika-1.3.2
pikaStamp := $(bwIntermediateServerCommonSiteDir)/pika/__init__.py

$(pikaStamp): $(pikaTarball) | $(bwIntermediateServerCommonSiteDir)
	@rm -rf $(pikaExtract)
	@mkdir -p $(pikaExtract)
	@tar -xzf $< --strip-components=1 -C $(pikaExtract)
	@cp -r $(pikaExtract)/pika $(bwIntermediateServerCommonSiteDir)/
	@echo pika-1.3.2 > $(bwIntermediateServerCommonSiteDir)/pika-1.3.2.pth

.PHONY: python_pika
python_pika: $(pikaStamp)
BW_MISC += python_pika

# msgpack
msgpackTarball := $(sourcePythonModulesDir)/msgpack-1.1.0.tar.gz
msgpackExtract := $(BW_THIRD_PARTY_BUILD_DIR)/msgpack-1.1.0
msgpackStamp := $(bwIntermediateServerCommonSiteDir)/msgpack/__init__.py

$(msgpackStamp): $(msgpackTarball) | $(bwIntermediateServerCommonSiteDir)
	@rm -rf $(msgpackExtract)
	@mkdir -p $(msgpackExtract)
	@tar -xzf $< --strip-components=1 -C $(msgpackExtract)
	@cp -r $(msgpackExtract)/msgpack $(bwIntermediateServerCommonSiteDir)/
	@echo msgpack-1.1.0 > $(bwIntermediateServerCommonSiteDir)/msgpack-1.1.0.pth

.PHONY: python_msgpack
python_msgpack: $(msgpackStamp)
BW_MISC += python_msgpack

# SQLAlchemy
sqlalchemyTarball := $(sourcePythonModulesDir)/sqlalchemy-1.4.54.tar.gz
sqlalchemyExtract := $(BW_THIRD_PARTY_BUILD_DIR)/sqlalchemy-1.4.54
sqlalchemyStamp := $(bwIntermediateServerCommonSiteDir)/sqlalchemy/__init__.py

$(sqlalchemyStamp): $(sqlalchemyTarball) | $(bwIntermediateServerCommonSiteDir)
	@rm -rf $(sqlalchemyExtract)
	@mkdir -p $(sqlalchemyExtract)
	@tar -xzf $< --strip-components=1 -C $(sqlalchemyExtract)
	@cp -r $(sqlalchemyExtract)/lib/sqlalchemy $(bwIntermediateServerCommonSiteDir)/
	@echo sqlalchemy-1.4.54 > $(bwIntermediateServerCommonSiteDir)/sqlalchemy-1.4.54.pth

.PHONY: python_sqlalchemy
python_sqlalchemy: $(sqlalchemyStamp)
BW_MISC += python_sqlalchemy

# zope.interface (provides the zope namespace + interface; used by twisted)
zopeInterfaceTarball := $(sourcePythonModulesDir)/zope.interface-7.0.tar.gz
zopeInterfaceExtract := $(BW_THIRD_PARTY_BUILD_DIR)/zope.interface-7.0
zopeInterfaceStamp := $(bwIntermediateServerCommonSiteDir)/zope/__init__.py

$(zopeInterfaceStamp): $(zopeInterfaceTarball) | $(bwIntermediateServerCommonSiteDir)
	@rm -rf $(zopeInterfaceExtract)
	@mkdir -p $(zopeInterfaceExtract)
	@tar -xzf $< --strip-components=1 -C $(zopeInterfaceExtract)
	@cp -r $(zopeInterfaceExtract)/src/zope $(bwIntermediateServerCommonSiteDir)/
	@echo zope.interface-7.0 > $(bwIntermediateServerCommonSiteDir)/zope.interface-7.0.pth

.PHONY: python_zope_interface
python_zope_interface: $(zopeInterfaceStamp)
BW_MISC += python_zope_interface

# Twisted
twistedTarball := $(sourcePythonModulesDir)/twisted-24.7.0.tar.gz
twistedExtract := $(BW_THIRD_PARTY_BUILD_DIR)/twisted-24.7.0
twistedStamp := $(bwIntermediateServerCommonSiteDir)/twisted/__init__.py

$(twistedStamp): $(twistedTarball) | $(bwIntermediateServerCommonSiteDir)
	@rm -rf $(twistedExtract)
	@mkdir -p $(twistedExtract)
	@tar -xzf $< --strip-components=1 -C $(twistedExtract)
	@cp -r $(twistedExtract)/src/twisted $(bwIntermediateServerCommonSiteDir)/
	@echo twisted-24.7.0 > $(bwIntermediateServerCommonSiteDir)/twisted-24.7.0.pth

.PHONY: python_twisted
python_twisted: $(twistedStamp)
BW_MISC += python_twisted

# vi: filetype=make
