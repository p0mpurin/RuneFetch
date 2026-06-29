#ifndef RUNEFETCH_H
#define RUNEFETCH_H

#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>

#define RF_SD_ROOT        "sdmc:"
#define RF_BASE_DIR       "sdmc:/3ds/Rune3DS"
#define RF_FETCH_DIR      "sdmc:/3ds/Rune3DS/runefetch"
#define RF_JOBS_DIR       "sdmc:/3ds/Rune3DS/runefetch/jobs"
#define RF_DONE_DIR       "sdmc:/3ds/Rune3DS/runefetch/done"
#define RF_FAILED_DIR     "sdmc:/3ds/Rune3DS/runefetch/failed"
#define RF_STATE_DIR      "sdmc:/3ds/Rune3DS/runefetch/state"
#define RF_STATUS_PATH    "sdmc:/3ds/Rune3DS/runefetch/state/status.txt"
#define RF_CACHE_DIR      "sdmc:/3ds/Rune3DS/cache"

#define RF_MAX_PATH       256
#define RF_MAX_URL        1024
#define RF_MAX_NAME       128
#define RF_CHUNK_SIZE     0x20000

typedef struct {
	char job_name[RF_MAX_NAME];
	char id[64];
	char title_id[32];
	char name[RF_MAX_NAME];
	char url[RF_MAX_URL];
	u64 size;
} RfJob;

Result rf_ensure_dirs(void);
bool rf_find_next_job(char *out_name, size_t out_size);
bool rf_load_job(const char *job_name, RfJob *job);
Result rf_move_job(const char *job_name, bool success);
void rf_write_status(const char *state, const RfJob *job, u64 done, u64 total, Result res, const char *message);
void rf_basename(const RfJob *job, char *out, size_t out_size);
Result rf_download_job(const RfJob *job);

void rf_led_init(void);
void rf_led_stage(u8 stage);
void rf_led_progress(u64 done, u64 total);
void rf_led_ready(void);
void rf_led_error(void);
void rf_led_paused(void);
void rf_led_off(void);

#endif
