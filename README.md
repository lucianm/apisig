# apisig

Deterministic API fingerprinting for compatibility gates.

apisig is a native C++ CLI intended for CI usage like clang-format and other build tools.

By default, apisig builds in self-contained mode and does not require a clang installation at runtime.

It computes two signatures:

- API hash: changes only when the normalized public API symbol set changes.
- Rebuild hash: changes when API hash or build metadata changes.

## Prerequisites

For a straightforward Windows setup:

- Visual Studio 2022 with the Desktop development with C++ workload
- CMake 3.22 or newer available on `PATH`
- A C++20-capable MSVC toolchain from Visual Studio 2022

Optional, depending on the build flavor you choose:

- internet access during CMake configure if you use the official LLVM SDK auto-download preset
- a preinstalled LLVM/Clang SDK if you use `APISIG_LLVM_ROOT` or the non-download LibTooling build
- `VCPKG_ROOT` if you use the vcpkg-based preset

## Quick Start

If you just checked out the sources and want one build that also enables the current test suite, use the official LLVM SDK preset:

```powershell
cmake --preset vs2022-x64-llvm-sdk-libtooling
cmake --build --preset build-release-llvm-sdk-libtooling
ctest --test-dir out/build-vs2022-llvm-sdk-libtooling -C Release --output-on-failure
```

That produces:

- `out/build-vs2022-llvm-sdk-libtooling/Release/apisig.exe`

You can then sanity-check the CLI surface with:

```powershell
out/build-vs2022-llvm-sdk-libtooling/Release/apisig.exe --help
```

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

Optional LibTooling-enabled flavor using an official prebuilt LLVM SDK downloaded during configure:

```powershell
cmake -S . -B out/build-libtooling-sdk -DAPISIG_SELF_CONTAINED=OFF -DAPISIG_WITH_LIBTOOLING=ON -DAPISIG_DOWNLOAD_LLVM_SDK=ON
cmake --build out/build-libtooling-sdk --config Release
```

To use a previously downloaded or centrally provisioned LLVM SDK instead of the automatic download:

```powershell
cmake -S . -B out/build-libtooling-sdk -DAPISIG_SELF_CONTAINED=OFF -DAPISIG_WITH_LIBTOOLING=ON -DAPISIG_LLVM_ROOT="C:/toolcache/clang+llvm-22.1.8-x86_64-pc-windows-msvc"
cmake --build out/build-libtooling-sdk --config Release
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

Official LLVM SDK preset variant:

```powershell
cmake --preset vs2022-x64-llvm-sdk-libtooling
cmake --build --preset build-release-llvm-sdk-libtooling
```

This is the most convenient preset for a fresh checkout when you want LibTooling support without preinstalling LLVM manually.
The VS2022 presets in this repository pin `CMAKE_SYSTEM_VERSION=10.0.26100.0` to avoid falling back to the Windows 8.1 SDK on machines that have multiple Windows Kits installed.

The auto-download preset caches the official LLVM SDK archive and extraction under `out/llvm-sdk/`.
Use `APISIG_LLVM_SDK_VERSION`, `APISIG_LLVM_SDK_URL`, or `APISIG_LLVM_SDK_CACHE_DIR` to override the default source or cache location.

Binary path:

- out/build/Release/apisig.exe (multi-config generators)
- out/build/apisig (single-config generators)

For the preset builds in this repository, the executable is typically under one of:

- `out/build-vs2022/Release/apisig.exe`
- `out/build-vs2022-libtooling/Release/apisig.exe`
- `out/build-vs2022-llvm-sdk-libtooling/Release/apisig.exe`

On Windows, `apisig.exe` embeds standard version resource metadata (File Version and Product Version) derived from the project version and visible in Explorer and Win32 version APIs.

## Tests

The current automated tests are registered through CTest only when LibTooling is enabled.
If you want to run all tests from a fresh checkout, prefer the official LLVM SDK LibTooling preset:

```powershell
cmake --preset vs2022-x64-llvm-sdk-libtooling
cmake --build --preset build-release-llvm-sdk-libtooling
ctest --test-dir out/build-vs2022-llvm-sdk-libtooling -C Release --output-on-failure
```

Run one focused test by name:

```powershell
ctest --test-dir out/build-vs2022-llvm-sdk-libtooling -C Release --output-on-failure -R apisig.semantic_model_compare
```

Current test coverage includes:

- compdb overlap and ordering invariance
- semantic change detection for enums, type members, and function signatures
- AST report deserialization and schema validation
- AST-to-AST compare mode

## Usage

apisig has three separate concepts that are easy to conflate:

- input mode: where the API model comes from
- stdout format: how the command prints its result
- persisted artifact: which JSON file, if any, gets written

In practice:

- `compute` computes the hash pair
- `--json` prints that command result to stdout as JSON
- `snapshot` computes the same hash pair and writes it as a baseline artifact
- `--out <file>` names that baseline artifact file
- `--ast-report-out <file>` writes a different artifact: the AST report JSON
- `--ast-report-no-locations` omits declaration `line` and `column` fields in the AST report for diff-stable version control
- `compute`, `snapshot`, and `compare` require exactly one input mode
- `--ast-report-out <file>` is supported only with `extract`
- `compare` also requires exactly one baseline mode: `--baseline` or `--baseline-ast-report-json`

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

Print the same snapshot result to stdout as JSON while also writing the baseline file:

```powershell
apisig snapshot --symbols symbols.txt --metadata build.env --out apisig-baseline.json --json
```

Compare current signatures against a baseline:

```powershell
apisig compare --symbols symbols.txt --metadata build.env --baseline apisig-baseline.json
```

Compare a current input mode directly against a baseline AST report JSON:

```powershell
apisig compare --ast-report-json current-ast-report.json --baseline-ast-report-json baseline-ast-report.json --json
```

LibTooling mode (when built with LibTooling enabled):

```powershell
apisig compute --compdb compile_commands.json --source-root . --metadata build.env --json
```

Inspect clang-derived symbols from AST traversal:

```powershell
apisig extract --compdb compile_commands.json --source-root . --json
```

Dump full AST-considered declaration records used to derive the hashed symbol set:

```powershell
apisig extract --compdb compile_commands.json --source-root . --ast-report-out ast-report.json --json
```

Compute directly from a previously extracted semantic model report (no re-extraction):

```powershell
apisig compute --ast-report-json ast-report.json --json
```

Input mode summary:

- `--symbols <file>`: line-based text input, normalized and hashed directly
- `--compdb <file>` with `--source-root <dir>`: clang LibTooling extraction input
- `--ast-report-json <file>`: previously extracted AST report JSON; reads `semantic_model`
- For `compute`, `snapshot`, and `compare`, choose exactly one of those three input modes.
- For `extract`, choose exactly one of `--symbols` or `--compdb`.
- For `compare`, also choose exactly one baseline mode:
  - `--baseline <file>`: baseline hash snapshot JSON
  - `--baseline-ast-report-json <file>`: baseline AST report JSON

Show command help explicitly:

```powershell
apisig --help
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
- `extract` prints canonical symbols only. `compute`/`snapshot`/`compare` hash that canonical symbol set (plus metadata for rebuild hash).
- Use `--ast-report-out <file>` to persist declaration-level AST records (`kind`, `USR`, `qualified_name`, `api_signature`, source location) plus semantic content (enum constants/values, base classes, fields, and method signatures) for validation without flooding console output.
- In LibTooling (`--compdb`) mode, `api_hash` is computed from a normalized semantic model (deduped, sorted, location-independent AST declaration data). In file-driven mode, `api_hash` remains based on normalized symbol lines.
- AST report schema is versioned and authoritative for semantic model interchange:
  - `schema`: `apisig.ast-report.v1`
  - `format_version`: `1`
  - `semantic_model`: canonical records used for API hashing
  - Reference schema: [docs/ast-report.schema.json](docs/ast-report.schema.json)

Strictness note:

- By default, apisig applies a compatibility adjuster for MSVC-oriented compile flags (for example `/pathmap`) and avoids warning-as-error termination so extraction can proceed on mixed toolchains.
- Use `--strict-tooling` with `--compdb` when you want the raw compilation arguments and warning policy with no compatibility relaxations.
- In strict mode, keep extraction successful by adding explicit strip/suppression controls: `--tooling-strip-arg <prefix>` and `--tooling-suppress <warning>` (both repeatable).
- Use `--tooling-suppress <warning>` (repeatable) to add explicit suppressions. Accepted forms include clang groups like `extern-c-compat`, clang flags like `-Wno-comment`, and MSVC warning codes like `C4100` / `4100`.
- By default, apisig prints `LibTooling mode: strict|compatibility` to stderr for compdb extraction runs. Use `--no-tooling-banner` for machine pipelines that merge stderr into stdout.

Recommendation:

- treat file-driven mode as a CI-safe fallback
- treat LibTooling mode as the authoritative semantic signature mode when available

## Input Format

Symbols file:

- one symbol per line
- blank lines ignored
- C/C++ comments are stripped (`//...` and `/*...*/`)
- C/C++ whitespace is normalized token-wise (formatting-only spacing changes are ignored)
- lines beginning with `#` followed by a space are treated as human comment lines and ignored
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

`compute` returns this result payload:

- `api_hash`
- `rebuild_hash`

Current scaffold uses a stable 64-bit FNV-1a hash for deterministic output.

`--json` affects stdout formatting only.

`snapshot` writes a baseline JSON artifact with `version`, `api_hash`, and `rebuild_hash`.

`--ast-report-out` writes an AST report JSON artifact containing `schema`, `format_version`, `symbols`, `semantic_model`, `declarations`, and `summary`.

When `compare` uses `--baseline-ast-report-json`, comparison is API-only because AST reports do not carry rebuild metadata.

`compare` returns:

- `0`: unchanged
- `10`: API changed
- `11`: rebuild hash changed while API hash is unchanged
- `1`: runtime/config/input error
- `2`: usage error

## Versioning

- Project version source of truth: `project(apisig VERSION ...)` in [CMakeLists.txt](CMakeLists.txt).
- CLI version output: `apisig --version` (also `-v`).
- Recommended release model: Semantic Versioning (`MAJOR.MINOR.PATCH`).
  - `MAJOR`: incompatible CLI or signature behavior changes.
  - `MINOR`: backward-compatible feature additions.
  - `PATCH`: bug fixes and non-breaking internal improvements.

## License

- Repository license text is in [LICENSE](LICENSE).
- If you later switch to an open-source license, update [LICENSE](LICENSE) and this README together in the same commit.

## Distribution And CI

- Source-only distribution is always possible.
- Binary distribution is also possible when license obligations are met.

GitHub Actions usage:

- Building apisig in GitHub Actions is generally allowed.
- Do not redistribute Microsoft toolchain components (Visual Studio/MSVC/DIA files).
- Redistributing your built `apisig` binary is typically fine, but you must comply with third-party license terms for linked dependencies (notably LLVM/Clang notices).
- For public releases, include a license/notice bundle with artifacts.

Note: this is technical guidance, not legal advice.

## Roadmap

- implement Clang LibTooling AST extraction for public C/C++ symbols
- classify changes (compatible, breaking, rebuild-only)
- extend report formats and policy controls for CI consumption

## Dependency Management

Yes, dependencies can be downloaded automatically.

Recommended options:

- self-contained default for baseline CI agents (no clang payload required)
- official LLVM SDK preset for a pinned LibTooling-capable build without vcpkg:
  - use preset `vs2022-x64-llvm-sdk-libtooling`
  - or set `APISIG_DOWNLOAD_LLVM_SDK=ON` directly
- vcpkg toolchain mode when enabling LibTooling (best for Windows CI and Visual Studio):
  - set `VCPKG_ROOT`
  - use preset `vs2022-x64-vcpkg-libtooling`
- CMake `FetchContent` for smaller pure-CMake dependencies

For LibTooling specifically, the practical options are the official prebuilt LLVM SDK, vcpkg, or preinstalled LLVM/Clang packages.
If agents do not have clang payloads, use the self-contained presets and feed symbol lists via `--symbols`.
