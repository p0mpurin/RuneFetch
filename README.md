# RuneFetch

Experimental Luma3DS external sysmodule for Rune3DS background CIA caching.

RuneFetch does not install titles. It downloads a raw CIA to the SD card while
the user leaves Rune3DS and plays another title. The completed CIA can then be
installed from FBI, and later from Rune3DS once app-side cached install support
is added.

## Target Flow

1. Rune3DS writes a job into `/3ds/Rune3DS/runefetch/jobs/`.
2. RuneFetch notices the job and downloads the URL to:
   `/3ds/Rune3DS/cache/<job-id>.cia.part`
3. On success, RuneFetch renames it to:
   `/3ds/Rune3DS/cache/<job-id>.cia`
4. The notification LED blinks green.
5. The user returns to Rune3DS or FBI to install the cached CIA.

## Install

Build output should be copied to:

```text
/luma/sysmodules/0004013000c0fe02.cxi
```

Luma3DS must have external FIRMs/modules enabled.

If the sysmodule causes boot trouble, remove or rename that CXI from the SD
card, or disable external modules in the Luma config menu.

## Status

This repository is at the architecture/MVP scaffold stage. The first target is
small CIA downloads from Rune3DS-authored direct URL jobs, FBI install first.

See [docs/protocol.md](docs/protocol.md) for the job and status file format.
