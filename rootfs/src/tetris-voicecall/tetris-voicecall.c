// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Voice call audio for the CMF Phone 1: the modem's speech path is switched on by
 * MediaTek's Android audio HAL (speech messages to the modem over /dev/ccci_aud, plus
 * the AFE routing). This loads that HAL (audio.primary.mediatek, legacy audio_hw_device
 * interface) through libhybris and puts it into call mode.
 *
 *   tetris-voicecall probe          load and initialise the HAL, print some parameters
 *   tetris-voicecall mode SECONDS   test: call mode (AUDIO_MODE_IN_CALL) for a while
 *   tetris-voicecall call SECONDS [speaker]   call mode with the earpiece (or speaker)
 *                                   routed, which opens the modem's speech path
 *
 * WORK IN PROGRESS (2026-11-05). With the stock (Android 14) system libraries first in
 * HYBRIS_LD_LIBRARY_PATH the HAL initialises (with Halium's Android 16 libs it crashed):
 *   HYBRIS_LD_LIBRARY_PATH=/run/tetris-stock-system/system/lib64:/vendor/lib64:\
 *       /vendor/lib64/hw:/apex/com.android.i18n/lib64   (system_a mounted there)
 * "call" then really switches the speech path on: AudioALSASpeechPhoneCallController
 * opens, SpeechDriverNormal sends SPH_ON (0x2f20) to the modem, modem PCM at 48 kHz.
 * But in a test call nothing was heard either way. Leads: the speech runs "opendsp" on
 * the SCP (RISC-V, no separate ADSP on MT6878), and audio_ipi says
 * "audio_ipi_dma_alloc_region(), dsp_id(6) invalid"; the smart PA library didn't load.
 */
#include <dlfcn.h>
#include <hybris/common/dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* hardware/libhardware/include/hardware/hardware.h (LP64) */
struct hw_module_t;
struct hw_device_t;
struct hw_module_methods_t {
	int (*open)(const struct hw_module_t *module, const char *id, struct hw_device_t **device);
};
struct hw_module_t {
	uint32_t tag;
	uint16_t module_api_version;
	uint16_t hal_api_version;
	const char *id;
	const char *name;
	const char *author;
	struct hw_module_methods_t *methods;
	void *dso;
	uint64_t reserved[32 - 7];
};
struct hw_device_t {
	uint32_t tag;
	uint32_t version;
	struct hw_module_t *module;
	uint64_t reserved[12];
	int (*close)(struct hw_device_t *device);
};

/* hardware/libhardware/include/hardware/audio.h: the start of audio_stream (also the
 * start of audio_stream_out) and of audio_hw_device */
struct audio_stream {
	uint32_t (*get_sample_rate)(const struct audio_stream *stream);
	int (*set_sample_rate)(struct audio_stream *stream, uint32_t rate);
	size_t (*get_buffer_size)(const struct audio_stream *stream);
	uint32_t (*get_channels)(const struct audio_stream *stream);
	uint32_t (*get_format)(const struct audio_stream *stream);
	int (*set_format)(struct audio_stream *stream, uint32_t format);
	int (*standby)(struct audio_stream *stream);
	int (*dump)(const struct audio_stream *stream, int fd);
	uint32_t (*get_device)(const struct audio_stream *stream);
	int (*set_device)(struct audio_stream *stream, uint32_t device);
	int (*set_parameters)(struct audio_stream *stream, const char *kv_pairs);
};

/* audio_config_t: sample_rate, channel_mask, format, offload_info, frame_count; zeroed
 * beyond the first three (offload_info.version/size get set below) */
struct audio_config {
	uint32_t sample_rate;
	uint32_t channel_mask;
	uint32_t format;
	uint8_t rest[256];
};

#define AUDIO_DEVICE_OUT_EARPIECE   0x1
#define AUDIO_DEVICE_OUT_SPEAKER    0x2
#define AUDIO_OUTPUT_FLAG_PRIMARY   0x2
#define AUDIO_CHANNEL_OUT_STEREO    0x3
#define AUDIO_FORMAT_PCM_16_BIT     0x1
#define AUDIO_MODE_NORMAL           0
#define AUDIO_MODE_IN_CALL          2
struct audio_hw_device {
	struct hw_device_t common;
	uint32_t (*get_supported_devices)(const struct audio_hw_device *dev);
	int (*init_check)(const struct audio_hw_device *dev);
	int (*set_voice_volume)(struct audio_hw_device *dev, float volume);
	int (*set_master_volume)(struct audio_hw_device *dev, float volume);
	int (*get_master_volume)(struct audio_hw_device *dev, float *volume);
	int (*set_mode)(struct audio_hw_device *dev, int mode);
	int (*set_mic_mute)(struct audio_hw_device *dev, int state);
	int (*get_mic_mute)(const struct audio_hw_device *dev, int *state);
	int (*set_parameters)(struct audio_hw_device *dev, const char *kv_pairs);
	char *(*get_parameters)(const struct audio_hw_device *dev, const char *keys);
	size_t (*get_input_buffer_size)(const struct audio_hw_device *dev, const void *config);
	int (*open_output_stream)(struct audio_hw_device *dev, int handle, uint32_t devices,
				  uint32_t flags, void *config, struct audio_stream **stream_out,
				  const char *address);
	void (*close_output_stream)(struct audio_hw_device *dev, struct audio_stream *stream_out);
};

#define AUDIO_HARDWARE_MODULE_ID "audio"
#define AUDIO_HARDWARE_INTERFACE "audio_hw_if"

int main(int argc, char **argv)
{
	int (*get_module)(const char *, const char *, const struct hw_module_t **);
	const struct hw_module_t *module = NULL;
	struct audio_hw_device *dev = NULL;
	void *lib;
	int ret;

	setvbuf(stdout, NULL, _IOLBF, 0);
	lib = hybris_dlopen("libhardware.so", RTLD_NOW);
	if (!lib) {
		fprintf(stderr, "libhardware: %s\n", hybris_dlerror());
		return 1;
	}
	*(void **)&get_module = hybris_dlsym(lib, "hw_get_module_by_class");
	ret = get_module(AUDIO_HARDWARE_MODULE_ID, "primary", &module);
	printf("hw_get_module_by_class: %d %s (api %x)\n", ret, module ? module->name : "-",
	       module ? module->module_api_version : 0);
	if (ret)
		return 1;
	ret = module->methods->open(module, AUDIO_HARDWARE_INTERFACE, (struct hw_device_t **)&dev);
	printf("open: %d, device version %x\n", ret, dev ? dev->common.version : 0);
	if (ret)
		return 1;
	printf("init_check: %d\n", dev->init_check(dev));
	if (argc > 1 && !strcmp(argv[1], "probe")) {
		char *p = dev->get_parameters(dev, "MTK_AUDENH_SUPPORT;GetSpeechCalibrationStatus");

		printf("parameters: %s\n", p ? p : "(null)");
	}
	if (argc > 2 && !strcmp(argv[1], "call")) {
		uint32_t device = argc > 3 && !strcmp(argv[3], "speaker") ?
			AUDIO_DEVICE_OUT_SPEAKER : AUDIO_DEVICE_OUT_EARPIECE;
		struct audio_config cfg = { 48000, AUDIO_CHANNEL_OUT_STEREO, AUDIO_FORMAT_PCM_16_BIT };
		struct audio_stream *out = NULL;
		char routing[32];

		printf("set_mode(IN_CALL): %d\n", dev->set_mode(dev, AUDIO_MODE_IN_CALL));
		ret = dev->open_output_stream(dev, 13, device, AUDIO_OUTPUT_FLAG_PRIMARY, &cfg, &out, "");
		printf("open_output_stream: %d\n", ret);
		if (!ret && out) {
			snprintf(routing, sizeof(routing), "routing=%u", device);
			printf("%s: %d\n", routing, out->set_parameters(out, routing));
		}
		sleep(atoi(argv[2]));
		printf("set_mode(NORMAL): %d\n", dev->set_mode(dev, AUDIO_MODE_NORMAL));
		if (out)
			dev->close_output_stream(dev, out);
	}
	if (argc > 2 && !strcmp(argv[1], "mode")) {
		printf("set_mode(IN_CALL): %d\n", dev->set_mode(dev, 2));
		sleep(atoi(argv[2]));
		printf("set_mode(NORMAL): %d\n", dev->set_mode(dev, 0));
	}
	dev->common.close(&dev->common);
	return 0;
}
