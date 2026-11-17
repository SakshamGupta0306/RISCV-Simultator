# micro.s -- Lab 3 Section 4 worked example (five instructions, hand-traceable).
# stall-only: 15 cycles, 6 stalls, CPI 3.00
# forwarding:  9 cycles, 0 stalls, CPI 1.80

.text
.globl main
main:
    addi t0, zero, 5      # I1 @0x00
    addi t1, t0, 10       # I2 @0x04 RAW on t0, distance 1
    add  t2, t1, t0       # I3 @0x08 RAW on t1 (d=1) and t0 (d=2)
    addi a7, zero, 10     # I4 @0x0c
    ecall                 # I5 @0x10
