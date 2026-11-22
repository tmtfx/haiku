/*
 * SpielBar v3 - elementi della lista: intestazioni, voci, volumi
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_LISTITEMS_H
#define SPIELBAR_LISTITEMS_H

#include <Bitmap.h>
#include <Entry.h>
#include <Font.h>
#include <GraphicsDefs.h>
#include <InterfaceDefs.h>
#include <ListItem.h>
#include <Node.h>
#include <Point.h>
#include <Rect.h>
#include <Size.h>
#include <String.h>
#include <StringItem.h>
#include <SupportDefs.h>
#include <View.h>
#include <math.h>
#include <algorithm>

#include "SpielBarTypes.h"
#include "SpielBarTheme.h"
#include "UiMetrics.h"
#include "Emblems.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


// -------------------------------------------------------------
// Lista
// -------------------------------------------------------------

class SectionHeaderItem : public BListItem {
public:
	SectionHeaderItem(const char* label)
		: BListItem(0, true)
		, fLabel(label)
		, fSpaced(false)
	{
		SetEnabled(false);	// non selezionabile

		// Pseudo maiuscoletto: maiuscolo, piccolo e leggermente spaziato.
		// Solo per etichette ASCII: ToUpper non e' affidabile con gli
		// accenti e gli alfabeti non latini, che restano come tradotti.
		bool ascii = true;
		for (const char* p = label; *p != '\0'; p++) {
			if ((unsigned char)*p > 127) {
				ascii = false;
				break;
			}
		}
		if (ascii) {
			fLabel.ToUpper();
			fSpaced = true;
		}
	}

	void DrawItem(BView* owner, BRect frame, bool complete = false) override
	{
		rgb_color listBg = ui_color(B_LIST_BACKGROUND_COLOR);
		rgb_color bg = ContrastShade(listBg, 1);
		owner->SetDrawingMode(B_OP_COPY);
		owner->SetHighColor(bg);
		owner->FillRect(frame);

		BFont font(be_plain_font);
		font.SetFace(B_BOLD_FACE);
		font.SetSize(font.Size() * 0.85f);
		owner->SetFont(&font);
		font_height fh;
		font.GetHeight(&fh);

		owner->SetHighColor(MutedText(ui_color(B_LIST_ITEM_TEXT_COLOR),
			bg, 4.6f));
		owner->SetLowColor(bg);
		float baseline = frame.top
			+ (frame.Height() + fh.ascent - fh.descent) / 2;

		if (fSpaced) {
			float x = frame.left + 10;
			for (int32 i = 0; i < fLabel.Length(); i++) {
				char one[2] = { fLabel[i], '\0' };
				owner->DrawString(one, BPoint(x, baseline));
				x += font.StringWidth(one) + 0.8f;
			}
		} else {
			owner->DrawString(fLabel.String(), BPoint(frame.left + 10, baseline));
		}

		owner->SetHighColor(ContrastShade(listBg, 2));
		owner->StrokeLine(BPoint(frame.left, frame.bottom),
			BPoint(frame.right, frame.bottom));
		owner->SetFont(be_plain_font);
	}

	void Update(BView* owner, const BFont* font) override
	{
		BListItem::Update(owner, font);
		SetHeight(UI().headerHeight);
	}

private:
	BString fLabel;
	bool fSpaced;
};

class UnifiedListItem : public BStringItem {
public:
	UnifiedListItem(const char* name, const char* path,
			const entry_ref& ref = entry_ref(),
			const char* sig = nullptr,
			BBitmap* icon = nullptr,
			const char* symlinkPath = nullptr)
		: BStringItem("")
		, fName(name)
		, fPath(path)
		, fRef(ref)
		, fSignature(sig ? sig : "")
		, fIcon(icon)
		, fSymlinkPath(symlinkPath ? symlinkPath : "")
		, fKind(kKindFile)
		, fItemFlags(0)
		, fFileName(ref.name ? ref.name : "")
		, fHideRecent(false)
	{
		fNode.device = -1;
		fNode.node = -1;
	}

	~UnifiedListItem() { delete fIcon; }

	const BString& SymlinkPath() const { return fSymlinkPath; }
	const BString& Name() const { return fName; }
	const BString& FileName() const { return fFileName; }
	const BString& Path() const { return fPath; }
	const entry_ref& Ref() const { return fRef; }
	const BString& Signature() const { return fSignature; }
	BBitmap* Icon() const { return fIcon; }
	uint32 Kind() const { return fKind; }
	uint32 ItemFlags() const { return fItemFlags; }
	const node_ref& Node() const { return fNode; }

	void SetKind(uint32 kind) { fKind = kind; }
	void SetItemFlags(uint32 flags) { fItemFlags = flags; }
	void SetNode(const node_ref& node) { fNode = node; }
	void SetFileName(const char* name) { fFileName = name; }

	// L'orologio e' ridondante dove tutti gli elementi sono recenti
	// (sezione Recenti, filtro Recent): lo imposta la finestra a ogni rebuild.
	void SetHideRecent(bool hide) { fHideRecent = hide; }

	// Rettangolo della stella (a destra): grande quanto la sua bitmap, che
	// segue il font.
	static BRect StarRect(BRect frame)
	{
		float size = EmblemWidth(kEmbFavorite);
		if (size <= 0)
			size = (float)UI().starSize;
		float right = frame.right - (UI().rowPad + 4);
		float left = floorf(right - size + 1);
		float top = floorf(frame.top + (frame.Height() + 1 - size) / 2);
		return BRect(left, top, left + size - 1, top + size - 1);
	}

	void DrawItem(BView* owner, BRect frame, bool complete) override;

	void Update(BView* owner, const BFont* font) override
	{
		BStringItem::Update(owner, font);
		SetHeight((float)UI().rowHeight);
	}

protected:
	BString fName;
	BString fPath;
	entry_ref fRef;
	BString fSignature;
	BBitmap* fIcon;
	BString fSymlinkPath;
	uint32 fKind;
	uint32 fItemFlags;
	node_ref fNode;
	BString fFileName;
	bool fHideRecent;
};

class VolumeListItem : public UnifiedListItem {
public:
	VolumeListItem(const char* name, const char* info,
				   int32 partitionId, bool isMounted,
				   int32 percent,
				   BBitmap* icon = nullptr)
		: UnifiedListItem(name, info, entry_ref(), "", icon)
		, fPartitionId(partitionId)
		, fIsMounted(isMounted)
		, fPercent(percent)
	{
		SetKind(kKindVolume);
	}

	int32 PartitionId() const { return fPartitionId; }
	bool IsMounted() const { return fIsMounted; }
	int32 PercentUsed() const { return fPercent; }

	void DrawItem(BView* owner, BRect frame, bool complete) override
	{
		UnifiedListItem::DrawItem(owner, frame, complete);

		if (!fIsMounted || fPercent < 0)
			return;

		// Stessa area dell'icona di UnifiedListItem::DrawItem.
		const UiMetrics& ui = UI();
		BPoint where(frame.left + ui.rowPad,
			floorf(frame.top + (frame.Height() + 1 - ui.rowIcon) / 2));
		BSize size(ui.rowIcon, ui.rowIcon);

		_DrawBar(where, owner, size);
	}

private:
	int32 fPartitionId;
	bool fIsMounted;
	int32 fPercent;

	void _DrawBar(BPoint where, BView* view, BSize size)
	{
		view->PushState();

		int32 iconSize = size.IntegerWidth();
		int32 yOffset;
		int32 barWidth = (int32)(7.0f / 32.0f * (float)(iconSize + 1));
		if (barWidth < 4) {
			barWidth = 4;
			yOffset = 0;
		} else
			yOffset = 2;

		view->SetHighColor(32, 32, 32, 92);
		view->MovePenTo(BPoint(where.x + iconSize, where.y + 1 + yOffset));
		view->StrokeLine(BPoint(where.x + iconSize, where.y + iconSize - yOffset));
		view->StrokeLine(BPoint(where.x + iconSize - barWidth + 1,
			where.y + iconSize - yOffset));

		int32 barHeight = iconSize - 4 - 2 * yOffset;

		float left = where.x + iconSize - barWidth;
		float top = where.y + yOffset;
		float right = where.x + iconSize - 1;
		float bottom = where.y + iconSize - 1 - yOffset;
		BRect rect(left, top, right, bottom);

		view->SetDrawingMode(B_OP_ALPHA);

		view->SetHighColor(76, 76, 76, 192);
		view->StrokeRect(rect);

		int32 percent = std::max((int32)0, std::min((int32)100, fPercent));
		int32 barPos = int32(barHeight * percent / 100.0);

		rect.InsetBy(1, 1);

		BRect freeBar(rect);
		freeBar.bottom = rect.bottom - barPos;
		if (freeBar.bottom >= freeBar.top) {
			view->SetHighColor(255, 255, 255, 192);
			view->FillRect(freeBar);
		}

		BRect usedBar(rect);
		usedBar.top = rect.bottom - barPos + 1;
		if (usedBar.top <= usedBar.bottom) {
			if (percent >= 90)
				view->SetHighColor(203, 0, 0, 192);
			else
				view->SetHighColor(0, 203, 0, 192);
			view->FillRect(usedBar);
		}

		view->PopState();
	}
};

#endif	// SPIELBAR_LISTITEMS_H
