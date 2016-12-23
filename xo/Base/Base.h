#pragma once

#if XO_PLATFORM_WIN_DESKTOP
#ifdef _DEBUG
#include <stdlib.h>
#include <crtdbg.h>
#endif
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <Shlobj.h>
#include <tchar.h>
#else
#if XO_PLATFORM_ANDROID
#include <jni.h>
#include <android/log.h>
#include <sys/atomics.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <X11/Xlib.h> // X11 definitions are needed by SysWnd.h
#include <X11/Xutil.h>
#include <GL/glx.h>
#endif
#include <pthread.h>
#include <semaphore.h>
#include <math.h>
#endif

#if XO_BUILD_DIRECTX
#include <D3D11.h>
#include <d3dcompiler.h>
#include <directxmath.h>
#include <directxcolors.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <float.h>

#include <atomic>
#include <string>
#include <algorithm>
#include <limits>
#include <functional>
#include <vector>
#include <mutex>

#ifdef _WIN32
typedef SSIZE_T ssize_t;
#endif

namespace xo {
// These purposefully do not pass by reference, because of this: http://randomascii.wordpress.com/2013/11/24/stdmin-causing-three-times-slowdown-on-vc/
template <typename T>
T Clamp(T v, T vmin, T vmax) { return (v < vmin) ? vmin : (v > vmax) ? vmax : v; }
template <typename T>
T Min(T a, T b) { return a < b ? a : b; }
template <typename T>
T Max(T a, T b) { return a < b ? b : a; }
}

// Found this in the Chrome sources, via a PVS studio blog post
template <typename T, size_t N>
char (&ArraySizeHelper(T (&array)[N]))[N];
#define arraysize(array) (sizeof(ArraySizeHelper(array)))

#define XO_DISALLOW_COPY_AND_ASSIGN(TypeName) \
	TypeName(const TypeName&) = delete;       \
	TypeName& operator=(const TypeName&) = delete

//#ifndef ASSERT
//#define TEMP_ASSERT
//#ifdef _DEBUG
//#define ASSERT(condition) (void) 0
//#else
//#define ASSERT(condition) assert(condition)
//#endif
//#endif

#include "../dependencies/agg/include/agg_basics.h"
#include "../dependencies/agg/include/agg_conv_stroke.h"
#include "../dependencies/agg/include/agg_conv_curve.h"
#include "../dependencies/agg/include/agg_conv_clip_polyline.h"
#include "../dependencies/agg/include/agg_conv_clip_polygon.h"
#include "../dependencies/agg/include/agg_path_storage.h"
#include "../dependencies/agg/include/agg_pixfmt_rgba.h"
#include "../dependencies/agg/include/agg_rasterizer_scanline_aa.h"
#include "../dependencies/agg/include/agg_renderer_scanline.h"
#include "../dependencies/agg/include/agg_rendering_buffer.h"
#include "../dependencies/agg/include/agg_scanline_u.h"

/*
#include "../dependencies/Panacea/Platform/coredefs.h"
#include "../dependencies/Panacea/Platform/stdint.h"
#include "../dependencies/Panacea/Containers/cheapvec.h"
#include "../dependencies/Panacea/Containers/cheapvec.h"
#include "../dependencies/Panacea/Containers/queue.h"
#include "../dependencies/Panacea/Platform/cpu.h"
#include "../dependencies/Panacea/Platform/err.h"
#include "../dependencies/Panacea/Platform/filesystem.h"
#include "../dependencies/Panacea/Platform/process.h"
#include "../dependencies/Panacea/Platform/syncprims.h"
#include "../dependencies/Panacea/Platform/timeprims.h"
#include "../dependencies/Panacea/Platform/thread.h"
#include "../dependencies/Panacea/Other/StackAllocators.h"
#include "../dependencies/Panacea/Bits/BitMap.h"
#include "../dependencies/Panacea/Platform/ConvertUTF.h"
*/

#include "../dependencies/sema.h"
#include "../dependencies/ConvertUTF/ConvertUTF.h"
#include "../dependencies/tsf/tsf.h"

#include "../dependencies/ohash/ohashtable.h"
#include "../dependencies/ohash/ohashset.h"
#include "../dependencies/ohash/ohashmap.h"

///////////////////////////////////////////////////////////////////////////
// Vector math
#include "../dependencies/vectormath/Vec2.h"
#include "../dependencies/vectormath/Vec3.h"
#include "../dependencies/vectormath/Vec4.h"
#include "../dependencies/vectormath/Mat4.h"

namespace xo {

typedef VecBase2T<float> VecBase2f;
typedef VecBase3T<float> VecBase3f;
typedef VecBase4T<float> VecBase4f;

typedef Vec2T<float> Vec2f;
typedef Vec3T<float> Vec3f;
typedef Vec4T<float> Vec4f;

#define XO_MAT4F_DEFINED
typedef Mat4T<float> Mat4f;
}
///////////////////////////////////////////////////////////////////////////

#include "../dependencies/hash/xxhash.h"

#include "Base/Alloc.h"
#include "Base/cheapvec.h"
#include "Base/CPU.h"
#include "Base/Queue.h"
#include "Base/xoString.h"
#include "Base/OS_Time.h"
#include "Base/OS_IO.h"

//#ifdef TEMP_ASSERT
//#undef TEMP_ASSERT
//#undef ASSERT
//#endif