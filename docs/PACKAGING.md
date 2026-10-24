# Windows package tools

Run the PowerShell tools with Python 3.9 or newer available as `python`, or pass
`-Python` with the interpreter path. Relative paths are relative to this source
checkout. Every invocation needs a fresh `-Out` directory. Existing outputs are
refused and never deleted or overwritten, including after a failed validation.

Public release numbering starts at **1.0**, then increases by **0.1** for each
release: 1.1, 1.2, ... 1.9, 2.0. Set `kReleaseVersion` in `play.cpp` and use the
same number in the GitHub release title. Version 1.1 uses internal tag
`v2026.10.24-d`. Keep the date-based GitHub tag, architecture-specific
`kBuildName` labels and `kUpdateBuildName` as internal compatibility identifiers,
advancing them at publication. Existing clients only recognize that date format.
The manifest retains `buildLabel` for their verifier and records the public
number separately as `releaseVersion`. Do not replace an existing release asset
or reuse an internal tag for a new public version.

Public packages contain the port, the bootstrap scene, notices and applicable
redistributable runtimes. They never copy an original installation, settings,
saves, rejoin tickets, texture packs, debug symbols or arbitrary sibling DLLs.

```powershell
./tools/make_dist.ps1 -Architecture x64 -Out build/dist-review-x64
```

Packaging defaults to x64. The default executable is
`build-x64/src/port/Release/hotd2.exe`, or `build/src/port/Release/hotd2.exe`
when explicitly selecting x86 for development. New releases publish x64 only.
To select a completed build explicitly:

```powershell
./tools/make_dist.ps1 -Architecture x64 -Exe build-x64/src/port/Release/play.exe -Out build/dist-review-custom
```

The PE machine and PE32/PE32+ optional header must agree with `-Architecture`.
The executable must contain exactly one current source `kBuildName` string, with
the NS32/NS64 suffix matching the actual PE. `-SourceRoot` can select the source
used for another build. Metadata calls its revision the source *at packaging*,
and records dirty source explicitly; a matching label alone is not proof of a
reproducible build.

Only known app-local NVIDIA, AMD, Intel and DXC DLL names are copied when present
beside the selected executable. Each DLL's PE architecture is checked. Required
license and third-party notice files must be present in the matching
`vendor-licenses` or `dxc-licenses` tree. Both DXC DLLs are required together.
No vendor SDK binary is stored in the source repository by these tools.

Privacy checks cover inputs, staged files and uncompressed ZIP contents. One
byte-exact NVIDIA DLSS 310.9.16.0 redistributable contains an NVIDIA training
checkpoint path. It also contains a coincidental identifier substring inside a
43,692-byte opaque Base64 payload. Its reviewed filename/SHA-256 exception covers
the vendor home path and identifier collisions strictly within Base64-alphabet
runs longer than 1,024 bytes with alphanumeric neighbors. Plaintext words, path
components, short strings, UTF-16 identifiers, credentials and member names remain
checked. The official SDK bytes and valid NVIDIA Authenticode signature were
verified. The package manifest reports both exceptions. Renamed or changed bytes
receive the ordinary strict checks; vendor updates require a new review.

Local complete packages require an explicitly selected installation and its
extraction metadata. They are for local use and include original game data.
The tools do not upload or publish any package.

```powershell
./tools/make_complete_dist.ps1 -Architecture x64 -GameDir R:/game -GameManifest R:/original/game-manifest.json -Out build/dist-local-review
```

`-GameManifest` defaults to `original/game-manifest.json` beside the selected
installation's parent. Every selected input is verified by manifest path,
length and SHA-256, then the staged copy is verified again. Original personal
save blobs and Config.exe are excluded. Input game files are never changed.
Output inside the selected installation is refused. The metadata must be the
complete extraction inventory. A small required common/chapter-one boot-data set
also catches obviously truncated manifests, but does not establish completeness
of an arbitrarily supplied inventory or validate original asset semantics.

Each package contains `BUILD.txt`, `PACKAGE-MANIFEST.json` and `SHA256SUMS.txt`.
The JSON records the actual executable machine, build label, source build ID,
executable SHA-256, packaging revision, optional runtimes, privacy exceptions and
payload hashes. `SHA256SUMS.txt` also hashes the JSON manifest; it excludes itself
to avoid a hash cycle. The adjacent `.zip.sha256` hashes the final archive.

Public packages also produce `hotd2re-win-<arch>.files.json`, its `.sha256`
sidecar, and a `file-assets` directory containing `hotd2re-file-<sha256>.gz`
assets. The index embeds the exact package metadata and describes the compressed
size and SHA-256 of each payload file. Gzip output is deterministic; identical
file contents share one asset. Complete packages do not emit these assets.

The public release has exactly one complete x64 download, named
`HOTD2RE-<version>-Windows-x64.zip`. Keep its generated checksum locally rather
than adding another public download; GitHub reports the uploaded asset's digest.
Do not publish client-only ZIPs or checksum sidecars on the main release.

Upload the x64 index, its sidecar and every unique gzip asset, plus
`hotd2re-win-x64.zip` and its checksum sidecar for full-download fallback, to
the matching `updates-<date-tag>` companion release, for example
`updates-v2026.10.24-d`. Mark this release **prerelease** and **not latest**.
Asset names are flat on GitHub: upload the files, not the `file-assets` directory.
Verify and publish all companion assets before making the
public release latest. Neither packaging tool uploads automatically.

Version 1.1 deliberately requires one manual x64 migration for old date-labeled
or x86 clients. Version 1.0 x64 can reach 1.1 through its selective feed, but its
old public ZIP fallback is no longer available. Explain the manual-install path
in release notes. Preserve previous companion releases. Never supply an x64
package to an x86 updater or republish the same version with different bytes.
The companion fallback is a client-only public package, not the complete ZIP;
the installer rejects original game data in automatic update payloads.

The file updater hashes installed files and stages verified copies of unchanged
ones. It downloads only missing or changed files, verifies the reconstructed
package, then uses the same transactional install and recovery as ZIP updates.
Any unavailable or invalid file index or asset triggers a verified ZIP fallback.
Clients without this updater need one full update to receive it. This is file
replacement, not binary patching: changing a DLL requires downloading that whole
compressed DLL. Staging and rollback still require space for the full program,
and release storage/request counts increase. Saves, settings, original game data
and installed packs remain outside the public payload.

Synthetic tests create public fixture packages only:

```powershell
python -m unittest discover -s tests -p test_package_release.py -v
python -m unittest discover -s tests -p release_privacy_test.py -v
```
