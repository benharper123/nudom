#pragma once

#include "xoDefs.h"

// It will be good if we can keep these inside 32 bits, for easy masking of handlers. If not, just use as many 32-bit words as necessary.
enum xoEvents
{
	xoEventTouch		= BIT(0),
	xoEventMouseMove	= BIT(1),
	xoEventMouseEnter	= BIT(2),
	xoEventMouseLeave	= BIT(3),
	xoEventWindowSize	= BIT(4),
	xoEventTimer		= BIT(5),
	xoEventClick		= BIT(6),
	xoEventGetFocus		= BIT(7),
	xoEventLoseFocus	= BIT(8),
};

/* User interface event (keyboard, mouse, touch, etc).
*/
class XOAPI xoEvent
{
public:
	xoDoc*			Doc			= nullptr;
	void*			Context		= nullptr;
	xoDomEl*		Target		= nullptr;
	xoEvents		Type		= xoEventMouseMove;
	int				PointCount	= 0;					// Mouse = 1	Touch >= 1
	xoVec2f			Points[XO_MAX_TOUCHES];

			xoEvent();
			~xoEvent();

	void	MakeWindowSize( int w, int h );
};

class XOAPI xoOriginalEvent
{
public:
	xoDocGroup*		DocGroup;
	xoEvent			Event;
};

typedef std::function<bool(const xoEvent& ev)> xoEventHandlerLambda;

typedef bool (*xoEventHandlerF)(const xoEvent& ev);

XOAPI bool xoEventHandler_LambdaStaticFunc(const xoEvent& ev);

enum xoEventHandlerFlags
{
	xoEventHandlerFlag_IsLambda = 1,
};

class XOAPI xoEventHandler
{
public:
	uint32				Mask;
	uint32				Flags;
	void*				Context;
	xoEventHandlerF		Func;

			xoEventHandler();
			~xoEventHandler();

	bool	Handles( xoEvents ev ) const	{ return !!(Mask & ev); }
	bool	IsLambda() const				{ return !!(Flags & xoEventHandlerFlag_IsLambda); }
	void	SetLambda()						{ Flags |= xoEventHandlerFlag_IsLambda; }
};