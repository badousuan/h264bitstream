#include "dep_analyzer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper function for qsort to sort reference pictures by frame_num in descending order
static int compare_ref_pics_desc(const void* a, const void* b) {
    RefPicInfo* pic_a = (RefPicInfo*)a;
    RefPicInfo* pic_b = (RefPicInfo*)b;
    return pic_b->frame_num - pic_a->frame_num;
}

// Helper for sorting integers for unique-ing
static int compare_ints_asc(const void* a, const void* b) {
    return (*(int*)a - *(int*)b);
}

// --- Forward declarations of static helper functions ---
static void add_pic_to_dpb(DepdenceAnalysis* da, int frame_num, int decode_id);
static void print_dependencies(DepdenceAnalysis* da, slice_header_t* sh);

// --- "Class" Implementation ---

DepdenceAnalysis* depdence_analysis_new() {
    DepdenceAnalysis* da = (DepdenceAnalysis*)malloc(sizeof(DepdenceAnalysis));
    if (da == NULL) return NULL;

    da->h = h264_new();
    if (da->h == NULL) {
        free(da);
        return NULL;
    }

    da->dpb_size = 0;
    da->decode_id_counter = 0;
    memset(da->dpb, 0, sizeof(da->dpb));

    return da;
}

void depdence_analysis_free(DepdenceAnalysis* da) {
    if (da != NULL) {
        h264_free(da->h);
        free(da);
    }
}

static const char* slice_type_to_string(int slice_type) {
    switch (slice_type % 5) {
        case SH_SLICE_TYPE_P: return "P";
        case SH_SLICE_TYPE_B: return "B";
        case SH_SLICE_TYPE_I: return "I";
        case SH_SLICE_TYPE_SP: return "SP";
        case SH_SLICE_TYPE_SI: return "SI";
    }
    return "Unknown";
}

void depdence_analysis_process_nal(DepdenceAnalysis* da, uint8_t* nal_buf, int nal_size) {
    read_nal_unit(da->h, nal_buf, nal_size);

    nal_t* nal = da->h->nal;
    slice_header_t* sh = da->h->sh;

    switch (nal->nal_unit_type) {
        case NAL_UNIT_TYPE_CODED_SLICE_IDR:
        case NAL_UNIT_TYPE_CODED_SLICE_NON_IDR: {
            if (sh->first_mb_in_slice == 0) {
                int current_decode_id = da->decode_id_counter;

                if (nal->nal_unit_type == NAL_UNIT_TYPE_CODED_SLICE_IDR) {
                    printf("Frame %d (Type: I, IDR) -> is an Intra frame, clears DPB.\n", current_decode_id);
                    da->dpb_size = 0;
                } else {
                    printf("Frame %d (Type: %s)", current_decode_id, slice_type_to_string(sh->slice_type));
                    print_dependencies(da, sh);
                }

                if (nal->nal_ref_idc != 0) {
                    add_pic_to_dpb(da, sh->frame_num, current_decode_id);
                }

                da->decode_id_counter++;
            } else {
                // This is another slice of a frame we've already processed.
                // We still need to update the DPB if this slice makes the frame a reference frame.
                if (nal->nal_ref_idc != 0) {
                    // Find the decode_id for the current frame_num. Since it's not the first slice,
                    // it must have been added to the DPB already if it was a reference frame.
                    // This part is tricky and for simplicity, we assume the first slice of a ref frame
                    // already added it to the DPB.
                }
            }
            break;
        }
        default:
            break;
    }
}

// --- Helper Function Implementations ---

static void add_pic_to_dpb(DepdenceAnalysis* da, int frame_num, int decode_id) {
    for (int i = 0; i < da->dpb_size; i++) {
        if (da->dpb[i].frame_num == frame_num) {
            // Update decode_id if it's a new instance of a wrapped-around frame_num
            da->dpb[i].decode_id = decode_id;
            return;
        }
    }

    if (da->dpb_size >= MAX_REF_FRAMES) {
        qsort(da->dpb, da->dpb_size, sizeof(RefPicInfo), compare_ref_pics_desc);
        da->dpb_size--;
    }

    da->dpb[da->dpb_size].frame_num = frame_num;
    da->dpb[da->dpb_size].decode_id = decode_id;
    da->dpb_size++;
}

static void print_dependencies(DepdenceAnalysis* da, slice_header_t* sh) {
    int slice_type_mod = sh->slice_type % 5;

    if (slice_type_mod == SH_SLICE_TYPE_I || slice_type_mod == SH_SLICE_TYPE_SI) {
        printf(" -> is an Intra frame, no inter-frame dependencies.\n");
        return;
    }

    qsort(da->dpb, da->dpb_size, sizeof(RefPicInfo), compare_ref_pics_desc);

    int dep_framenum_list[MAX_REF_FRAMES * 2];
    int dep_count = 0;

    if (slice_type_mod == SH_SLICE_TYPE_P || slice_type_mod == SH_SLICE_TYPE_SP) {
        int num_l0_refs = sh->num_ref_idx_l0_active_minus1 + 1;
        for (int i = 0; i < num_l0_refs && i < da->dpb_size; i++) {
            dep_framenum_list[dep_count++] = da->dpb[i].frame_num;
        }
    } else if (slice_type_mod == SH_SLICE_TYPE_B) {
        int num_l0_refs = sh->num_ref_idx_l0_active_minus1 + 1;
        for (int i = 0; i < num_l0_refs && i < da->dpb_size; i++) {
            dep_framenum_list[dep_count++] = da->dpb[i].frame_num;
        }
        int num_l1_refs = sh->num_ref_idx_l1_active_minus1 + 1;
        for (int i = 0; i < num_l1_refs && i < da->dpb_size; i++) {
            dep_framenum_list[dep_count++] = da->dpb[i].frame_num;
        }
    }

    if (dep_count == 0) {
        printf(" -> has no available reference frames in DPB.\n");
        return;
    }

    // --- Translate frame_num to decode_id ---
    int dep_decodeid_list[MAX_REF_FRAMES * 2];
    int decodeid_count = 0;
    for (int i = 0; i < dep_count; i++) {
        for (int j = 0; j < da->dpb_size; j++) {
            if (da->dpb[j].frame_num == dep_framenum_list[i]) {
                dep_decodeid_list[decodeid_count++] = da->dpb[j].decode_id;
                break; // Found mapping, move to next dependency
            }
        }
    }

    if (decodeid_count == 0) {
        printf(" -> could not map any dependencies to a decode_id.\n");
        return;
    }

    qsort(dep_decodeid_list, decodeid_count, sizeof(int), compare_ints_asc);

    int unique_count = 0;
    if (decodeid_count > 0) {
        unique_count = 1;
        for (int i = 1; i < decodeid_count; i++) {
            if (dep_decodeid_list[i] != dep_decodeid_list[unique_count - 1]) {
                dep_decodeid_list[unique_count++] = dep_decodeid_list[i];
            }
        }
    }

    printf(" -> depends on [");
    for (int i = 0; i < unique_count; i++) {
        printf("%d%s", dep_decodeid_list[i], (i == unique_count - 1) ? "" : ", ");
    }
    printf("]\n");
}
