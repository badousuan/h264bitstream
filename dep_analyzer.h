#ifndef DEP_ANALYZER_H
#define DEP_ANALYZER_H

#include "h264_stream.h"
#include <stdint.h>

#define MAX_DEPS 32       // Max dependencies a single frame can have
#define MAX_REF_FRAMES 16 // Max reference frames in our simulated DPB

// This structure holds all information we gather about a single frame during the first pass.
typedef struct {
    int decode_id;          // Unique ID assigned in decode order (0, 1, 2...)
    int poc;                // Picture Order Count (local to GOP)
    int global_poc;         // Global POC used for sorting across the whole video
    char frame_type;        // 'I', 'P', 'B'
    int is_idr;             // Flag if it's an IDR frame
    int is_ref;             // Flag if it's a reference frame (nal_ref_idc != 0)

    // Copied from slice header/PPS to isolate from parsing side-effects
    int frame_num;
    int poc_lsb;
    int num_ref_idx_l0;
    int num_ref_idx_l1;

    int dependencies[MAX_DEPS]; // List of dependencies, stored as DECODE_IDs.
    int dep_count;
} FrameInfo;

// This structure holds info about a picture in the Decoded Picture Buffer (DPB).
typedef struct {
    int frame_num;
    int decode_id;
    int poc;                // Needed for B-frame ref list construction rules
} RefPicInfo;

// The main analyzer "class" managing the state.
typedef struct {
    h264_stream_t* h;

    // --- Pass 1: Information Gathering & State ---
    FrameInfo** frames;         // A dynamic array to log info for every frame.
    int frame_count;            // Number of frames logged so far.
    int frame_capacity;         // Allocated capacity of the frames array.

    // Internal state for Pass 1
    int decode_id_counter;
    RefPicInfo dpb[MAX_REF_FRAMES]; // The simulated Decoded Picture Buffer.
    int dpb_size;

    // POC calculation state
    int prev_poc_lsb;
    int prev_poc_msb;
    int max_poc_lsb;

    int global_poc_offset;      // Used to make POC monotonically increasing across IDRs
    int max_poc_in_gop;

} DepdenceAnalysis;


// --- Public API ---
DepdenceAnalysis* depdence_analysis_new();
void depdence_analysis_free(DepdenceAnalysis* da);
void depdence_analysis_process_nal(DepdenceAnalysis* da, uint8_t* nal_buf, int nal_size);
void depdence_analysis_report_results(DepdenceAnalysis* da);

#endif //DEP_ANALYZER_H
