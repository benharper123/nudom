#include "pch.h"

// In order to debug tests, launch the process as "test.exe test :TheTestName"

#include "../dependencies/TinyTest/TinyTestBuild.h"

void VDomDiffFuzz();

#ifdef _WIN32
static int __cdecl CrtAllocHook(int allocType, void *pvData, size_t size, int blockUse, long request, const unsigned char *filename, int fileLine)
{
	return TRUE;
}
#endif

int main(int argc, char** argv)
{
#ifdef _WIN32
	_CrtSetAllocHook(CrtAllocHook);
#endif

	xo::Initialize();

	// Uncomment this line to run tests on DirectX
	//xoGlobal()->PreferOpenGL = false;

	// Make clear color a predictable pink, no matter what the default is
	xo::Global()->ClearColor.Set(255, 30, 240, 255);

	int retval = 1;
	if (argc == 2 && strcmp(argv[1], "fuzz-vdom-diff") == 0) {
		retval = 0;
		VDomDiffFuzz();
	} else {
		retval = TTRun(argc, argv);
	}

	xo::Shutdown();

#ifdef _WIN32
	TTASSERT(_CrtDumpMemoryLeaks() == FALSE);
#endif
	return retval;
}
