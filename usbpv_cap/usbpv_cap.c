#include "libusb.h"
#include "usbpv_cap.h"
#include "windows.h"



long __cdecl usbpv_get_option(char* option, long length) {
	return length;
}

void* __cdecl usbpv_open(const char* option, void* context, pfn_packet_handler callback) {
	return NULL;
}

long __cdecl usbpv_close(void* handle) {
	return 0;
}