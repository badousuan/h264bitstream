#include <stdio.h>
#include <stdlib.h>
#include "dep_analyzer.h"

#define BUF_SIZE 1024 * 1024 // 1MB buffer for reading file

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.h264>\n", argv[0]);
        return 1;
    }

    const char* filename = argv[1];
    FILE* f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: Could not open file %s\n", filename);
        return 1;
    }

    // Read the entire file into a buffer
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* file_buffer = (uint8_t*)malloc(file_size);
    if (!file_buffer) {
        fprintf(stderr, "Error: Could not allocate memory for file buffer\n");
        fclose(f);
        return 1;
    }

    if (fread(file_buffer, 1, file_size, f) != file_size) {
        fprintf(stderr, "Error: Could not read the entire file\n");
        free(file_buffer);
        fclose(f);
        return 1;
    }
    fclose(f);

    printf("Analyzing file: %s (%ld bytes)\n", filename, file_size);

    DepdenceAnalysis* da = depdence_analysis_new();
    if (!da) {
        fprintf(stderr, "Error: Could not create DepdenceAnalysis\n");
        free(file_buffer);
        return 1;
    }

    int nal_start, nal_end;
    uint8_t* buf = file_buffer;
    long size = file_size;

    while (find_nal_unit(buf, size, &nal_start, &nal_end) > 0) {
        // Pass the found NAL unit to the analyzer
        // Note: We pass the pointer to the start of the NAL unit and its size
        depdence_analysis_process_nal(da, buf + nal_start, nal_end - nal_start);

        // Move buffer pointer and size to the next search position
        buf += nal_end;
        size -= nal_end;
    }

    // Cleanup
    depdence_analysis_free(da);
    free(file_buffer);

    printf("Analysis complete.\n");

    return 0;
}
