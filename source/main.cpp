#include <3ds.h>
#include <cstdio>
#include <cstring>

namespace {
alignas(8) u8 g_static_heap[0x10000];

void write_file(const char *path, const char *data, u32 size)
{
	Handle file = 0;
	Result open_res = FSUSER_OpenFileDirectly(&file, ARCHIVE_SDMC,
		fsMakePath(PATH_EMPTY, nullptr),
		fsMakePath(PATH_ASCII, path),
		FS_OPEN_CREATE | FS_OPEN_WRITE, 0);
	if(R_FAILED(open_res))
		return;

	u32 written = 0;
	FSFILE_Write(file, &written, 0, data, size, FS_WRITE_FLUSH);
	FSFILE_Close(file);
}

void write_boot_marker(const char *stage)
{
	char data[96];
	int len = snprintf(data, sizeof(data), "stage=%s\nresult=00000000\n", stage);
	if(len > 0)
		write_file("/runefetch_boot.txt", data, static_cast<u32>(len));
}

Result create_dir(FS_Archive archive, const char *path)
{
	return FSUSER_CreateDirectory(archive, fsMakePath(PATH_ASCII, path), 0);
}

Result ensure_dirs()
{
	FS_Archive archive;
	Result res = FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, nullptr));
	if(R_FAILED(res))
		return res;

	create_dir(archive, "/3ds");
	create_dir(archive, "/3ds/Rune3DS");
	create_dir(archive, "/3ds/Rune3DS/runefetch");
	create_dir(archive, "/3ds/Rune3DS/runefetch/jobs");
	create_dir(archive, "/3ds/Rune3DS/runefetch/done");
	create_dir(archive, "/3ds/Rune3DS/runefetch/failed");
	create_dir(archive, "/3ds/Rune3DS/runefetch/state");
	create_dir(archive, "/3ds/Rune3DS/cache");

	FSUSER_CloseArchive(archive);
	return 0;
}

void utf16_to_ascii(const u16 *in, char *out, u32 out_size)
{
	if(out_size == 0)
		return;

	u32 i = 0;
	for(; i + 1 < out_size && in[i] != 0; ++i)
	{
		u16 ch = in[i];
		out[i] = ch >= 0x20 && ch < 0x7F ? static_cast<char>(ch) : '_';
	}
	out[i] = '\0';
}

bool has_suffix(const char *value, const char *suffix)
{
	size_t value_len = strlen(value);
	size_t suffix_len = strlen(suffix);
	return value_len >= suffix_len &&
		strcmp(value + value_len - suffix_len, suffix) == 0;
}

bool find_next_job(char *out_name, u32 out_size)
{
	FS_Archive archive;
	Result res = FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, nullptr));
	if(R_FAILED(res))
		return false;

	Handle dir = 0;
	res = FSUSER_OpenDirectory(&dir, archive,
		fsMakePath(PATH_ASCII, "/3ds/Rune3DS/runefetch/jobs"));
	if(R_FAILED(res))
	{
		FSUSER_CloseArchive(archive);
		return false;
	}

	bool found = false;
	for(;;)
	{
		FS_DirectoryEntry entry {};
		u32 entries_read = 0;
		res = FSDIR_Read(dir, &entries_read, 1, &entry);
		if(R_FAILED(res) || entries_read == 0)
			break;

		char name[128];
		utf16_to_ascii(entry.name, name, sizeof(name));
		if(has_suffix(name, ".job"))
		{
			snprintf(out_name, out_size, "%s", name);
			found = true;
			break;
		}
	}

	FSDIR_Close(dir);
	FSUSER_CloseArchive(archive);
	return found;
}

bool read_job(const char *job_name, char *out, u32 out_size)
{
	char path[256];
	snprintf(path, sizeof(path), "/3ds/Rune3DS/runefetch/jobs/%s", job_name);

	Handle file = 0;
	Result res = FSUSER_OpenFileDirectly(&file, ARCHIVE_SDMC,
		fsMakePath(PATH_EMPTY, nullptr),
		fsMakePath(PATH_ASCII, path),
		FS_OPEN_READ, 0);
	if(R_FAILED(res))
		return false;

	u32 bytes_read = 0;
	res = FSFILE_Read(file, &bytes_read, 0, out, out_size - 1);
	FSFILE_Close(file);
	if(R_FAILED(res))
		return false;

	out[bytes_read < out_size ? bytes_read : out_size - 1] = '\0';
	return true;
}

void copy_field(const char *text, const char *key, char *out, u32 out_size)
{
	if(out_size == 0)
		return;
	out[0] = '\0';

	size_t key_len = strlen(key);
	const char *line = text;
	while(*line)
	{
		const char *next = strchr(line, '\n');
		size_t line_len = next ? static_cast<size_t>(next - line) : strlen(line);
		if(line_len > key_len && strncmp(line, key, key_len) == 0 && line[key_len] == '=')
		{
			size_t value_len = line_len - key_len - 1;
			if(value_len >= out_size)
				value_len = out_size - 1;
			memcpy(out, line + key_len + 1, value_len);
			out[value_len] = '\0';
			return;
		}
		line = next ? next + 1 : line + line_len;
	}
}

void write_job_status()
{
	char job_name[128];
	if(!find_next_job(job_name, sizeof(job_name)))
	{
		static const char status[] =
			"state=idle\n"
			"result=00000000\n"
			"message=No jobs found\n";
		write_file("/3ds/Rune3DS/runefetch/state/status.txt",
			status, sizeof(status) - 1);
		write_boot_marker("idle");
		return;
	}

	char job_text[1600];
	char id[64];
	char title_id[32];
	char url[8];
	bool loaded = read_job(job_name, job_text, sizeof(job_text));
	if(loaded)
	{
		copy_field(job_text, "id", id, sizeof(id));
		copy_field(job_text, "title_id", title_id, sizeof(title_id));
		copy_field(job_text, "url", url, sizeof(url));
	}
	else
	{
		id[0] = '\0';
		title_id[0] = '\0';
		url[0] = '\0';
	}

	char status[512];
	int len = snprintf(status, sizeof(status),
		"state=%s\n"
		"job=%s\n"
		"id=%s\n"
		"title_id=%s\n"
		"url=%s\n"
		"result=00000000\n"
		"message=RuneFetch saw queued job\n",
		loaded ? "job_found" : "job_read_failed",
		job_name,
		id,
		title_id,
		url[0] ? "yes" : "no");
	if(len > 0)
		write_file("/3ds/Rune3DS/runefetch/state/status.txt",
			status, static_cast<u32>(len));
	write_boot_marker(loaded ? "job_found" : "job_read_failed");
}
}

extern "C" {
u32 __ctru_heap_size = sizeof(g_static_heap);
u32 __ctru_linear_heap_size = 0;

void __system_allocateHeaps(void)
{
	extern char *fake_heap_start;
	extern char *fake_heap_end;
	extern u32 __ctru_heap;
	extern u32 __ctru_linear_heap;

	__ctru_heap = reinterpret_cast<u32>(g_static_heap);
	__ctru_linear_heap = 0;
	fake_heap_start = reinterpret_cast<char *>(g_static_heap);
	fake_heap_end = reinterpret_cast<char *>(g_static_heap) + sizeof(g_static_heap);
}

void __appInit(void)
{
	srvInit();
}

void __appExit(void)
{
	srvExit();
}
}

int main()
{
	Result res = fsInit();
	if(R_SUCCEEDED(res))
	{
		write_boot_marker("fsInit");

		res = ensure_dirs();
		if(R_SUCCEEDED(res))
		{
			write_boot_marker("dirs");
			static const char status[] =
				"state=probe\n"
				"result=00000000\n"
				"message=RuneFetch folders ready\n";
			write_file("/3ds/Rune3DS/runefetch/state/status.txt",
				status, sizeof(status) - 1);
			write_job_status();
		}
	}

	for(;;)
	{
		svcSleepThread(1000LL * 1000LL * 1000LL);
	}
}
