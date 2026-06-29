#include <3ds.h>
#include <cstdio>
#include <cstring>

namespace {
alignas(8) u8 g_static_heap[0x20000];
alignas(0x1000) u8 g_http_buffer[0x10000];

constexpr const char *RUNE3DS_USER_AGENT = "hShop (3DS/CTR/KTR; ARMv6) 3hs/1.5.42";
constexpr u64 STATUS_UPDATE_STEP = 0x40000;
constexpr u64 FILE_FLUSH_STEP = 0x100000;
constexpr u64 STREAM_INSTALL_STATUS_STEP = 0x80000;
constexpr u32 HTTP_SHARED_MEMORY_SIZE = 1024 * 1024;

bool g_led_ready = false;
u32 g_last_led_bucket = 0xFFFFFFFF;
u32 g_last_http_status = 0;
u64 g_last_done = 0;
u64 g_last_total = 0;
const char *g_last_stage = "";

Result init_http()
{
	Result res = httpcInit(HTTP_SHARED_MEMORY_SIZE);
	if(R_FAILED(res))
		res = httpcInit(0);
	return res;
}

void set_info_led(u8 red, u8 green, u8 blue, bool blink)
{
	if(!g_led_ready)
		return;

	InfoLedPattern pattern {};
	pattern.delay = blink ? 0x04 : 0x10;
	pattern.smoothing = blink ? 0x00 : 0x02;
	pattern.loopDelay = 0x00;
	pattern.blinkSpeed = 0x00;

	for(u32 i = 0; i < 32; ++i)
	{
		bool on = !blink || i < 16;
		pattern.redPattern[i] = on ? red : 0;
		pattern.greenPattern[i] = on ? green : 0;
		pattern.bluePattern[i] = on ? blue : 0;
	}

	MCUHWC_SetInfoLedPattern(&pattern);
}

void init_led()
{
	if(R_SUCCEEDED(mcuHwcInit()))
	{
		g_led_ready = true;
		set_info_led(0, 0, 0, false);
	}
}

void led_idle()
{
	set_info_led(0, 0, 0, false);
}

void led_downloading()
{
	set_info_led(0, 0, 0xFF, false);
	g_last_led_bucket = 0;
}

void led_download_progress(u64 done, u64 total)
{
	if(!g_led_ready)
		return;

	u32 bucket = total ? static_cast<u32>((done * 20) / total) : 0;
	if(bucket > 20)
		bucket = 20;
	if(bucket == g_last_led_bucket)
		return;

	g_last_led_bucket = bucket;
	u8 green = static_cast<u8>((bucket * 255) / 20);
	u8 blue = static_cast<u8>(255 - green);
	set_info_led(0, green, blue, false);
}

void led_ready()
{
	set_info_led(0, 0xFF, 0, true);
	svcSleepThread(5LL * 1000LL * 1000LL * 1000LL);
	set_info_led(0, 0, 0, false);
}

void led_error()
{
	set_info_led(0xFF, 0, 0, false);
}

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
		"led=%s\n"
		"message=%s\n",
		state,
		job_name ? job_name : "",
		title_id ? title_id : "",
		static_cast<unsigned long long>(done),
		static_cast<unsigned long long>(total),
		res,
		g_led_ready ? "ready" : "unavailable",
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

bool file_size(const char *path, u64 *size)
{
	Handle file = 0;
	Result res = FSUSER_OpenFileDirectly(&file, ARCHIVE_SDMC,
		fsMakePath(PATH_EMPTY, nullptr),
		fsMakePath(PATH_ASCII, path),
		FS_OPEN_READ, 0);
	if(R_FAILED(res))
		return false;

	res = FSFILE_GetSize(file, size);
	FSFILE_Close(file);
	return R_SUCCEEDED(res);
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

Result open_http_context(httpcContext *ctx, const char *url, u64 resume_offset, u32 *status)
{
	Result res = httpcOpenContext(ctx, HTTPC_METHOD_GET, url, 0);
	if(R_FAILED(res))
		return res;

	httpcSetSSLOpt(ctx, SSLCOPT_DisableVerify);
	httpcSetKeepAlive(ctx, HTTPC_KEEPALIVE_ENABLED);
	httpcAddRequestHeaderField(ctx, "User-Agent", RUNE3DS_USER_AGENT);
	httpcAddRequestHeaderField(ctx, "Accept-Encoding", "identity");
	httpcAddRequestHeaderField(ctx, "Cache-Control", "no-cache");
	httpcAddRequestHeaderField(ctx, "Connection", "Keep-Alive");
	if(resume_offset)
	{
		char range[48];
		snprintf(range, sizeof(range), "bytes=%llu-",
			static_cast<unsigned long long>(resume_offset));
		httpcAddRequestHeaderField(ctx, "Range", range);
	}

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

Result open_download_context(httpcContext *ctx, const char *url, u64 resume_offset, u32 *status)
{
	char current_url[1200];
	char redirect_url[1200];
	snprintf(current_url, sizeof(current_url), "%s", url);

	Result res = 0;
	for(u32 redirects = 0; redirects < 4; ++redirects)
	{
		res = open_http_context(ctx, current_url, resume_offset, status);
		if(R_FAILED(res))
			return res;

		bool redirect = (*status >= 301 && *status <= 303) || (*status >= 307 && *status <= 308);
		if(!redirect)
			return 0;

		res = httpcGetResponseHeader(ctx, "Location", redirect_url, sizeof(redirect_url));
		httpcCloseContext(ctx);
		if(R_FAILED(res))
			return res;
		snprintf(current_url, sizeof(current_url), "%s", redirect_url);
	}

	return MAKERESULT(RL_PERMANENT, RS_OUTOFRESOURCE, RM_APPLICATION, 15);
}

Result download_job(const char *job_name, const char *url, const char *title_id,
	const char *id, u64 expected_size)
{
	Result res = init_http();
	if(R_FAILED(res))
		return res;
	g_last_http_status = 0;
	g_last_done = 0;
	g_last_total = expected_size;

	char base[128];
	char part_path[256];
	char final_path[256];
	safe_basename(title_id, id[0] ? id : job_name, base, sizeof(base));
	snprintf(part_path, sizeof(part_path), "/3ds/Rune3DS/cache/%s.cia.part", base);
	snprintf(final_path, sizeof(final_path), "/3ds/Rune3DS/cache/%s.cia", base);

	u64 final_size = 0;
	if(file_size(final_path, &final_size) && (!expected_size || final_size == expected_size))
	{
		httpcExit();
		return 0;
	}

	u64 existing_size = 0;
	if(file_size(part_path, &existing_size))
	{
		if(expected_size && existing_size > expected_size)
		{
			delete_path(part_path);
			existing_size = 0;
		}
		else if(expected_size && existing_size == expected_size)
		{
			res = rename_path(part_path, final_path);
			httpcExit();
			return res;
		}
	}

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
	u64 total = expected_size;
	u64 done = existing_size;
	u64 last_status_done = done;
	u64 last_flush_done = done;
	bool context_open = false;
	u32 expected_status = existing_size ? 206 : 200;
	g_last_done = done;
	g_last_total = total;
	led_downloading();
	led_download_progress(done, total);
	write_status(existing_size ? "resuming" : "downloading",
		job_name, title_id, done, total, 0,
		existing_size ? "Resuming download" : "Downloading");
	for(u32 redirects = 0; redirects < 4; ++redirects)
	{
		res = open_http_context(&ctx, current_url, existing_size, &status);
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

	if(existing_size && status == 200)
	{
		httpcCloseContext(&ctx);
		context_open = false;
		FSFILE_Close(out);
		delete_path(part_path);
		httpcExit();
		return download_job(job_name, url, title_id, id, expected_size);
	}

	if(status != expected_status)
	{
		g_last_http_status = status;
		res = MAKERESULT(RL_PERMANENT, RS_INVALIDSTATE, RM_APPLICATION, status & 0x3FF);
		goto out;
	}

	httpcGetDownloadSizeState(&ctx, nullptr, &total32);
	if(!total)
		total = existing_size + total32;
	g_last_done = done;
	g_last_total = total;
	led_download_progress(done, total);
	write_status(existing_size ? "resuming" : "downloading",
		job_name, title_id, done, total, 0,
		existing_size ? "Resuming download" : "Downloading");

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
				chunk, 0);
			if(R_FAILED(write_res) || written != chunk)
			{
				res = R_FAILED(write_res) ? write_res :
					MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_APPLICATION, 4);
				goto out;
			}
			done += chunk;
			g_last_done = done;
			g_last_total = total;
			led_download_progress(done, total);
			if(done - last_flush_done >= FILE_FLUSH_STEP)
			{
				write_res = FSFILE_Flush(out);
				if(R_FAILED(write_res))
				{
					res = write_res;
					goto out;
				}
				last_flush_done = done;
			}
			if(done - last_status_done >= STATUS_UPDATE_STEP || (total && done >= total))
			{
				write_status("downloading", job_name, title_id, done, total, 0, "Downloading");
				last_status_done = done;
			}
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
	if(R_SUCCEEDED(res))
	{
		Result flush_res = FSFILE_Flush(out);
		if(R_FAILED(flush_res))
			res = flush_res;
	}
	FSFILE_Close(out);

	if(R_SUCCEEDED(res))
		res = rename_path(part_path, final_path);
	httpcExit();
	return res;
}

Result stream_install_job(const char *job_name, const char *url, const char *title_id,
	u64 expected_size)
{
	Result res = init_http();
	if(R_FAILED(res))
		return res;

	Result am_res = amInit();
	if(R_FAILED(am_res))
	{
		httpcExit();
		return am_res;
	}

	g_last_http_status = 0;
	g_last_done = 0;
	g_last_total = expected_size;
	g_last_stage = "init";

	httpcContext ctx {};
	u32 status = 0;
	u32 total32 = 0;
	bool context_open = false;
	bool http_ready = true;
	Handle cia = 0;
	bool cia_open = false;
	u64 done = 0;
	u64 total = expected_size;
	u64 last_status_done = 0;
	u32 context_pos = 0;
	u32 reconnects = 0;

	led_downloading();
	write_status("install_stream_opening", job_name, title_id, 0, total, 0,
		"Opening stream install");

	g_last_stage = "http_open";
	res = open_download_context(&ctx, url, 0, &status);
	context_open = R_SUCCEEDED(res);
	if(R_FAILED(res))
		goto out;

	if(status != 200)
	{
		g_last_stage = "http_status";
		g_last_http_status = status;
		res = MAKERESULT(RL_PERMANENT, RS_INVALIDSTATE, RM_APPLICATION, status & 0x3FF);
		goto out;
	}

	g_last_stage = "http_size";
	httpcGetDownloadSizeState(&ctx, nullptr, &total32);
	if(!total)
		total = total32;
	g_last_total = total;

	g_last_stage = "am_start";
	res = AM_StartCiaInstall(MEDIATYPE_SD, &cia);
	if(R_FAILED(res))
		goto out;
	cia_open = true;

	write_status("installing", job_name, title_id, done, total, 0,
		"Downloading and installing");

receive_loop:
	context_pos = 0;
	for(;;)
	{
		if(total && done >= total)
		{
			res = 0;
			break;
		}

		u32 request_size = sizeof(g_http_buffer);
		if(total && total - done < request_size)
			request_size = static_cast<u32>(total - done);

		g_last_stage = "http_receive";
		res = httpcReceiveDataTimeout(&ctx, g_http_buffer, request_size,
			10ULL * 1000ULL * 1000ULL * 1000ULL);

		u32 pos = context_pos;
		Result progress_res = httpcGetDownloadSizeState(&ctx, &pos, nullptr);
		if(R_FAILED(progress_res))
			res = progress_res;
		else if(pos < context_pos)
			res = MAKERESULT(RL_PERMANENT, RS_INVALIDSTATE, RM_HTTP, 73);
		else if(total && done + (pos - context_pos) >= total
			&& R_FAILED(res) && res != static_cast<Result>(HTTPC_RESULTCODE_DOWNLOADPENDING))
		{
			res = 0;
		}

		u32 chunk = pos >= context_pos ? pos - context_pos : 0;
		if(chunk > request_size)
			chunk = request_size;
		if(chunk)
		{
			u32 written = 0;
			g_last_stage = "am_write";
			Result write_res = FSFILE_Write(cia, &written, done, g_http_buffer, chunk, 0);
			if(R_FAILED(write_res) || written != chunk)
			{
				res = R_FAILED(write_res) ? write_res :
					MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_APPLICATION, 4);
				goto out;
			}

			done += chunk;
			context_pos += chunk;
			g_last_done = done;
			g_last_total = total;
			led_download_progress(done, total);
			if(done - last_status_done >= STREAM_INSTALL_STATUS_STEP || (total && done >= total))
			{
				write_status("installing", job_name, title_id, done, total, 0,
					"Downloading and installing");
				last_status_done = done;
			}

			svcSleepThread(2LL * 1000LL * 1000LL);
		}

		if(chunk == 0 && res == static_cast<Result>(HTTPC_RESULTCODE_DOWNLOADPENDING))
			continue;
		if(res == static_cast<Result>(HTTPC_RESULTCODE_DOWNLOADPENDING))
			continue;
		if(R_FAILED(res))
		{
			while(done && done < total && reconnects < 6)
			{
				++reconnects;
				if(context_open)
				{
					httpcCancelConnection(&ctx);
					httpcCloseContext(&ctx);
				}
				context_open = false;
				if(http_ready)
				{
					httpcExit();
					http_ready = false;
				}
				write_status("install_reconnecting", job_name, title_id, done, total, res,
					"HTTP reconnecting during install");
				svcSleepThread((500LL + 250LL * reconnects) * 1000LL * 1000LL);

				g_last_stage = "http_reinit";
				res = init_http();
				if(R_FAILED(res))
					continue;
				http_ready = true;

				g_last_stage = "http_reopen_range";
				res = open_download_context(&ctx, url, done, &status);
				context_open = R_SUCCEEDED(res);
				if(R_FAILED(res))
					continue;
				if(status != 206)
				{
					g_last_stage = "http_range_status";
					g_last_http_status = status;
					res = MAKERESULT(RL_PERMANENT, RS_INVALIDSTATE, RM_APPLICATION, status & 0x3FF);
					goto out;
				}
				goto receive_loop;
			}
			goto out;
		}
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
	if(cia_open)
	{
		if(R_FAILED(res))
		{
			g_last_stage = "am_cancel";
			AM_CancelCIAInstall(cia);
		}
		else
		{
			g_last_stage = "am_finish";
			res = AM_FinishCiaInstall(cia);
		}
		svcCloseHandle(cia);
	}
	amExit();
	if(http_ready)
		httpcExit();
	return res;
}

void write_job_status()
{
	char job_name[128];
	if(!find_next_job(job_name, sizeof(job_name)))
	{
		led_idle();
		char status[128];
		int len = snprintf(status, sizeof(status),
			"state=idle\n"
			"result=00000000\n"
			"led=%s\n"
			"message=No jobs found\n",
			g_led_ready ? "ready" : "unavailable");
		if(len > 0)
			write_file("/3ds/Rune3DS/runefetch/state/status.txt",
				status, static_cast<u32>(len));
		write_boot_marker("idle");
		return;
	}

	char job_text[1600];
	char id[64];
	char title_id[32];
	char size_text[32];
	char mode[32];
	char url[1200];
	bool loaded = read_job(job_name, job_text, sizeof(job_text));
	bool deleted = false;
	if(loaded)
	{
		copy_field(job_text, "id", id, sizeof(id));
		copy_field(job_text, "title_id", title_id, sizeof(title_id));
		copy_field(job_text, "url", url, sizeof(url));
		copy_field(job_text, "size", size_text, sizeof(size_text));
		copy_field(job_text, "mode", mode, sizeof(mode));
		if(url[0])
		{
			bool cache_only = strcmp(mode, "cache") == 0 || strcmp(mode, "download_only") == 0;
			Result job_res = cache_only ?
				download_job(job_name, url, title_id, id, parse_u64(size_text)) :
				stream_install_job(job_name, url, title_id, parse_u64(size_text));
			if(R_SUCCEEDED(job_res))
				deleted = delete_job(job_name);
			else
			{
				led_error();
				char message[96];
				if(g_last_http_status)
					snprintf(message, sizeof(message), "Job failed; HTTP status %lu",
						static_cast<unsigned long>(g_last_http_status));
				else
					snprintf(message, sizeof(message), "Job failed at %s", g_last_stage);
				write_status(cache_only ? "download_failed" : "install_failed",
					job_name, title_id, g_last_done,
					g_last_total ? g_last_total : parse_u64(size_text), job_res, message);
				write_boot_marker(cache_only ? "download_failed" : "install_failed");
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
		mode[0] = '\0';
	}

	char status[512];
	bool cache_only = strcmp(mode, "cache") == 0 || strcmp(mode, "download_only") == 0;
	const char *ready_state = cache_only ? "download_ready" : "install_ready";
	int len = snprintf(status, sizeof(status),
		"state=%s\n"
		"job=%s\n"
		"id=%s\n"
		"title_id=%s\n"
		"url=%s\n"
		"deleted=%s\n"
		"result=00000000\n"
		"led=%s\n"
		"message=%s\n",
		loaded ? (deleted ? ready_state : "job_delete_failed") : "job_read_failed",
		job_name,
		id,
		title_id,
		url[0] ? "yes" : "no",
		deleted ? "yes" : "no",
		g_led_ready ? "ready" : "unavailable",
		cache_only ? "RuneFetch downloaded queued job" : "RuneFetch installed queued job");
	if(len > 0)
		write_file("/3ds/Rune3DS/runefetch/state/status.txt",
			status, static_cast<u32>(len));
	if(loaded && deleted)
		led_ready();
	else
		led_error();
	write_boot_marker(loaded ? (deleted ? ready_state : "job_delete_failed") : "job_read_failed");
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
			init_led();
			write_job_status();
		}

		if(g_led_ready)
		{
			mcuHwcExit();
			g_led_ready = false;
		}
		write_boot_marker("exit");
		fsExit();
	}

	return 0;
}
