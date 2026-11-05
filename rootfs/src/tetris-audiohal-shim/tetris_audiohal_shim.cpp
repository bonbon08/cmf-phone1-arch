// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LD_PRELOAD shim for MediaTek's audio HAL service in the Halium container. Halium has no
 * linkerconfig, so the vendor service and libbinder_ndk share one libbinder, i.e. one
 * ProcessState; on Android they are separate (vendor: /dev/vndbinder, NDK: /dev/binder).
 * - The service switches its ProcessState to /dev/vndbinder: the NDK side then registers
 *   its AIDL services with the vndservicemanager and fails. Stay on /dev/binder.
 * - Its late ABinderProcess_setThreadPoolMaxThreadCount() would shrink the already
 *   running pool, which libbinder aborts on. Keep the pool as it is.
 * Built freestanding (no libc/libc++): the symbols it calls are resolved in the process.
 */
#include <stdint.h>

struct sp_state {           // android::sp<ProcessState>: one pointer, but not trivially
	void *ptr;          // copyable, so returned through x8 like the real one
	~sp_state() {}
};

namespace android {
struct ProcessState {
	static sp_state self();
	static sp_state initWithDriver(const char *driver);
};

__attribute__((visibility("default")))
sp_state ProcessState::initWithDriver(const char *driver)
{
	(void)driver;
	return self();
}
} // namespace android

extern "C" __attribute__((visibility("default")))
bool ABinderProcess_setThreadPoolMaxThreadCount(uint32_t numThreads)
{
	(void)numThreads;
	return true;
}
