#ifndef APEX_CFS_EXACT_H
#define APEX_CFS_EXACT_H

#include "task.h"
#include <stdint.h>
#include <math.h>

/* Exact kernel fixed-point shift */
#define PELT_SHIFT          32

/* Maximum periods before load = 0 */
#define PELT_MAX_PERIODS    63

/* vruntime weight shift */
#define WMULT_SHIFT         32
#define WMULT_CONST         (1ULL << WMULT_SHIFT)

u64 exact_mul_u64_u32_shr(u64 a, u32 b, int shift);
u64 exact_decay_load(u64 load, int periods);
u64 exact_calc_delta_fair(u64 delta_exec, u32 weight);
void exact_update_curr(cfs_rq_t *rq, int task_idx);
void exact_update_load_avg(cfs_rq_t *rq, int task_idx);
int exact_pick_next_task(const cfs_rq_t *rq);
void exact_tick(cfs_rq_t *rq);

#endif /* APEX_CFS_EXACT_H */
