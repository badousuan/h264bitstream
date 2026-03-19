#include "dep_analyzer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Forward declarations ---
static void add_frame_to_log(DepdenceAnalysis* da, FrameInfo* fi);
static void update_dpb(DepdenceAnalysis* da, FrameInfo* fi);
static int calculate_poc(FrameInfo* fi, DepdenceAnalysis* da);
static int compare_ints_asc(const void* a, const void* b);

// --- Sorting helpers ---
int compare_frames_by_global_poc(const void* a, const void* b) {
    FrameInfo* frame_a = *(FrameInfo**)a;
    FrameInfo* frame_b = *(FrameInfo**)b;
    return frame_a->global_poc - frame_b->global_poc;
}

// P-frames (L0): sort DPB descending by frame_num
int compare_refpic_by_framenum_desc(const void* a, const void* b) {
    RefPicInfo* pic_a = (RefPicInfo*)a;
    RefPicInfo* pic_b = (RefPicInfo*)b;
    return pic_b->frame_num - pic_a->frame_num;
}

// B-frames: sort past by POC descending, future by POC ascending
int compare_refpic_by_poc_desc(const void* a, const void* b) {
    RefPicInfo* pic_a = *(RefPicInfo**)a;
    RefPicInfo* pic_b = *(RefPicInfo**)b;
    return pic_b->poc - pic_a->poc;
}
int compare_refpic_by_poc_asc(const void* a, const void* b) {
    RefPicInfo* pic_a = *(RefPicInfo**)a;
    RefPicInfo* pic_b = *(RefPicInfo**)b;
    return pic_a->poc - pic_b->poc;
}

// --- "Class" Implementation ---

DepdenceAnalysis* depdence_analysis_new() {
    DepdenceAnalysis* da = (DepdenceAnalysis*)malloc(sizeof(DepdenceAnalysis));
    if (da == NULL) return NULL;

    da->h = h264_new();
    if (da->h == NULL) { free(da); return NULL; }

    da->frame_count = 0;
    da->frame_capacity = 128;
    da->frames = (FrameInfo**)malloc(da->frame_capacity * sizeof(FrameInfo*));
    if (da->frames == NULL) { h264_free(da->h); free(da); return NULL; }

    da->decode_id_counter = 0;
    da->dpb_size = 0;
    da->prev_poc_lsb = 0;
    da->prev_poc_msb = 0;
    da->max_poc_lsb = 0;
    da->global_poc_offset = 0;
    da->max_poc_in_gop = 0;

    return da;
}

void depdence_analysis_free(DepdenceAnalysis* da) {
    if (da != NULL) {
        for (int i = 0; i < da->frame_count; i++) { free(da->frames[i]); }
        free(da->frames);
        h264_free(da->h);
        free(da);
    }
}

// --- Pass 1: Gather Info Safely ---
void depdence_analysis_process_nal(DepdenceAnalysis* da, uint8_t* nal_buf, int nal_size) {
    // CRITICAL FIX: Reset state before parsing to prevent stale data pollution on read failure
    da->h->nal->nal_unit_type = 0;
    da->h->sh->first_mb_in_slice = -1;

    if (read_nal_unit(da->h, nal_buf, nal_size) < 0) {
        return; // Ignore parsing failures or unhandled NALs to prevent ghost frames
    }

    if (da->h->nal->nal_unit_type == NAL_UNIT_TYPE_SPS && da->h->sps->pic_order_cnt_type == 0) {
        da->max_poc_lsb = 1 << (da->h->sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
    }

    // Only process VCL NAL units that start a new picture
    if ((da->h->nal->nal_unit_type == NAL_UNIT_TYPE_CODED_SLICE_IDR ||
         da->h->nal->nal_unit_type == NAL_UNIT_TYPE_CODED_SLICE_NON_IDR) &&
         da->h->sh->first_mb_in_slice == 0) {

        FrameInfo* fi = (FrameInfo*)calloc(1, sizeof(FrameInfo));

        fi->decode_id = da->decode_id_counter++;
        fi->is_idr = (da->h->nal->nal_unit_type == NAL_UNIT_TYPE_CODED_SLICE_IDR);
        fi->is_ref = (da->h->nal->nal_ref_idc != 0);
        fi->frame_num = da->h->sh->frame_num;
        fi->poc_lsb = da->h->sh->pic_order_cnt_lsb;

        // Active minus 1 needs +1 for total count
        fi->num_ref_idx_l0 = da->h->sh->num_ref_idx_l0_active_minus1 + 1;
        fi->num_ref_idx_l1 = da->h->sh->num_ref_idx_l1_active_minus1 + 1;

        int slice_type_mod = da->h->sh->slice_type % 5;
        if (fi->is_idr || slice_type_mod == SH_SLICE_TYPE_I || slice_type_mod == SH_SLICE_TYPE_SI) fi->frame_type = 'I';
        else if (slice_type_mod == SH_SLICE_TYPE_P || slice_type_mod == SH_SLICE_TYPE_SP) fi->frame_type = 'P';
        else fi->frame_type = 'B';

        fi->poc = calculate_poc(fi, da);

        if (fi->is_idr) {
            da->global_poc_offset += da->max_poc_in_gop + 2;
            da->max_poc_in_gop = 0;
            da->dpb_size = 0; // Clear DPB on IDR
        }

        fi->global_poc = da->global_poc_offset + fi->poc;
        if (fi->poc > da->max_poc_in_gop) {
            da->max_poc_in_gop = fi->poc;
        }

        // --- Step 1: 解析 l0 和 l1, 查看在 DPB 里依赖了哪些帧 ---
        if (fi->frame_type == 'P') {
            RefPicInfo temp_dpb[MAX_REF_FRAMES];
            memcpy(temp_dpb, da->dpb, da->dpb_size * sizeof(RefPicInfo));
            qsort(temp_dpb, da->dpb_size, sizeof(RefPicInfo), compare_refpic_by_framenum_desc);

            for (int i = 0; i < fi->num_ref_idx_l0 && i < da->dpb_size; i++) {
                fi->dependencies[fi->dep_count++] = temp_dpb[i].decode_id;
            }

        } else if (fi->frame_type == 'B') {
            RefPicInfo* past[MAX_REF_FRAMES];
            RefPicInfo* future[MAX_REF_FRAMES];
            int past_count = 0;
            int future_count = 0;

            for (int i = 0; i < da->dpb_size; i++) {
                if (da->dpb[i].poc < fi->global_poc) past[past_count++] = &da->dpb[i];
                else future[future_count++] = &da->dpb[i];
            }

            qsort(past, past_count, sizeof(RefPicInfo*), compare_refpic_by_poc_desc);
            qsort(future, future_count, sizeof(RefPicInfo*), compare_refpic_by_poc_asc);

            int l0_count = 0;
            for (int i = 0; i < past_count && l0_count < fi->num_ref_idx_l0; i++, l0_count++) {
                fi->dependencies[fi->dep_count++] = past[i]->decode_id;
            }
            for (int i = 0; i < future_count && l0_count < fi->num_ref_idx_l0; i++, l0_count++) {
                fi->dependencies[fi->dep_count++] = future[i]->decode_id;
            }

            int l1_count = 0;
            for (int i = 0; i < future_count && l1_count < fi->num_ref_idx_l1; i++, l1_count++) {
                int is_dup = 0;
                for (int j=0; j<fi->dep_count; j++) { if (fi->dependencies[j] == future[i]->decode_id) { is_dup = 1; break; } }
                if (!is_dup) fi->dependencies[fi->dep_count++] = future[i]->decode_id;
            }
            for (int i = 0; i < past_count && l1_count < fi->num_ref_idx_l1; i++, l1_count++) {
                int is_dup = 0;
                for (int j=0; j<fi->dep_count; j++) { if (fi->dependencies[j] == past[i]->decode_id) { is_dup = 1; break; } }
                if (!is_dup) fi->dependencies[fi->dep_count++] = past[i]->decode_id;
            }
        }

        add_frame_to_log(da, fi);

        // --- Step 2: 按 264 规则更新 DPB, 将当前帧加入 DPB ---
        if (fi->is_ref) {
            update_dpb(da, fi);
        }
    }
}

// --- Pass 2: Report Results (实现了“等两帧间的其他帧解析完再输出”的效果) ---
void depdence_analysis_report_results(DepdenceAnalysis* da) {
    if (da->frame_count == 0) { printf("No frames found to analyze.\n"); return; }

    // Sort all gathered frames by Global POC to get the exact Display Order
    qsort(da->frames, da->frame_count, sizeof(FrameInfo*), compare_frames_by_global_poc);

    // Build mapping from Decode_ID to Display_ID (Display_ID is 1-based index)
    int* decode_to_display_map = (int*)malloc(da->decode_id_counter * sizeof(int));
    if(!decode_to_display_map) { return; }
    for (int i = 0; i < da->frame_count; i++) {
        decode_to_display_map[da->frames[i]->decode_id] = i + 1; // 1, 2, 3...
    }

    // Print the results in strict display order
    for (int i = 0; i < da->frame_count; i++) {
        FrameInfo* fi = da->frames[i];
        int display_id = i + 1;
        printf("Frame %d (Type: %c)", display_id, fi->frame_type);

        if (fi->frame_type == 'I') {
            printf(" -> is an Intra frame.\n");
            continue;
        }
        if (fi->dep_count == 0) {
            printf(" -> no dependencies found.\n");
            continue;
        }

        printf(" -> depends on [");

        int* dep_display_ids = (int*)malloc(fi->dep_count * sizeof(int));
        int dep_display_count = 0;
        for (int j = 0; j < fi->dep_count; j++) {
            dep_display_ids[dep_display_count++] = decode_to_display_map[fi->dependencies[j]];
        }

        qsort(dep_display_ids, dep_display_count, sizeof(int), compare_ints_asc);

        for (int j = 0; j < dep_display_count; j++) {
            printf("%d%s", dep_display_ids[j], (j == dep_display_count - 1) ? "" : ", ");
        }
        printf("]\n");
        free(dep_display_ids);
    }

    free(decode_to_display_map);
}

// --- Internal Helper Functions ---
static void add_frame_to_log(DepdenceAnalysis* da, FrameInfo* fi) {
    if (da->frame_count >= da->frame_capacity) {
        da->frame_capacity *= 2;
        da->frames = (FrameInfo**)realloc(da->frames, da->frame_capacity * sizeof(FrameInfo*));
    }
    da->frames[da->frame_count++] = fi;
}

static void update_dpb(DepdenceAnalysis* da, FrameInfo* fi) {
    for (int i = 0; i < da->dpb_size; i++) {
        if (da->dpb[i].frame_num == fi->frame_num) {
            da->dpb[i].decode_id = fi->decode_id;
            da->dpb[i].poc = fi->global_poc;
            return;
        }
    }

    // DPB full? Evict oldest frame based on decode order
    if (da->dpb_size >= MAX_REF_FRAMES) {
        int oldest_idx = 0;
        for(int i = 1; i < da->dpb_size; ++i) {
            if(da->dpb[i].decode_id < da->dpb[oldest_idx].decode_id) {
                oldest_idx = i;
            }
        }
        da->dpb[oldest_idx] = da->dpb[da->dpb_size - 1];
        da->dpb_size--;
    }

    da->dpb[da->dpb_size].frame_num = fi->frame_num;
    da->dpb[da->dpb_size].decode_id = fi->decode_id;
    da->dpb[da->dpb_size].poc = fi->global_poc;
    da->dpb_size++;
}

static int calculate_poc(FrameInfo* fi, DepdenceAnalysis* da) {
    if (da->h->sps->pic_order_cnt_type == 0) {
        if (da->max_poc_lsb == 0) return fi->decode_id; // Failsafe
        int poc_msb;
        if (fi->is_idr) {
            poc_msb = 0;
        } else {
            if ((fi->poc_lsb < da->prev_poc_lsb) && ((da->prev_poc_lsb - fi->poc_lsb) >= (da->max_poc_lsb / 2))) {
                poc_msb = da->prev_poc_msb + da->max_poc_lsb;
            } else if ((fi->poc_lsb > da->prev_poc_lsb) && ((fi->poc_lsb - da->prev_poc_lsb) > (da->max_poc_lsb / 2))) {
                poc_msb = da->prev_poc_msb - da->max_poc_lsb;
            } else {
                poc_msb = da->prev_poc_msb;
            }
        }
        int poc = poc_msb + fi->poc_lsb;
        if (fi->is_ref) {
            da->prev_poc_msb = poc_msb;
            da->prev_poc_lsb = fi->poc_lsb;
        }
        if (fi->is_idr) {
            da->prev_poc_msb = 0;
            da->prev_poc_lsb = 0;
        }
        return poc;
    } else if (da->h->sps->pic_order_cnt_type == 2) {
        return 2 * fi->frame_num; // Basic support for POC Type 2
    }
    return fi->decode_id; // Fallback
}

static int compare_ints_asc(const void* a, const void* b) {
    return (*(int*)a - *(int*)b);
}
