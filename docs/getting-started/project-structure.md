# Project Structure


<div class="arch-hero">

BigWorld is a large C/C++ MMO engine. This document describes the project's
directory layout and responsibility split.

</div>

## Top-level directories

```
BigWorld/
├── programming/          # Source code (core)
├── docs/                 # Documentation (VitePress)
├── game/                 # Build output root (Makefile default install directory)
├── scripts/              # Helper scripts (CMake sample builds / daily packaging)
├── .github/              # GitHub configuration
├── logo.svg              # Project logo
├── package.json          # Node.js configuration (docs)
├── README.md             # Project overview
├── CONTRIBUTING.md       # Contributing guide
├── MAINTAINTERS.md       # Maintainers
├── TESTING.md            # Testing standards and pitfall log
├── todo.md               # Patrol to-do ledger
├── LICENSE               # License
└── THIRD-PARTY-NOTICES   # Third-party notices
```

## Source directories

### `programming/`

```
programming/
├── bigworld/             # BigWorld main body
│   ├── server/           # Server processes
│   ├── client/           # Client
│   ├── lib/              # Core libraries
│   ├── tools/            # Tools
│   ├── third_party/      # Third-party dependencies
│   ├── build/            # Build scripts
│   ├── common/           # Shared definitions
│   └── examples/         # Example code
├── fantasydemo/          # FantasyDemo sample
├── Makefile              # Top-level Makefile
└── Makefile.rules        # Make rules
```

### `programming/bigworld/server/`

Server process directories:

| Directory | Process | Responsibility |
|------|------|------|
| `baseapp/` | BaseApp | Player base-data management |
| `cellapp/` | CellApp | Game-world logic |
| `dbapp/` | DBApp | Database proxy |
| `loginapp/` | LoginApp | Login service |
| `baseappmgr/` | BaseAppMgr | BaseApp manager |
| `cellappmgr/` | CellAppMgr | CellApp manager |
| `dbappmgr/` | DBAppMgr | DBApp manager |
| `reviver/` | Reviver | Process recovery |
| `tools/` | - | Server tools |
| `*_extensions/` | - | Extension modules |

### `programming/bigworld/lib/`

Core library directories (57 sub-libraries):

#### Infrastructure

| Library | Responsibility |
|---|---|
| `cstdmf` | Foundation utility library (debug, memory, timers) |
| `network` | Network stack (Mercury) |
| `math` | Math library |
| `resmgr` | Resource management |

#### Server shared

| Library | Responsibility |
|---|---|
| `entitydef` | Entity definition system |
| `script` | Script binding abstraction |
| `pyscript` | Python bridge layer |
| `db` | Database abstraction |
| `server` | Server shared code |

#### Rendering and scene

| Library | Responsibility |
|---|---|
| `moo` | Rendering engine |
| `scene` | Scene management |
| `space` | Space management |
| `chunk` | Indoor scenes |
| `terrain` | Terrain system |

#### Client-specific

| Library | Responsibility |
|---|---|
| `connection` | Client connection |
| `connection_model` | Connection model |
| `camera` | Camera system |
| `controls` | Input controls |

### `programming/bigworld/tools/`

Editors and tools:

| Tool | Responsibility |
|------|------|
| `worldeditor/` | World editor |
| `modeleditor/` | Model editor |
| `particle_editor/` | Particle editor |
| `assetprocessor/` | Asset processor |
| `navgen/` | Navigation mesh generation |

### `programming/bigworld/third_party/`

Third-party dependencies: 22 vendored directories (another 7 dependencies
come through vcpkg; see below).

| Dependency | Purpose |
|------|------|
| `python` | Embedded Python 3.13 interpreter (BigWorld-patched version; not via vcpkg) |
| `mongodb` | Legacy MongoDB C++ driver (the vcpkg mongo-cxx-driver 3.x API is incompatible; kept) |
| `CppUnitLite2` | Unit-test framework |
| `re2` | Regular expressions |
| `recastnavigation` | Navigation meshes |
| `nedalloc` / `nvtt` / `nvmeshmender` | Memory allocation / texture compression / mesh fixing |
| `png` / `zip` / `sqlite` / `curl` | Historical vendored copies (platform-dependent) |
| `openssl-vs2010` … `openssl-vs2015` / `openssl_android` | Historical platform build copies |
| `putty` / `WTL` / `openautomate` / `cmake-win32-x86` / `python_modules` | Tool libraries and build support |

vcpkg ports on the el7 build path (`programming/bigworld/vcpkg.json`):
`openssl`, `curl` (openssl feature), `jsoncpp`, `zlib`, `libpng`,
`sqlite3`, `bzip2` (the last only for the CPython `_bz2` extension).

### `programming/bigworld/build/`

Build scripts:

```
build/
├── make/                 # Linux Make build
│   ├── Makefile          # Main Makefile
│   ├── platform_el6.mak  # CentOS 6 configuration
│   ├── platform_el7.mak  # CentOS 7 configuration
│   └── third_party_python.mak
├── cmake/                # CMake configuration
├── docker/               # Docker configuration
│   └── Dockerfile
└── *.bat                 # Windows batch scripts
```

## Documentation directories

### `docs/`

```
docs/
├── index.md              # Home page
├── .vitepress/           # VitePress configuration
├── public/               # Static assets (logo.svg, pdf/)
├── getting-started/      # Getting started
├── architecture/         # Architecture research (28 chapters)
├── analysis/             # Project analysis
├── upgrade-plan/         # Migration and upgrade topics (6 documents)
├── references/           # Reference material
├── pdf/                  # Original PDFs (server whitepaper / build guide / installation guide, 3 files)
├── python-313-migration.md  # Full Python 3.13 migration ledger
├── vcpkg-migration.md    # vcpkg migration topic
└── cpp23-migration.md    # C++23 migration topic
```

## Dependency relationships

```
┌─────────────┐
│   server/   │ ← server processes
└──────┬──────┘
       │
┌──────▼──────┐
│    lib/     │ ← core libraries
└──────┬──────┘
       │
┌──────▼──────┐
│ third_party │ ← third-party dependencies
└─────────────┘
```

## Next steps

- [Getting started](/getting-started/) — building and running
- [Source code guide](/analysis/source-code-guide) — reading paths
- [Architecture research method](/architecture/research-method) — deeper study
