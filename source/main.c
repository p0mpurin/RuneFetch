#include "runefetch.h"

#include <3ds.h>
#include <string.h>

static void idle_sleep(void)
{
	svcSleepThread(1000ULL * 1000ULL * 1000ULL);
}

static bool g_led_available;

static Result init_core_services(void)
{
	Result res = fsInit();
	if(R_FAILED(res))
		return res;

	rf_ensure_dirs();
	rf_write_status("starting", NULL, 0, 0, 0, "RuneFetch starting");
	return 0;
}

static Result init_download_services(void)
{
	Result res = acInit();
	if(R_FAILED(res)) return res;

	res = ptmuInit();
	if(R_FAILED(res)) return res;

	res = ptmSysmInit();
	g_led_available = R_SUCCEEDED(res);
	if(R_FAILED(res)) return res;

	res = httpcInit(512 * 1024);
	return res;
}

static void exit_services(void)
{
	httpcExit();
	if(g_led_available)
		ptmSysmExit();
	ptmuExit();
	acExit();
	fsExit();
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	Result res = init_core_services();
	if(R_FAILED(res))
	{
		for(;;) idle_sleep();
	}

	res = init_download_services();
	if(R_FAILED(res))
	{
		rf_write_status("failed", NULL, 0, 0, res, "Service initialization failed");
		for(;;) idle_sleep();
	}

	if(g_led_available)
		rf_led_init();
	rf_write_status("idle", NULL, 0, 0, 0, "Waiting for jobs");

	for(;;)
	{
		char job_name[RF_MAX_NAME];
		if(!rf_find_next_job(job_name, sizeof(job_name)))
		{
			rf_write_status("idle", NULL, 0, 0, 0, "Waiting for jobs");
			idle_sleep();
			continue;
		}

		RfJob job;
		if(!rf_load_job(job_name, &job))
		{
			rf_write_status("failed", NULL, 0, 0,
				MAKERESULT(RL_PERMANENT, RS_INVALIDARG, RM_APPLICATION, 7),
				"Invalid job");
			rf_move_job(job_name, false);
			if(g_led_available)
				rf_led_error();
			continue;
		}

		rf_write_status("downloading", &job, 0, job.size, 0, "Starting");

		acWaitInternetConnection();
		res = rf_download_job(&job);
		if(R_SUCCEEDED(res))
		{
			rf_write_status("ready", &job, job.size, job.size, 0, "CIA ready");
			rf_move_job(job_name, true);
			if(g_led_available)
				rf_led_ready();
		}
		else
		{
			rf_write_status("failed", &job, 0, job.size, res, "Download failed");
			rf_move_job(job_name, false);
			if(g_led_available)
				rf_led_error();
		}
	}

	exit_services();
	return 0;
}
