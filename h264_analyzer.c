#include "h264_analyzer.h"
#include <string.h>

#define BUFSIZE 32*1024*1024

void h264_analyze_file(const char* filename, nal_callback_t callback, void* user_data) {
    FILE* infile = fopen(filename, "rb");
    if (infile == NULL) return;

    uint8_t* buf = (uint8_t*)malloc(BUFSIZE);
    size_t sz = 0;
    int64_t off = 0;
    uint8_t* p = buf;
    int nal_start, nal_end;

    while (1) {
        size_t rsz = fread(buf + sz, 1, BUFSIZE - sz, infile);
        if (rsz == 0) {
            if (ferror(infile)) break;
            break;
        }
        sz += rsz;

        while (find_nal_unit(p, sz, &nal_start, &nal_end) > 0) {
            int64_t absolute_off = off + (p - buf) + nal_start;
            callback(user_data, absolute_off, p + nal_start, nal_end - nal_start);

            p += nal_end;
            sz -= nal_end;
        }

        if (p == buf) {
            p = buf + sz;
            sz = 0;
        }
        memmove(buf, p, sz);
        off += p - buf;
        p = buf;
    }

    free(buf);
    fclose(infile);
}
