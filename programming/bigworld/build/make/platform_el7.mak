#
# Configuration for EL 7-based platforms
#
# BIGWORLD(c++23 migration): was -std=c++11. GCC 15 in this image supports
# the full C++23 language; see docs/cpp23-migration.md.
CXX11_CXXFLAGS := -std=c++23

# Test if the system installed Mongo DB library, 0 is installed. Now as we are
# building and linking to mongodb cxx driver locally, the testing is not
# appropriate anymore. We only support it on el7 at the moment, so we just
# assume system has mongodb for el7. In future, if we go with system installed
# mongodb cxx driver again, we can re-enable this testing. 
# BIGWORLD(3.13 migration): re-enabled the probe -- the hard-coded
# `SYSTEM_HAS_MONGODB := 1` forces -lmongoclient into message_logger's link
# line even on hosts without the driver, which cannot link.
SYSTEM_TEST_MONGODB_RESULT := $(shell $(BW_BLDDIR)/test_mongodb.sh && echo $$?)
ifeq ($(SYSTEM_TEST_MONGODB_RESULT),0)
SYSTEM_HAS_MONGODB := 1
endif


# Enable C++11 as a standard compiler for EL 7
CXXFLAGS   += $(CXX11_CXXFLAGS)

# BIGWORLD_BEGIN(3.13 migration)
# Modern GCC (>= 8) turns these into errors with -Werror; neither blocks
# the Python 3.13 port:
# - expansion-to-defined: long-standing preprocessor patterns in cstdmf.
# - deprecated-declarations: CPython 3.13 headers deprecate APIs still in
#   use across the engine (e.g. PyWeakref_GET_OBJECT); those call sites
#   are being migrated separately.
# - deprecated-copy: classes with user-declared copy constructors relying
#   on the implicit copy assignment; pre-existing engine-wide pattern the
#   newer compiler now flags.
CXXFLAGS   += -Wno-error=expansion-to-defined
CXXFLAGS   += -Wno-error=deprecated-declarations
CXXFLAGS   += -Wno-error=deprecated-copy
# BIGWORLD_END

# build/make/platform_el7.mak
