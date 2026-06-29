#include "runefetch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u8 g_http_buffer[RF_CHUNK_SIZE] __attribute__((aligned(0x1000)));

static Result open_context(httpcContext *ctx, const char *url, u64 existing)
{
	Result res = httpcOpenContext(ctx, HTTPC_METHOD_GET, url, 0);
	if(R_FAILED(res)) return res;

	httpcSetKeepAlive(ctx, HTTPC_KEEPALIVE_ENABLED);
	httpcSetSSLOpt(ctx, SSLCOPT_DisableVerify);
	httpcAddRequestHeaderField(ctx, "User-Agent", "RuneFetch/0.1");

	if(existing)
	{
		char range[64];
		snprintf(range, sizeof(range), "bytes=%llu-", existing);
		httpcAddRequestHeaderField(ctx, "Range", range);
	}

	return 0;
}

Result rf_download_job(const RfJob *job)
{
	char base[RF_MAX_NAME];
	char part_path[RF_MAX_PATH];
	char final_path[RF_MAX_PATH];
	rf_basename(job, base, sizeof(base));
	snprintf(part_path, sizeof(part_path), "%s/%s.cia.part", RF_CACHE_DIR, base);
	snprintf(final_path, sizeof(final_path), "%s/%s.cia", RF_CACHE_DIR, base);

	FILE *out = fopen(part_path, "ab+");
	if(!out)
		return MAKERESULT(RL_PERMANENT, RS_NOTFOUND, RM_APPLICATION, 2);

	fseek(out, 0, SEEK_END);
	u64 existing = (u64)ftell(out);

	httpcContext ctx;
	memset(&ctx, 0, sizeof(ctx));
	Result res = open_context(&ctx, job->url, existing);
	if(R_FAILED(res))
	{
		fclose(out);
		return res;
	}

	res = httpcBeginRequest(&ctx);
	if(R_FAILED(res)) goto out;

	u32 status = 0;
	res = httpcGetResponseStatusCodeTimeout(&ctx, &status, 10ULL * 1000ULL * 1000ULL * 1000ULL);
	if(R_FAILED(res)) goto out;

	if((existing && status != 206) || (!existing && status != 200))
	{
		res = MAKERESULT(RL_PERMANENT, RS_INVALIDSTATE, RM_APPLICATION, 3);
		goto out;
	}

	u32 total32 = 0;
	httpcGetDownloadSizeState(&ctx, NULL, &total32);
	u64 total = job->size ? job->size : ((u64)total32 + existing);
	u64 done = existing;

	rf_write_status("downloading", job, done, total, 0, "Downloading");
	rf_led_progress(done, total);

	for(;;)
	{
		u32 before = 0, after = 0;
		httpcGetDownloadSizeState(&ctx, &before, NULL);

		res = httpcReceiveDataTimeout(&ctx, g_http_buffer, sizeof(g_http_buffer),
			10ULL * 1000ULL * 1000ULL * 1000ULL);

		httpcGetDownloadSizeState(&ctx, &after, NULL);
		u32 chunk = after >= before ? after - before : 0;

		if(chunk)
		{
			if(fwrite(g_http_buffer, 1, chunk, out) != chunk)
			{
				res = MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_APPLICATION, 4);
				goto out;
			}
			fflush(out);
			done += chunk;
			rf_write_status("downloading", job, done, total, 0, "Downloading");
			rf_led_progress(done, total);
		}

		if(R_FAILED(res) && res != (Result)HTTPC_RESULTCODE_DOWNLOADPENDING)
		{
			if(total && done >= total)
			{
				res = 0;
				break;
			}
			goto out;
		}

		if(res != (Result)HTTPC_RESULTCODE_DOWNLOADPENDING)
			break;

		if(total && done >= total)
		{
			res = 0;
			break;
		}
	}

	if(total && done < total)
		res = MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_APPLICATION, 5);

out:
	httpcCloseContext(&ctx);
	fclose(out);

	if(R_SUCCEEDED(res))
	{
		remove(final_path);
		if(rename(part_path, final_path) != 0)
			res = MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_APPLICATION, 6);
	}
	return res;
}
