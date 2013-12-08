#pragma once

#ifdef _WIN32
	#define _CRT_SECURE_NO_WARNINGS 1
	#ifndef _CRTDBG_MAP_ALLOC
		#define _CRTDBG_MAP_ALLOC
	#endif
	#include <stdlib.h>
	#include <crtdbg.h>
	#include <windows.h>
#endif

#include "../nuDom/nuDom.h"
#include "../dependencies/stb_image_write.h"

#define STBI_HEADER_FILE_ONLY
#include "../dependencies/stb_image.c"
#undef STBI_HEADER_FILE_ONLY

#define TT_MODULE_NAME nudom
#include "../dependencies/TinyTest/TinyTest.h"
#define TESTFUNC(f) TT_TEST_FUNC(NULL, NULL, TTSizeSmall, f, TTParallelDontCare)
