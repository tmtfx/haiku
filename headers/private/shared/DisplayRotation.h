/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Display rotation utilities for software and hardware rotation support.
 *
 * Settings file: ~/config/settings/display_rotation
 *
 *    display 0        # or "normal", "right_side_up"
 *    display 90        # or "right", "left_side_up", "cw"
 *    display 180        # or "inverted", "upside_down", "bottom_up"
 *    display 270        # or "left", "ccw"
 *    touch 90        # optional, defaults to display value for input conversion
 *
 * Values:
 *    0/90/180/270 or normal/right/inverted/left - degrees clockwise
 *    right_side_up, left_side_up, bottom_up, upside_down - Linux/DRM panel mount names
 */
#ifndef _DISPLAY_ROTATION_H
#define _DISPLAY_ROTATION_H

#include <FindDirectory.h>
#include <Rect.h>
#include <Region.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>


namespace BPrivate {


static inline int32
parse_display_rotation(const char* value)
{
    if (value == NULL)
        return 0;

    if (strcasecmp(value, "right") == 0 || strcmp(value, "90") == 0
        || strcasecmp(value, "cw") == 0
        || strcasecmp(value, "left_side_up") == 0)
        return 90;
    if (strcasecmp(value, "inverted") == 0 || strcmp(value, "180") == 0
        || strcasecmp(value, "upside_down") == 0
        || strcasecmp(value, "bottom_up") == 0)
        return 180;
    if (strcasecmp(value, "left") == 0 || strcmp(value, "270") == 0
        || strcasecmp(value, "ccw") == 0
        || strcasecmp(value, "right_side_up") == 0)
        return 270;
    if (strcasecmp(value, "normal") == 0 || strcmp(value, "0") == 0
        || strcasecmp(value, "0_degrees") == 0)
        return 0;
    return 0;
}


/*!    Reads the rotation for \a key ("display" or "touch"), in clockwise
    degrees (0, 90, 180 or 270). A missing "touch" entry falls back to
    "display"; a missing file means no rotation.
*/
static inline int32
get_display_rotation(const char* key = "display")
{
    char path[B_PATH_NAME_LENGTH];
    if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, false, path,
            sizeof(path)) != B_OK) {
        return 0;
    }
    strlcat(path, "/display_rotation", sizeof(path));

    FILE* file = fopen(path, "r");
    if (file == NULL)
        return 0;

    int32 display = 0;
    int32 touch = -1;
    char line[128];
    while (fgets(line, sizeof(line), file) != NULL) {
        char* hash = strchr(line, '#');
        if (hash != NULL)
            *hash = '\0';

        char name[32];
        char value[32];
        int count = sscanf(line, "%31s %31s", name, value);
        if (count == 1) {
            // bare value means display rotation
            display = parse_display_rotation(name);
        } else if (count == 2) {
            if (strcasecmp(name, "display") == 0)
                display = parse_display_rotation(value);
            else if (strcasecmp(name, "touch") == 0)
                touch = parse_display_rotation(value);
        }
    }
    fclose(file);

    if (strcmp(key, "touch") == 0 && touch >= 0)
        return touch;
    return display;
}


/*!    Write rotation settings to ~/config/settings/display_rotation.
    Both display and touch keys are written if provided.
    Pass -1 to skip writing that key.
*/
static inline status_t
set_display_rotation(int32 displayRotation, int32 touchRotation = -1)
{
    char path[B_PATH_NAME_LENGTH];
    if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, path,
            sizeof(path)) != B_OK) {
        return B_ERROR;
    }
    strlcat(path, "/display_rotation", sizeof(path));

    FILE* file = fopen(path, "w");
    if (file == NULL)
        return B_ERROR;

    fprintf(file, "display %d\n", displayRotation);
    if (touchRotation >= 0)
        fprintf(file, "touch %d\n", touchRotation);

    fclose(file);
    return B_OK;
}


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


}    // namespace BPrivate


#endif    // _DISPLAY_ROTATION_H
