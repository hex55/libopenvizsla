#include "usbpv_cap.h"
#include "libusb.h"
#include "windows.h"
#include <ov.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
																											\
#define ReportError(fmt, ...)                         \
	do {                                                \
		char temp[1024];                                  \
		sprintf_s(temp, sizeof(temp), fmt, ##__VA_ARGS__);\
		if (callback) {                                   \
			callback(context, 0, 0, temp, strlen(temp), -1);\
		}                                                 \
	} while (0)																					

static const char* checkOption(const char* option, const char* name, void* context, pfn_packet_handler callback) {
	char n[128] = "<";
	const char* res = NULL;
	strcat_s(n, sizeof(n), name);
	strcat_s(n, sizeof(n), ",");
	res = strstr(option, n);
	if (!res) {
		ReportError("%s option not found", name);
		return NULL;
	}
	res += strlen(n);
	while (*res && *res == ' ')
		res++;
	return res;
}

typedef struct _info {
	void* context;
	pfn_packet_handler cb;
	struct ov_device* ov;
	HANDLE hThread;

	uint32_t utc_ts;     /**< Seconds since epoch */
	uint32_t last_ov_ts; /**< Last seen OpenVizsla timestamp. Used to detect overflows */
	uint32_t ts_offset;  /**< Timestamp offset in 1/OV_TIMESTAMP_FREQ_HZ units */

	int running;
	union {
		struct ov_packet packet;
		char buf[1024];
	} p;
} info_t;

#define OV_TIMESTAMP_FREQ_HZ (60000000)

static void packet_handler(struct ov_packet* packet, void* data) {
	info_t* info = (info_t*)data;

		uint32_t nsec;
	/* Increment timestamp based on the 60 MHz 24-bit counter value.
	 * Convert remaining clocks to nanoseconds: 1 clk = 1 / 60 MHz = 16.(6) ns
	 */
	uint64_t clks;
	if (packet->timestamp < info->last_ov_ts) {
		info->ts_offset += (1 << 24);
	}
	info->last_ov_ts = packet->timestamp;
	clks = info->ts_offset + packet->timestamp;
	if (clks >= OV_TIMESTAMP_FREQ_HZ) {
		info->utc_ts += 1;
		info->ts_offset -= OV_TIMESTAMP_FREQ_HZ;
		clks -= OV_TIMESTAMP_FREQ_HZ;
	}
	nsec = (clks * 17) - (clks / 3);
	info->cb(info->context, info->utc_ts, nsec, packet->data, packet->size, (int)packet->flags);
}



static DWORD WINAPI dipatch_thread(void* p) {
	info_t* info = (info_t*)p;
	while (info->running) {
		pfn_packet_handler callback = info->cb;
		void* context = info->context;
		int ret = ov_capture_dispatch(info->ov, 100);
		if (ret < 0) {
			ReportError("Cannot dispatch capture : %s", ov_get_error_string(info->ov));
			break;
		}
	}
	return 0;
}

long __cdecl usbpv_get_option(char* option, long length) {

	const char op[] = "<select,Speed,HS,FS,LS><file,Firmware,,(*.fwpkg)>";
	strcpy_s(option, length, op);
	return sizeof(op);
}

void* __cdecl usbpv_open(const char* option, void* context, pfn_packet_handler callback) {
	const char* speed = checkOption(option, "Speed", context, callback);
	const char* firmware = checkOption(option, "Firmware", context, callback);

	if (*firmware == '>') {
		firmware = NULL;
	}

	struct ov_device* ov = ov_new(firmware);
	if (!ov) {
		ReportError("Cannot create ov_device handler with firmware %s", firmware ? firmware : "<preload>");
		return NULL;
	}

	int ret = ov_open(ov);
	if (ret < 0) {
		ReportError("Cannot open OpenVizsla device : %s", ov_get_error_string(ov));
		ov_free(ov);
		return NULL;
	}

	int spd = OV_LOW_SPEED;
	if (speed[0] == 'H') {
		spd == OV_HIGH_SPEED;
	} else if (speed[0] == 'F') {
		spd == OV_FULL_SPEED;
	}
	ret = ov_set_usb_speed(ov, spd);
	if (ret < 0) {
		ReportError("Cannot set USB speed : %s", ov_get_error_string(ov));
		ov_free(ov);
		return NULL;
	}

	info_t* info = malloc(sizeof(info_t));
	info->cb = callback;
	info->context = context;
	info->ov = ov;

	struct timespec ts;
		/* Assume OpenVizsla timestamp value 0 is the current realtime */
#ifdef _MSC_VER
	timespec_get(&ts, TIME_UTC);
#else
	clock_gettime(CLOCK_REALTIME, &ts);
#endif
	info->utc_ts = ts.tv_sec;
	info->last_ov_ts = 0;
	info->ts_offset = (ts.tv_nsec / 17) + (ts.tv_nsec / 850);

	ret = ov_capture_start(ov, &info->p.packet, sizeof(info->p), &packet_handler, info);
	if (ret < 0) {
		ReportError("Cannot start capture : %s", ov_get_error_string(ov));
		ov_free(ov);
		free(info);
		return NULL;
	}
	info->running = 1;
	
	DWORD id;
	info->hThread = CreateThread(NULL, 0, dipatch_thread, info, 0, &id);
	if (info->hThread == NULL) {
		ReportError("Cannot start capture thread :  Error code %d", GetLastError());
		ov_free(ov);
		free(info);
		return NULL;
	}

	return info;
}

long __cdecl usbpv_close(void* handle) {

	if (handle) {
		info_t* info = (info_t*)handle;
		ov_capture_stop(info->ov);
		ov_free(info->ov);
		info->running = 0;

		if (WaitForSingleObject(info->hThread, 2000) == WAIT_TIMEOUT) {
			TerminateThread(info->hThread, 0);
		}
		free(info);
		return 0;
	}
	return -1;
}