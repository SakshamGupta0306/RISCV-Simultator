/*
 * Branch predictor helpers called by pipeline.c (declared in pipeline_state.h).
 *
 * The predictor table lives in Pipeline.pht. Only the first BP_USED entries
 * are used, indexed by pc[7:2] -- the 64-entry table the Lab 4 sheet
 * specifies. (Using all 1024 entries would change aliasing, and with it the
 * mispredict counts on the longer test programs.)
 *
 *   BP_NOT_TAKEN : always 0
 *   BP_BTFN      : backward (imm < 0) -> taken
 *   BP_1BIT      : pht[i] holds the last outcome (0/1), starts at 0
 *   BP_2BIT      : pht[i] is a saturating counter 0..3, predict taken if >= 2
 *                  (pipeline_init sets every entry to 1 = weakly not taken)
 */
#include <stddef.h>
#include "pipeline_state.h"

#define BP_USED_BITS 6
#define BP_USED      (1 << BP_USED_BITS)

static int bp_index(uint32_t pc) {
    return (int)((pc >> 2) & (BP_USED - 1));
}

int bp_predict(Pipeline *p, uint32_t pc, int32_t imm) {
    switch (p->bp_mode) {
        case BP_BTFN:   return imm < 0;
        case BP_1BIT:   return p->pht[bp_index(pc)] != 0;
        case BP_2BIT:   return p->pht[bp_index(pc)] >= 2;
        case BP_NOT_TAKEN:
        case BP_NONE:
        default:        return 0;
    }
}

void bp_update(Pipeline *p, uint32_t pc, int actually_taken) {
    uint8_t *e = &p->pht[bp_index(pc)];

    if (p->bp_mode == BP_1BIT) {
        *e = actually_taken ? 1 : 0;
    } else if (p->bp_mode == BP_2BIT) {
        if (actually_taken) { if (*e < 3) (*e)++; }   /* saturate, don't wrap */
        else                { if (*e > 0) (*e)--; }
    }
    /* static predictors keep no state */
}

const char *bp_name(BPMode mode) {
    switch (mode) {
        case BP_NOT_TAKEN: return "not-taken";
        case BP_BTFN:      return "btfn";
        case BP_1BIT:      return "1bit";
        case BP_2BIT:      return "2bit";
        default:           return NULL;     /* BP_NONE: no predictor line */
    }
}   