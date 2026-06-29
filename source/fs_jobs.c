#include "runefetch.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void trim_newline(char *s)
{
	size_t n = strlen(s);
	while(n && (s[n - 1] == '\n' || s[n - 1] == '\r'))
		s[--n] = '\0';
}

static bool has_suffix(const char *s, const char *suffix)
{
	size_t slen = strlen(s);
	size_t tlen = strlen(suffix);
	return slen >= tlen && strcmp(s + slen - tlen, suffix) == 0;
}

static void copy_value(char *dst, size_t dst_size, const char *src)
{
	if(!dst_size) return;
	strncpy(dst, src, dst_size - 1);
	dst[dst_size - 1] = '\0';
}

Result rf_ensure_dirs(void)
{
	mkdir("sdmc:/3ds", 0777);
	mkdir(RF_BASE_DIR, 0777);
	mkdir(RF_FETCH_DIR, 0777);
	mkdir(RF_JOBS_DIR, 0777);
	mkdir(RF_DONE_DIR, 0777);
	mkdir(RF_FAILED_DIR, 0777);
	mkdir(RF_STATE_DIR, 0777);
	mkdir(RF_CACHE_DIR, 0777);
	return 0;
}

bool rf_find_next_job(char *out_name, size_t out_size)
{
	DIR *dir = opendir(RF_JOBS_DIR);
	if(!dir) return false;

	struct dirent *ent;
	bool found = false;
	while((ent = readdir(dir)) != NULL)
	{
		if(ent->d_name[0] == '.')
			continue;
		if(!has_suffix(ent->d_name, ".job"))
			continue;
		copy_value(out_name, out_size, ent->d_name);
		found = true;
		break;
	}
	closedir(dir);
	return found;
}

bool rf_load_job(const char *job_name, RfJob *job)
{
	memset(job, 0, sizeof(*job));
	copy_value(job->job_name, sizeof(job->job_name), job_name);

	char path[RF_MAX_PATH];
	snprintf(path, sizeof(path), "%s/%s", RF_JOBS_DIR, job_name);

	FILE *f = fopen(path, "r");
	if(!f) return false;

	char line[RF_MAX_URL + 32];
	while(fgets(line, sizeof(line), f))
	{
		trim_newline(line);
		if(line[0] == '\0' || line[0] == '#')
			continue;

		char *eq = strchr(line, '=');
		if(!eq) continue;
		*eq++ = '\0';

		if(strcmp(line, "id") == 0)
			copy_value(job->id, sizeof(job->id), eq);
		else if(strcmp(line, "title_id") == 0)
			copy_value(job->title_id, sizeof(job->title_id), eq);
		else if(strcmp(line, "name") == 0)
			copy_value(job->name, sizeof(job->name), eq);
		else if(strcmp(line, "url") == 0)
			copy_value(job->url, sizeof(job->url), eq);
		else if(strcmp(line, "size") == 0)
			sscanf(eq, "%llu", &job->size);
	}
	fclose(f);

	return job->url[0] != '\0';
}

Result rf_move_job(const char *job_name, bool success)
{
	char from[RF_MAX_PATH], to[RF_MAX_PATH];
	snprintf(from, sizeof(from), "%s/%s", RF_JOBS_DIR, job_name);
	snprintf(to, sizeof(to), "%s/%s", success ? RF_DONE_DIR : RF_FAILED_DIR, job_name);
	remove(to);
	return rename(from, to) == 0 ? 0 : MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_APPLICATION, 1);
}

void rf_write_status(const char *state, const RfJob *job, u64 done, u64 total, Result res, const char *message)
{
	FILE *f = fopen(RF_STATUS_PATH, "w");
	if(!f) return;

	fprintf(f, "state=%s\n", state ? state : "unknown");
	if(job)
	{
		fprintf(f, "job=%s\n", job->job_name);
		fprintf(f, "id=%s\n", job->id);
		fprintf(f, "title_id=%s\n", job->title_id);
		fprintf(f, "name=%s\n", job->name);
	}
	fprintf(f, "done=%llu\n", done);
	fprintf(f, "total=%llu\n", total);
	fprintf(f, "result=%08lX\n", res);
	fprintf(f, "message=%s\n", message ? message : "");
	fclose(f);
}

void rf_basename(const RfJob *job, char *out, size_t out_size)
{
	const char *src = job->title_id[0] ? job->title_id : (job->id[0] ? job->id : job->job_name);
	size_t pos = 0;

	for(size_t i = 0; src[i] && pos + 1 < out_size; ++i)
	{
		char c = src[i];
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '-' || c == '_';
		if(ok) out[pos++] = c;
	}

	if(pos == 0 && out_size > 1)
		out[pos++] = 'j';
	out[pos] = '\0';
}
