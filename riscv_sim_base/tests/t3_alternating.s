# t3_alternating.s -- a forward branch whose outcome alternates T N T N T N T N
# (rebuilt from the Lab 4 sheet).
.text
.globl main
main:
    addi t0, zero, 0          # i
    addi t1, zero, 8          # limit
    addi t3, zero, 0          # accumulator
loop:
    andi t2, t0, 1
    beq  t2, zero, even       # forward: taken when i is even
    addi t3, t3, 1            # odd path
    jal  zero, next           # a plain jump, executed 4 times
even:
    addi t3, t3, 2
next:
    addi t0, t0, 1
    bne  t0, t1, loop         # backward, taken 7 of 8
    mv   a0, t3
    addi a7, zero, 10
    ecall