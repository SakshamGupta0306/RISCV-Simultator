#include <string.h>
#include "branch_predictor.h"

/* Predictors available so far. Add &bp_1bit and &bp_2bit here in Tasks 2-3. */
static const BranchPredictor *const all_predictors[] = {
    &bp_not_taken,
    &bp_btfn,
};

const BranchPredictor *bp_find(const char *name) {
    for (size_t i = 0; i < sizeof(all_predictors) / sizeof(all_predictors[0]); i++)
        if (strcmp(all_predictors[i]->name, name) == 0)
            return all_predictors[i];
    return NULL;
}
