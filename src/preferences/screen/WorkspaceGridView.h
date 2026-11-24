/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#ifndef WORKSPACE_GRID_VIEW_H
#define WORKSPACE_GRID_VIEW_H


#include <ObjectList.h>
#include <StringList.h>
#include <View.h>


// Schematic preview of the workspace grid, grouped by which GPU drives
// each row (see MULTIGPU_PLAN.md Fase F / the "Workspaces" tab). Not a
// pixel-accurate map of workspace-switch order: rows are drawn grouped
// together by owning GPU (GPU 0's rows as one contiguous block, then
// GPU 1's, ...) rather than interleaved in literal row order, since that
// is what makes "this block of monitors belongs to this card" legible at
// a glance -- exactly the layout the project's examples describe (e.g.
// 3 rows / 2 columns / 2 GPUs: a 2x2 block for GPU 0, a 1x2 block for
// GPU 1 beneath it, each with its own label).
//
// Row-to-GPU assignment mirrors Desktop::_SetCurrentWorkspaceConfiguration()
// exactly: row R (0-indexed, R = workspace / columns) belongs to GPU
// R % gpuCount. With gpuCount == 1 every row belongs to the single GPU,
// same as today's behavior.
class WorkspaceGridView : public BView {
public:
							WorkspaceGridView(const char* name);
	virtual					~WorkspaceGridView();

	virtual	void			Draw(BRect updateRect);
	virtual	void			GetPreferredSize(float* _width, float* _height);
	virtual	void			AttachedToWindow();

			// \a gpuNames.CountStrings() is the number of active GPUs;
			// an empty list is treated as a single unnamed GPU (today's
			// single-GPU case).
			void			SetGridLayout(int32 columns, int32 rows,
								const BStringList& gpuNames);

private:
			int32			_RowsForGpu(int32 gpu) const;
			float			_LabelColumnWidth() const;

			int32			fColumns;
			int32			fRows;
			BStringList		fGpuNames;

			static const float kCellWidth;
			static const float kCellHeight;
			static const float kCellGap;
			static const float kSectionGap;
};

#endif	/* WORKSPACE_GRID_VIEW_H */
