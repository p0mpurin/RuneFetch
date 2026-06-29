#include <3ds.h>
#include <cstdio>
#include <cstring>

namespace {
alignas(8) u8 g_static_heap[0x10000];
alignas(0x1000) u8 g_http_buffer[0x4000];

void write_file(const char *path, const char *data, u32 size)
{
	FS_Archive archive;
	if(R_SUCCEEDED(FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, nullptr))))
	{
		FSUSER_DeleteFile(archive, fsMakePath(PATH_ASCII, path));
		FSUSER_CloseArchive(archive);
	}

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

void write_status(const char *state, const char *job_name, const char *title_id,
	u64 done, u64 total, Result res, const char *message)
{
	char status[512];
	int len = snprintf(status, sizeof(status),
		"state=%s\n"
		"job=%s\n"
		"title_id=%s\n"
		"done=%llu\n"
		"total=%llu\n"
		"result=%08lX\n"
		"message=%s\n",
		state,
		job_name ? job_name : "",
		title_id ? title_id : "",
		static_cast<unsigned long long>(done),
		static_cast<unsigned long long>(total),
		res,
		message);
	if(len > 0)
		write_file("/3ds/Rune3DS/runefetch/state/status.txt",
			status, static_cast<u32>(len));
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

bool delete_job(const char *job_name)
{
	char path[256];
	snprintf(path, sizeof(path), "/3ds/Rune3DS/runefetch/jobs/%s", job_name);

	FS_Archive archive;
	Result res = FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, nullptr));
	if(R_FAILED(res))
		return false;

	res = FSUSER_DeleteFile(archive, fsMakePath(PATH_ASCII, path));
	FSUSER_CloseArchive(archive);
	return R_SUCCEEDED(res);
}

Result delete_path(const char *path)
{
	FS_Archive archive;
	Result res = FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, nullptr));
	if(R_FAILED(res))
		return res;

	res = FSUSER_DeleteFile(archive, fsMakePath(PATH_ASCII, path));
	FSUSER_CloseArchive(archive);
	return res;
}

Result rename_path(const char *from, const char *to)
{
	FS_Archive archive;
	Result res = FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, nullptr));
	if(R_FAILED(res))
		return res;

	FSUSER_DeleteFile(archive, fsMakePath(PATH_ASCII, to));
	res = FSUSER_RenameFile(archive, fsMakePath(PATH_ASCII, from),
		archive, fsMakePath(PATH_ASCII, to));
	FSUSER_CloseArchive(archive);
	return res;
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

u64 parse_u64(const char *text)
{
	u64 value = 0;
	while(*text >= '0' && *text <= '9')
	{
		value = value * 10 + static_cast<u64>(*text - '0');
		++text;
	}
	return value;
}

void safe_basename(const char *primary, const char *fallback, char *out, u32 out_size)
{
	if(out_size == 0)
		return;

	const char *src = primary && primary[0] ? primary : fallback;
	u32 pos = 0;
	for(u32 i = 0; src && src[i] && pos + 1 < out_size; ++i)
	{
		char ch = src[i];
		bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
			(ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
		if(ok)
			out[pos++] = ch;
	}
	if(pos == 0 && out_size > 1)
		out[pos++] = 'j';
	out[pos] = '\0';
}

Result open_http_context(httpcContext *ctx, const char *url, u32 *status)
{
	Result res = httpcOpenContext(ctx, HTTPC_METHOD_GET, url, 0);
	if(R_FAILED(res))
		return res;

	httpcSetSSLOpt(ctx, SSLCOPT_DisableVerify);
	httpcSetKeepAlive(ctx, HTTPC_KEEPALIVE_ENABLED);
	httpcAddRequestHeaderField(ctx, "User-Agent", "RuneFetch/0.1");
	httpcAddRequestHeaderField(ctx, "Connection", "Keep-Alive");

	res = httpcBeginRequest(ctx);
	if(R_FAILED(res))
	{
		httpcCancelConnection(ctx);
		httpcCloseContext(ctx);
		return res;
	}

	res = httpcGetResponseStatusCodeTimeout(ctx, status,
		10ULL * 1000ULL * 1000ULL * 1000ULL);
	if(R_FAILED(res))
	{
		httpcCancelConnection(ctx);
		httpcCloseContext(ctx);
	}
	return res;
}

Result download_job(const char *job_name, const char *url, const char *title_id,
	const char *id, u64 expected_size)
{
	Result res = httpcInit(0);
	if(R_FAILED(res))
		return res;

	char base[128];
	char part_path[256];
	char final_path[256];
	safe_basename(title_id, id[0] ? id : job_name, base, sizeof(base));
	snprintf(part_path, sizeof(part_path), "/3ds/Rune3DS/cache/%s.cia.part", base);
	snprintf(final_path, sizeof(final_path), "/3ds/Rune3DS/cache/%s.cia", base);

	delete_path(part_path);
	delete_path(final_path);

	Handle out = 0;
	res = FSUSER_OpenFileDirectly(&out, ARCHIVE_SDMC,
		fsMakePath(PATH_EMPTY, nullptr),
		fsMakePath(PATH_ASCII, part_path),
		FS_OPEN_CREATE | FS_OPEN_WRITE, 0);
	if(R_FAILED(res))
	{
		httpcExit();
		return res;
	}

	char current_url[1200];
	char redirect_url[1200];
	snprintf(current_url, sizeof(current_url), "%s", url);

	httpcContext ctx {};
	u32 status = 0;
	u32 total32 = 0;
	u64 total = 0;
	u64 done = 0;
	bool context_open = false;
	for(u32 redirects = 0; redirects < 4; ++redirects)
	{
		res = open_http_context(&ctx, current_url, &status);
		context_open = R_SUCCEEDED(res);
		if(R_FAILED(res))
			goto out;

		bool redirect = (status >= 301 && status <= 303) || (status >= 307 && status <= 308);
		if(!redirect)
			break;

		res = httpcGetResponseHeader(&ctx, "Location", redirect_url, sizeof(redirect_url));
		httpcCloseContext(&ctx);
		context_open = false;
		if(R_FAILED(res))
			goto out;
		snprintf(current_url, sizeof(current_url), "%s", redirect_url);
	}

	if(status != 200)
	{
		res = MAKERESULT(RL_PERMANENT, RS_INVALIDSTATE, RM_APPLICATION, status & 0x3FF);
		goto out;
	}

	httpcGetDownloadSizeState(&ctx, nullptr, &total32);
	total = expected_size ? expected_size : total32;
	write_status("downloading", job_name, title_id, done, total, 0, "Downloading");

	for(;;)
	{
		u32 before = 0;
		u32 after = 0;
		httpcGetDownloadSizeState(&ctx, &before, nullptr);
		res = httpcReceiveDataTimeout(&ctx, g_http_buffer, sizeof(g_http_buffer),
			10ULL * 1000ULL * 1000ULL * 1000ULL);
		httpcGetDownloadSizeState(&ctx, &after, nullptr);

		u32 chunk = after >= before ? after - before : 0;
		if(chunk > sizeof(g_http_buffer))
			chunk = sizeof(g_http_buffer);
		if(chunk)
		{
			u32 written = 0;
			Result write_res = FSFILE_Write(out, &written, done, g_http_buffer,
				chunk, FS_WRITE_FLUSH);
			if(R_FAILED(write_res) || written != chunk)
			{
				res = R_FAILED(write_res) ? write_res :
					MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_APPLICATION, 4);
				goto out;
			}
			done += chunk;
			write_status("downloading", job_name, title_id, done, total, 0, "Downloading");
		}

		if(res == static_cast<Result>(HTTPC_RESULTCODE_DOWNLOADPENDING))
			continue;
		if(R_FAILED(res))
			goto out;
		break;
	}

	if(total && done < total)
	{
		res = MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_APPLICATION, 5);
		goto out;
	}

	res = 0;

out:
	if(context_open)
	{
		if(R_FAILED(res))
			httpcCancelConnection(&ctx);
		httpcCloseContext(&ctx);
	}
	FSFILE_Close(out);

	if(R_SUCCEEDED(res))
		res = rename_path(part_path, final_path);
	if(R_FAILED(res))
		delete_path(part_path);
	httpcExit();
	return res;
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
	char size_text[32];
	char url[1200];
	bool loaded = read_job(job_name, job_text, sizeof(job_text));
	bool deleted = false;
	if(loaded)
	{
		copy_field(job_text, "id", id, sizeof(id));
		copy_field(job_text, "title_id", title_id, sizeof(title_id));
		copy_field(job_text, "url", url, sizeof(url));
		copy_field(job_text, "size", size_text, sizeof(size_text));
		if(url[0])
		{
			Result download_res = download_job(job_name, url, title_id, id, parse_u64(size_text));
			if(R_SUCCEEDED(download_res))
				deleted = delete_job(job_name);
			else
			{
				write_status("download_failed", job_name, title_id, 0,
					parse_u64(size_text), download_res, "Download failed");
				write_boot_marker("download_failed");
				return;
			}
		}
	}
	else
	{
		id[0] = '\0';
		title_id[0] = '\0';
		url[0] = '\0';
		size_text[0] = '\0';
	}

	char status[512];
	int len = snprintf(status, sizeof(status),
		"state=%s\n"
		"job=%s\n"
		"id=%s\n"
		"title_id=%s\n"
		"url=%s\n"
		"deleted=%s\n"
		"result=00000000\n"
		"message=RuneFetch downloaded queued job\n",
		loaded ? (deleted ? "download_ready" : "job_delete_failed") : "job_read_failed",
		job_name,
		id,
		title_id,
		url[0] ? "yes" : "no",
		deleted ? "yes" : "no");
	if(len > 0)
		write_file("/3ds/Rune3DS/runefetch/state/status.txt",
			status, static_cast<u32>(len));
	write_boot_marker(loaded ? (deleted ? "download_ready" : "job_delete_failed") : "job_read_failed");
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
