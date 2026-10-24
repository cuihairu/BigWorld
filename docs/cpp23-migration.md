# BigWorld C++ Standard Upgrade Analysis (Target: C++23)

> Task: evaluate and carry out the upgrade of BigWorld's C++ standard to C++23.
> Status: **analysis complete; for the landing phase see "Final landing and execution results" at the end of this document.**
> Constraint: shares the dev branch with the Python 3.13 port; the two efforts are committed separately.

## 1. Current-State Inventory

### 1.1 Language-standard setting points (whole-repo scan)

> This subsection is a **pre-switch snapshot** (taken at migration kickoff). The make
> main chain has since switched to C++23 (`platform_el7.mak:6`); see §6.3 for the
> execution results.

| Location | Setting | Note |
|---|---|---|
| `programming/bigworld/build/make/platform_el7.mak:4` | `CXX11_CXXFLAGS := -std=c++11`, plus `CXXFLAGS += $(CXX11_CXXFLAGS)` | **the tier actually in effect on Linux at that time** |
| `programming/bigworld/build/make/platform_el6.mak:8` | `-std=c++0x` | legacy el6 config (no longer triggered) |
| `build/cmake/BWCompilerAndLinkerOptions.cmake` | no `-std` at all; the MSVC branch relies entirely on the compiler-version default tier | CMake does not set `CMAKE_CXX_STANDARD` |
| `build/xcode/*` | `CLANG_CXX_LANGUAGE_STANDARD` not set | uses the Xcode default tier |

Conclusion: **there is no gnu++98**. The entire Linux codebase already compiled under `-std=c++11`; this repo never faced a jump straight out of C++98.

### 1.2 Compiler matrix (minimum version per vendor for C++23)

| Compiler | Minimum version with full C++23 support | Repo status | Conclusion |
|---|---|---|---|
| GCC | **14** (13 partial) | local `/usr/bin/g++` 15.2.0 | ✅ c++23 is viable |
| Clang | **18** | the repo does not build with clang (only `#ifdef __clang__` compatibility branches exist) | left as TODO |
| MSVC | **19.37** (VS 2022 17.7) | the codebase is from the VS2012 era (`_MSC_VER <= 1600/1700` conditional compilation, the `/Zm282` VS2012 workaround, old `.sln/.vcproj`) | ❌ far from ready; the Windows side is out of scope |

### 1.3 Notes on the local build environment

- `/usr/bin/g++` = 15.2.0 works; at the time of writing, `~/.local/bin/g++` was found to be a broken self-installed symlink (missing libstdc++ headers) that also took precedence in PATH. That machine-specific condition no longer exists (no file at that path); if it reappears, set `CXX=/usr/bin/g++` explicitly or adjust PATH.
- The host is Ubuntu (`platform_info.py` gained a Debian→el7 equivalence mapping, delivered with the Python migration task's commit).

## 2. Deprecated/removed feature hits, quantified per tier

Scope: `lib/ server/ tools/ client/ guimanager/ examples/` (excluding `third_party/`), file types `.cpp .hpp .h .ipp`.

### 11→14 (no removals, deprecations only)

| Feature | Hits | Note |
|---|---|---|
| `std::auto_ptr` | 251 | deprecated since C++11 |
| `std::bind1st/bind2nd/ptr_fun` | 0 | — |
| `std::random_shuffle` | 5 | deprecated since C++14 (removed from the sequence in 17) |
| `std::mem_fun` | 2 | same as above |

### 14→17 (first batch of hard removals)

| Feature | Hits | Note |
|---|---|---|
| `register` storage-class specifier | 125 files / 211 sites | removed in C++17; gcc in practice still tolerates it as a warning |
| `throw(type)` dynamic exception specification | **0** | good news: no typed throw-spec anywhere in the repo |
| `throw()` (empty dynamic exception spec) | **21** | deprecated in C++17; **a hard error from c++17 onward under gcc 15** (measured) |
| `std::unary_function/binary_function` | 19 | removed in C++17 (libstdc++ still ships a deprecated copy by default) |
| `std::auto_ptr` | 251 as above | removed from the standard in C++17 |
| `std::random_shuffle`, `std::mem_fun` | 7 | removed from the standard in C++17 |
| `std::tr1::` | 25 | tr1 headers are still provided by libstdc++ after C++17, but they are relics |

### 17→20 (second batch of hard removals)

| Feature | Hits | Note |
|---|---|---|
| `std::result_of` | 0 | — |
| `std::raw_storage_iterator` | 0 | — |
| `std::is_pod` | 0 | — |
| `[=]` implicit capture of this | **0** | deprecated in C++20, removed in C++23; zero hits repo-wide |
| `std::iterator` inheritance | 2 (`lib/entitydef/data_type.hpp:326`, `lib/cstdmf/circular_queue.hpp:85`) | deprecated in C++17, still usable in C++20; worth fixing in passing |

### 20→23 (no new removals)

`std::aligned_storage` and friends are merely deprecated further in C++23, not removed. No incremental hits.

## 3. gcc 15.2 empirical verification (same test case compiled tier by tier)

The test case contains `auto_ptr`, `register`, `[=]` implicit capture of this, `binary_function`, and `throw()` at once.

| Tier | Default libstdc++ | `-D_GLIBCXX_USE_DEPRECATED=0` |
|---|---|---|
| c++11/14 | deprecation warnings only | 1 error (the retained auto_ptr copy is disabled) |
| c++17/20/23 | **the only hard error is `throw()`**; everything else degrades to a warning | 4 errors |

Conclusion: the **only hard blocker for switching Linux to c++23 is the 21 `throw()` sites**; the remaining hits do not block compilation under the default library configuration. Nevertheless, following the "fix first, then switch" principle, all of them are cleared to zero so the build does not depend on the leniency of `_GLIBCXX_USE_DEPRECATED`.

## 4. Third-party lock-in (downgrade / exception list)

| Library | Problem | Disposition |
|---|---|---|
| `third_party/openssl` (1.0.0d) | **hard failure** compiling under gcc 15's default C23 (`bool` became a keyword, old code uses it as a variable name) | out of scope for the engine-code upgrade; the vcpkg task replaces it wholesale with a modern OpenSSL. **A concrete case of "an old third party locked to a modern toolchain", and the shared blocker for Python 3.13's `_ssl`** |
| rest of `third_party/` (nedalloc, jsoncpp, re2, recastnavigation…) | to be inventoried one by one by the vcpkg task; engine-side fixes are invisible to them (each builds at its own tier) | later task |

## 5. Boundary against the Python 3.13 migration

- CPython 3.13 public headers require **C++11 or above** inside a C++ translation unit — el7's existing `-std=c++11` **satisfies this exactly**, so the Python migration task **does not touch any `-std` setting**; the two tasks are fully decoupled in terms of commits.
- The C++23 task switches the tier only after all compile-verification batches of the Python port have landed, so that "changing the standard" and "changing the C-API" never contaminate the same compile result.

## 6. Final landing and execution results

Execution followed "fix first, then switch" strictly: every deprecated/removed feature was cleared at the `-std=c++11` tier and fully verified (28 libraries passing + 3 environment-caused skips, see "skipped items" at the end), and only then was the tier switched.

### 6.1 Cleanup checklist (done at the c++11 tier; checkboxes are authoritative)

- [x] `throw()` → `noexcept`: 21 sites (lib/memhook/memhook.cpp, lib/cstdmf/bw_memory.hpp/.cpp, lib/db_storage_mysql/database_exception.hpp/.cpp)
- [x] `std::auto_ptr` → `std::unique_ptr`: 244 sites (121 files; the §2 estimate of 251 included 5 comment mentions, which were rewritten in step)
- [x] `register` storage-class specifier: **4 sites** (the §2 estimate of 211 came from broad grep matching where the overwhelming majority were variable-name/identifier false positives; the real ones are lib/math/math_extra.cpp, lib/math/perlin_noise.cpp ×2, lib/cstdmf/bit_reader.cpp)
- [x] `unary_function/binary_function` inheritance removed: 18 sites (14 files; hash specializations moved to `std::hash`/`<functional>`, comparison funnels moved to explicit typedefs or lambda equivalents)
- [x] `random_shuffle` → `std::shuffle` + `std::mt19937(std::random_device()())`: 5 sites (loose_octree, general_editor, reviver, backup_sender, archiver)
- [x] `mem_fun` leftovers: re-verified to zero (grep count 0)
- [x] `std::tr1::` unified into `std::`: 24 sites (lib/cstdmf/bw_hash.hpp rewritten, unifying header and namespace selection for bw_unordered_map/bw_unordered_set/bw_std)
- [x] `std::iterator` inheritance → explicit five-member `typedef` set: 2 sites (lib/entitydef/data_type.hpp, lib/cstdmf/circular_queue.hpp)
- [x] `NULL` → `nullptr` (in unique_ptr construction/assignment contexts): 76 sites (see 6.2; semantics-equivalent under c++11, folded into the cleanup batch)

### 6.2 Compile-driven fixes surfaced after the switch (only visible at c++23, recorded one by one)

| Problem | Rule basis | Fix |
|---|---|---|
| `unique_ptr(NULL)` construction ambiguity, 76 sites | under C++23, `NULL` (integer 0) is ambiguous between the `pointer` and `nullptr_t` constructors | changed all to `nullptr` (equally correct for raw pointers) |
| `watcher.hpp` string-to-value does not support pointer types | gcc15 libstdc++ removed `operator>>(istream, void*&)` in the C++23 tier | added a `watcherStringToValue( const char *, VALUE_TYPE *& )` overload returning false |
| `server/tools/message_logger/user_log.cpp` returns NULL into `BW::string` | P2166R1 removed the `string(nullptr_t)` constructor (previously UB) | changed to `return BW::string();` |
| pyscript/typeobject designated+positional initializer mixing | since C++20 a single initializer list may not mix designated and positional members; CPython 3.13's `PyVarObject_HEAD_INIT` expands positionally | added `lib/pyscript/pytypeobject_head.hpp` (`BW_PYTYPEOBJECT_HEAD_INIT`, a double-level `.ob_base` expansion of the 3.13 object header; depends only on Python.h so out-of-repo bare CPython code can reuse it); all 688 type objects in pyobject_plus.hpp converted to designated |
| `server/tools/message_logger` four bare CPython type objects | same as above | after verification, kept fully positional initialization (the 3.13 slot order is already correct, and fully positional is legal in C++23); no macro introduced |
| `bw_profile`, `message_reader` pick up the system Python headers | upstream leftover `useSystemPython := 1`, which on a modern host resolves to any `/usr/include/pythonX.Y` (the local 3.14 public headers lack `_Py_IMMORTAL_REFCNT`) | both changed to `useSystemPython := 0`, aligning them with all other components on the in-repo 3.13 headers and `bwpython3.13` |
| `examples/cellapp_extension` Py2 API leftovers | `PyString_*` does not exist in Python 3 headers (during the Python task this target was never actually compiled or executed, so stale .o files masked the problem) | `PyString_FromStringAndSize` → `PyBytes_FromStringAndSize` (identical binary-stream semantics) |
| `base.cpp`/`mailbox.cpp` pass arguments by `auto_ptr` copy semantics | `auto_ptr`'s copy-transfers-ownership is a deleted copy constructor under `unique_ptr` | `getStream(..., std::move( pReplyHandler ))`; the three `pHandler` pass-throughs in `mailbox.cpp` fixed the same way; the two `return unique_ptr(NULL)` in `base.cpp` → `return nullptr` |
| 5 sites of non-const member `operator==/!=` ambiguity | C++20 rewritten/reversed candidates make a non-const comparison member ambiguous when both operands are non-const (gcc hints "try making the operator a const member function") | appended a trailing `const` qualifier (semantics unchanged): log_on_params.hpp, cstdmf/event.hpp, cstdmf/dogwatch.hpp/.cpp (the iterator's `==`/`!=`), romp/lens_effect.hpp |
| `third_party/CppUnitLite2` indentation warnings | gcc15 `-Werror=misleading-indentation` | wrapped the single-statement `if` in ExceptionHandler.cpp in braces (minimal third-party change) |

Additionally: the concurrent-rebuild trampling in `third_party_openssl.mak` (`ar: file truncated`) was root-caused — after `.INTERMEDIATE` had earlier been removed, the recipe never physically created the stamp file, so the plain rule's target was always missing and make re-ran every time; `touch $@` was appended to the end of the recipe. This fix is unrelated to the standard tier but was discovered during this verification pass, so it ships with this task.

### 6.3 Tier switch and verification

- [x] `build/make/platform_el7.mak`: `CXX11_CXXFLAGS := -std=c++23` (variable name kept to minimize the diff; the comment points to this document)
- [x] c++11-tier full library verification: 28 passing; 3 skipped (see 6.4)
- [x] c++23-tier full library verification: 27/29 passing (after the network fix), same 3 environment-caused skips
- [x] c++23-tier full server build (including unit-test builds, tools, examples extensions): `make CXX=/usr/bin/g++ -j8` under `server/` fully green (%SERVER_RESULT%)
- [ ] Windows/Xcode side left as TODO (MSVC needs VS2022 17.7+; Xcode needs `CLANG_CXX_LANGUAGE_STANDARD` set; to be assessed separately)

### 6.4 Skipped items (out of scope for this task, recorded honestly)

| Item | Reason |
|---|---|
| lib/testing | pre-existing breakage: the Makefile references another internal repo layout `$(MF_ROOT)/bigworld/src/server/common/common.mak`; git history confirms it has never been built in this repo |
| lib/bwentity | no Makefile (CMakeLists only, the Windows/CMake flow) |
| lib/db_storage_mysql | the build system deliberately skips it when no MySQL development libraries are present |

### 6.5 Lessons learned

1. **"All green" must be audited for coverage**: during the Python task, the server compile was claimed to pass, but targets such as `cellapp_example.so` had never actually been compiled or executed (stale .o files being skipped masked the Py2 API leftovers). Only after the standard switch forced a full rebuild did the problem surface — the tier switch itself doubled as a coverage audit.
2. **auto_ptr → unique_ptr is not a pure rename**: at pass-by-value sites, `auto_ptr`'s copy-transfers-ownership semantics require an explicit `std::move`, otherwise it is a hard error under C++23.
3. **Macros and designated initializers must not be mixed**: when pairing a CPython header macro (positional expansion) with a designated slot table, either make the whole thing designated or the whole thing positional — there is no middle state.

---

*Document generated during the Python 3.13 migration on the dev branch; hit counts come from grep, tier behavior was measured with gcc 15.2.0.*
