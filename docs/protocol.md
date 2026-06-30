# RuneFetch Protocol

All paths are on SD.

## Directories

```text
/3ds/Rune3DS/runefetch/jobs/
/3ds/Rune3DS/runefetch/cancel/
/3ds/Rune3DS/runefetch/done/
/3ds/Rune3DS/runefetch/failed/
/3ds/Rune3DS/runefetch/state/
/3ds/Rune3DS/cache/
```

## Job Format

Jobs are plain UTF-8 key/value files with extension `.job`.

```text
version=1
id=12345
title_id=0004000000123400
name=Example Title
url=https://example.invalid/content.cia?token=...
size=123456789
mode=stream_install
```

Required fields:

- `version`
- `url`

Recommended fields:

- `id`
- `title_id`
- `name`
- `size`
- `mode`

Modes:

- `stream_install`: download and write directly to AM. This is the fast path.
- `cache`: download the CIA to `/3ds/Rune3DS/cache/`.
- `download_only`: legacy alias for cache mode.
- `stream_install_unsafe`: legacy alias for stream install.

Stream install mode cannot be safely canceled after AM starts. Reboot the
console if a stream job must be stopped.

`url` should be a direct CIA URL. For Rune/hShop jobs, Rune3DS should request
the tokenized CDN URL and write it into the job immediately before asking the
user to leave the app. RuneFetch starts the download as soon as it sees the job.

## Output Naming

RuneFetch chooses a basename in this order:

1. `title_id`
2. `id`
3. source job filename

Files are written as:

```text
/3ds/Rune3DS/cache/<basename>.cia.part
/3ds/Rune3DS/cache/<basename>.cia
```

## Status Format

RuneFetch writes one status file:

```text
/3ds/Rune3DS/runefetch/state/status.txt
```

Example:

```text
state=downloading
job=0004000000123400.job
name=Example Title
done=1048576
total=123456789
result=00000000
message=Downloading
```

Common states:

- `idle`
- `downloading`
- `resuming`
- `download_ready`
- `download_failed`
- `install_ready`
- `install_failed`
- `canceled`

Rune3DS can request cancellation by writing:

```text
/3ds/Rune3DS/runefetch/cancel/<basename>.cancel
```

RuneFetch checks for that marker between download/write chunks, deletes the job
and marker, then writes `state=canceled`.

## Job Lifecycle

```text
jobs/<name>.job
  -> deleted after success or cancel
```

Partial downloads remain as `.cia.part` so a later job can resume.
