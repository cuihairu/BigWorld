// pch.hpp
//
// The bw_lsp unit-test precompiled header, following the
// lib/entitydef/unit_test/pch.hpp shape: Python.h first (the engine's headers
// assume the CPython defines are in place before any standard header), then
// the test framework and the entitydef harness the tests below reuse.

#ifndef __APPLE__
#include "Python.h"
#endif

#ifdef _WIN32
#pragma once

#include <stdio.h>
#include <tchar.h>
#endif

#include "CppUnitLite2/src/CppUnitLite2.h"
#include "unit_test_lib/unit_test.hpp"
#include "entitydef/unit_test/test_harness.hpp"
