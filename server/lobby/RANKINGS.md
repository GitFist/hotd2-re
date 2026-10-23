# Optional shared leaderboards

Shared results are **client-reported and unverified**. Online display names contain 1–12
characters and are not unique accounts. Original local records still use three-character
initials. The server does not verify gameplay, ownership of names,
cheat-free runs or accuracy of client metadata. Do not label these trusted or competitive
anti-cheat rankings. The client never uploads save history automatically; publishing a
qualifying new result is an explicit player action.

The feature is independent of deterministic lockstep and uses a separate client socket.
It shares the existing UDP port with the lobby under the distinct `H2RK` magic. Reading
rankings neither creates a room nor uploads a result. No room/rejoin token, save file,
device identifier, username or source IP is stored in the ranking database.

## Enable locally

```
python lobby_server.py --bind 127.0.0.1 --port 27020 --rankings-db records.sqlite3
```

Without `--rankings-db` or `RANKINGS_DB`, the ranking service reports disabled. Room/relay
behavior is unchanged. A configured database is SQLite with transactional writes, WAL and
FULL synchronous durability. Board storage is bounded to 1000 results per board/build and
32 build values; operators can archive/reset a season by choosing a new database. On startup,
the server transactionally widens an existing three-byte `name` constraint to 1–12 bytes,
preserving the `rankings_v1` table name, run IDs, scores, metadata and timestamps. The table
rename, copy and index restoration commit together; failure leaves the old table intact.
No manual data rewrite is required. The game
partitions results by compatibility build ID. Different scoring/rules builds do not mix.

For a deliberate Docker deployment, the optional overlay adds a persistent named volume:

```
docker compose -f compose.yaml -f compose.rankings.yaml up -d --build
```

The base container remains read-only; only `/rankings` is writable in the overlay. The
Dockerfile creates that directory for the existing non-root uid/gid 65534. Back up the
SQLite database with SQLite's online backup API, or stop the service and retain the database
and any WAL files together. The overlay is configuration only; adding it does not deploy a
remote service. The standard room-list health check remains valid.

## Board and metadata semantics

| ID | Board | Order | Metadata byte |
|---|---|---|---|
| 0 | Arcade | Score descending | Stage reached, native `b5` (0..6) |
| 1 | Original | Score descending | Stage reached, native `b5` (0..6) |
| 2 | Horde | Completed rounds descending, then score descending | Difficulty (0..4) |
| 3..12 | Judgment, Hierophant A/B, Tower A/B, Strength A/B, Magician, Emperor, Boss Rush | Time ascending, in fixed 60-Hz frames | Difficulty (0..4) |

Ties use submission time then run ID for a stable order. Arcade/Horde support one or two
players as metadata; original boss records are one-player only. These are mixed metadata
tables, following the original presentation; a later explicit design can add separate
difficulty/player filters. Score must be positive and fit within 2,000,000,000. Boss times
are bounded to six hours; Horde completed rounds are 1..65535. Other boards use zero rounds.

## UDP protocol, versions 1 and 2

All packets begin with `H2RK`, version byte 1 or 2, type byte, request ID u32 little-endian.
Exact payload lengths are required. Unknown/oversized packets are ignored; the maximum
ranking request is 128 bytes. Maximum responses are 127 bytes for v1 and 217 for v2.
Replies use the request's version. Both versions share the same board/build records.

| Type | Direction | Payload after request ID |
|---|---|---|
| 1 | Client hello | Empty |
| 101 | Cookie | 16 bytes |
| 2 | Query | build u32, board u8, cookie 16 bytes |
| 102 | Results | build u32, board u8, status u8, count u8, count × row (11 bytes v1 / 20 bytes v2) |
| 3 | Submit | build u32, board u8, name (3 bytes v1 / 12 bytes v2), score u32, rounds u16, metadata u8, players u8, run ID 16 bytes, cookie 16 bytes |
| 103 | Acknowledgement | status u8 |

A row is a name field, score/time u32, rounds u16, metadata u8 and players u8. V1 retains
its fixed three-character initials. V2 uses 12 bytes: shorter names have trailing NUL
padding, while a 12-character name has no terminator. Embedded NUL or nonzero data after
padding is invalid. The submit packet is 58 bytes in v1 and 67 bytes in v2; queries remain
31 bytes. A v1 query abbreviates longer stored names to their first three characters and
space-pads shorter names. If the first three characters are all spaces, it skips leading
spaces so old clients still receive a nonblank name. Stored names are never shortened.

At most ten rows are returned. Result status is 0 enabled, 3 disabled or 5 busy. Submit status is 0 stored,
1 exact duplicate, 2 invalid/conflicting run ID, 3 disabled, 4 outside retained table, or
5 busy/rate limited. Names allow uppercase ASCII letters, digits, space, period,
underscore and hyphen; empty, all-space, non-ASCII and overlength names are rejected.
Existing three-character names remain valid in both versions.

The HMAC cookie binds the source address and UDP port to a five-minute bucket, accepting
the current/previous bucket. It proves receipt at that endpoint, reducing spoofed requests
and amplification. It does not authenticate a player. A new hello precedes every operation.
Per-IP bounded buckets allow four read operations/second (burst twenty) and one new write
per ten seconds (burst two), with at most 2048 tracked addresses. Duplicate retries consume
read tokens but not another write token. The random run ID deduplicates a submission and
cannot be changed to overwrite it with different values. Repeating an identical candidate
after a lost acknowledgement reuses the client run ID. It is not a stable player ID.

The client receives a positive monotonic `completionToken` for each new live run. This
token is local bookkeeping and is never transmitted. Equal-valued independent runs get
distinct run IDs; retrying the same completion, including after acceptance, keeps its
original ID. An older completion cannot be republished. Once a submission payload has
actually been sent, changing its name is rejected with the original name shown
so an uncertain acknowledgement cannot create a duplicate. DNS or hello-only failures
leave the unsent candidate editable.

## Client and verification

`src/port/net/leaderboard.h` exposes `LeaderboardClient`. Configure once with the selected
lobby hostname, port and build. DNS runs on a worker; requests, retries and receive polling
are nonblocking. Each operation has a six-second deadline. `Query` replaces an old query,
but is ignored during an explicit submission so its acknowledgement is preserved. On a
finished submission, the UI can report the result and refresh the currently selected board.
Only packets from the connected UDP endpoint with matching request/build/board and valid
fixed rows are accepted. Malformed responses do not become displayed records.

Run these local-only checks from the repository root:

```
python tests/leaderboard_service_test.py
python tests/lobby_service_test.py
python tests/leaderboard_client_integration.py build/Release/leaderboard_client_test.exe
```

The C++ integration runner creates an isolated `127.0.0.1` service and temporary database.
It does not contact the public server, publish a real score, or read personal save files.
