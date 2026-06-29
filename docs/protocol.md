# RuneFetch Protocol

All paths are on SD.

## Directories

```text
/3ds/Rune3DS/runefetch/jobs/
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
```

Required fields:

- `version`
- `url`

Recommended fields:

- `id`
- `title_id`
- `name`
- `size`

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

States:

- `idle`
- `downloading`
- `ready`
- `failed`
- `paused`

## Job Lifecycle

```text
jobs/<name>.job
  -> done/<name>.job
  -> failed/<name>.job
```

The job file itself is moved only after RuneFetch has finished with it.
