#include "h264_stream.h"
#include "h264_analyzer.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#if (defined(__GNUC__))
#define HAVE_GETOPT_LONG
#include <getopt.h>

static struct option long_options[] =
{
    { "probe",   no_argument, NULL, 'p'},
    { "output",  required_argument, NULL, 'o'},
    { "help",    no_argument,       NULL, 'h'},
    { "verbose", required_argument, NULL, 'v'},
};
#endif

static char options[] =
"\t-o output_file, defaults to test.264\n"
"\t-v verbose_level, print more info\n"
"\t-p print codec for HTML5 video tag's codecs parameter, per RFC6381\n"
"\t-h print this message and exit\n";

void usage( )
{
    fprintf( stderr, "h264_analyze, version 0.2.0\n");
    fprintf( stderr, "Analyze H.264 bitstreams in Annex B format\n");
    fprintf( stderr, "Usage: \n");
    fprintf( stderr, "h264_analyze [options] <input bitstream>\noptions:\n%s\n", options);
}

typedef struct {
    int opt_verbose;
    int opt_probe;
    h264_stream_t* h;
    int probe_done;
} analyze_ctx_t;

void nal_callback(void* user_data, int64_t offset, uint8_t* data, int size) {
    analyze_ctx_t* ctx = (analyze_ctx_t*)user_data;
    if (ctx->probe_done) return;

    if (ctx->opt_verbose > 0) {
        fprintf(h264_dbgfile, "!! Found NAL at offset %lld (0x%04llX), size %lld (0x%04llX) \n",
                (long long int)offset, (long long int)offset,
                (long long int)size, (long long int)size);
    }

    read_debug_nal_unit(ctx->h, data, size);

    if (ctx->opt_probe && ctx->h->nal->nal_unit_type == NAL_UNIT_TYPE_SPS) {
        int constraint_byte = ctx->h->sps->constraint_set0_flag << 7;
        constraint_byte |= ctx->h->sps->constraint_set1_flag << 6;
        constraint_byte |= ctx->h->sps->constraint_set2_flag << 5;
        constraint_byte |= ctx->h->sps->constraint_set3_flag << 4;
        constraint_byte |= ctx->h->sps->constraint_set4_flag << 3;

        fprintf(h264_dbgfile, "codec: avc1.%02X%02X%02X\n", ctx->h->sps->profile_idc, constraint_byte, ctx->h->sps->level_idc);
        ctx->probe_done = 1;
    }
}

int main(int argc, char *argv[])
{
    analyze_ctx_t ctx;
    ctx.opt_verbose = 1;
    ctx.opt_probe = 0;
    ctx.probe_done = 0;
    ctx.h = h264_new();

    if (argc < 2) { usage(); return EXIT_FAILURE; }

#ifdef HAVE_GETOPT_LONG
    int c;
    int long_options_index;
    while ( ( c = getopt_long( argc, argv, "o:phv:", long_options, &long_options_index) ) != -1 )
    {
        switch ( c )
        {
            case 'o':
                if (h264_dbgfile == NULL) { h264_dbgfile = fopen( optarg, "wt"); }
                break;
            case 'p':
                ctx.opt_probe = 1;
                ctx.opt_verbose = 0;
                break;
            case 'v':
                ctx.opt_verbose = atoi( optarg );
                break;
            case 'h':
            default:
                usage( );
                return 1;
        }
    }
    const char* input_file = argv[optind];
#else
    const char* input_file = argv[1];
#endif

    if (input_file == NULL) { usage(); return EXIT_FAILURE; }

    if (h264_dbgfile == NULL) { h264_dbgfile = stdout; }

    h264_analyze_file(input_file, nal_callback, &ctx);

    h264_free(ctx.h);
    if (h264_dbgfile != stdout) fclose(h264_dbgfile);

    return 0;
}
