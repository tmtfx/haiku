/*
 * YMF7XX Haiku Driver for Yamaha DS-XG (YMF-7xx) audio
 *
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * The hmulti_audio ioctl callbacks below follow the same structure as
 * the other Haiku AC'97 drivers (auich, es1370): a small hardware-
 * specific implementation of 12 callbacks, then a textual #include of
 * generic/multi.c, which supplies the shared ioctl dispatcher. The
 * AC'97 mixer plumbing (ymf7xx_ac97_get_mix/set_mix/create_controls_list)
 * is copied essentially unchanged from auich/multi.c, since both drivers
 * hand the same ac97.c source_info[] table off to the same kind of
 * codec register read/write pair.
 */

#include <driver_settings.h>
#include <OS.h>
#include <MediaDefs.h>
#include <string.h>
#include <strings.h>

#include <kernel.h>

#include "hmulti_audio.h"
#include "multi.h"
#include "ac97.h"

#include "debug.h"
#include "ymf7xx.h"
#include "util.h"
#include "io.h"


static void
ymf7xx_ac97_get_mix(void *card, const void *cookie, int32 type, float *values)
{
	ymf7xx_dev *dev = (ymf7xx_dev*)card;
	ac97_source_info *info = (ac97_source_info *)cookie;
	uint16 value, mask;
	float gain;

	switch (type) {
		case B_MIX_GAIN:
			value = ymf7xx_codec_read(&dev->config, info->reg);
			if (info->type & B_MIX_STEREO) {
				mask = ((1 << (info->bits + 1)) - 1) << 8;
				gain = ((value & mask) >> 8) * info->granularity;
				if (info->polarity == 1)
					values[0] = info->max_gain - gain;
				else
					values[0] = gain - info->min_gain;

				mask = ((1 << (info->bits + 1)) - 1);
				gain = (value & mask) * info->granularity;
				if (info->polarity == 1)
					values[1] = info->max_gain - gain;
				else
					values[1] = gain - info->min_gain;
			} else {
				mask = ((1 << (info->bits + 1)) - 1);
				gain = (value & mask) * info->granularity;
				if (info->polarity == 1)
					values[0] = info->max_gain - gain;
				else
					values[0] = gain - info->min_gain;
			}
			break;
		case B_MIX_MUTE:
			mask = ((1 << 1) - 1) << 15;
			value = ymf7xx_codec_read(&dev->config, info->reg);
			value &= mask;
			values[0] = ((value >> 15) == 1) ? 1.0 : 0.0;
			break;
		case B_MIX_MICBOOST:
			mask = ((1 << 1) - 1) << 6;
			value = ymf7xx_codec_read(&dev->config, info->reg);
			value &= mask;
			values[0] = ((value >> 6) == 1) ? 1.0 : 0.0;
			break;
		case B_MIX_MUX:
			mask = ((1 << 3) - 1);
			value = ymf7xx_codec_read(&dev->config, AC97_RECORD_SELECT);
			value &= mask;
			values[0] = (float)value;
			break;
	}
}


static void
ymf7xx_ac97_set_mix(void *card, const void *cookie, int32 type, float *values)
{
	ymf7xx_dev *dev = (ymf7xx_dev*)card;
	ac97_source_info *info = (ac97_source_info *)cookie;
	uint16 value, mask;
	float gain;

	switch (type) {
		case B_MIX_GAIN:
			value = ymf7xx_codec_read(&dev->config, info->reg);
			if (info->type & B_MIX_STEREO) {
				mask = ((1 << (info->bits + 1)) - 1) << 8;
				value &= ~mask;
				if (info->polarity == 1)
					gain = info->max_gain - values[0];
				else
					gain = values[0] - info->min_gain;
				value |= ((uint16)(gain / info->granularity) << 8) & mask;

				mask = ((1 << (info->bits + 1)) - 1);
				value &= ~mask;
				if (info->polarity == 1)
					gain = info->max_gain - values[1];
				else
					gain = values[1] - info->min_gain;
				value |= ((uint16)(gain / info->granularity)) & mask;
			} else {
				mask = ((1 << (info->bits + 1)) - 1);
				value &= ~mask;
				if (info->polarity == 1)
					gain = info->max_gain - values[0];
				else
					gain = values[0] - info->min_gain;
				value |= ((uint16)(gain / info->granularity)) & mask;
			}
			ymf7xx_codec_write(&dev->config, info->reg, value);
			break;
		case B_MIX_MUTE:
			mask = ((1 << 1) - 1) << 15;
			value = ymf7xx_codec_read(&dev->config, info->reg);
			value &= ~mask;
			value |= ((values[0] == 1.0 ? 1 : 0) << 15 & mask);
			if (info->reg == AC97_SURR_VOLUME) {
				mask = ((1 << 1) - 1) << 7;
				value &= ~mask;
				value |= ((values[0] == 1.0 ? 1 : 0) << 7 & mask);
			}
			ymf7xx_codec_write(&dev->config, info->reg, value);
			break;
		case B_MIX_MICBOOST:
			mask = ((1 << 1) - 1) << 6;
			value = ymf7xx_codec_read(&dev->config, info->reg);
			value &= ~mask;
			value |= ((values[0] == 1.0 ? 1 : 0) << 6 & mask);
			ymf7xx_codec_write(&dev->config, info->reg, value);
			break;
		case B_MIX_MUX:
			mask = ((1 << 3) - 1);
			value = ((int32)values[0]) & mask;
			value = value | (value << 8);
			ymf7xx_codec_write(&dev->config, AC97_RECORD_SELECT, value);
			break;
	}
}


static int32
ymf7xx_create_group_control(multi_dev *multi, uint32 *index, int32 parent,
	int32 string, const char* name)
{
	uint32 i = *index;
	(*index)++;
	multi->controls[i].mix_control.id = YMF7XX_MULTI_CONTROL_FIRSTID + i;
	multi->controls[i].mix_control.parent = parent;
	multi->controls[i].mix_control.flags = B_MULTI_MIX_GROUP;
	multi->controls[i].mix_control.master = YMF7XX_MULTI_CONTROL_MASTERID;
	multi->controls[i].mix_control.string = string;
	if (name)
		strcpy(multi->controls[i].mix_control.name, name);

	return multi->controls[i].mix_control.id;
}


static status_t
ymf7xx_create_controls_list(multi_dev *multi)
{
	uint32 i = 0, index = 0, count, id, parent, parent2, parent3;
	const ac97_source_info *info;

	/* AC97 Mixer */
	parent = ymf7xx_create_group_control(multi, &index, 0, 0, "AC97 mixer");

	count = source_info_size;
	/* ignore source_info[0], it's for recording -- see ac97.c */
	for (i = 1; i < count; i++) {
		info = &source_info[i];
		PRINT(("name : %s\n", info->name));

		parent2 = ymf7xx_create_group_control(multi, &index, parent, 0, info->name);

		if (info->type & B_MIX_GAIN) {
			if (info->type & B_MIX_MUTE) {
				multi->controls[index].mix_control.id = YMF7XX_MULTI_CONTROL_FIRSTID + index;
				multi->controls[index].mix_control.flags = B_MULTI_MIX_ENABLE;
				multi->controls[index].mix_control.master = YMF7XX_MULTI_CONTROL_MASTERID;
				multi->controls[index].mix_control.parent = parent2;
				multi->controls[index].mix_control.string = S_MUTE;
				multi->controls[index].cookie = info;
				multi->controls[index].type = B_MIX_MUTE;
				multi->controls[index].get = &ymf7xx_ac97_get_mix;
				multi->controls[index].set = &ymf7xx_ac97_set_mix;
				index++;
			}

			multi->controls[index].mix_control.id = YMF7XX_MULTI_CONTROL_FIRSTID + index;
			multi->controls[index].mix_control.flags = B_MULTI_MIX_GAIN;
			multi->controls[index].mix_control.master = YMF7XX_MULTI_CONTROL_MASTERID;
			multi->controls[index].mix_control.parent = parent2;
			strcpy(multi->controls[index].mix_control.name, info->name);
			multi->controls[index].mix_control.u.gain.min_gain = info->min_gain;
			multi->controls[index].mix_control.u.gain.max_gain = info->max_gain;
			multi->controls[index].mix_control.u.gain.granularity = info->granularity;
			multi->controls[index].cookie = info;
			multi->controls[index].type = B_MIX_GAIN;
			multi->controls[index].get = &ymf7xx_ac97_get_mix;
			multi->controls[index].set = &ymf7xx_ac97_set_mix;
			id = multi->controls[index].mix_control.id;
			index++;

			if (info->type & B_MIX_STEREO) {
				multi->controls[index].mix_control.id = YMF7XX_MULTI_CONTROL_FIRSTID + index;
				multi->controls[index].mix_control.flags = B_MULTI_MIX_GAIN;
				multi->controls[index].mix_control.master = id;
				multi->controls[index].mix_control.parent = parent2;
				strcpy(multi->controls[index].mix_control.name, info->name);
				multi->controls[index].mix_control.u.gain.min_gain = info->min_gain;
				multi->controls[index].mix_control.u.gain.max_gain = info->max_gain;
				multi->controls[index].mix_control.u.gain.granularity = info->granularity;
				multi->controls[index].cookie = info;
				multi->controls[index].type = B_MIX_GAIN;
				multi->controls[index].get = &ymf7xx_ac97_get_mix;
				multi->controls[index].set = &ymf7xx_ac97_set_mix;
				index++;
			}

			if (info->type & B_MIX_MICBOOST) {
				multi->controls[index].mix_control.id = YMF7XX_MULTI_CONTROL_FIRSTID + index;
				multi->controls[index].mix_control.flags = B_MULTI_MIX_ENABLE;
				multi->controls[index].mix_control.master = YMF7XX_MULTI_CONTROL_MASTERID;
				multi->controls[index].mix_control.parent = parent2;
				strcpy(multi->controls[index].mix_control.name, "+20 dB");
				multi->controls[index].cookie = info;
				multi->controls[index].type = B_MIX_MICBOOST;
				multi->controls[index].get = &ymf7xx_ac97_get_mix;
				multi->controls[index].set = &ymf7xx_ac97_set_mix;
				index++;
			}
		}
	}

	/* AC97 Record */
	parent = ymf7xx_create_group_control(multi, &index, 0, 0, "Recording");

	info = &source_info[0];
	PRINT(("name : %s\n", info->name));

	parent2 = ymf7xx_create_group_control(multi, &index, parent, 0, info->name);

	if (info->type & B_MIX_GAIN) {
		if (info->type & B_MIX_MUTE) {
			multi->controls[index].mix_control.id = YMF7XX_MULTI_CONTROL_FIRSTID + index;
			multi->controls[index].mix_control.flags = B_MULTI_MIX_ENABLE;
			multi->controls[index].mix_control.master = YMF7XX_MULTI_CONTROL_MASTERID;
			multi->controls[index].mix_control.parent = parent2;
			multi->controls[index].mix_control.string = S_MUTE;
			multi->controls[index].cookie = info;
			multi->controls[index].type = B_MIX_MUTE;
			multi->controls[index].get = &ymf7xx_ac97_get_mix;
			multi->controls[index].set = &ymf7xx_ac97_set_mix;
			index++;
		}

		multi->controls[index].mix_control.id = YMF7XX_MULTI_CONTROL_FIRSTID + index;
		multi->controls[index].mix_control.flags = B_MULTI_MIX_GAIN;
		multi->controls[index].mix_control.master = YMF7XX_MULTI_CONTROL_MASTERID;
		multi->controls[index].mix_control.parent = parent2;
		strcpy(multi->controls[index].mix_control.name, info->name);
		multi->controls[index].mix_control.u.gain.min_gain = info->min_gain;
		multi->controls[index].mix_control.u.gain.max_gain = info->max_gain;
		multi->controls[index].mix_control.u.gain.granularity = info->granularity;
		multi->controls[index].cookie = info;
		multi->controls[index].type = B_MIX_GAIN;
		multi->controls[index].get = &ymf7xx_ac97_get_mix;
		multi->controls[index].set = &ymf7xx_ac97_set_mix;
		id = multi->controls[index].mix_control.id;
		index++;

		if (info->type & B_MIX_STEREO) {
			multi->controls[index].mix_control.id = YMF7XX_MULTI_CONTROL_FIRSTID + index;
			multi->controls[index].mix_control.flags = B_MULTI_MIX_GAIN;
			multi->controls[index].mix_control.master = id;
			multi->controls[index].mix_control.parent = parent2;
			strcpy(multi->controls[index].mix_control.name, info->name);
			multi->controls[index].mix_control.u.gain.min_gain = info->min_gain;
			multi->controls[index].mix_control.u.gain.max_gain = info->max_gain;
			multi->controls[index].mix_control.u.gain.granularity = info->granularity;
			multi->controls[index].cookie = info;
			multi->controls[index].type = B_MIX_GAIN;
			multi->controls[index].get = &ymf7xx_ac97_get_mix;
			multi->controls[index].set = &ymf7xx_ac97_set_mix;
			index++;
		}

		if (info->type & B_MIX_RECORDMUX) {
			multi->controls[index].mix_control.id = YMF7XX_MULTI_CONTROL_FIRSTID + index;
			multi->controls[index].mix_control.flags = B_MULTI_MIX_MUX;
			multi->controls[index].mix_control.parent = parent2;
			strcpy(multi->controls[index].mix_control.name, "Record mux");
			multi->controls[index].cookie = info;
			multi->controls[index].type = B_MIX_MUX;
			multi->controls[index].get = &ymf7xx_ac97_get_mix;
			multi->controls[index].set = &ymf7xx_ac97_set_mix;
			parent3 = multi->controls[index].mix_control.id;
			index++;

			multi->controls[index].mix_control.id = YMF7XX_MULTI_CONTROL_FIRSTID + index;
			multi->controls[index].mix_control.flags = B_MULTI_MIX_MUX_VALUE;
			multi->controls[index].mix_control.parent = parent3;
			multi->controls[index].mix_control.string = S_MIC;
			index++;
			multi->controls[index].mix_control.id = YMF7XX_MULTI_CONTROL_FIRSTID + index;
			multi->controls[index].mix_control.flags = B_MULTI_MIX_MUX_VALUE;
			multi->controls[index].mix_control.parent = parent3;
			strcpy(multi->controls[index].mix_control.name, "CD in");
			index++;
			multi->controls[index].mix_control.id = YMF7XX_MULTI_CONTROL_FIRSTID + index;
			multi->controls[index].mix_control.flags = B_MULTI_MIX_MUX_VALUE;
			multi->controls[index].mix_control.parent = parent3;
			strcpy(multi->controls[index].mix_control.name, "Video in");
			index++;
			multi->controls[index].mix_control.id = YMF7XX_MULTI_CONTROL_FIRSTID + index;
			multi->controls[index].mix_control.flags = B_MULTI_MIX_MUX_VALUE;
			multi->controls[index].mix_control.parent = parent3;
			strcpy(multi->controls[index].mix_control.name, "Aux in");
			index++;
			multi->controls[index].mix_control.id = YMF7XX_MULTI_CONTROL_FIRSTID + index;
			multi->controls[index].mix_control.flags = B_MULTI_MIX_MUX_VALUE;
			multi->controls[index].mix_control.parent = parent3;
			strcpy(multi->controls[index].mix_control.name, "Line in");
			index++;
			multi->controls[index].mix_control.id = YMF7XX_MULTI_CONTROL_FIRSTID + index;
			multi->controls[index].mix_control.flags = B_MULTI_MIX_MUX_VALUE;
			multi->controls[index].mix_control.parent = parent3;
			multi->controls[index].mix_control.string = S_STEREO_MIX;
			index++;
			multi->controls[index].mix_control.id = YMF7XX_MULTI_CONTROL_FIRSTID + index;
			multi->controls[index].mix_control.flags = B_MULTI_MIX_MUX_VALUE;
			multi->controls[index].mix_control.parent = parent3;
			multi->controls[index].mix_control.string = S_MONO_MIX;
			index++;
			multi->controls[index].mix_control.id = YMF7XX_MULTI_CONTROL_FIRSTID + index;
			multi->controls[index].mix_control.flags = B_MULTI_MIX_MUX_VALUE;
			multi->controls[index].mix_control.parent = parent3;
			strcpy(multi->controls[index].mix_control.name, "TAD");
			index++;
		}
	}

	multi->control_count = index;
	PRINT(("multi->control_count %" B_PRIu32 "\n", multi->control_count));
	return B_OK;
}


static status_t
ymf7xx_get_mix(ymf7xx_dev *card, multi_mix_value_info * mmvi)
{
	int32 i, id;
	multi_mixer_control *control = NULL;
	for (i = 0; i < mmvi->item_count; i++) {
		id = mmvi->values[i].id - YMF7XX_MULTI_CONTROL_FIRSTID;
		if (id < 0 || (uint32)id >= card->multi.control_count) {
			PRINT(("ymf7xx_get_mix : invalid control id requested : %" B_PRIi32 "\n", id));
			continue;
		}
		control = &card->multi.controls[id];

		if (control->mix_control.flags & B_MULTI_MIX_GAIN) {
			if (control->get) {
				float values[2];
				control->get(card, control->cookie, control->type, values);
				if (control->mix_control.master == YMF7XX_MULTI_CONTROL_MASTERID)
					mmvi->values[i].u.gain = values[0];
				else
					mmvi->values[i].u.gain = values[1];
			}
		}

		if (control->mix_control.flags & B_MULTI_MIX_ENABLE && control->get) {
			float values[1];
			control->get(card, control->cookie, control->type, values);
			mmvi->values[i].u.enable = (values[0] == 1.0);
		}

		if (control->mix_control.flags & B_MULTI_MIX_MUX && control->get) {
			float values[1];
			control->get(card, control->cookie, control->type, values);
			mmvi->values[i].u.mux = (int32)values[0];
		}
	}
	return B_OK;
}


static status_t
ymf7xx_set_mix(ymf7xx_dev *card, multi_mix_value_info * mmvi)
{
	int32 i, id;
	multi_mixer_control *control = NULL;
	for (i = 0; i < mmvi->item_count; i++) {
		id = mmvi->values[i].id - YMF7XX_MULTI_CONTROL_FIRSTID;
		if (id < 0 || (uint32)id >= card->multi.control_count) {
			PRINT(("ymf7xx_set_mix : invalid control id requested : %" B_PRIi32 "\n", id));
			continue;
		}
		control = &card->multi.controls[id];

		if (control->mix_control.flags & B_MULTI_MIX_GAIN) {
			multi_mixer_control *control2 = NULL;
			if (i+1 < mmvi->item_count) {
				id = mmvi->values[i + 1].id - YMF7XX_MULTI_CONTROL_FIRSTID;
				if (id < 0 || (uint32)id >= card->multi.control_count) {
					PRINT(("ymf7xx_set_mix : invalid control id requested : %" B_PRIi32 "\n", id));
				} else {
					control2 = &card->multi.controls[id];
					if (control2->mix_control.master != control->mix_control.id)
						control2 = NULL;
				}
			}

			if (control->set) {
				float values[2];
				values[0] = 0.0;
				values[1] = 0.0;

				if (control->mix_control.master == YMF7XX_MULTI_CONTROL_MASTERID)
					values[0] = mmvi->values[i].u.gain;
				else
					values[1] = mmvi->values[i].u.gain;

				if (control2 && control2->mix_control.master != YMF7XX_MULTI_CONTROL_MASTERID)
					values[1] = mmvi->values[i+1].u.gain;

				control->set(card, control->cookie, control->type, values);
			}

			if (control2)
				i++;
		}

		if (control->mix_control.flags & B_MULTI_MIX_ENABLE && control->set) {
			float values[1];
			values[0] = mmvi->values[i].u.enable ? 1.0 : 0.0;
			control->set(card, control->cookie, control->type, values);
		}

		if (control->mix_control.flags & B_MULTI_MIX_MUX && control->set) {
			float values[1];
			values[0] = (float)mmvi->values[i].u.mux;
			control->set(card, control->cookie, control->type, values);
		}
	}
	return B_OK;
}


static status_t
ymf7xx_list_mix_controls(ymf7xx_dev *card, multi_mix_control_info * mmci)
{
	multi_mix_control	*mmc;
	uint32 i;

	mmc = mmci->controls;
	if (mmci->control_count < 24)
		return B_ERROR;

	if (ymf7xx_create_controls_list(&card->multi) < B_OK)
		return B_ERROR;

	for (i = 0; i < card->multi.control_count; i++)
		mmc[i] = card->multi.controls[i].mix_control;

	mmci->control_count = card->multi.control_count;
	return B_OK;
}


static status_t
ymf7xx_list_mix_connections(ymf7xx_dev *card, multi_mix_connection_info * data)
{
	return B_ERROR;
}


static status_t
ymf7xx_list_mix_channels(ymf7xx_dev *card, multi_mix_channel_info *data)
{
	return B_ERROR;
}


static void
ymf7xx_create_channels_list(multi_dev *multi)
{
	ymf7xx_stream *stream;
	uint32 index, i, mode, designations;
	multi_channel_info *chans;
	uint32 chan_designations[] = { B_CHANNEL_LEFT, B_CHANNEL_RIGHT };

	chans = multi->chans;
	index = 0;

	for (mode = YMF7XX_USE_PLAY; (int32)mode != -1;
		mode = (mode == YMF7XX_USE_PLAY) ? YMF7XX_USE_RECORD : -1) {
		LIST_FOREACH(stream, &((ymf7xx_dev*)multi->card)->streams, next) {
			if ((stream->use & mode) == 0)
				continue;

			designations = (stream->channels == 2)
				? B_CHANNEL_STEREO_BUS : B_CHANNEL_SURROUND_BUS;

			for (i = 0; i < stream->channels; i++) {
				chans[index].channel_id = index;
				chans[index].kind = (mode == YMF7XX_USE_PLAY)
					? B_MULTI_OUTPUT_CHANNEL : B_MULTI_INPUT_CHANNEL;
				chans[index].designations = designations | chan_designations[i];
				chans[index].connectors = 0;
				index++;
			}
		}

		if (mode == YMF7XX_USE_PLAY)
			multi->output_channel_count = index;
		else
			multi->input_channel_count = index - multi->output_channel_count;
	}

	chans[index].channel_id = index;
	chans[index].kind = B_MULTI_OUTPUT_BUS;
	chans[index].designations = B_CHANNEL_LEFT | B_CHANNEL_STEREO_BUS;
	chans[index].connectors = B_CHANNEL_MINI_JACK_STEREO;
	index++;

	chans[index].channel_id = index;
	chans[index].kind = B_MULTI_OUTPUT_BUS;
	chans[index].designations = B_CHANNEL_RIGHT | B_CHANNEL_STEREO_BUS;
	chans[index].connectors = B_CHANNEL_MINI_JACK_STEREO;
	index++;

	multi->output_bus_channel_count = index - multi->output_channel_count
		- multi->input_channel_count;

	chans[index].channel_id = index;
	chans[index].kind = B_MULTI_INPUT_BUS;
	chans[index].designations = B_CHANNEL_LEFT | B_CHANNEL_STEREO_BUS;
	chans[index].connectors = B_CHANNEL_MINI_JACK_STEREO;
	index++;

	chans[index].channel_id = index;
	chans[index].kind = B_MULTI_INPUT_BUS;
	chans[index].designations = B_CHANNEL_RIGHT | B_CHANNEL_STEREO_BUS;
	chans[index].connectors = B_CHANNEL_MINI_JACK_STEREO;
	index++;

	multi->input_bus_channel_count = index - multi->output_channel_count
		- multi->input_channel_count - multi->output_bus_channel_count;

	multi->aux_bus_channel_count = 0;
}


static const char *
ymf7xx_chip_name(uint16 device_id)
{
	switch (device_id) {
		case YAMAHA_YMF724_ID: return "Yamaha YMF724";
		case YAMAHA_YMF734_ID: return "Yamaha YMF734";
		case YAMAHA_YMF740_ID: return "Yamaha YMF740";
		case YAMAHA_YMF740C_ID: return "Yamaha YMF740C";
		case YAMAHA_YMF724F_ID: return "Yamaha YMF724F";
		case YAMAHA_YMF744_ID: return "Yamaha YMF744";
		case YAMAHA_YMF754_ID: return "Yamaha YMF754";
	}
	return FRIENDLY_NAME;
}


static status_t
ymf7xx_get_description(ymf7xx_dev *card, multi_description *data)
{
	uint32 size;

	data->interface_version = B_CURRENT_INTERFACE_VERSION;
	data->interface_minimum = B_CURRENT_INTERFACE_VERSION;

	strncpy(data->friendly_name, ymf7xx_chip_name(card->info.device_id), 32);
	strcpy(data->vendor_info, AUTHOR);

	data->output_channel_count = card->multi.output_channel_count;
	data->input_channel_count = card->multi.input_channel_count;
	data->output_bus_channel_count = card->multi.output_bus_channel_count;
	data->input_bus_channel_count = card->multi.input_bus_channel_count;
	data->aux_bus_channel_count = card->multi.aux_bus_channel_count;

	size = card->multi.output_channel_count + card->multi.input_channel_count
			+ card->multi.output_bus_channel_count + card->multi.input_bus_channel_count
			+ card->multi.aux_bus_channel_count;

	LOG(("request_channel_count = %" B_PRId32 "\n", data->request_channel_count));
	if (data->request_channel_count >= (int32)size) {
		LOG(("copying data\n"));
		memcpy(data->channels, card->multi.chans, size * sizeof(card->multi.chans[0]));
	}

	switch (current_settings.sample_rate) {
		case 48000: data->output_rates = data->input_rates = B_SR_48000; break;
		case 44100: data->output_rates = data->input_rates = B_SR_44100; break;
		default: data->output_rates = data->input_rates = B_SR_48000; break;
	}
	data->min_cvsr_rate = 0;
	data->max_cvsr_rate = 48000;

	data->output_formats = B_FMT_16BIT;
	data->input_formats = B_FMT_16BIT;
	data->lock_sources = B_MULTI_LOCK_INTERNAL;
	data->timecode_sources = 0;
	data->interface_flags = B_MULTI_INTERFACE_PLAYBACK | B_MULTI_INTERFACE_RECORD;
	data->start_latency = 3000;

	strcpy(data->control_panel, "");

	return B_OK;
}


static status_t
ymf7xx_get_enabled_channels(ymf7xx_dev *card, multi_channel_enable *data)
{
	B_SET_CHANNEL(data->enable_bits, 0, true);
	B_SET_CHANNEL(data->enable_bits, 1, true);
	B_SET_CHANNEL(data->enable_bits, 2, true);
	B_SET_CHANNEL(data->enable_bits, 3, true);
	data->lock_source = B_MULTI_LOCK_INTERNAL;
	return B_OK;
}


static status_t
ymf7xx_get_global_format(ymf7xx_dev *card, multi_format_info *data)
{
	data->output_latency = 0;
	data->input_latency = 0;
	data->timecode_kind = 0;
	switch (current_settings.sample_rate) {
		case 48000:
			data->input.rate = data->output.rate = B_SR_48000;
			data->input.cvsr = data->output.cvsr = 48000;
			break;
		case 44100:
			data->input.rate = data->output.rate = B_SR_44100;
			data->input.cvsr = data->output.cvsr = 44100;
			break;
		default:
			data->input.rate = data->output.rate = B_SR_48000;
			data->input.cvsr = data->output.cvsr = current_settings.sample_rate;
			break;
	}
	data->input.format = data->output.format = B_FMT_16BIT;
	return B_OK;
}


static status_t
ymf7xx_set_global_format(ymf7xx_dev *card, multi_format_info* data)
{
	/* TODO: it looks like we're not supposed to fail; fix this! */
	return B_OK;
}


static status_t
ymf7xx_get_buffers(ymf7xx_dev *card, multi_buffer_list *data)
{
	uint8 i, j, pchannels, rchannels, bufcount;

	pchannels = card->pstream->channels;
	rchannels = card->rstream->channels;

	if (data->request_playback_buffers < current_settings.buffer_count ||
		data->request_playback_channels < (pchannels) ||
		data->request_record_buffers < current_settings.buffer_count ||
		data->request_record_channels < (rchannels)) {
		LOG(("not enough channels/buffers\n"));
	}

	data->flags = B_MULTI_BUFFER_PLAYBACK | B_MULTI_BUFFER_RECORD;

	data->return_playback_buffers = current_settings.buffer_count;
	data->return_playback_channels = pchannels;
	data->return_playback_buffer_size = current_settings.buffer_frames;

	bufcount = current_settings.buffer_count;
	if (bufcount > data->request_playback_buffers)
		bufcount = data->request_playback_buffers;

	for (i = 0; i < bufcount; i++) {
		struct buffer_desc descs[data->return_playback_channels];
		for (j = 0; j < pchannels; j++)
			ymf7xx_stream_get_nth_buffer(card->pstream, j, i,
				&descs[j].base, &descs[j].stride);
		if (!IS_USER_ADDRESS(data->playback_buffers[i])
			|| user_memcpy(data->playback_buffers[i], descs, sizeof(descs)) < B_OK) {
			return B_BAD_ADDRESS;
		}
	}

	data->return_record_buffers = current_settings.buffer_count;
	data->return_record_channels = rchannels;
	data->return_record_buffer_size = current_settings.buffer_frames;

	bufcount = current_settings.buffer_count;
	if (bufcount > data->request_record_buffers)
		bufcount = data->request_record_buffers;

	for (i = 0; i < bufcount; i++) {
		struct buffer_desc descs[data->return_record_channels];
		for (j = 0; j < rchannels; j++)
			ymf7xx_stream_get_nth_buffer(card->rstream, j, i,
				&descs[j].base, &descs[j].stride);
		if (!IS_USER_ADDRESS(data->record_buffers[i])
			|| user_memcpy(data->record_buffers[i], descs, sizeof(descs)) < B_OK) {
			return B_BAD_ADDRESS;
		}
	}
	return B_OK;
}


static void
ymf7xx_play_inth(void* inthparams)
{
	ymf7xx_stream *stream = (ymf7xx_stream *)inthparams;

	acquire_spinlock(&slock);
	stream->real_time = system_time();
	stream->frames_count += current_settings.buffer_frames;
	stream->buffer_cycle = (stream->trigblk + stream->blkmod - 1) % stream->blkmod;
	stream->update_needed = true;
	release_spinlock(&slock);

	release_sem_etc(stream->card->buffer_ready_sem, 1, B_DO_NOT_RESCHEDULE);
}


static void
ymf7xx_record_inth(void* inthparams)
{
	ymf7xx_stream *stream = (ymf7xx_stream *)inthparams;

	acquire_spinlock(&slock);
	stream->real_time = system_time();
	stream->frames_count += current_settings.buffer_frames;
	stream->buffer_cycle = (stream->trigblk + stream->blkmod - 1) % stream->blkmod;
	stream->update_needed = true;
	release_spinlock(&slock);

	release_sem_etc(stream->card->buffer_ready_sem, 1, B_DO_NOT_RESCHEDULE);
}


static status_t
ymf7xx_buffer_exchange(ymf7xx_dev *card, multi_buffer_info *data)
{
	cpu_status status;
	ymf7xx_stream *pstream, *rstream;
	multi_buffer_info buffer_info;

	if (user_memcpy(&buffer_info, data, sizeof(buffer_info)) < B_OK)
		return B_BAD_ADDRESS;

	buffer_info.flags = B_MULTI_BUFFER_PLAYBACK | B_MULTI_BUFFER_RECORD;

	if (!(card->pstream->state & YMF7XX_STATE_STARTED))
		ymf7xx_stream_start(card->pstream, ymf7xx_play_inth, card->pstream);

	if (!(card->rstream->state & YMF7XX_STATE_STARTED))
		ymf7xx_stream_start(card->rstream, ymf7xx_record_inth, card->rstream);

	if (acquire_sem_etc(card->buffer_ready_sem, 1, B_RELATIVE_TIMEOUT | B_CAN_INTERRUPT, 50000)
		== B_TIMED_OUT) {
		LOG(("buffer_exchange timeout\n"));
	}

	status = lock();

	LIST_FOREACH(pstream, &card->streams, next) {
		if ((pstream->use & YMF7XX_USE_PLAY) == 0 ||
			(pstream->state & YMF7XX_STATE_STARTED) == 0)
			continue;
		if (pstream->update_needed)
			break;
	}

	LIST_FOREACH(rstream, &card->streams, next) {
		if ((rstream->use & YMF7XX_USE_RECORD) == 0 ||
			(rstream->state & YMF7XX_STATE_STARTED) == 0)
			continue;
		if (rstream->update_needed)
			break;
	}

	if (!pstream)
		pstream = card->pstream;
	if (!rstream)
		rstream = card->rstream;

	buffer_info.playback_buffer_cycle = pstream->buffer_cycle;
	buffer_info.played_real_time = pstream->real_time;
	buffer_info.played_frames_count = pstream->frames_count;
	buffer_info._reserved_0 = pstream->first_channel;
	pstream->update_needed = false;

	buffer_info.record_buffer_cycle = rstream->buffer_cycle;
	buffer_info.recorded_frames_count = rstream->frames_count;
	buffer_info.recorded_real_time = rstream->real_time;
	buffer_info._reserved_1 = rstream->first_channel;
	rstream->update_needed = false;
	unlock(status);

	if (user_memcpy(data, &buffer_info, sizeof(buffer_info)) < B_OK)
		return B_BAD_ADDRESS;

	return B_OK;
}


static status_t
ymf7xx_buffer_force_stop(ymf7xx_dev *card)
{
	return B_OK;
}


#define cookie_type ymf7xx_dev
#define get_description ymf7xx_get_description
#define get_enabled_channels ymf7xx_get_enabled_channels
#define get_global_format ymf7xx_get_global_format
#define set_global_format ymf7xx_set_global_format
#define list_mix_channels ymf7xx_list_mix_channels
#define list_mix_controls ymf7xx_list_mix_controls
#define list_mix_connections ymf7xx_list_mix_connections
#define get_mix ymf7xx_get_mix
#define set_mix ymf7xx_set_mix
#define get_buffers ymf7xx_get_buffers
#define buffer_exchange ymf7xx_buffer_exchange
#define buffer_force_stop ymf7xx_buffer_force_stop
#include "../generic/multi.c"


static status_t
ymf7xx_multi_control(void *cookie, uint32 op, void *arg, size_t length)
{
	ymf7xx_dev *card = (ymf7xx_dev *)cookie;

	return multi_audio_control_generic(card, op, arg, length);
}

static status_t ymf7xx_open(const char *name, uint32 flags, void** cookie);
static status_t ymf7xx_close(void* cookie);
static status_t ymf7xx_free(void* cookie);
static status_t ymf7xx_control(void* cookie, uint32 op, void* arg, size_t len);
static status_t ymf7xx_read(void* cookie, off_t position, void *buf, size_t* num_bytes);
static status_t ymf7xx_write(void* cookie, off_t position, const void* buffer, size_t* num_bytes);

device_hooks multi_hooks = {
	ymf7xx_open,
	ymf7xx_close,
	ymf7xx_free,
	ymf7xx_control,
	ymf7xx_read,
	ymf7xx_write,
	NULL,
	NULL,
	NULL,
	NULL
};


static status_t
ymf7xx_open(const char *name, uint32 flags, void** cookie)
{
	ymf7xx_dev *card = NULL;
	void *settings_handle;
	int ix;

	LOG(("open()\n"));

	for (ix = 0; ix < num_cards; ix++) {
		if (!strcmp(cards[ix].name, name)) {
			card = &cards[ix];
		}
	}

	if (card == NULL) {
		LOG(("open() card not found %s\n", name));
		return B_ERROR;
	}

	if (card->pstream != NULL)
		return B_ERROR;
	if (card->rstream != NULL)
		return B_ERROR;

	*cookie = card;
	card->multi.card = card;

	settings_handle = load_driver_settings(YMF7XX_SETTINGS);
	if (settings_handle != NULL) {
		const char *item;
		char       *end;
		uint32      value;

		item = get_driver_parameter(settings_handle, "sample_rate", "48000", "48000");
		value = strtoul(item, &end, 0);
		if (*end == '\0')
			current_settings.sample_rate = value;

		item = get_driver_parameter(settings_handle, "buffer_frames", "512", "512");
		value = strtoul(item, &end, 0);
		if (*end == '\0')
			current_settings.buffer_frames = value;

		item = get_driver_parameter(settings_handle, "buffer_count", "4", "4");
		value = strtoul(item, &end, 0);
		if (*end == '\0')
			current_settings.buffer_count = value;

		unload_driver_settings(settings_handle);
	}

	card->rstream = ymf7xx_stream_new(card, YMF7XX_USE_RECORD,
		current_settings.buffer_frames, current_settings.buffer_count);
	card->pstream = ymf7xx_stream_new(card, YMF7XX_USE_PLAY,
		current_settings.buffer_frames, current_settings.buffer_count);

	card->buffer_ready_sem = create_sem(0, "ymf7xx buffer ready");

	ymf7xx_stream_set_audioparms(card->pstream, 2, true, current_settings.sample_rate);
	ymf7xx_stream_set_audioparms(card->rstream, 2, true, current_settings.sample_rate);

	card->pstream->first_channel = 0;
	card->rstream->first_channel = 2;

	ymf7xx_stream_commit_parms(card->pstream);
	ymf7xx_stream_commit_parms(card->rstream);

	ymf7xx_create_channels_list(&card->multi);

	return B_OK;
}


static status_t
ymf7xx_close(void* cookie)
{
	LOG(("close()\n"));
	return B_OK;
}


static status_t
ymf7xx_free(void* cookie)
{
	ymf7xx_dev *card = cookie;
	ymf7xx_stream *stream;
	LOG(("free()\n"));

	/* Halt every stream (clearing YMF7XX_STATE_STARTED, which the ISR
	 * checks) BEFORE deleting the semaphore. Unlike auich/es1370, this
	 * chip's interrupt is a global timer tick that keeps firing for as
	 * long as the DSP engine is running, regardless of any one stream's
	 * state -- deleting the semaphore first leaves a window where a
	 * tick can still call ymf7xx_play_inth()/record_inth(), which calls
	 * release_sem_etc() on an already-deleted semaphore. */
	LIST_FOREACH(stream, &card->streams, next) {
		ymf7xx_stream_halt(stream);
	}

	if (card->buffer_ready_sem > B_OK)
		delete_sem(card->buffer_ready_sem);

	while (!LIST_EMPTY(&card->streams)) {
		ymf7xx_stream_delete(LIST_FIRST(&card->streams));
	}

	card->pstream = NULL;
	card->rstream = NULL;

	return B_OK;
}


static status_t
ymf7xx_control(void* cookie, uint32 op, void* arg, size_t len)
{
	return ymf7xx_multi_control(cookie, op, arg, len);
}


static status_t
ymf7xx_read(void* cookie, off_t position, void *buf, size_t* num_bytes)
{
	*num_bytes = 0;
	return B_IO_ERROR;
}


static status_t
ymf7xx_write(void* cookie, off_t position, const void* buffer, size_t* num_bytes)
{
	*num_bytes = 0;
	return B_IO_ERROR;
}
