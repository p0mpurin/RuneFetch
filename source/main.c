#include "runefetch.h"

#include <3ds.h>
#include <string.h>

static u8 g_static_heap[0x10000] __attribute__((aligned(8)));

u32 __ctru_heap_size = sizeof(g_static_heap);
u32 __ctru_linear_heap_size = 0;

void __system_allocateHeaps(void)
{
	extern char *fake_heap_start;
	extern char *fake_heap_end;
	extern u32 __ctru_heap;
	extern u32 __ctru_linear_heap;

	__ctru_heap = (u32)g_static_heap;
	__ctru_linear_heap = 0;
	fake_heap_start = (char *)g_static_heap;
	fake_heap_end = (char *)g_static_heap + sizeof(g_static_heap);
}

void __appInit(void)
{
	srvInit();
}

void __appExit(void)
{
	srvExit();
}

static void idle_sleep(void)
{
	svcSleepThread(1000ULL * 1000ULL * 1000ULL);
}

static void startup_fail(u8 stage)
{
	(void)stage;
	for(;;)
	{
		svcSleepThread(600ULL * 1000ULL * 1000ULL);
	}
}

static Result init_core_services(void)
{
	Result res = fsInit();
	if(R_FAILED(res))
		return res;

	rf_write_boot_marker("fsInit", res);

	rf_ensure_dirs();
	rf_write_boot_marker("ensure_dirs", 0);

	rf_write_status("starting", NULL, 0, 0, 0, "RuneFetch starting");
	rf_write_boot_marker("status", 0);

	return 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	Result res = init_core_services();
	if(R_FAILED(res))
	{
		startup_fail(10);
	}

	rf_write_status("probe", NULL, 0, 0, 0, "RuneFetch boot probe alive");
	rf_write_boot_marker("probe_alive", 0);

	for(;;)
	{
		idle_sleep();
	}
}
