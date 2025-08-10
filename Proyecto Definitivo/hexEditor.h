#ifndef HEXEDITOR1_H
#define HEXEDITOR1_H

#include <sys/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void hex_viewer_from_map(unsigned char *map, long map_size, off_t start_offset, size_t view_length);

#ifdef __cplusplus
}
#endif

#endif  