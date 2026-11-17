/*
 * hazard.h — Hazard detection unit (ID) and forwarding unit (EX).
 *
 * This file contains ONLY declarations.  Implementations are in Hazard.c.
 */
#ifndef HAZARD_H
#define HAZARD_H

#include <stdint.h>
#include "pipeline_state.h"   /* Pipeline, IDEX, PF_NONE            */
#include "control.h"          /* ControlSignals                      */
#include "decoder.h"          /* DecodedInstr                        */

/* Result of the hazard check in ID. */
typedef struct {
    int stall;      /* 1 → insert bubble, freeze IF/ID */
    int load_use;   /* 1 → this stall is a load-use    */
} HazardVerdict;

/* Forwarded operands produced in EX. */
typedef struct {
    uint32_t a;         /* forwarded value for rs1 */
    uint32_t b;         /* forwarded value for rs2 */
    int      fwd_ex;    /* count of EX/MEM forwards this cycle */
    int      fwd_mem;   /* count of MEM/WB forwards this cycle */
} Operands;

/* Returns 1 if the latch is valid, writes rd, rd != x0, and rd == rs. */
int           produces(int valid, const ControlSignals *lc,
                       uint32_t rd, uint32_t rs);

/* Called in ID: decide whether to stall. */
HazardVerdict hazard_check(const Pipeline *p, const DecodedInstr *d,
                           const ControlSignals *c);

/* Called in EX: return forwarded operands (or original register values). */
Operands      forward_operands(const Pipeline *p, const IDEX *d);

#endif /* HAZARD_H */
