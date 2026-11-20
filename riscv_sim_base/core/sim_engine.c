#include "sim_engine.h"
#include "executor.h"

static void single_step(SimEngine *e) {
    execute_cycle(e->cpu, e->mem);
}

static int single_stage_views(const SimEngine *e, PipelineStageView *out, int max) {
    if (max < 1) return 0;
    out[0].label     = "PC";
    out[0].pc        = e->cpu->pc;
    out[0].is_bubble = (e->cpu->status != CPU_RUNNING);
    return 1;
}

void sim_engine_init_single(SimEngine *e, CPUState *cpu, Memory *mem) {
    e->name         = "single-cycle";
    e->cpu          = cpu;
    e->mem          = mem;
    e->impl         = NULL;
    e->step         = single_step;
    e->stage_views  = single_stage_views;
    e->format_stats = NULL;      /* the oracle prints no pipeline statistics */
}