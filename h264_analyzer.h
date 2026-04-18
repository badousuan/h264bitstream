#ifndef H264_ANALYZER_H
#define H264_ANALYZER_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "h264_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*nal_callback_t)(void* user_data, int64_t offset, uint8_t* data, int size);

void h264_analyze_file(const char* filename, nal_callback_t callback, void* user_data);

#ifdef __cplusplus
}
#endif

#endif
