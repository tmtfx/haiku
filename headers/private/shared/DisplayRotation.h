/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Geometry and display_mode transform helpers shared between HWInterface,
 * AccelerantHWInterface, and InputServer to convert between physical
 * (actual panel) and logical (rotated) coordinates and modes.
 */
#ifndef _DISPLAY_ROTATION_H
#define _DISPLAY_ROTATION_H

#include <Accelerant.h>
#include <Rect.h>
#include <Region.h>


namespace BPrivate {


/*!    Convert logical point to physical screen coordinates when rotated.
    Pass display bounds in logical (rotated) space.
    Returns physical coordinates for use in framebuffer operations.
*/
static inline BPoint
logical_to_physical_point(BPoint logicalPoint, BRect logicalBounds,
    int32 rotation)
{
    if (rotation == 0)
        return logicalPoint;

    BPoint physical = logicalPoint;
    switch (rotation) {
        case 90:
            // Rotate 90° CW: (x,y) → (y, W-x)
            physical.x = logicalPoint.y;
            physical.y = logicalBounds.Width() - logicalPoint.x;
            break;
        case 180:
            // Rotate 180°: (x,y) → (W-x, H-y)
            physical.x = logicalBounds.Width() - logicalPoint.x;
            physical.y = logicalBounds.Height() - logicalPoint.y;
            break;
        case 270:
            // Rotate 270° CW: (x,y) → (H-y, x)
            physical.x = logicalBounds.Height() - logicalPoint.y;
            physical.y = logicalPoint.x;
            break;
    }
    return physical;
}


/*!    Convert physical point back to logical screen coordinates.
    Inverse of logical_to_physical_point().
*/
static inline BPoint
physical_to_logical_point(BPoint physicalPoint, BRect logicalBounds,
    int32 rotation)
{
    if (rotation == 0)
        return physicalPoint;

    BPoint logical = physicalPoint;
    switch (rotation) {
        case 90:
            // Inverse 90° CW: (x,y) → (W-y, x)
            logical.x = logicalBounds.Width() - physicalPoint.y;
            logical.y = physicalPoint.x;
            break;
        case 180:
            // Inverse 180°: (x,y) → (W-x, H-y) (same as forward)
            logical.x = logicalBounds.Width() - physicalPoint.x;
            logical.y = logicalBounds.Height() - physicalPoint.y;
            break;
        case 270:
            // Inverse 270° CW: (x,y) → (y, H-x)
            logical.x = physicalPoint.y;
            logical.y = logicalBounds.Height() - physicalPoint.x;
            break;
    }
    return logical;
}


/*!    Swap width and height of a rect for rotated display modes (90° or 270°).
*/
static inline BRect
swap_rect_axes(BRect rect)
{
    return BRect(0, 0, rect.Height(), rect.Width());
}


/*!    Get rotated dimensions: if rotation is 90° or 270°, swap width/height.
*/
static inline void
get_rotated_dimensions(int32 width, int32 height, int32 rotation,
    int32& outWidth, int32& outHeight)
{
    if (rotation == 90 || rotation == 270) {
        outWidth = height;
        outHeight = width;
    } else {
        outWidth = width;
        outHeight = height;
    }
}


/*!    Convert logical rectangle to physical screen coordinates when rotated.
    For 90°/270°, dimensions are swapped.
*/
static inline BRect
logical_to_physical_rect(BRect logicalRect, BRect logicalBounds,
    int32 rotation)
{
    if (rotation == 0)
        return logicalRect;

    // Transform top-left and bottom-right corners
    BPoint tl = logical_to_physical_point(logicalRect.LeftTop(), logicalBounds, rotation);
    BPoint br = logical_to_physical_point(logicalRect.RightBottom(), logicalBounds, rotation);

    // Handle dimension swap for 90°/270°
    if (rotation == 90 || rotation == 270) {
        // After rotation, coordinates may be reversed
        BRect result(
            min_c(tl.x, br.x),
            min_c(tl.y, br.y),
            max_c(tl.x, br.x),
            max_c(tl.y, br.y)
        );
        return result;
    }

    // 180° doesn't swap dimensions, just coordinates
    return BRect(
        min_c(tl.x, br.x),
        min_c(tl.y, br.y),
        max_c(tl.x, br.x),
        max_c(tl.y, br.y)
    );
}


/*!    Convert physical rectangle back to logical coordinates.
    Inverse of logical_to_physical_rect().
*/
static inline BRect
physical_to_logical_rect(BRect physicalRect, BRect logicalBounds,
    int32 rotation)
{
    if (rotation == 0)
        return physicalRect;

    // Transform corners back
    BPoint tl = physical_to_logical_point(physicalRect.LeftTop(), logicalBounds, rotation);
    BPoint br = physical_to_logical_point(physicalRect.RightBottom(), logicalBounds, rotation);

    return BRect(
        min_c(tl.x, br.x),
        min_c(tl.y, br.y),
        max_c(tl.x, br.x),
        max_c(tl.y, br.y)
    );
}


/*!    Convert logical region to physical coordinates when rotated.
    Applies logical_to_physical_point() to each rectangle in the region.
*/
static inline BRegion
logical_to_physical_region(const BRegion& logicalRegion, BRect logicalBounds,
    int32 rotation)
{
    if (rotation == 0)
        return logicalRegion;

    BRegion physicalRegion;
    for (int32 i = 0; i < logicalRegion.CountRects(); i++) {
        BRect physicalRect = logical_to_physical_rect(
            logicalRegion.RectAt(i), logicalBounds, rotation);
        physicalRegion.Include(physicalRect);
    }
    return physicalRegion;
}


/*!    Swap the axis-specific fields of a display_mode in place (self-inverse).
    Used at the AccelerantHWInterface boundary to convert between the
    physical display_mode the accelerant/driver works with and the logical
    (rotated) display_mode everything above HWInterface works with.
*/
static inline void
swap_mode_axes(display_mode& mode)
{
    uint16 t;
    t = mode.timing.h_display;
    mode.timing.h_display = mode.timing.v_display;
    mode.timing.v_display = t;

    t = mode.timing.h_sync_start;
    mode.timing.h_sync_start = mode.timing.v_sync_start;
    mode.timing.v_sync_start = t;

    t = mode.timing.h_sync_end;
    mode.timing.h_sync_end = mode.timing.v_sync_end;
    mode.timing.v_sync_end = t;

    t = mode.timing.h_total;
    mode.timing.h_total = mode.timing.v_total;
    mode.timing.v_total = t;

    t = mode.virtual_width;
    mode.virtual_width = mode.virtual_height;
    mode.virtual_height = t;

    t = mode.h_display_start;
    mode.h_display_start = mode.v_display_start;
    mode.v_display_start = t;
}


/*!    Swap the axis-specific fields of a display_timing_constraints in place
    (self-inverse). The pixel clock limits (a clock rate, not an axis) are
    a separate, unrelated accelerant call and must NOT be swapped.
*/
static inline void
swap_timing_constraints_axes(display_timing_constraints& constraints)
{
    uint16 t;
    t = constraints.h_res; constraints.h_res = constraints.v_res; constraints.v_res = t;
    t = constraints.h_sync_min; constraints.h_sync_min = constraints.v_sync_min; constraints.v_sync_min = t;
    t = constraints.h_sync_max; constraints.h_sync_max = constraints.v_sync_max; constraints.v_sync_max = t;
    t = constraints.h_blank_min; constraints.h_blank_min = constraints.v_blank_min; constraints.v_blank_min = t;
    t = constraints.h_blank_max; constraints.h_blank_max = constraints.v_blank_max; constraints.v_blank_max = t;
}


}    // namespace BPrivate


#endif    // _DISPLAY_ROTATION_H
