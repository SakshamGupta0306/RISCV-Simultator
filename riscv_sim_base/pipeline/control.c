#include "control.h"

/* Task 1: generate the control signals in ID from the handler's format.
 * Every later stage acts only on these signals, never on the opcode. */
ControlSignals control_decode(const DecodedInstr *i, const InstructionHandler *h) {
    (void)i;                    /* format alone is enough for RV32I */
    ControlSignals cs = {0};    /* everything off by default */

    switch (h->format) {
        case FMT_R:
            cs.reg_write = 1; cs.wb_src = WB_ALU; cs.uses_rs1 = 1; cs.uses_rs2 = 1;
            break;
        case FMT_I:
        case FMT_I_SHAMT:
            cs.reg_write = 1; cs.wb_src = WB_ALU; cs.uses_rs1 = 1;
            break;
        case FMT_U:
            cs.reg_write = 1; cs.wb_src = WB_ALU;
            break;
        case FMT_I_LOAD:
            cs.reg_write = 1; cs.wb_src = WB_MEM; cs.mem_read = 1; cs.uses_rs1 = 1;
            break;
        case FMT_S:
            cs.wb_src = WB_NONE; cs.mem_write = 1; cs.uses_rs1 = 1; cs.uses_rs2 = 1;
            break;
        case FMT_B:
            cs.wb_src = WB_NONE; cs.is_branch = 1; cs.uses_rs1 = 1; cs.uses_rs2 = 1;
            break;
        case FMT_J:
            cs.reg_write = 1; cs.wb_src = WB_PC4; cs.is_jump = 1;
            break;
        case FMT_I_JALR:
            cs.reg_write = 1; cs.wb_src = WB_PC4; cs.is_jump = 1; cs.uses_rs1 = 1;
            break;
        case FMT_I_SYS:
            cs.wb_src = WB_NONE; cs.is_system = 1;   /* ecall: handled at WB */
            break;
    }
    return cs;
}
