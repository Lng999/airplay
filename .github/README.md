# CI (.github)

`workflows/build.yml` defines a single workflow, **build**, that runs on pushes to `main`,
on pull requests, and on manual dispatch. Runs are grouped per ref and older runs are cancelled.

Two jobs:

- **build-uxplay** (`windows-latest`, `shell: msys2 {0}`) — installs the MSYS2 **UCRT64**
  toolchain and GStreamer stack via `msys2/setup-msys2@v2`, configures
  `third_party/UxPlay` with `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DNO_MARCH_NATIVE=ON`,
  builds `build/uxplay.exe` and smoke-checks it with `uxplay.exe -v`.
  A **best-effort** bundling step collects the binary, its `ntldd -R` DLL closure and the
  GStreamer plugins into `dist/` — transitive deps of the *plugin* DLLs are not yet resolved
  (Phase 3 work), so `dist/` is not guaranteed to run on a machine without MSYS2.
- **lint-scripts** (`ubuntu-latest`, only when `scripts/**` exists) — `bash -n` on every
  `scripts/*.sh` and a `[Parser]::ParseFile` syntax check on every `scripts/*.ps1`.

**Artifact:** `dist/` is uploaded as **`uxplay-win64-ucrt64`** (kept 7 days). Download it from the
run's summary page: Actions → the *build* run → *Artifacts*.
