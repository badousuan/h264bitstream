#ifndef DEP_ANALYZER_H
#define DEP_ANALYZER_H

#include "h264_stream.h"
#include <stdint.h>

#define MAX_REF_FRAMES 16 // Maximum number of reference frames we'll track in our DPB

// A structure to hold comprehensive info about a reference picture in the DPB
// This will be our mapping between a frame's actual frame_num and its unique decode_id
typedef struct {
    int frame_num;      // The actual frame_num from the slice header (can wrap around)
    int decode_id;      // The unique, monotonically increasing ID we assign to each frame
} RefPicInfo;

// "Class" representing the dependency analyzer
typedef struct {
    h264_stream_t* h;

    // --- State Management for Dependency Tracking ---
    RefPicInfo dpb[MAX_REF_FRAMES]; // Our simplified Decoded Picture Buffer, now storing more info
    int dpb_size;                   // Current number of pictures in the DPB

    // --- State for correct processing and display ---
    int decode_id_counter;          // A monotonically increasing frame number for user-friendly display

} DepdenceAnalysis;

// Constructor
DepdenceAnalysis* depdence_analysis_new();

// Destructor
void depdence_analysis_free(DepdenceAnalysis* da);

// Method to process a single NAL unit and print dependency info
void depdence_analysis_process_nal(DepdenceAnalysis* da, uint8_t* nal_buf, int nal_size);

#endif //DEP_ANALYZER_H
