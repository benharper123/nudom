#include "pch.h"
#include "xoDefs.h"
#include "xoDocGroup.h"
#include "xoDoc.h"
#include "xoSysWnd.h"
#include "Render/xoRenderGL.h"
#include "Text/xoFontStore.h"
#include "Text/xoGlyphCache.h"

static const int					MAX_WORKER_THREADS = 32;
static volatile uint32				ExitSignalled = 0;
static int							InitializeCount = 0;
static xoStyle*						DefaultTagStyles[xoTagEND];
static AbcThreadHandle				WorkerThreads[MAX_WORKER_THREADS];

#if XO_PLATFORM_WIN_DESKTOP
static AbcThreadHandle				UIThread = NULL;
#endif

// Single globally accessible data
static xoGlobalStruct*				xoGlobals = NULL;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void xoTexture::FlipVertical()
{
	byte sline[4096];
	byte* line = sline;
	size_t astride = std::abs(TexStride);
	if ( astride > sizeof(sline) )
		line = (byte*) AbcMallocOrDie( astride );
	for ( uint32 i = 0; i < TexHeight / 2; i++ )
	{
		memcpy( line, TexDataAtLine(TexHeight - i - 1), astride );
		memcpy( TexDataAtLine(TexHeight - i - 1), TexDataAtLine(i), astride );
		memcpy( TexDataAtLine(i), line, astride );
	}
	if ( line != sline )
		free( line );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void xoBox::SetInt( int32 left, int32 top, int32 right, int32 bottom )
{
	Left = xoRealToPos((float) left);
	Top = xoRealToPos((float) top);
	Right = xoRealToPos((float) right);
	Bottom = xoRealToPos((float) bottom);
}

void xoBox::ExpandToFit( const xoBox& expando )
{
	Left = xoMin(Left, expando.Left);
	Top = xoMin(Top, expando.Top);
	Right = xoMax(Right, expando.Right);
	Bottom = xoMax(Bottom, expando.Bottom);
}

void xoBox::ClampTo( const xoBox& clamp )
{
	Left = xoMax(Left, clamp.Left);
	Top = xoMax(Top, clamp.Top);
	Right = xoMin(Right, clamp.Right);
	Bottom = xoMin(Bottom, clamp.Bottom);
}

xoBox xoBox::ShrunkBy( const xoBox& margins )
{
	xoBox c = *this;
	c.Left += margins.Left;
	c.Right -= margins.Right;
	c.Top += margins.Top;
	c.Bottom -= margins.Bottom;
	return c;
}

xoBoxF xoBox::ToRealBox() const
{
	xoBoxF f;
	f.Left = xoPosToReal( Left );
	f.Right = xoPosToReal( Right );
	f.Top = xoPosToReal( Top );
	f.Bottom = xoPosToReal( Bottom );
	return f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

xoBoxF xoBox16::ToRealBox() const
{
	xoBoxF f;
	f.Left = xoPosToReal( Left );
	f.Right = xoPosToReal( Right );
	f.Top = xoPosToReal( Top );
	f.Bottom = xoPosToReal( Bottom );
	return f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

xoVec4f	xoColor::GetVec4sRGB() const
{
	float s = 1.0f / 255.0f;
	return xoVec4f( r * s, g * s, b * s, a * s );
}

xoVec4f	xoColor::GetVec4Linear() const
{
	float s = 1.0f / 255.0f;
	return xoVec4f( xoSRGB2Linear(r),
					xoSRGB2Linear(g),
					xoSRGB2Linear(b),
					a * s );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const float sRGB_Low	= 0.0031308f;
static const float sRGB_a	= 0.055f;

XOAPI float	xoSRGB2Linear( uint8 srgb )
{
	float g = srgb * (1.0f / 255.0f);
	if ( g <= 0.04045 )
		return g / 12.92f;
	else
		return pow( (g + sRGB_a) / (1.0f + sRGB_a), 2.4f );
}

XOAPI uint8	xoLinear2SRGB( float linear )
{
	float g;
	if ( linear <= sRGB_Low )
		g = 12.92f * linear;
	else
		g = (1.0f + sRGB_a) * pow(linear, 1.0f / 2.4f) - sRGB_a;
	return (uint8) xoRound( 255.0f * g );
}

void xoRenderStats::Reset()
{
	memset( this, 0, sizeof(*this) );
}

// add or remove documents that are queued for addition or removal
XOAPI void xoProcessDocQueue()
{
	xoDocGroup* p = NULL;

	while ( p = xoGlobal()->DocRemoveQueue.PopTailR() )
		erase_delete( xoGlobal()->Docs, xoGlobal()->Docs.find(p) );

	while ( p = xoGlobal()->DocAddQueue.PopTailR() )
		xoGlobal()->Docs += p;
}

AbcThreadReturnType AbcKernelCallbackDecl xoWorkerThreadFunc( void* threadContext )
{
	while ( true )
	{
		AbcSemaphoreWait( xoGlobal()->JobQueue.SemaphoreObj(), AbcINFINITE );
		if ( ExitSignalled )
			break;
		xoJob job;
		XOVERIFY( xoGlobal()->JobQueue.PopTail( job ) );
		job.JobFunc( job.JobData );
	}

	return 0;
}

#if XO_PLATFORM_WIN_DESKTOP

AbcThreadReturnType AbcKernelCallbackDecl xoUIThread( void* threadContext )
{
	while ( true )
	{
		AbcSemaphoreWait( xoGlobal()->EventQueue.SemaphoreObj(), AbcINFINITE );
		if ( ExitSignalled )
			break;
		xoOriginalEvent ev;
		XOVERIFY( xoGlobal()->EventQueue.PopTail( ev ) );
		ev.DocGroup->ProcessEvent( ev.Event );
	}
	return 0;
}

static void xoInitialize_Win32()
{
	XOVERIFY( AbcThreadCreate( &xoUIThread, NULL, UIThread ) );
}

static void xoShutdown_Win32()
{
	if ( UIThread != NULL )
	{
		xoGlobal()->EventQueue.Add( xoOriginalEvent() );
		for ( uint waitNum = 0; true; waitNum++ )
		{
			if ( WaitForSingleObject( UIThread, waitNum ) == WAIT_OBJECT_0 )
				break;
		}
		UIThread = NULL;
	}
}


#endif


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

XOAPI xoGlobalStruct* xoGlobal()
{
	return xoGlobals;
}

static float ComputeEpToPixel()
{
#if XO_PLATFORM_WIN_DESKTOP
	float scale = 1;
	HDC dc = GetDC( NULL );
	if ( dc != NULL )
	{
		int dpi = GetDeviceCaps( dc, LOGPIXELSX );
		scale = (float) dpi / 96.0f;
		ReleaseDC( NULL, dc );
	}
	return scale;
#elif XO_PLATFORM_ANDROID
	// xoGlobals->EpToPixel is sent by Java_com_android_xo_XoLib_init when it calls xoInitialize()
	return 1;
#elif XO_PLATFORM_LINUX_DESKTOP
	// TODO
	return 1;
#else
	XOTODO_STATIC
#endif
}

static void InitializePlatform()
{
#if XO_PLATFORM_WIN_DESKTOP
	SetProcessDPIAware();
#elif XO_PLATFORM_ANDROID
#elif XO_PLATFORM_LINUX_DESKTOP
#else
	XOTODO_STATIC;
#endif
}

XOAPI void xoInitialize( xoInitParams* init )
{
	InitializeCount++;
	if ( InitializeCount != 1 )
		return;

	InitializePlatform();

	AbcMachineInformation minf;
	AbcMachineInformationGet( minf );

	xoGlobals = new xoGlobalStruct();
	if ( init != nullptr && init->EpToPixel != 0 )
		xoGlobals->EpToPixel = init->EpToPixel;
	else
		xoGlobals->EpToPixel = ComputeEpToPixel();
	xoGlobals->TargetFPS = 60;
	xoGlobals->NumWorkerThreads = std::min( minf.LogicalCoreCount, MAX_WORKER_THREADS );
	xoGlobals->MaxSubpixelGlyphSize = 60;
	xoGlobals->PreferOpenGL = true;
	xoGlobals->EnableVSync = false;
	// Freetype's output is linear coverage percentage, so if we treat our freetype texture as GL_LUMINANCE
	// (and not GL_SLUMINANCE), and we use an sRGB framebuffer, then we get perfect results without
	// doing any tweaking to the freetype glyphs.
	// Setting SubPixelTextGamma = 2.2 will get results very close to default cleartype on Windows 8.1
	// As far as I know, leaving the gamma at 1.0 is "true to the font designer", but this does leave the fonts
	// quite a bit heavier than the default on Windows. For this reason, we set the gamma to 1.5. It seems like
	// a reasonable blend between the "correct weight" and "prior art".
	// CORRECTION. A gamma of anything other than 1.0 looks bad at small font sizes (like 12 or 13 pixels)
	// We might want to have a "gamma curve" of pixel size vs gamma.
	// FURTHER CORRECTION. The "infinality" patches to Freetype, as well as fixing up our incorrect glyph
	// UV coordinates has made this all redundant.
	xoGlobals->SubPixelTextGamma = 1.0f;
	xoGlobals->WholePixelTextGamma = 1.0f;
#if XO_PLATFORM_WIN_DESKTOP || XO_PLATFORM_LINUX_DESKTOP
	xoGlobals->EnableSubpixelText = true;
	xoGlobals->EnableSRGBFramebuffer = true;
	//xoGlobals->EmulateGammaBlending = true;
#else
	xoGlobals->EnableSubpixelText = false;
	xoGlobals->EnableSRGBFramebuffer = false;
	//xoGlobals->EmulateGammaBlending = false;
#endif
	xoGlobals->EnableKerning = true;
	// Do we round text line heights to whole pixels?
	// We only render sub-pixel text on low resolution monitors that do not change orientation (ie desktop).
	xoGlobals->RoundLineHeights = xoGlobals->EnableSubpixelText || xoGlobals->EpToPixel < 2.0f;
	xoGlobals->SnapBoxes = true;
	xoGlobals->SnapSubpixelHorzText = true;
	//xoGlobals->DebugZeroClonedChildList = true;
	xoGlobals->MaxTextureID = ~((xoTextureID) 0);
    //xoGlobals->ClearColor.Set( 200, 0, 200, 255 );  // Make our clear color a very noticeable purple, so you know when you've screwed up the root node
	xoGlobals->ClearColor.Set( 255, 150, 255, 255 );  // Make our clear color a very noticeable purple, so you know when you've screwed up the root node
	xoGlobals->DocAddQueue.Initialize( false );
	xoGlobals->DocRemoveQueue.Initialize( false );
	xoGlobals->EventQueue.Initialize( true );
	xoGlobals->JobQueue.Initialize( true );
	xoGlobals->FontStore = new xoFontStore();
	xoGlobals->FontStore->InitializeFreetype();
	xoGlobals->GlyphCache = new xoGlyphCache();
	xoSysWnd::PlatformInitialize();
#if XO_PLATFORM_WIN_DESKTOP
	xoInitialize_Win32();
#endif
	XOTRACE( "Using %d/%d processors.\n", (int) xoGlobals->NumWorkerThreads, (int) minf.LogicalCoreCount );
	for ( int i = 0; i < xoGlobals->NumWorkerThreads; i++ )
	{
		XOVERIFY( AbcThreadCreate( xoWorkerThreadFunc, NULL, WorkerThreads[i] ) );
	}
}

XOAPI void xoSurfaceLost()
{

}

XOAPI void xoShutdown()
{
	XOASSERT(InitializeCount > 0);
	InitializeCount--;
	if ( InitializeCount != 0 ) return;

	AbcInterlockedSet( &ExitSignalled, 1 );

	for ( int i = 0; i < xoTagEND; i++ )
		delete DefaultTagStyles[i];

	// allow documents scheduled for deletion to be deleted
	xoProcessDocQueue();

#if XO_PLATFORM_WIN_DESKTOP
	xoShutdown_Win32();
#endif

	// signal all threads to exit
	xoJob nullJob = xoJob();
	for ( int i = 0; i < xoGlobal()->NumWorkerThreads; i++ )
		xoGlobal()->JobQueue.Add( nullJob );

	// wait for each thread in turn
	for ( int i = 0; i < xoGlobal()->NumWorkerThreads; i++ )
		XOVERIFY( AbcThreadJoin( WorkerThreads[i] ) );

	xoGlobals->GlyphCache->Clear();
	delete xoGlobals->GlyphCache;
	xoGlobals->GlyphCache = NULL;

	xoGlobals->FontStore->Clear();
	xoGlobals->FontStore->ShutdownFreetype();
	delete xoGlobals->FontStore;
	xoGlobals->FontStore = NULL;

	delete xoGlobals;
}

XOAPI xoStyle** xoDefaultTagStyles()
{
	return DefaultTagStyles;
}

XOAPI void xoParseFail( const char* msg, ... )
{
	char buff[4096] = "";
	va_list va;
	va_start( va, msg );
	uint r = vsnprintf( buff, arraysize(buff), msg, va );
	va_end( va );
	if ( r < arraysize(buff) )
		XOTRACE_WRITE(buff);
}

XOAPI void XOTRACE( const char* msg, ... )
{
	char buff[4096] = "";
	va_list va;
	va_start( va, msg );
	uint r = vsnprintf( buff, arraysize(buff), msg, va );
	va_end( va );
	if ( r < arraysize(buff) )
		XOTRACE_WRITE(buff);
}

XOAPI void NUTIME( const char* msg, ... )
{
	const int timeChars = 16;
	char buff[4096] = "";
	sprintf( buff, "%-15.3f  ", AbcTimeAccurateRTSeconds() * 1000 );
	va_list va;
	va_start( va, msg );
	uint r = vsnprintf( buff + timeChars, arraysize(buff) - timeChars, msg, va );
	va_end( va );
	if ( r < arraysize(buff) )
		XOTRACE_WRITE(buff);
}