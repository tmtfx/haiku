/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef CURSOR_ANIMATION_DATA_H
#define CURSOR_ANIMATION_DATA_H

#include <SupportDefs.h>


struct cursor_animation_frame_data {
	const uint8*	vectorData;
	size_t			vectorSize;
};


struct cursor_animation_definition {
	const char*							assetName;
	uint32								frameCount;
	uint32								startDelayMs;
	uint32								idleDelayMs;
	float								speed;
	uint32								trigger;
	const cursor_animation_frame_data*	frames;
	size_t								frameDataCount;
};


extern const cursor_animation_definition kSystemCursorAnimations[];
extern const size_t kSystemCursorAnimationCount;

const cursor_animation_definition* FindSystemCursorAnimationDefinition(
	const char* assetName);

#endif // CURSOR_ANIMATION_DATA_H
