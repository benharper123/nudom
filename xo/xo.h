#pragma once

#include "Base/PlatformDefine.h"
#include "Base/Base.h"

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable : 4345) // POD initialized with ()
#endif

#include "Defs.h"
#include "Doc.h"
#include "DocGroup.h"
#include "Dom/DomCanvas.h"
#include "Canvas/Canvas2D.h"
#include "Layout/Layout.h"
#include "Render/Renderer.h"
#include "Render/RenderDoc.h"
#include "Render/StyleResolve.h"
#include "Image/ImageStore.h"
#include "Image/Image.h"
#include "SysWnd.h"
#include "Event.h"
#include "Controls/EditBox.h"
#include "Controls/Button.h"
#include "Controls/MsgBox.h"
#include "Reactive/Control.h"
#include "VirtualDom/Diff.h"
#include "VirtualDom/VirtualDom.h"

#include "SysWnd_android.h"
#include "SysWnd_linux.h"
#include "SysWnd_windows.h"

#ifdef _WIN32
#pragma warning(pop)
#endif
