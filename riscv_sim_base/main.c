#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_state.h"
#include "memory.h"
#include "isa_registry.h"
#include "rv32i_base.h"
#include "assembler.h"
#include "cli_display.h"
#include "sim_engine.h"
#include "pipeline_state.h"

#define DEFAULT_MEM_SIZE   (1u << 20)   /* 1 MB */
#define DEFAULT_MAX_CYCLES 1000000ull

enum { HZ_UNSET = -1, HZ_NONE = 0, HZ_STALL = 1, HZ_FORWARD = 2 };

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s <program.s> [options]\n"
        "  (no mode option)           interactive debugger on the default pipeline\n"
        "  --batch                    run to completion, then dump state\n"
        "  --single-cycle             use the single-cycle oracle engine\n"
        "  --hazard none|stall|forward  pipeline hazard unit\n"
        "  --predictor <name>         not-taken | btfn | 1bit | 2bit  (alias: --predict)\n"
        "  --pipeline                 same as --hazard stall (no predictor)\n"
        "  --pipeline-forward         same as --hazard forward (no predictor)\n"
        "  --trace                    print one line per cycle (pipeline stages)\n"
        "  --max-cycles N             cycle limit (default %llu)\n"
        "  --mem-size N               memory size in bytes (default %u)\n"
        "Defaults: with no --hazard/--predictor, the pipeline runs with\n"
        "          --hazard forward --predictor 2bit.\n",
        prog, (unsigned long long)DEFAULT_MAX_CYCLES, DEFAULT_MEM_SIZE);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *program_path = argv[1];
    int      batch      = 0;      /* run non-interactively                      */
    int      single     = 0;      /* --single-cycle                             */
    int      trace      = 0;
    int      hazard     = HZ_UNSET;
    int      bp_given   = 0;
    BPMode   bp_mode    = BP_NONE;
    uint64_t max_cycles = DEFAULT_MAX_CYCLES;
    uint32_t mem_size   = DEFAULT_MEM_SIZE;

    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--batch") == 0) {
            batch = 1;
        } else if (strcmp(a, "--single-cycle") == 0) {
            single = 1; batch = 1;
        } else if (strcmp(a, "--trace") == 0) {
            trace = 1;
        } else if (strcmp(a, "--pipeline") == 0) {
            hazard = HZ_STALL; batch = 1;
        } else if (strcmp(a, "--pipeline-forward") == 0) {
            hazard = HZ_FORWARD; batch = 1;
        } else if (strcmp(a, "--hazard") == 0 && i + 1 < argc) {
            const char *hz = argv[++i];
            if      (strcmp(hz, "none") == 0)    hazard = HZ_NONE;
            else if (strcmp(hz, "stall") == 0)   hazard = HZ_STALL;
            else if (strcmp(hz, "forward") == 0) hazard = HZ_FORWARD;
            else {
                fprintf(stderr, "unknown hazard unit '%s' (choices: none stall forward)\n", hz);
                usage(argv[0]);
                return 1;
            }
            batch = 1;
        } else if ((strcmp(a, "--predict") == 0 || strcmp(a, "--predictor") == 0) && i + 1 < argc) {
            const char *name = argv[++i];
            if      (strcmp(name, "not-taken") == 0) bp_mode = BP_NOT_TAKEN;
            else if (strcmp(name, "btfn") == 0)      bp_mode = BP_BTFN;
            else if (strcmp(name, "1bit") == 0)      bp_mode = BP_1BIT;
            else if (strcmp(name, "2bit") == 0)      bp_mode = BP_2BIT;
            else {
                fprintf(stderr, "unknown predictor '%s' (choices: not-taken btfn 1bit 2bit)\n", name);
                usage(argv[0]);
                return 1;
            }
            bp_given = 1; batch = 1;
        } else if (strcmp(a, "--max-cycles") == 0 && i + 1 < argc) {
            max_cycles = strtoull(argv[++i], NULL, 0);
        } else if (strcmp(a, "--mem-size") == 0 && i + 1 < argc) {
            mem_size = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else {
            fprintf(stderr, "unrecognized option '%s'\n", a);
            usage(argv[0]);
            return 1;
        }
    }

    /* Defaults. Nothing chosen at all -> forward + 2bit. A predictor alone
     * implies forwarding. A hazard unit alone means "no predictor". */
    if (hazard == HZ_UNSET) {
        hazard = HZ_FORWARD;
        if (!bp_given) bp_mode = BP_2BIT;
    }

    /* 1. Bring up the ISA. */
    isa_registry_init();
    register_rv32i_base();

    /* 2. Set up memory and assemble the program into it. */
    Memory mem;
    if (memory_init(&mem, mem_size) != 0) {
        fprintf(stderr, "error: could not allocate %u bytes of simulated memory\n", mem_size);
        return 1;
    }

    uint32_t entry_point = 0;
    AsmError err;
    AsmStatus asm_status = assemble_file(program_path, &mem, &entry_point, &err);
    if (asm_status != ASM_OK) {
        fprintf(stderr, "assembly error");
        if (err.line_number > 0) fprintf(stderr, " (line %d)", err.line_number);
        fprintf(stderr, ": %s\n", err.message);
        memory_free(&mem);
        return 1;
    }

    printf("assembled '%s' ok. entry point = 0x%08x\n", program_path, entry_point);

    /* 3. Reset the CPU, pick the engine, run. */
    CPUState cpu;
    cpu_reset(&cpu, entry_point);

    SimEngine eng;
    Pipeline  pipe;
    if (single) {
        sim_engine_init_single(&eng, &cpu, &mem);
    } else {
        pipeline_init(&pipe, hazard == HZ_FORWARD, bp_mode);
        pipe.no_hazard = (hazard == HZ_NONE);
        pipeline_engine_init(&eng, &pipe, &cpu, &mem);
    }

    if (batch) run_batch(&eng, max_cycles, trace);
    else       run_repl(&eng);

    memory_free(&mem);
    return 0;
}