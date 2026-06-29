# Implementation Notes

## Current MVP

RuneFetch is a polling sysmodule. It checks `/3ds/Rune3DS/runefetch/jobs`
once per second, downloads the first `.job` it finds, and moves that job to
`done` or `failed`.

The current MVP intentionally avoids app/sysmodule IPC. File handoff is slower,
but it is easy to inspect on SD and survives crashes.

## Rune3DS Integration Point

For "Download in background", Rune3DS should:

1. Call the existing catalog download-link function.
2. Write a `.job` file with the returned direct CIA URL.
3. Place it in `/3ds/Rune3DS/runefetch/jobs`.
4. Show the user that RuneFetch accepted the job once `status.txt` changes.

Avoid writing the job directly into the jobs directory if possible. Write to a
temporary path first, close it, then rename it to `.job` so RuneFetch never sees
a half-written URL.

## Known Early Limitations

- No SHA-256 verification yet.
- No speed limiter yet.
- No pause/cancel command yet.
- No named service IPC yet.
- No Rune3DS cached-CIA installer yet.
- Direct URL token expiry is handled only by starting quickly and resuming while
  the same URL remains valid.

## Test Strategy

Start with small public CIA/test payload URLs. Verify:

- `.part` is created while downloading.
- `.cia` appears only after the download completes.
- the job moves to `done`.
- FBI can browse to `/3ds/Rune3DS/cache` and install the CIA.
- deleting `/luma/sysmodules/0004013000c0fe02.cxi` cleanly disables RuneFetch.
