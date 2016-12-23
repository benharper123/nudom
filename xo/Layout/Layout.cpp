#include "pch.h"
#include "Layout.h"
#include "Doc.h"
#include "Dom/DomNode.h"
#include "Dom/DomText.h"
#include "Render/RenderDomEl.h"
#include "Render/StyleResolve.h"
#include "Text/FontStore.h"
#include "Text/GlyphCache.h"

namespace xo {

/* This is called serially.

Why do we perform layout in multiple passes, loading all missing glyphs at the end of each pass?
The reason is because we eventually want to be able to parallelize layout.

Missing glyphs are a once-off cost (ie once per application instance),
so it's not worth trying to use a mutable glyph cache.

*/
void Layout::PerformLayout(const xo::Doc& doc, RenderDomNode& root, xo::Pool* pool) {
	Doc        = &doc;
	Pool       = pool;
	Boxer.Pool = pool;
	Stack.Initialize(Doc, Pool);

	// These are thumbsuck numbers.
	// 100 is max expected tree depth.
	// 64 is related to size of LayoutOutput, and number of expected objects
	// inside the vectors that store LayoutOutput inside RunNode
	FHeap.Initialize(100, 64);

	Fonts                = Global()->FontStore->GetImmutableTable();
	SnapBoxes            = Global()->SnapBoxes;
	SnapSubpixelHorzText = Global()->SnapSubpixelHorzText;
	EnableKerning        = Global()->EnableKerning;

	while (true) {
		LayoutInternal(root);

		if (GlyphsNeeded.size() == 0) {
			XOTRACE_LAYOUT_VERBOSE("Layout done\n");
			break;
		} else {
			XOTRACE_LAYOUT_VERBOSE("Layout done (but need another pass for missing glyphs)\n");
			RenderGlyphsNeeded();
		}
	}
}

void Layout::RenderGlyphsNeeded() {
	for (auto it = GlyphsNeeded.begin(); it != GlyphsNeeded.end(); it++)
		Global()->GlyphCache->RenderGlyph(*it);
	GlyphsNeeded.clear();
}

void Layout::LayoutInternal(RenderDomNode& root) {
	PtToPixel = 1.0; // TODO
	EpToPixel = Global()->EpToPixel;

	XOTRACE_LAYOUT_VERBOSE("Layout 1\n");

	Pool->FreeAll();
	root.Children.clear();
	Stack.Reset();

	XOTRACE_LAYOUT_VERBOSE("Layout 2\n");

	LayoutInput in;
	in.ParentWidth   = IntToPos(Doc->UI.GetViewportWidth());
	in.ParentHeight  = IntToPos(Doc->UI.GetViewportHeight());
	in.ParentRNode   = &root;
	in.RestartPoints = nullptr;

	LayoutOutput out;

	Boxer.BeginDocument();
	RunNode(&Doc->Root, in, out);
	Boxer.EndDocument();
}

void Layout::RunNode(const DomNode* node, const LayoutInput& in, LayoutOutput& out) {
	static int debug_num_run;
	debug_num_run++;

	BoxLayout::NodeInput boxIn;

	StyleResolver::ResolveAndPush(Stack, node);

	Box         margin        = ComputeBox(in.ParentWidth, in.ParentHeight, CatMargin_Left);
	Box         padding       = ComputeBox(in.ParentWidth, in.ParentHeight, CatPadding_Left);
	Box         border        = ComputeBox(in.ParentWidth, in.ParentHeight, CatBorder_Left);
	Pos         contentWidth  = ComputeDimension(in.ParentWidth, CatWidth);
	Pos         contentHeight = ComputeDimension(in.ParentHeight, CatHeight);
	BoxSizeType boxSizeType   = Stack.Get(CatBoxSizing).GetBoxSizing();

	if (boxSizeType == BoxSizeMargin) {
		if (contentWidth != PosNULL)
			contentWidth -= margin.Left + margin.Right + padding.Left + padding.Right + border.Left + border.Right;
		if (contentHeight != PosNULL)
			contentHeight -= margin.Top + margin.Bottom + padding.Top + padding.Bottom + border.Top + border.Bottom;
	} else if (boxSizeType == BoxSizeBorder) {
		if (contentWidth != PosNULL)
			contentWidth -= padding.Left + padding.Right + border.Left + border.Right;
		if (contentHeight != PosNULL)
			contentHeight -= padding.Top + padding.Bottom + border.Top + border.Bottom;
	}

	BreakType myBreak = Stack.Get(CatBreak).GetBreakType();
	if (myBreak == BreakBefore && in.RestartPoints->size() == 0)
		Boxer.Linebreak();

	if (SnapBoxes) {
		if (IsDefined(contentWidth))
			contentWidth = PosRoundUp(contentWidth);
		if (IsDefined(contentHeight))
			contentHeight = PosRoundUp(contentHeight);
	}

	RenderDomNode* rnode = new (Pool->AllocT<RenderDomNode>(false)) RenderDomNode(node->GetInternalID(), node->GetTag(), Pool);
	in.ParentRNode->Children += rnode;

	boxIn.InternalID          = node->GetInternalID();
	boxIn.Tag                 = node->GetTag();
	boxIn.ContentWidth        = contentWidth;
	boxIn.ContentHeight       = contentHeight;
	boxIn.MarginBorderPadding = margin.PiecewiseSum(border).PiecewiseSum(padding);
	boxIn.NewFlowContext      = Stack.Get(CatFlowContext).GetFlowContext() == FlowContextNew || node->GetTag() == TagBody; // Body MUST be a new flow context
	boxIn.Bump                = Stack.Get(CatBump).GetBump();

	cheapvec<int32_t> myRestartPoints;

	LayoutInput childIn;
	childIn.ParentWidth  = contentWidth;
	childIn.ParentHeight = contentHeight;
	childIn.ParentRNode  = rnode;
	if (boxIn.NewFlowContext)
		childIn.RestartPoints = &myRestartPoints;
	else
		childIn.RestartPoints = in.RestartPoints;

	Boxer.BeginNode(boxIn);

	size_t istart = 0;
	if (childIn.RestartPoints->size() != 0)
		istart = childIn.RestartPoints->rpop();

	// Remember that childOuts can be larger than node->ChildCount(), due to restarts.
	// childOuts contains a unique entry for every generated render-node.
	FixedVector<LayoutOutput> childOuts(FHeap);

	// Understanding RestartPoints
	// RestartPoints can be difficult to understand because they are an in/out parameter
	// that is carried inside childIn. Combine the in/out with recursion, and it is easy
	// to lose track of what is going on. It turns out it's simple enough to understand:
	// If RestartPoints are not empty, then calling RunNode or RunText should "use up"
	// the entire RestartPoints array. It gets used up deep, not wide. If RunNode or
	// RunText returns with a non-empty RestartPoints, it means a node inside that child
	// has begun a new restart.

	for (size_t i = istart; i < node->ChildCount(); i++) {
		LayoutOutput childOut;
		const DomEl* c = node->ChildByIndex(i);
		if (c->IsNode()) {
			RunNode(static_cast<const DomNode*>(c), childIn, childOut);
		} else {
			RunText(static_cast<const DomText*>(c), childIn, childOut);
		}

		bool isRestarting       = childIn.RestartPoints->size() != 0;
		bool isRestartAllZeroes = IsAllZeros(*childIn.RestartPoints);

		if (!(isRestarting && isRestartAllZeroes)) {
			// Notify the boxer of this new node's baseline and index
			Boxer.NotifyNodeEmitted(childOut.BaselinePlusRNodeTop(), (int) childOuts.Size());
			//Boxer.NotifyNodeEmitted(childOut.Baseline, (int) childOuts.Size());
			childOuts.Push(childOut);

			if (childOut.Break == BreakAfter)
				Boxer.Linebreak();
		}

		if (isRestarting) {
			// Child is breaking out. Continue to break out until we hit a NewFlowContext
			if (boxIn.NewFlowContext) {
				// We are the final stop on a restart. So restart at our current child.
				i--;
				Boxer.Linebreak();
				Boxer.Restart();
				// If all children are going to restart at zero, then delete the rnode that we already
				// created, because it will consist purely of empty husks. Unfortunately the empty children
				// end up as garbage memory in our render pool, but hopefully that's not a significant waste.
				if (IsAllZeros(*childIn.RestartPoints))
					rnode->Children.Count--;
			} else {
				// We are just an intermediate node along the way
				childIn.RestartPoints->push((int32_t) i);
				break;
			}
		}
	}

	// I don't know yet how to think about having a restart initiated here. So far
	// I have only thought about the case where a restart is initiated from a word
	// emitted by a text object.
	Box  marginBox;
	bool restart = Boxer.EndNode(marginBox) == BoxLayout::FlowRestart;
	if (restart) {
		XO_DIE_MSG("Untested layout restart position");
		in.RestartPoints->push(0);
	}

	// Boxer doesn't know what our element's padding or margins are. The only thing it
	// emits is the margin-box for our element. We need to subtract the margin and
	// the padding in order to compute the content-box, which is what RenderDomNode needs.
	rnode->Pos = marginBox.ShrunkBy(boxIn.MarginBorderPadding);
	rnode->SetStyle(Stack);
	rnode->Style.BorderRadius = PosToReal(ComputeDimension(in.ParentWidth, CatBorderRadius));
	rnode->Style.BorderSize   = border;
	rnode->Style.Padding      = padding;

	// Apply alignment bindings
	if (childIn.ParentWidth == PosNULL)
		childIn.ParentWidth = rnode->Pos.Width();

	if (childIn.ParentHeight == PosNULL)
		childIn.ParentHeight = rnode->Pos.Height();

	// This tracks the line that we're on. The boxer keeps track of the last entity that got placed
	// on every line, and we use that information to figure out which line our child is on.
	int  linebox_index = 0;
	auto linebox       = Boxer.GetLineFromPreviousNode(linebox_index);
	for (size_t i = 0; i < childOuts.Size(); i++) {
		while (i > linebox->LastChild)
			linebox = Boxer.GetLineFromPreviousNode(++linebox_index);

		if (childOuts[i].RNode != nullptr) {
			Point offset = PositionChildFromBindings(childIn, linebox->InnerBaseline, childOuts[i]);
			if (linebox->InnerBaselineDefinedBy == i) {
				// The child that originally defined the baseline has been moved, so we need to move the baseline along with it
				linebox->InnerBaseline += offset.Y;
			}
		}
	}

	// This node's baseline is the baseline of it's first linebox. Note that if the baseline has been moved
	// by the previous alignment phase, then we will receive the updated baseline here.
	Pos myBaseline = Boxer.GetLineFromPreviousNode(0)->InnerBaseline;

	if (myBaseline != PosNULL)
		out.Baseline = myBaseline; // +rnode->Pos.Top;	// we emit baseline in the coordinate system of our parent
	else
		out.Baseline = PosNULL;

	out.Binds           = ComputeBinds();
	out.RNode           = rnode;
	out.MarginBoxWidth  = marginBox.Width();
	out.MarginBoxHeight = marginBox.Height();
	out.Break           = Stack.Get(CatBreak).GetBreakType();

	Stack.StackPop();
}

void Layout::RunText(const DomText* node, const LayoutInput& in, LayoutOutput& out) {
	//XOTRACE_LAYOUT_VERBOSE( "Layout text (%d) Run 1\n", node.GetInternalID() );

	FontID fontID = Stack.Get(CatFontFamily).GetFont();

	StyleAttrib fontSizeAttrib = Stack.Get(CatFontSize);
	Pos         fontHeight     = ComputeDimension(in.ParentHeight, fontSizeAttrib.GetSize());

	float fontSizePxUnrounded = PosToReal(fontHeight);

	// round font size to integer units
	uint8_t fontSizePx = (uint8_t) Round(fontSizePxUnrounded);

	// Nothing prevents somebody from setting a font size to zero
	if (fontSizePx < 1)
		return;

	TempText.Node           = node;
	TempText.RNode          = in.ParentRNode;
	TempText.RNodeTxt       = nullptr;
	TempText.FontWidthScale = 1.0f;
	TempText.IsSubPixel     = Global()->EnableSubpixelText && fontSizePx <= Global()->MaxSubpixelGlyphSize;
	TempText.FontID         = fontID;
	TempText.FontSizePx     = fontSizePx;
	TempText.Color          = Stack.Get(CatColor).GetColor();
	TempText.RestartPoints  = in.RestartPoints;
	TempText.FontAscender   = PosNULL;
	GenerateTextWords(TempText);

	// If we are restarting output, then it's possible that some characters in the
	// buffer were not flushed. So just wipe the buffer always.
	TempText.Chars.Clear();

	// It makes sense to bind text words on baseline. The extremely simply document
	// "<span style='padding: 10px'>something</span> else" would not have 'else' aligned
	// to 'something' if we didn't align words to baseline.
	// Since we only bind on baseline, we don't need to populate width and height
	if (TempText.RNodeTxt != nullptr)
		out.Baseline = TempText.FontAscender;
	else
		out.Baseline = PosNULL;
	out.MarginBoxHeight      = 0;
	out.MarginBoxWidth       = 0;
	out.Binds                = BindingSet();
	out.Binds.VChildBaseline = VerticalBindingBaseline;
	out.RNode                = TempText.RNodeTxt;
	out.Break                = BreakNULL;
}

/*
Calling FinishTextRNode:
The order of events here can be a little bit confusing. We need to
do it this way to ensure that we only write out the characters into
the RenderDomText element once. The simpler approach would be to
append characters to RenderDomText after every word, but that
appending involves growing a vector, so plenty of memory reallocs.
Instead, we queue up a string of characters and write them all
out at once, when we either detect a new line, or when we are done
with the entire text object.
Having this tight coupling between Layout3 and BoxLayout is
unfortunate. Perhaps if all the dust has settled, then it might
be worth it to break that coupling, to the degree that Layout3
doesn't know where its word boxes are going - it just dumps them
to BoxLayout and forgets about them.
This would mean that BoxLayout is responsible for adjusting
the positions of the glyphs inside an RenderDomText object.

NOTE: This function has some defunct history to it. It was originally
written such that it would output words on different lines.
Since implementing "restarts" though, this function will never
emit more than a single text object, and that single text object
will always be on one line only. We should clean this function up.

This diagram was created using asciiflow (http://asciiflow.com/)

               +�����������������������������������+  XXXXXXXXXXXX
            XX |      X       X                    |             X
            X  |      X       X                    |             X
            X  |      X       X                    |             X
            X  |      X       X                    |             X
 ascender   X  |      XXXXXXXXX     XXXXXXXX       |             X
            X  |      X       X     X      X       |             X
            X  |      X       X     X      X       |             X lineheight
            XX |      X       X     XXXXXXXX       |             X
               +�������������������+X+��������������+ baseline   X
            XX |                    X              |             X
descender   X  |                    X              |             X
            X  |                    X              |             X
            XX |                    X              |             X
               |                                   |             X
               +�����������������������������������+  XXXXXXXXXXXX

*/
void Layout::GenerateTextWords(TextRunState& ts) {
	XO_DEBUG_ASSERT(ts.Chars.Size() == 0);

	const char* txt        = ts.Node->GetText();
	int32_t     txt_offset = 0;
	if (ts.RestartPoints->size() != 0) {
		txt_offset = ts.RestartPoints->rpop();
		txt += txt_offset;
		XO_ASSERT(ts.RestartPoints->size() == 0); // Text is a leaf node. The restart stack must be empty now.
	}

	GlyphCache*   glyphCache = Global()->GlyphCache;
	GlyphCacheKey key        = MakeGlyphCacheKey(ts);
	const Font*   font       = Fonts.GetByFontID(ts.FontID);

	Pos fontHeightRounded = RealToPos((float) ts.FontSizePx);
	Pos charWidth_32      = Realx256ToPos(font->LinearHoriAdvance_Space_x256) * ts.FontSizePx;
	Pos fontAscender      = Realx256ToPos(font->Ascender_x256 * ts.FontSizePx);
	ts.FontAscender       = fontAscender;

	if (SnapSubpixelHorzText)
		charWidth_32 = PosRound(charWidth_32);

	// if we add a "line-height" style then we'll want to multiply that by this
	Pos lineHeight = Realx256ToPos(ts.FontSizePx * font->LineHeight_x256);
	if (Global()->RoundLineHeights)
		lineHeight = PosRoundUp(lineHeight);

	ts.GlyphsNeeded                = false;
	const Glyph*   prevGlyph       = nullptr;
	RenderDomText* rtxt            = nullptr;
	Pos            rtxt_left       = PosNULL;
	Pos            lastWordTop     = PosNULL;
	int            numCharsInQueue = 0; // Number of characters in TextRunState::Chars that are waiting to be flushed to rtxt
	bool           aborted         = false;
	Chunk          chunk;
	Chunker        chunker(txt);
	while (!aborted && chunker.Next(chunk)) {
		switch (chunk.Type) {
		case ChunkWord: {
			int32_t chunkLen  = chunk.End - chunk.Start;
			Pos     wordWidth = MeasureWord(txt, font, fontAscender, chunk, ts);

			if (ts.GlyphsNeeded)
				continue;

			// output word
			BoxLayout::WordInput wordin;
			wordin.Width  = wordWidth;
			wordin.Height = lineHeight;
			Box marginBox;
			if (Boxer.AddWord(wordin, marginBox) == BoxLayout::FlowRestart) {
				aborted = true;
				ts.RestartPoints->push(chunk.Start + txt_offset);
				break;
			}

			if (rtxt == nullptr || marginBox.Top != lastWordTop) {
				// We need a new output object
				RenderDomText* rtxt_new = new (Pool->AllocT<RenderDomText>(false)) RenderDomText(ts.Node->GetInternalID(), Pool);
				XO_ANALYSIS_ASSUME(rtxt_new != nullptr);
				ts.RNode->Children += rtxt_new;
				if (rtxt != nullptr) {
					// retire previous text object - which is all characters in the queue, except for the most recent word
					FinishTextRNode(ts, rtxt, numCharsInQueue);
				}
				rtxt_new->Pos   = marginBox;
				rtxt            = rtxt_new;
				rtxt_left       = marginBox.Left;
				lastWordTop     = marginBox.Top;
				numCharsInQueue = chunkLen;
			} else {
				// another word on existing line
				numCharsInQueue += chunkLen;
				OffsetTextHorz(ts, marginBox.Left - rtxt_left, chunkLen);
			}
		} break;
		case ChunkSpace:
			Boxer.AddSpace(charWidth_32);
			break;
		case ChunkLineBreak: {
			aborted = true;
			Boxer.AddNewLineCharacter(lineHeight);
			ts.RestartPoints->push(chunk.Start + txt_offset + 1);
			break;
		}
		}
	}
	// the end
	if (rtxt != nullptr)
		FinishTextRNode(ts, rtxt, numCharsInQueue);
	ts.RNodeTxt = rtxt;
}

void Layout::FinishTextRNode(TextRunState& ts, RenderDomText* rnode, size_t numChars) {
	rnode->FontID     = ts.FontID;
	rnode->Color      = ts.Color;
	rnode->FontSizePx = ts.FontSizePx;
	if (ts.IsSubPixel)
		rnode->Flags |= RenderDomText::FlagSubPixelGlyphs;

	XO_ASSERT(ts.Chars.Size() >= numChars);

	rnode->Text.resize(numChars);
	for (size_t i = 0; i < numChars; i++)
		rnode->Text[i] = ts.Chars.PopTail();
}

void Layout::OffsetTextHorz(TextRunState& ts, Pos offsetHorz, size_t numChars) {
	for (size_t i = 0; i < numChars; i++)
		ts.Chars.FromHead((int) i).X += offsetHorz;
}

// While measuring the length of the word, we are also recording its character placements.
// All characters go into a queue, which gets flushed whenever we flow onto a new line.
// Returns the width of the word
Pos Layout::MeasureWord(const char* txt, const Font* font, Pos fontAscender, Chunk chunk, TextRunState& ts) {
	// I find it easier to understand when referring to this value as "baseline" instead of "ascender"
	Pos baseline = fontAscender;

	Pos           posX      = 0;
	GlyphCacheKey key       = MakeGlyphCacheKey(ts);
	const Glyph*  prevGlyph = nullptr;

	for (int32_t i = chunk.Start; i < chunk.End; i++) {
		key.Char           = txt[i];
		const Glyph* glyph = Global()->GlyphCache->GetGlyph(key);
		if (!glyph) {
			ts.GlyphsNeeded = true;
			GlyphsNeeded.insert(key);
			continue;
		}
		if (glyph->IsNull()) {
			// TODO: Handle missing glyph by drawing a rectangle or something
			continue;
			prevGlyph = nullptr;
		}
		if (EnableKerning && prevGlyph) {
			// Multithreading hazard here. I'm not sure whether FT_Get_Kerning is thread safe.
			// Also, I have stepped inside there and I see it does a binary search. We might
			// be better off caching the kerning for frequent pairs of glyphs in a hash table.
			FT_Vector kern;
			FT_Get_Kerning(font->FTFace, prevGlyph->FTGlyphIndex, glyph->FTGlyphIndex, FT_KERNING_UNSCALED, &kern);
			Pos kerning = ((kern.x * ts.FontSizePx) << PosShift) / font->FTFace->units_per_EM;
			posX += kerning;
		}

		RenderCharEl& rtxt = ts.Chars.PushHead();
		rtxt.Char          = key.Char;
		rtxt.X             = posX + Realx256ToPos(glyph->MetricLeftx256);
		rtxt.Y             = baseline - RealToPos(glyph->MetricTop); // rtxt.Y is the top of the glyph bitmap. glyph->MetricTop is the distance from the baseline to the top of the glyph
		// For determining the word width, one might want to not use the horizontal advance for the very last glyph, but instead
		// use the glyph's exact width. The difference would be tiny, and it may even be annoying, because you would end up with
		// no padding on the right side of a word.
		posX += HoriAdvance(glyph, ts);
		prevGlyph = glyph;
	}
	return posX;
}

Point Layout::PositionChildFromBindings(const LayoutInput& cin, Pos parentBaseline, LayoutOutput& cout) {
	Point retval(0, 0);

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Horizontal
	HBindHelper helperH(cin.ParentWidth, cout.RNode->Pos.Left, cout.MarginBoxWidth);

	// If HCenter is bound, then move the child to the binding point
	Pos moveX = helperH.Delta(cout.Binds.HChildCenter, HorizontalBindingCenter);

	if (IsDefined(moveX)) {
		cout.RNode->Pos.Offset(moveX, 0);
		helperH.ChildLeft += moveX;
		retval.X = moveX;
	}

	Pos leftDelta  = helperH.Delta(cout.Binds.HChildLeft, HorizontalBindingLeft);
	Pos rightDelta = helperH.Delta(cout.Binds.HChildRight, HorizontalBindingRight);
	if (IsDefined(leftDelta)) {
		if (IsDefined(moveX) || IsDefined(rightDelta)) {
			// stretch to left. don't change absolute position of children of cout.RNode
			MoveLeftTop(cout.RNode, Point(leftDelta, 0));
			helperH.ChildLeft += leftDelta;
			helperH.ChildWidth -= leftDelta;
		} else {
			// Move to left
			cout.RNode->Pos.Offset(leftDelta, 0);
			retval.X = leftDelta;
		}
	}

	if (IsDefined(rightDelta)) {
		if (IsDefined(moveX) || IsDefined(leftDelta)) {
			// Stretch to right. Since we're not changing our reference frame, this is simpler than stretching the top.
			cout.RNode->Pos.Right += rightDelta;
		} else {
			// Move to right
			cout.RNode->Pos.Offset(rightDelta, 0);
			retval.X = rightDelta;
		}
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Vertical
	VBindHelper helperV(cin.ParentHeight, parentBaseline, cout.RNode->Pos.Top, cout.MarginBoxHeight, cout.Baseline);

	Pos moveY = PosNULL;

	// If either VCenter or Baseline were bound, then move the child to the binding point
	moveY = helperV.Delta(cout.Binds.VChildBaseline, VerticalBindingBaseline);
	if (!IsDefined(moveY))
		moveY = helperV.Delta(cout.Binds.VChildCenter, VerticalBindingCenter);

	if (IsDefined(moveY)) {
		cout.RNode->Pos.Offset(0, moveY);
		helperV.ChildTop += moveY;
		retval.Y = moveY;
	}

	Pos topDelta    = helperV.Delta(cout.Binds.VChildTop, VerticalBindingTop);
	Pos bottomDelta = helperV.Delta(cout.Binds.VChildBottom, VerticalBindingBottom);
	if (IsDefined(topDelta)) {
		if (IsDefined(moveY) || IsDefined(bottomDelta)) {
			// stretch to top. don't change absolute position of children of cout.RNode
			MoveLeftTop(cout.RNode, Point(0, topDelta));
			cout.Baseline -= topDelta;
			helperV.ChildTop += topDelta;
			helperV.ChildHeight -= topDelta;
			helperV.ChildBaseline -= topDelta;
		} else {
			// Move to top
			cout.RNode->Pos.Offset(0, topDelta);
			retval.Y = topDelta;
		}
	}

	if (IsDefined(bottomDelta)) {
		if (IsDefined(moveY) || IsDefined(topDelta)) {
			// Stretch to bottom. Since we're not changing our reference frame, this is simpler than stretching the top.
			cout.RNode->Pos.Bottom += bottomDelta;
		} else {
			// Move to bottom
			cout.RNode->Pos.Offset(0, bottomDelta);
			retval.Y = bottomDelta;
		}
	}

	return retval;
}

Pos Layout::ComputeDimension(Pos container, StyleCategories cat) {
	return ComputeDimension(container, Stack.Get(cat).GetSize());
}

Pos Layout::ComputeDimension(Pos container, Size size) {
	switch (size.Type) {
	case Size::NONE: return PosNULL;
	case Size::PX: return RealToPos(size.Val);
	case Size::PT: return RealToPos(size.Val * PtToPixel);
	case Size::EP: return RealToPos(size.Val * EpToPixel);
	case Size::PERCENT:
		if (container == PosNULL)
			return PosNULL;
		else
			return Pos(Round((float) container * (size.Val * 0.01f))); // this might be sloppy floating point. Small rational percentages like 25% (1/4) ought to be preserved precisely.
	default: XO_DIE_MSG("Unrecognized size type"); return 0;
	}
}

Box Layout::ComputeBox(Pos containerWidth, Pos containerHeight, StyleCategories cat) {
	return ComputeBox(containerWidth, containerHeight, Stack.GetBox(cat));
}

Box Layout::ComputeBox(Pos containerWidth, Pos containerHeight, StyleBox box) {
	Box b;
	b.Left   = ComputeDimension(containerWidth, box.Left);
	b.Right  = ComputeDimension(containerWidth, box.Right);
	b.Top    = ComputeDimension(containerHeight, box.Top);
	b.Bottom = ComputeDimension(containerHeight, box.Bottom);
	return b;
}

Layout::BindingSet Layout::ComputeBinds() {
	BindingSet binds;

	binds.HChildLeft   = Stack.Get(CatLeft).GetHorizontalBinding();
	binds.HChildCenter = Stack.Get(CatHCenter).GetHorizontalBinding();
	binds.HChildRight  = Stack.Get(CatRight).GetHorizontalBinding();

	binds.VChildTop      = Stack.Get(CatTop).GetVerticalBinding();
	binds.VChildCenter   = Stack.Get(CatVCenter).GetVerticalBinding();
	binds.VChildBottom   = Stack.Get(CatBottom).GetVerticalBinding();
	binds.VChildBaseline = Stack.Get(CatBaseline).GetVerticalBinding();

	return binds;
}

Pos Layout::HoriAdvance(const Glyph* glyph, const TextRunState& ts) {
	if (SnapSubpixelHorzText)
		return IntToPos(glyph->MetricHoriAdvance);
	else
		return RealToPos(glyph->MetricLinearHoriAdvance * ts.FontWidthScale);
}

Pos Layout::HBindOffset(HorizontalBindings bind, Pos left, Pos width) {
	switch (bind) {
	case HorizontalBindingNULL:
	case HorizontalBindingLeft: return left;
	case HorizontalBindingCenter: return left + width / 2;
	case HorizontalBindingRight: return left + width;
	default:
		XO_DEBUG_ASSERT(false);
		return 0;
	}
}

Pos Layout::VBindOffset(VerticalBindings bind, Pos top, Pos baseline, Pos height) {
	switch (bind) {
	case VerticalBindingNULL:
	case VerticalBindingTop: return top;
	case VerticalBindingCenter: return top + height / 2;
	case VerticalBindingBottom: return top + height;
	case VerticalBindingBaseline:
		if (IsDefined(baseline))
			return top + baseline;
		else {
			// This occurs often enough that it's not too noisy to be useful
			//XOTRACE_LAYOUT_WARNING("Undefined baseline used in alignment\n");
			return PosNULL;
		}
	default:
		XO_DEBUG_ASSERT(false);
		return 0;
	}
}

bool Layout::IsSpace(int ch) {
	return ch == 32;
}

bool Layout::IsLinebreak(int ch) {
	return ch == '\r' || ch == '\n';
}

GlyphCacheKey Layout::MakeGlyphCacheKey(RenderDomText* rnode) {
	uint8_t glyphFlags = rnode->IsSubPixel() ? GlyphFlag_SubPixel_RGB : 0;
	return GlyphCacheKey(rnode->FontID, 0, rnode->FontSizePx, glyphFlags);
}

GlyphCacheKey Layout::MakeGlyphCacheKey(const TextRunState& ts) {
	return MakeGlyphCacheKey(ts.IsSubPixel, ts.FontID, ts.FontSizePx);
}

GlyphCacheKey Layout::MakeGlyphCacheKey(bool isSubPixel, FontID fontID, int fontSizePx) {
	uint8_t glyphFlags = isSubPixel ? GlyphFlag_SubPixel_RGB : 0;
	return GlyphCacheKey(fontID, 0, fontSizePx, glyphFlags);
}

bool Layout::IsAllZeros(const cheapvec<int32_t>& list) {
	for (size_t i = 0; i < list.size(); i++) {
		if (list[i] != 0)
			return false;
	}
	return true;
}

void Layout::MoveLeftTop(RenderDomEl* relem, Point delta) {
	relem->Pos.Left += delta.X;
	relem->Pos.Top += delta.Y;

	RenderDomNode* rnode = relem->ToNode();
	if (rnode != nullptr) {
		for (size_t i = 0; i < rnode->Children.size(); i++)
			rnode->Children[i]->Pos.Offset(-delta.X, -delta.Y);
	} else {
		// do we need to do anything here?
		//RenderDomText* rtxt = relem->ToText();
	}
}

Pos Layout::LayoutOutput::BaselinePlusRNodeTop() const {
	return Baseline == PosNULL ? PosNULL : Baseline + RNode->Pos.Top;
}

Layout::Chunker::Chunker(const char* txt) : Txt(txt),
                                            Pos(0) {
}

bool Layout::Chunker::Next(Chunk& c) {
	if (Txt[Pos] == 0)
		return false;

	char first = Txt[Pos];
	c.Start    = Pos;
	switch (first) {
	case 9:
	case 32:
		c.Type = ChunkSpace;
		for (; Txt[Pos] == first; Pos++) {
		}
		break;
	case '\r':
		c.Type = ChunkLineBreak;
		if (Txt[Pos] == '\n')
			Pos += 2;
		else
			Pos += 1;
		break;
	case '\n':
		c.Type = ChunkLineBreak;
		Pos++;
		break;
	default:
		c.Type = ChunkWord;
		while (true) {
			Pos++;
			char ch = Txt[Pos];
			if (ch == 0 || ch == 9 || ch == 32 || ch == '\r' || ch == '\n')
				break;
		}
	}
	c.End = Pos;
	return true;
}

Pos Layout::VBindHelper::Parent(VerticalBindings bind) {
	return Layout::VBindOffset(bind, 0, ParentBaseline, ParentHeight);
}

Pos Layout::VBindHelper::Child(VerticalBindings bind) {
	return Layout::VBindOffset(bind, ChildTop, ChildBaseline, ChildHeight);
}

Pos Layout::VBindHelper::Delta(VerticalBindings parent, VerticalBindings child) {
	if (parent == VerticalBindingNULL || child == VerticalBindingNULL)
		return PosNULL;
	Pos p = Parent(parent);
	Pos c = Child(child);
	if (IsDefined(p) && IsDefined(c))
		return p - c;
	return PosNULL;
}

Pos Layout::HBindHelper::Parent(HorizontalBindings bind) {
	return Layout::HBindOffset(bind, 0, ParentWidth);
}

Pos Layout::HBindHelper::Child(HorizontalBindings bind) {
	return Layout::HBindOffset(bind, ChildLeft, ChildWidth);
}

Pos Layout::HBindHelper::Delta(HorizontalBindings parent, HorizontalBindings child) {
	if (parent == HorizontalBindingNULL || child == HorizontalBindingNULL)
		return PosNULL;
	Pos p = Parent(parent);
	Pos c = Child(child);
	if (IsDefined(p) && IsDefined(c))
		return p - c;
	return PosNULL;
}
}