#include <3ds.h>
#include <cstdio>

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
		}
	}

	for(;;)
	{
		svcSleepThread(1000LL * 1000LL * 1000LL);
	}
}
