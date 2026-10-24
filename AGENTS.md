# Release versioning

- Public release numbering starts at **1.0**. Every subsequent release advances by **0.1**: 1.1, 1.2, ... 1.9, 2.0, 2.1. Use one decimal digit, not 1.10. Check the latest published public version before assigning the next number; the initial transition from date-only versions is 1.0.
- `kReleaseVersion` in `src/port/tools/play.cpp` is the public version used by the game and package documentation. Use it in the GitHub release title.
- Keep date-based GitHub tags, `kBuildName`, `kUpdateBuildName` and manifest `buildLabel` as internal compatibility identifiers. Existing installed updaters require this format. Advance those identifiers when publishing each release; do not replace them with `v1.0` tags without implementing an upgrade migration first.
- Multiplayer `kBuildId` is separate: change it only when game or protocol changes require incompatible clients to be separated.

# Release distribution

- Publish exactly one complete Windows x64 ZIP on the main release page. The user accepted a one-time manual migration instead of retaining legacy client ZIPs/checksums there. Do not republish x86 compatibility downloads or expose updater assets on the main release.
- Version 1.1 is the clean-distribution baseline. Older/date-only and x86 clients require a manual x64 installation. Version 1.0 x64 can reach 1.1 through selective updates, but needs manual installation if that path fails; its old public ZIP fallback is retired.
- Publish the x64 selective-update index, its checksum, content-addressed gzip files, and the client-only full fallback ZIP plus checksum in `updates-<date-tag>`. The fallback must be `packageKind=public`, without original game data. Mark the companion release prerelease and never latest; verify all assets before making the main release latest.
- Keep checksum verification and transactional recovery. Never silently replace an x86 installation with x64. Preserve existing historical companion releases for their remaining references.
- Promote the versioned complete x64 ZIP in the release title/body and README. Keep its generated checksum locally; GitHub provides its asset digest, so it needs no separate main-page checksum download.
