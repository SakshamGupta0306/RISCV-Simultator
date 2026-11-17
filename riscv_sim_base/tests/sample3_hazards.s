# Hazard stress test for the 5-stage pipeline (no forwarding, branches in EX).
# NOTE: Appendix A was missing from the lab-sheet PDF, so this file was written
# to exercise the same cases: back-to-back RAW, load-use, a store whose data
# comes from the previous instruction, jal/jalr, and taken/not-taken branches.
#
# Expected final state (identical in --batch and --pipeline):
#   t0 = 5, t1 = 8, t2 = 13 (0xd), t3 = 13, t4 = 14 (0xe), t5 = 56 (0x38)
#   a0 = 0 (the addi after the taken bne is skipped), a7 = 10

.text
.globl main
main:
    addi t0, zero, 5        # t0 = 5
    addi t1, t0, 3          # RAW on t0 (distance 1)       -> t1 = 8
    add  t2, t0, t1         # RAW on t1 (distance 1)       -> t2 = 13
    li   gp, buf            # gp = &buf
    sw   t2, 0(gp)          # RAW on gp: store base register
    lw   t3, 0(gp)          # t3 = 13
    addi t4, t3, 1          # load-use hazard on t3        -> t4 = 14
    jal  ra, times4         # jump (flush #1)
    beq  t5, zero, fail     # not taken: t5 != 0
    bne  t5, zero, done     # taken (flush #3)
fail:
    addi a0, zero, 99       # wrong path - must never retire
done:
    addi a7, zero, 10       # exit
    ecall

times4:
    slli t5, t4, 2          # t5 = 56
    jalr zero, 0(ra)        # return (flush #2)

.data
buf:
    .word 0
