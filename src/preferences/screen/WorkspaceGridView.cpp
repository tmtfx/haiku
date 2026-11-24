/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */


#include "WorkspaceGridView.h"

#include <Font.h>


const float WorkspaceGridView::kCellWidth = 28.0f;
const float WorkspaceGridView::kCellHeight = 21.0f;
const float WorkspaceGridView::kCellGap = 6.0f;
const float WorkspaceGridView::kSectionGap = 10.0f;


WorkspaceGridView::WorkspaceGridView(const char* name)
	:
	BView(name, B_WILL_DRAW),
	fColumns(1),
	fRows(1)
{
}


WorkspaceGridView::~WorkspaceGridView()
{
}


void
WorkspaceGridView::AttachedToWindow()
{
	SetViewColor(B_TRANSPARENT_COLOR);
	SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
}


void
WorkspaceGridView::SetGridLayout(int32 columns, int32 rows,
	const BStringList& gpuNames)
{
	fColumns = columns > 0 ? columns : 1;
	fRows = rows > 0 ? rows : 1;
	fGpuNames = gpuNames;

	InvalidateLayout();
	Invalidate();
}


int32
WorkspaceGridView::_RowsForGpu(int32 gpu) const
{
	int32 gpuCount = fGpuNames.CountStrings();
	if (gpuCount <= 0)
		gpuCount = 1;

	int32 count = 0;
	for (int32 row = 0; row < fRows; row++) {
		if (row % gpuCount == gpu)
			count++;
	}
	return count;
}


float
WorkspaceGridView::_LabelColumnWidth() const
{
	int32 gpuCount = fGpuNames.CountStrings();
	if (gpuCount <= 0)
		return 0.0f;

	float maxWidth = 0.0f;
	for (int32 i = 0; i < gpuCount; i++) {
		float width = StringWidth(fGpuNames.StringAt(i));
		if (width > maxWidth)
			maxWidth = width;
	}

	return maxWidth > 0.0f ? maxWidth + 12.0f : 0.0f;
}


void
WorkspaceGridView::GetPreferredSize(float* _width, float* _height)
{
	int32 gpuCount = fGpuNames.CountStrings();
	if (gpuCount <= 0)
		gpuCount = 1;

	float width = _LabelColumnWidth()
		+ fColumns * kCellWidth + (fColumns - 1) * kCellGap;

	float height = 0.0f;
	for (int32 gpu = 0; gpu < gpuCount; gpu++) {
		int32 rows = _RowsForGpu(gpu);
		if (rows == 0)
			continue;
		if (height > 0.0f)
			height += kSectionGap;
		height += rows * kCellHeight + (rows - 1) * kCellGap;
	}

	if (_width != NULL)
		*_width = width;
	if (_height != NULL)
		*_height = height;
}


void
WorkspaceGridView::Draw(BRect updateRect)
{
	rgb_color darkColor = {160, 160, 160, 255};
	rgb_color blackColor = {0, 0, 0, 255};
	// Schematic only -- not meant to reflect any one workspace's actual
	// desktop color (unlike the single-monitor preview in each GPU tab),
	// so a fixed neutral tone is fine here.
	rgb_color deskColor = {58, 95, 158, 255};
	rgb_color textColor = ui_color(B_PANEL_TEXT_COLOR);

	SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	FillRect(updateRect);

	int32 gpuCount = fGpuNames.CountStrings();
	if (gpuCount <= 0)
		gpuCount = 1;

	float labelWidth = _LabelColumnWidth();

	font_height fontHeight;
	GetFontHeight(&fontHeight);
	float textAscent = fontHeight.ascent;
	float textLineHeight = ceilf(fontHeight.ascent + fontHeight.descent);

	float y = 0.0f;
	for (int32 gpu = 0; gpu < gpuCount; gpu++) {
		int32 rows = _RowsForGpu(gpu);
		if (rows == 0)
			continue;

		float sectionHeight = rows * kCellHeight + (rows - 1) * kCellGap;

		if (labelWidth > 0.0f && gpu < fGpuNames.CountStrings()) {
			SetHighColor(textColor);
			SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
			DrawString(fGpuNames.StringAt(gpu),
				BPoint(0.0f, y + (sectionHeight - textLineHeight) / 2
					+ textAscent));
		}

		for (int32 row = 0; row < rows; row++) {
			float cellTop = y + row * (kCellHeight + kCellGap);

			for (int32 column = 0; column < fColumns; column++) {
				float cellLeft = labelWidth
					+ column * (kCellWidth + kCellGap);
				BRect outer(cellLeft, cellTop, cellLeft + kCellWidth,
					cellTop + kCellHeight);

				SetDrawingMode(B_OP_COPY);
				SetHighColor(darkColor);
				FillRoundRect(outer, 2.0, 2.0);
				SetHighColor(blackColor);
				StrokeRoundRect(outer, 2.0, 2.0);

				BRect inner(outer.InsetByCopy(2, 2));
				SetHighColor(deskColor);
				FillRoundRect(inner, 1.0, 1.0);
				SetHighColor(blackColor);
				StrokeRoundRect(inner, 1.0, 1.0);
			}
		}

		y += sectionHeight + kSectionGap;
	}
}
