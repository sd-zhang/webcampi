/* SPDX-License-Identifier: MIT
 * Capture-only ALSA extplug. The existing route selects the left I2S slot;
 * this filter consumes mono S32_LE and returns mono S16_LE to alsaloop.
 */
#define _POSIX_C_SOURCE 200809L
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include "voice-filter.h"

struct voice_pcm {
	snd_pcm_extplug_t ext;
	struct voice_filter filter;
	struct audio_shared *shared;
	uint32_t peak, clips, meter_frames;
};

static snd_pcm_sframes_t voice_transfer(snd_pcm_extplug_t *ext,
	const snd_pcm_channel_area_t *dst, snd_pcm_uframes_t dst_offset,
	const snd_pcm_channel_area_t *src, snd_pcm_uframes_t src_offset,
	snd_pcm_uframes_t frames)
{
	struct voice_pcm *v = ext->private_data;
	if (v->shared) {
		uint32_t word=audio_load(&v->shared->requested);
		if(audio_valid_word(word)) voice_filter_update(&v->filter,audio_unpack(word));
	}
	if (src->first % 8 || dst->first % 8 || src->step % 8 || dst->step % 8)
		return -EINVAL;
	const unsigned char *in = (const unsigned char *)src->addr +
		src->first/8 + src_offset*(src->step/8);
	unsigned char *out = (unsigned char *)dst->addr +
		dst->first/8 + dst_offset*(dst->step/8);
	for (snd_pcm_uframes_t i = 0; i < frames; ++i) {
		/* Explicit LE and byte access also handle non-native/aligned areas. */
		uint32_t bits = (uint32_t)in[0] | (uint32_t)in[1] << 8 |
			(uint32_t)in[2] << 16 | (uint32_t)in[3] << 24;
		int32_t sample;
		memcpy(&sample, &bits, sizeof sample);
		int16_t y = voice_filter_sample(&v->filter, sample / 65536.0f);
		if(!v->filter.remaining && v->filter.current.config.bypass) {
			/* Preserve arbitrary S32 exactly, including values rounded by float. */
			uint16_t high=(uint16_t)(bits>>16);memcpy(&y,&high,sizeof y);
		}
		unsigned magnitude=y<0?-(int)y:y;
		if(magnitude>v->peak)v->peak=magnitude;
		v->clips+=(unsigned)v->filter.clipped;
		out[0] = (uint16_t)y & 255;
		out[1] = (uint16_t)y >> 8;
		in += src->step/8;
		out += dst->step/8;
	}
	voice_filter_quiet_tail(&v->filter);
	if(v->shared) {
		if(!v->filter.remaining)audio_store(&v->shared->applied,audio_pack(v->filter.current.config));
		v->meter_frames+=(uint32_t)frames;
		if(v->meter_frames>=4800) {
			struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
			audio_store(&v->shared->peak,v->peak);audio_store(&v->shared->clips,v->clips);
			audio_store(&v->shared->heartbeat,(uint32_t)now.tv_sec);
			v->peak=0;v->meter_frames=0;
		}
	}
	return (snd_pcm_sframes_t)frames;
}

static int voice_init(snd_pcm_extplug_t *ext)
{
	struct voice_pcm *v = ext->private_data;
	if (ext->rate != 48000) return -EINVAL;
	voice_filter_reset(&v->filter);
	v->peak=v->clips=v->meter_frames=0;
	if(v->shared) {
		uint32_t word=audio_load(&v->shared->requested);
		if(audio_valid_word(word))v->filter.current=voice_bank_create(audio_unpack(word));
		audio_store(&v->shared->applied,audio_pack(v->filter.current.config));
		audio_store(&v->shared->heartbeat,0);audio_store(&v->shared->peak,0);audio_store(&v->shared->clips,0);
	}
	return 0;
}

static int voice_close(snd_pcm_extplug_t *ext)
{
	struct voice_pcm *v=ext->private_data;
	if(v->shared) {audio_store(&v->shared->heartbeat,0);munmap(v->shared,sizeof(*v->shared));}
	free(ext->private_data);
	return 0;
}

static const snd_pcm_extplug_callback_t voice_callbacks = {
	.transfer = voice_transfer,
	.init = voice_init,
	.close = voice_close,
};

SND_PCM_PLUGIN_DEFINE_FUNC(pisight_voice)
{
	snd_config_iterator_t i, next;
	snd_config_t *slave = NULL;
	if (stream != SND_PCM_STREAM_CAPTURE) return -EINVAL;
	snd_config_for_each(i, next, conf) {
		snd_config_t *node = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(node, &id) < 0) continue;
		if (!strcmp(id, "type") || !strcmp(id, "comment") || !strcmp(id, "hint"))
			continue;
		if (!strcmp(id, "slave")) slave = node;
		else return -EINVAL;
	}
	if (!slave) return -EINVAL;
	struct voice_pcm *v = calloc(1, sizeof *v);
	if (!v) return -ENOMEM;
	v->ext.version = SND_PCM_EXTPLUG_VERSION;
	v->ext.name = "PiSight configurable voice filter";
	v->ext.callback = &voice_callbacks;
	v->ext.private_data = v;
	int err = snd_pcm_extplug_create(&v->ext, name, root, slave, stream, mode);
	if (err < 0) { free(v); return err; }
	v->shared=audio_map(AUDIO_RUNTIME_PATH);
	if(!v->shared) {snd_pcm_extplug_delete(&v->ext);return -EIO;}
	if ((err = snd_pcm_extplug_set_param(&v->ext, SND_PCM_EXTPLUG_HW_FORMAT, SND_PCM_FORMAT_S16_LE)) < 0 ||
	    (err = snd_pcm_extplug_set_slave_param(&v->ext, SND_PCM_EXTPLUG_HW_FORMAT, SND_PCM_FORMAT_S32_LE)) < 0 ||
	    (err = snd_pcm_extplug_set_param(&v->ext, SND_PCM_EXTPLUG_HW_CHANNELS, 1)) < 0 ||
	    (err = snd_pcm_extplug_set_slave_param(&v->ext, SND_PCM_EXTPLUG_HW_CHANNELS, 1)) < 0) {
		snd_pcm_extplug_delete(&v->ext);
		return err;
	}
	*pcmp = v->ext.pcm;
	return 0;
}

SND_PCM_PLUGIN_SYMBOL(pisight_voice);
