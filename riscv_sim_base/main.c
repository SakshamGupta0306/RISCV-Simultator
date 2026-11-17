#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_state.h"
#include "memory.h"
#include "isa_registry.h"
#include "rv32i_base.h"
#include "assembler.h"
#include "cli_display.h"
#include "pipeline_state.h"

#define DEFAULT_MEM_SIZE   (1u << 20)   /* 1 MB */
#define DEFAULT_MAX_CYCLES 1000000ull

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s <program.s> [options]\n"
        "  --batch              run to completion with no interaction, then dump state\n"
        "  --single-cycle       same as --batch (single-cycle oracle)\n"
        "  --pipeline           like --batch, but on the 5-stage pipeline (stall-only)\n"
        "  --pipeline-forward   like --pipeline, but with forwarding + load-use detection\n"
        "  --hazard <stall|forward>  same as --pipeline / --pipeline-forward\n"
        "  --predict <name>     branch predictor: not-taken | btfn | 1bit | 2bit\n"
        "  --predictor <name>   same as --predict\n"
        "  --max-cycles N       cycle limit for --batch / pipeline modes (default %llu)\n"
        "  --mem-size N         memory size in bytes (default %u)\n",
        prog, (unsigned long long)DEFAULT_MAX_CYCLES, DEFAULT_MEM_SIZE);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *program_path = argv[1];
    int      batch_mode = 0;
    uint64_t max_cycles = DEFAULT_MAX_CYCLES;
    uint32_t mem_size   = DEFAULT_MEM_SIZE;
    BPMode   bp_mode    = BP_NONE;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--batch") == 0 ||
            strcmp(argv[i], "--single-cycle") == 0) {
            batch_mode = 1;
        } else if (strcmp(argv[i], "--pipeline") == 0) {
            batch_mode = 2;
        } else if (strcmp(argv[i], "--pipeline-forward") == 0) {
            batch_mode = 3;
        } else if (strcmp(argv[i], "--hazard") == 0 && i + 1 < argc) {
            const char *hz = argv[++i];
            if (strcmp(hz, "stall") == 0)        batch_mode = 2;
            else if (strcmp(hz, "forward") == 0) batch_mode = 3;
            else {
                fprintf(stderr, "unknown hazard unit '%s' (choices: stall forward)\n", hz);
                usage(argv[0]);
                return 1;
            }
        } else if ((strcmp(argv[i], "--predict") == 0 ||
                    strcmp(argv[i], "--predictor") == 0) && i + 1 < argc) {
            const char *name = argv[++i];
            if (strcmp(name, "not-taken") == 0)      bp_mode = BP_NOT_TAKEN;
            else if (strcmp(name, "btfn") == 0)      bp_mode = BP_BTFN;
            else if (strcmp(name, "1bit") == 0)      bp_mode = BP_1BIT;
            else if (strcmp(name, "2bit") == 0)      bp_mode = BP_2BIT;
            else {
                fprintf(stderr, "unknown predictor '%s' (choices: not-taken btfn 1bit 2bit)\n", name);
                usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--max-cycles") == 0 && i + 1 < argc) {
            max_cycles = strtoull(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--mem-size") == 0 && i + 1 < argc) {
            mem_size = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else {
            fprintf(stderr, "unrecognized option '%s'\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (bp_mode != BP_NONE && batch_mode != 3) {
        fprintf(stderr, "warning: --predict requires --pipeline-forward; enabling it.\n");
        batch_mode = 3;
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

    /* 3. Reset the CPU and run. */
    CPUState cpu;
    cpu_reset(&cpu, entry_point);

    if (batch_mode >= 2) {
        Pipeline pipe;
        pipeline_init(&pipe, batch_mode == 3, bp_mode);
        pipeline_run_batch(&pipe, &cpu, &mem, max_cycles);
    } else if (batch_mode) {
        run_batch(&cpu, &mem, max_cycles);
    } else {
        run_repl(&cpu, &mem);
    }

    memory_free(&mem);
    return 0;
}
