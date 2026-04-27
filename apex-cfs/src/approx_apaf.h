#ifndef APEX_CFS_APAF_H
#define APEX_CFS_APAF_H

#include "task.h"
#include "cfs_exact.h"
#include "approx_bsa.h"
#include "fairness.h"

#ifndef APAF_TIGHT
#define APAF_TIGHT   0
#endif
#ifndef APAF_MEDIUM
#define APAF_MEDIUM  1
#endif
#ifndef APAF_LOOSE
#define APAF_LOOSE   2
#endif

/* TIGHT mode (n in [0,16] only, e <= 1.0%) */
#define APAF_TIGHT_A0   0.999755378067218
#define APAF_TIGHT_A1  -0.021439723696865
#define APAF_TIGHT_A2   0.000197742040395

/* MEDIUM mode (n in [0,32], e <= 3.0%) */
#define APAF_MEDIUM_A0  0.998121961273224
#define APAF_MEDIUM_A1 -0.020869371290712
#define APAF_MEDIUM_A2  0.000167397421507

/* LOOSE mode (linear, n in [0,32], e <= 4.15%) */
#define APAF_LOOSE_A0   0.977779922300000
#define APAF_LOOSE_A1  -0.015578660700000
#define APAF_LOOSE_A2   0.0

/* Fairness monitor runs every N ticks */
#define APAF_MONITOR_INTERVAL  4

/* Newton iterations per state */
#define APAF_TIGHT_NEWTON_ITERS   2
#define APAF_MEDIUM_NEWTON_ITERS  2
#define APAF_LOOSE_NEWTON_ITERS   1

/* Load balancer margin per state */
#define APAF_TIGHT_LB_MARGIN    1.00
#define APAF_MEDIUM_LB_MARGIN   1.02
#define APAF_LOOSE_LB_MARGIN    1.05

double apaf_poly_eval(int n, int state);
u64 apaf_decay_load(u64 load, int periods, int state);
u64 apaf_vruntime_delta(u64 delta_exec, u32 weight, int state);
void apaf_update_state(cfs_rq_t *rq);
void apaf_update_curr(cfs_rq_t *rq, int task_idx);
void apaf_update_load_avg(cfs_rq_t *rq, int task_idx);
int apaf_pick_next_task(const cfs_rq_t *rq);
void apaf_tick(cfs_rq_t *rq);

#endif /* APEX_CFS_APAF_H */
