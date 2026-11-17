#ifndef PIPELINE_STATE_H
#define PIPELINE_STATE_H

#include <stdint.h>
#include "cpu_state.h"      /* CPUState, CPUStatus */
#include "memory.h"         /* Memory, MemStatus   */
#include "decoder.h"        /* DecodedInstr, InstructionHandler, decode_instruction */
#include "control.h"        /* ControlSignals */

/* Precise-exception fault codes, carried down the pipe and raised only at WB */
#define PF_NONE    0
#define PF_ILLEGAL 1
#define PF_FETCH   2
#define PF_MEM     3

/* IF/ID latch */
typedef struct {
    int      valid;
    uint32_t pc;
    uint32_t raw;
    int      fault;
    /* Lab 4: the prediction made when this instruction was fetched */
    int      predicted_taken;   /* 1 = we predicted taken, 0 = not-taken */
    uint32_t predicted_target;  /* the PC we fetched next (taken target)  */
} IFID;

/* ID/EX latch */
typedef struct {
    int                       valid;
    uint32_t                  pc;
    int                       fault;
    DecodedInstr              instr;
    const InstructionHandler *handler;
    ControlSignals            ctrl;
    uint32_t                  rs1_val, rs2_val;
    /* Lab 4: carry prediction down to EX where outcome is known */
    int      predicted_taken;
    uint32_t predicted_target;
} IDEX;

/* EX/MEM latch */
typedef struct {
    int                       valid;
    uint32_t                  pc;
    int                       fault;
    DecodedInstr              instr;
    const InstructionHandler *handler;
    ControlSignals            ctrl;
    uint32_t                  alu_result;
    uint32_t                  store_val;
    uint32_t                  fault_addr;
} EXMEM;

/* MEM/WB latch */
typedef struct {
    int                       valid;
    uint32_t                  pc;
    int                       fault;
    DecodedInstr              instr;
    const InstructionHandler *handler;
    ControlSignals            ctrl;
    uint32_t                  wb_val;
    uint32_t                  fault_addr;
} MEMWB;

/* Branch predictor mode selector */
typedef enum {
    BP_NONE = 0,    /* no predictor: used for --pipeline / --pipeline-forward (always not-taken via flush) */
    BP_NOT_TAKEN,   /* always predict not-taken */
    BP_BTFN,        /* backward-taken / forward-not-taken */
    BP_1BIT,        /* 1-bit saturating counter PHT */
    BP_2BIT         /* 2-bit saturating counter PHT */
} BPMode;

#define BP_PHT_SIZE 1024   /* number of entries in the PHT (power of 2) */

typedef struct {
    IFID  if_id;
    IDEX  id_ex;
    EXMEM ex_mem;
    MEMWB mem_wb;
    uint64_t cycles, retired, stalls, flushes;

    /* Lab 3 */
    int      forwarding;        /* 0 = stall-only (--pipeline), 1 = --pipeline-forward */
    uint64_t load_use_stalls;   /* subset of stalls: load in EX, consumer in ID       */
    uint64_t fwd_ex;            /* operands taken from EX/MEM.alu_result              */
    uint64_t fwd_mem;           /* operands taken from MEM/WB.wb_val                  */

    /* Lab 4 */
    BPMode   bp_mode;           /* which predictor is active                           */
    uint8_t  pht[BP_PHT_SIZE];  /* PHT for 1-bit and 2-bit predictors                 */
    uint64_t branches;          /* conditional branches retired                        */
    uint64_t mispredicts;       /* conditional branch mispredictions                   */
    uint64_t jal_count;         /* jal instructions retired                            */
    uint64_t jalr_count;        /* jalr instructions retired                           */
    uint64_t redirects;         /* total redirections (mispredicts + unconditional jmp)*/
    uint64_t squashed;          /* instructions squashed due to redirects              */
} Pipeline;

void      pipeline_init      (Pipeline *p, int forwarding, BPMode bp_mode);
CPUStatus pipeline_cycle     (Pipeline *p, CPUState *cpu, Memory *mem);
void      pipeline_run_batch (Pipeline *p, CPUState *cpu, Memory *mem, uint64_t max_cycles);

/* Branch predictor helpers (called by pipeline.c) */
int      bp_predict  (Pipeline *p, uint32_t pc, int32_t imm);   /* 1=predict taken */
void     bp_update   (Pipeline *p, uint32_t pc, int actually_taken);

/* Human-readable name of the predictor (used in output) */
const char *bp_name(BPMode mode);

#endif /* PIPELINE_STATE_H */
