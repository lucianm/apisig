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

Inspect exactly what is hashed (normalized symbol set):

```powershell
apisig extract --symbols symbols.txt --json
```

Create or refresh a baseline file:

```powershell
apisig snapshot --symbols symbols.txt --metadata build.env --out apisig-baseline.json
```

Compare current signatures against a baseline:

```powershell
apisig compare --symbols symbols.txt --metadata build.env --baseline apisig-baseline.json
```

LibTooling mode (when built with LibTooling enabled):

```powershell
apisig compute --compdb compile_commands.json --source-root . --metadata build.env --json
```

Inspect clang-derived symbols from AST traversal:

```powershell
apisig extract --compdb compile_commands.json --source-root . --json
```

## Extraction Modes And Trust Level

apisig currently supports two different extraction modes. They are not equivalent in semantic precision.

### File-driven mode (`--symbols`)

- Purpose: self-contained fallback when clang/LibTooling is not available on build agents.
- Input: text lines from a symbols file or directly from a header fed as symbols input.
- Normalization: comment stripping, BOM stripping, token-aware whitespace normalization, macro continuation folding.
- Canonicalization: sorted, deduplicated symbol list for stable order-invariant output.

What this mode is good at:

- robust change detection for many practical header text edits
- deterministic behavior on minimal CI agents

Known limitations:

- text-based, not full C++ semantic analysis
- can lose declaration context/scope relationships
- deduplication can hide multiplicity of identical normalized lines

### LibTooling mode (`--compdb` + `--source-root`)

- Purpose: semantic extraction path using clang AST.
- Input: compile_commands.json and source root.
- Extraction: externally linked declarations (functions, record types, enums, globals) using AST traversal.

What this mode is good at:

- language-aware extraction and stronger semantic fidelity
- better long-term authority for API compatibility gates

Operational note:

- LibTooling mode means apisig uses clang as a parser library.
- It does not instrument your product code and does not force your product build to use clang.

Recommendation:

- treat file-driven mode as a CI-safe fallback
- treat LibTooling mode as the authoritative semantic signature mode when available

## Input Format

Symbols file:

- one symbol per line
- blank lines ignored
- C/C++ comments are stripped (`//...` and `/*...*/`)
- C/C++ whitespace is normalized token-wise (formatting-only spacing changes are ignored)
- lines beginning with `# ` (hash + space) are treated as human comment lines and ignored
- preprocessor directives such as `#define` are included in hashing input
- multiline macros (`\\` line continuation) are folded into one normalized logical line
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

`snapshot` writes a baseline JSON with `version`, `api_hash`, and `rebuild_hash`.

`compare` returns:

- `0`: unchanged
- `10`: API changed
- `11`: rebuild hash changed while API hash is unchanged
- `1`: runtime/config/input error
- `2`: usage error

## Roadmap

- implement Clang LibTooling AST extraction for public C/C++ symbols
- classify changes (compatible, breaking, rebuild-only)
- extend report formats and policy controls for CI consumption

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
