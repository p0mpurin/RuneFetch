#include <3ds.h>

namespace {
alignas(8) u8 g_static_heap[0x10000];
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
		static const char data[] = "stage=fsInit\nresult=00000000\n";
		Handle file = 0;
		Result open_res = FSUSER_OpenFileDirectly(&file, ARCHIVE_SDMC,
			fsMakePath(PATH_EMPTY, nullptr),
			fsMakePath(PATH_ASCII, "/runefetch_boot.txt"),
			FS_OPEN_CREATE | FS_OPEN_WRITE, 0);
		if(R_SUCCEEDED(open_res))
		{
			u32 written = 0;
			FSFILE_Write(file, &written, 0, data, sizeof(data) - 1, FS_WRITE_FLUSH);
			FSFILE_Close(file);
		}
	}

	for(;;)
	{
		svcSleepThread(1000LL * 1000LL * 1000LL);
	}
}
