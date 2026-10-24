# Third-Party Dependency Upgrade List


<div class="arch-hero">

BigWorld embeds a large number of third-party libraries, generally at old
versions. This document inventories the current dependency state and offers
upgrade recommendations.

</div>

::: tip Status update (2026-09-28)
The Python row of the table below has landed: the embedded interpreter now
runs **Python 3.13** (originally planned as 3.12, raised along the
dependency chain); the process is in the [full migration ledger](/python-313-migration).
Among the C/C++ rows, OpenSSL, curl, jsoncpp, zlib, libpng, sqlite3, and
bzip2 have switched to a **vcpkg manifest** (measured installs: OpenSSL
3.6.4 / curl 8.22.0 / sqlite3 3.53.4; see `vcpkg.json` and the [vcpkg
migration](/vcpkg-migration)); only CPython (the BWHooks-modified tree) and
the MongoDB legacy driver remain vendored.
:::

## Dependency status

### Python-related

| Dependency | Current version | Python 3 compatible | Recommendation |
|------|----------|---------------|------|
| Python | 2.7.x → **3.13** | ✅ | Migration complete (see the [migration ledger](/python-313-migration)) |
| SQLAlchemy | 0.6.6 | ❌ | Upgrade to 2.0+ |
| oursql | 0.9.2 | ❌ | Replace with mysqlclient |
| Pympler | 0.3.0 | ❌ | Upgrade to 1.0+ |
| pika | 0.9.13 | ❌ | Upgrade to 1.0+ |

### C/C++ dependencies

| Dependency | Current version | Status | Recommendation |
|------|----------|------|------|
| OpenSSL | **3.6.4 (vcpkg)**, originally 1.0.x | ✅ | Done (R1, `vcpkg.json`) |
| curl | **8.22.0 (vcpkg, openssl feature)**, originally 7.x | ✅ | Done |
| SQLite | **3.53.4 (vcpkg)** | ✅ | Done |
| MongoDB C Driver | Old version (kept vendored) | ⚠️ | Assess whether needed |
| jsoncpp | Provided by vcpkg (`vcpkg.json`) | ✅ | Done |
| re2 | Old version | ✅ | Optional upgrade |
| recastnavigation | Old version | ✅ | Keep |

### Build tools

| Tool | Current version | Recommendation |
|------|----------|------|
| CMake | 2.8.12 | Upgrade to 3.20+ |
| GCC | 4.8.x | Upgrade to 11+ |
| Make | GNU Make | Keep |

## Upgrade priorities

### P0 — must upgrade

1. **Python 2.7 → 3.13 (done, 3.13.16)**
   - Blast radius: the whole repository
   - See the [Python upgrade roadmap](/upgrade-plan/python-upgrade-plan)

2. **OpenSSL 1.0 → 3.x**
   - Reason: security vulnerabilities, no longer maintained
   - Impact: network encryption, certificate handling
   - Risk: sizeable API changes

### P1 — recommended upgrades

3. **SQLAlchemy 0.6 → 2.0**
   - Reason: Python 3 compatibility
   - Impact: database operations
   - Risk: API changes

4. **CMake 2.8 → 3.20**
   - Reason: modern C++ support
   - Impact: build system
   - Risk: low

5. **GCC 4.8 → 11**
   - Reason: C++17/20 support
   - Impact: compiler
   - Risk: may introduce new warnings

### P2 — optional upgrades

6. **curl, SQLite, jsoncpp**
   - Reason: feature updates
   - Impact: low
   - Risk: low

## Replacement options

### oursql → [mysqlclient]

**Reason**: oursql is no longer maintained; mysqlclient is officially
recommended by MySQL.

```python
# Old code
import oursql
conn = oursql.connect(host='localhost', user='root', passwd='')

# New code (the example uses mysql-connector-python; if mysqlclient is
# chosen, the API is MySQLdb.connect — keep the package name and API choice
# consistent)
import mysql.connector
conn = mysql.connector.connect(host='localhost', user='root', password='')
```

### SQLAlchemy 0.6 → [2.0]

**Main changes**:
- Session API changes
- Query syntax changes
- Type-system changes

```python
# Old code (0.6)
session.query(User).filter_by(name='Alice').first()

# New code (2.0)
stmt = select(User).where(User.name == 'Alice')
result = session.execute(stmt).first()
```

### OpenSSL 1.0 → [3.x]

**Main changes**:
- Old APIs deprecated
- New Provider mechanism
- Higher default security level

```cpp
// Old code
SSL_CTX_new(SSLv23_method());

// New code
SSL_CTX_new(TLS_method());
```

## Compatibility matrix

| Dependency | Python 2.7 | Python 3.8 | Python 3.13 |
|------|------------|------------|-------------|
| SQLAlchemy 0.6 | ✅ | ❌ | ❌ |
| SQLAlchemy 2.0 | ❌ | ✅ | ✅ |
| oursql 0.9 | ✅ | ❌ | ❌ |
| mysqlclient | ✅ | ✅ | ✅ |
| OpenSSL 1.0 | ✅ | ⚠️ | ❌ |
| OpenSSL 3.x | ⚠️ | ✅ | ✅ |

## Migration strategy

### Phase 1: assessment

1. Scan the source and confirm the actual usage points of each dependency
2. Assess replacement cost
3. Decide the upgrade order

### Phase 2: replacement

Replace in dependency order:

1. The Python interpreter
2. Python standard-library modules
3. Python third-party packages
4. C/C++ dependencies

### Phase 3: validation

1. Compile tests
2. Unit tests
3. Integration tests
4. Performance tests

## Risk notes

::: warning Caution
- Do not upgrade several dependencies at once
- Test thoroughly after each upgrade
- Keep the ability to roll back to old versions
:::

## References

- [Python upgrade roadmap](/upgrade-plan/python-upgrade-plan)
- [Build system modernization](/upgrade-plan/build-system-modernization)
- [Technical debt and risks](/analysis/risks)
