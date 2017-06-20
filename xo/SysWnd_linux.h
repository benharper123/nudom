#pragma once
#include "SysWnd.h"

#if XO_PLATFORM_LINUX_DESKTOP
namespace xo {

class XO_API SysWndLinux : public SysWnd {
public:
	enum XClientMessages {
		XClientMessage_Repaint = 1,
	};

	Display*     XDisplay    = nullptr;
	int          XDisplay_FD = -1; // file descriptor from ConnectionNumber(XDisplay)
	Window       XWindowRoot;
	XVisualInfo* VisualInfo = nullptr;
	Colormap     ColorMap;
	Window       XWindow;
	GLXContext   GLContext = nullptr;
	XEvent       Event;
	int          EventLoopWakePipe[2]; // read,write ends of pipe used to wake message loop's select()

	SysWndLinux();
	~SysWndLinux() override;

	Error Create(uint32_t createFlags) override;
	Box   GetRelativeClientRect() override;
	void  PostCursorChangedMessage() override;
	void  PostRepaintMessage() override;
};
} // namespace xo
#endif