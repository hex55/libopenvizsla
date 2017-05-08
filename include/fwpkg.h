#ifndef _FWPKG_H
#define _FWPKG_H

#include <zip.h>

struct fwpkg {
	struct zip* pkg;
	zip_uint64_t map_index;
	zip_uint64_t bitstream_index;
	const char* error_str;
};

struct fwpkg* fwpkg_new();
int fwpkg_from_file(struct fwpkg* fwpkg, const char* filename);
void fwpkg_free(struct fwpkg* fwpkg);

const char* fwpkg_get_error_string(struct fwpkg* fwpkg);

int fwpkg_read_map(struct fwpkg* fwpkg, void* buf, size_t* size);
int fwpkg_read_bitstream(struct fwpkg* fwpkg, void* buf, size_t* size);

size_t fwpkg_map_size(struct fwpkg* fwpkg);
size_t fwpkg_bitstream_size(struct fwpkg* fwpkg);

#endif // _FWPKG_H
