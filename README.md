# apisig

Deterministic API fingerprinting for compatibility gates.

apisig is a native C++ CLI intended for CI usage like clang-format and other build tools.

By default, apisig builds in self-contained mode and does not require a clang installation at runtime.

It computes two signatures:

- API hash: changes only when the normalized public API symbol set changes.
- Rebuild hash: changes when API hash or build metadata changes.

## Build

```powershell
cmake -S . -B out/build -DAPISIG_SELF_CONTAINED=ON -DAPISIG_WITH_LIBTOOLING=OFF
cmake --build out/build --config Release
```

Optional LibTooling-enabled flavor:

```powershell
cmake -S . -B out/build-libtooling -DAPISIG_SELF_CONTAINED=OFF -DAPISIG_WITH_LIBTOOLING=ON
cmake --build out/build-libtooling --config Release
```

Native Visual Studio toolchain:

```powershell
cmake -S . -B out/build-vs2022 -G "Visual Studio 17 2022" -A x64
cmake --build out/build-vs2022 --config Release
```

Preset-based build:

```powershell
cmake --preset vs2022-x64
cmake --build --preset build-release
```

LibTooling preset variant:

```powershell
cmake --preset vs2022-x64-libtooling
cmake --build --preset build-release-libtooling
```

Binary path:

- out/build/Release/apisig.exe (multi-config generators)
- out/build/apisig (single-config generators)

## Usage

File-driven mode (available now):

```powershell
apisig compute --symbols symbols.txt --metadata build.env --json
```

LibTooling mode (interface present, extractor pass pending):

```powershell
apisig compute --compdb compile_commands.json --source-root . --metadata build.env --json
```

## Input Format

Symbols file:

- one symbol per line
- blank lines ignored
- lines beginning with `#` ignored
- duplicate symbols are de-duplicated
- ordering does not matter

Metadata file:

- one `key=value` pair per line
- blank lines ignored
- lines beginning with `#` ignored
- duplicate keys keep the last value

## Output

`compute` returns:

- `api_hash`
- `rebuild_hash`

Current scaffold uses a stable 64-bit FNV-1a hash for deterministic output.

## Roadmap

- implement Clang LibTooling AST extraction for public C/C++ symbols
- add baseline management (`snapshot`, `compare`, `assert`)
- classify changes (compatible, breaking, rebuild-only)
- add CI-oriented exit codes and report formats

## Dependency Management

Yes, dependencies can be downloaded automatically.

Recommended options:

- self-contained default for baseline CI agents (no clang payload required)
- vcpkg toolchain mode when enabling LibTooling (best for Windows CI and Visual Studio):
  - set `VCPKG_ROOT`
  - use preset `vs2022-x64-vcpkg-libtooling`
- CMake `FetchContent` for smaller pure-CMake dependencies

For LibTooling specifically, vcpkg or preinstalled LLVM/Clang packages are the practical options.
If agents do not have clang payloads, use the self-contained presets and feed symbol lists via `--symbols`.
