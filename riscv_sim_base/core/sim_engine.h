#ifndef SIM_ENGINE_H
#define SIM_ENGINE_H

/* A "simulation engine" is anything that can advance the machine by one
 * clock cycle: the single-cycle oracle or the 5-stage pipeline. The UI
 * (ui/cli_display.c) talks only to this interface, so it never needs to know
 * which engine is running. */

#include <stddef.h>
#include <stdint.h>
#include "cpu_state.h"
#include "memory.h"

#define SIM_MAX_STAGES 8

/* What one pipeline stage holds this cycle (for --trace / the 'pipe' command). */
typedef struct {
    const char *label;     /* "IF", "ID", "EX", "MEM", "WB" */
    uint32_t    pc;
    int         is_bubble; /* 1 = stage is empty */
} PipelineStageView;

typedef struct SimEngine SimEngine;

struct SimEngine {
    const char *name;      /* shown by the REPL banner */
    CPUState   *cpu;
    Memory     *mem;
    void       *impl;      /* engine-private state (e.g. a Pipeline *) */

    /* Advance exactly one clock cycle. Must keep cpu->cycle_count up to date. */
    void (*step)(SimEngine *e);

    /* Fill `out` (at most `max` entries); return how many were written. */
    int  (*stage_views)(const SimEngine *e, PipelineStageView *out, int max);

    /* Write the statistics text (one "name = value" per line) into buf.
     * May be NULL if the engine has no statistics (single-cycle). */
    void (*format_stats)(const SimEngine *e, char *buf, size_t size);
};

/* Single-cycle oracle engine (core/sim_engine.c). */
void sim_engine_init_single(SimEngine *e, CPUState *cpu, Memory *mem);

#endif /* SIM_ENGINE_H */