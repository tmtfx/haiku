#ifndef _BTTV_DRIVER_H
#define _BTTV_DRIVER_H

#include <Drivers.h>
#include <SupportDefs.h>


enum {
	BTV_STD_PAL = 0,
	BTV_STD_NTSC = 1
};

enum {
	BTV_INPUT_TUNER = 0,
	BTV_INPUT_COMPOSITE = 1
};

enum {
	BTV_PIXEL_FORMAT_YUY2 = 0x32595559U
};

enum {
	BTV_GET_CARD_INFO = B_DEVICE_OP_CODES_END + 0x100,
	BTV_GET_CAPTURE_FORMAT,
	BTV_SET_CAPTURE_FORMAT,
	BTV_START_CAPTURE,
	BTV_STOP_CAPTURE,
	BTV_SET_INPUT,
	BTV_GET_CONTROLS,
	BTV_SET_CONTROLS
};

typedef struct bttv_card_info {
	uint16	vendor_id;
	uint16	device_id;
	uint16	subsystem_vendor_id;
	uint16	subsystem_device_id;
	uint32	capabilities;
	char	card_name[32];
	char	device_name[64];
} bttv_card_info;

typedef struct bttv_capture_format {
	uint32	width;
	uint32	height;
	uint32	bytes_per_line;
	uint32	frame_size;
	uint32	pixel_format;
	uint32	video_standard;
	uint32	input;
} bttv_capture_format;

typedef struct bttv_video_controls {
	int32	brightness;
	int32	contrast;
	int32	saturation;
	int32	hue;
} bttv_video_controls;

#define BTV_CAP_VIDEO_CAPTURE	0x00000001U

#endif
