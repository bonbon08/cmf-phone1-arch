// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Voice call audio for the CMF Phone 1: the modem's speech path is switched on by
 * MediaTek's Android audio HAL (speech messages to the modem over /dev/ccci_aud, plus
 * the AFE routing). This loads that HAL (audio.primary.mediatek, legacy audio_hw_device
 * interface) through libhybris and puts it into call mode.
 *
 *   tetris-voicecall probe          load and initialise the HAL, print some parameters
 *
 * WORK IN PROGRESS, does not work yet: loaded into a glibc process via libhybris, the
 * HAL crashes in AudioALSAHardware's constructor (AudioALSADeviceParser, a return to
 * address 0 after its PCM scan), with the stock system's libtinyalsa as well
 * (HYBRIS_LD_LIBRARY_PATH=/opt/tetris-audio-compat/lib64:...). The likely way forward is
 * what Droidian does: run the vendor audio HAL service in the container and talk to it
 * over HIDL (android.hardware.audio@7.1 IDevicesFactory -> IDevice.setMode(IN_CALL)).
 */
#include <dlfcn.h>
#include <hybris/common/dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* hardware/libhardware/include/hardware/audio.h: the start of audio_hw_device */
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
	dev->common.close(&dev->common);
	return 0;
}
