#ifndef _BTTV_MEDIA_DRIVER_INTERFACE_H
#define _BTTV_MEDIA_DRIVER_INTERFACE_H

#include <Drivers.h>
#include <SupportDefs.h>


enum {
	BTV_STD_PAL = 0,
	BTV_STD_NTSC = 1,
	// See bttv_driver.h for the full rationale: PAL colour encoding on
	// NTSC-style 525-line/60Hz timing (480 active lines, not 576).
	BTV_STD_PAL60 = 2,
	// See bttv_driver.h for the full rationale: NTSC's entire register
	// configuration (the only standard confirmed to capture genuinely
	// live video on this card) with VTOTAL=624 instead of NTSC's 0, for
	// PAL-encoded sources whose vertical timing this card's PLL path
	// can't actually lock onto.
	BTV_STD_FAKE_PAL = 3
};

enum {
	BTV_INPUT_TUNER = 0,
	BTV_INPUT_COMPOSITE = 1,
	BTV_INPUT_SVIDEO = 2
};

enum {
	BTV_PIXEL_FORMAT_YUY2 = 0x32595559U
};

// BParameterWeb discrete-parameter IDs for the bttv media add-on's
// BttvProducer, and the values its P_RESOLUTION parameter takes. Shared
// here (not private to BttvProducer) so a client with no BMediaNode of
// its own -- e.g. bttv_viewer's MainWindow -- can still push these via
// BMediaRoster::GetParameterWebFor()/BParameter::SetValue() *before*
// Connect(), which it needs to for BTV_STD_PAL60: Connect()'s format
// negotiation (width/height/field_rate) can never distinguish PAL-60
// from NTSC on its own, since both are 480 active lines at the same
// ~29.97 Hz field rate -- see BttvProducer::_SpecializeFormat() in
// Producer.cpp.
enum {
	BTV_PARAM_INPUT = 1,
	BTV_PARAM_STANDARD,
	BTV_PARAM_RESOLUTION,
	BTV_PARAM_BRIGHTNESS,
	BTV_PARAM_CONTRAST,
	BTV_PARAM_SATURATION,
	BTV_PARAM_HUE
};

enum {
	BTV_RESOLUTION_FULL = 0,
	BTV_RESOLUTION_HALF = 1
};

enum {
	BTV_GET_CARD_INFO = B_DEVICE_OP_CODES_END + 0x100,
	BTV_GET_CAPTURE_FORMAT,
	BTV_SET_CAPTURE_FORMAT,
	BTV_START_CAPTURE,
	BTV_STOP_CAPTURE,
	BTV_SET_INPUT,
	BTV_GET_CONTROLS,
	BTV_SET_CONTROLS,
	BTV_GET_STATS
};

#define BTV_CAP_VIDEO_CAPTURE	0x00000001U
#define BTV_CAP_HAS_TUNER		0x00000002U
#define BTV_CAP_HAS_COMPOSITE	0x00000004U
#define BTV_CAP_HAS_SVIDEO		0x00000008U

typedef struct bttv_card_info {
	uint16	vendor_id;
	uint16	device_id;
	uint16	subsystem_vendor_id;
	uint16	subsystem_device_id;
	uint32	capabilities;
	uint32	video_inputs;
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

// Diagnostic counters, updated only from the interrupt handler.
// frame_count: number of frames whose RISC program completed (BT848_INT_RISCI).
// error_count: number of FIFO/RISC/sync errors (or unexpected RISC stops)
//   recovered from -- the single most common cause is no stable input signal.
// Neither is reset by BTV_START_CAPTURE/BTV_STOP_CAPTURE; they accumulate for
// the life of the open device, so sample both before and after a capture run
// to see what happened during that run specifically.
typedef struct bttv_capture_stats {
	uint32	frame_count;
	uint32	error_count;
	// Raw BT848_DSTATUS value sampled opportunistically by the interrupt
	// handler (bit 7 PRES, bit 6 HLOC, bit 4 NUML, bit 2 PLOCK). HLOC is
	// horizontal sync lock to the *incoming video signal* -- separate
	// from and independent of PLOCK (the internal clock PLL). For
	// diagnostics only.
	uint8	last_dstatus;
	// INT_STAT error bits that triggered the last recovery (SCERR/OCERR/
	// PABORT/RIPERR/PPERR/OFLOW). For diagnostics only.
	uint32	last_error_bits;
} bttv_capture_stats;

#endif
