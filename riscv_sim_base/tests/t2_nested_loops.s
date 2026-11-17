# t2_nested_loops.s -- inner loop of 3 iterations, run 4 times (rebuilt from the Lab 4 sheet).
# Inner bne outcomes: T T N  T T N  T T N  T T N
.text
.globl main
main:
    addi t0, zero, 4          # outer count
outer:
    addi t1, zero, 3          # inner count
inner:
    addi t2, t2, 1            # work
    addi t1, t1, -1
    bne  t1, zero, inner      # backward, taken 2 of every 3
    addi t0, t0, -1
    bne  t0, zero, outer      # backward, taken 3 of 4
    mv   a0, t2
    addi a7, zero, 10
    ecall