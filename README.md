# RuneFetch

Experimental Luma3DS external sysmodule for Rune3DS background stream installs
and CIA caching.

RuneFetch can either stream a CIA directly into AM for the fastest install path
or cache a raw CIA to the SD card for later FBI install. Stream mode can block
Rune3DS while AM is busy and cannot be safely canceled after install starts.
Reboot the console if you need to stop a stream job.

## Target Flow

1. Rune3DS writes a job into `/3ds/Rune3DS/runefetch/jobs/`.
2. Rune3DS launches the RuneFetch sysmodule through PM.
3. In stream mode, RuneFetch downloads and writes directly to AM.
4. In cache mode, RuneFetch downloads the URL to:
   `/3ds/Rune3DS/cache/<job-id>.cia.part`
5. On cache success, RuneFetch renames it to:
   `/3ds/Rune3DS/cache/<job-id>.cia`
6. The notification LED blinks green.
7. The user returns to Rune3DS or FBI depending on the selected mode.

## Install

Build output should be copied to:

```text
/luma/sysmodules/0004013000c0fe02.cxi
```

Luma3DS must have external FIRMs/modules enabled.

If the sysmodule causes boot trouble, remove or rename that CXI from the SD
card, or disable external modules in the Luma config menu.

## Status

RuneFetch is launched on demand by Rune3DS after a job is queued. Rune3DS can
choose stream install for speed or cache mode for safer browsing while the job
runs.

See [docs/protocol.md](docs/protocol.md) for the job and status file format.
