#include <stdio.h>
#include "branch_predictor.h"
/* t1_counted_loop: bne at 0x10, target 0x08, outcomes T x9 then N */
int main(void) {
    const char *names[] = {"not-taken", "btfn"};
    for (int k = 0; k < 2; k++) {
        const BranchPredictor *p = bp_find(names[k]);
        BPState s; p->reset(&s);
        int miss = 0;
        for (int i = 0; i < 10; i++) {
            int taken = (i < 9);
            if (p->predict(&s, 0x10, 0x08) != taken) miss++;
            p->update(&s, 0x10, 0x08, taken);
        }
        printf("%-10s misses=%d\n", p->name, miss);
    }
    printf("unknown -> %p\n", (void *)bp_find("nope"));
    return 0;
}