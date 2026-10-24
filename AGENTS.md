# Release versioning

- Public release numbering starts at **1.0**. Every subsequent release advances by **0.1**: 1.1, 1.2, ... 1.9, 2.0, 2.1. Use one decimal digit, not 1.10. Check the latest published public version before assigning the next number; the initial transition from date-only versions is 1.0.
- `kReleaseVersion` in `src/port/tools/play.cpp` is the public version used by the game and package documentation. Use it in the GitHub release title.
- Keep date-based GitHub tags, `kBuildName`, `kUpdateBuildName` and manifest `buildLabel` as internal compatibility identifiers. Existing installed updaters require this format. Advance those identifiers when publishing each release; do not replace them with `v1.0` tags without implementing an upgrade migration first.
- Multiplayer `kBuildId` is separate: change it only when game or protocol changes require incompatible clients to be separated.
