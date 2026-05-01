#ifndef APEX_CFS_BSA_H
#define APEX_CFS_BSA_H

#include "task.h"
#include "cfs_exact.h"

/* BSA decay approximation
 * y_bsa = 1 - 1/32 = 0.96875
 * vs y_exact = 0.97857206
 * Error: 1.0037% per tick */
#define BSA_DECAY_SHIFT     5
/* load_bsa = load - (load >> BSA_DECAY_SHIFT) */

/* BSA vruntime: nearest power of 2 + Newton */
#define BSA_NEWTON_ITERS    1
/* 1 Newton-Raphson iteration after pow2 guess */
/* Error: <= 1.9110% */

u32 bsa_nearest_pow2(u32 weight);
u64 bsa_newton_reciprocal(u32 weight, int iterations);
u64 bsa_decay_load(u64 load, int periods);
u64 bsa_vruntime_delta(u64 delta_exec, u32 weight);
void bsa_update_curr(cfs_rq_t *rq, int task_idx);
void bsa_update_load_avg(cfs_rq_t *rq, int task_idx);
int bsa_pick_next_task(const cfs_rq_t *rq);
void bsa_tick(cfs_rq_t *rq);

#endif /* APEX_CFS_BSA_H */
