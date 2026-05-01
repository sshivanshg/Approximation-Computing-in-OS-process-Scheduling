#ifndef APEX_CFS_CLTI_H
#define APEX_CFS_CLTI_H

#include "task.h"
#include "cfs_exact.h"
#include "approx_bsa.h"

/* CLTI table size — 8 entries at step 4 */
#define CLTI_TABLE_SIZE     8
#define CLTI_TABLE_STEP     4

/* Q0.15 fixed point format for CLTI table */
#define CLTI_SHIFT          15

/* CLTI decay table — y^(n*4) in Q0.15
 * entry[i] = y^(i*4) * 2^15
 * y = 0.97857206, y^4 = 0.9170040432 */
static const u32 clti_decay_table[8] = {
    32768,  /* y^0  = 1.0000 */
    30048,  /* y^4  = 0.9170 */
    27554,  /* y^8  = 0.8409 */
    25268,  /* y^12 = 0.7711 */
    23170,  /* y^16 = 0.7071 */
    21247,  /* y^20 = 0.6484 */
    19484,  /* y^24 = 0.5946 */
    17867   /* y^28 = 0.5453 */
};

/* CLTI weight class representatives
 * 8 power-of-2 values covering nice -20..+19
 * Class 4 (w_rep=1024) is exact for nice 0 */
static const u32 clti_weight_class[8] = {
    65536,  /* class 0: nice -20 to -17 */
    16384,  /* class 1: nice -16 to -13 */
    4096,   /* class 2: nice -12 to  -9 */
    2048,   /* class 3: nice  -8 to  -5 */
    1024,   /* class 4: nice  -4 to  +1 (exact) */
    256,    /* class 5: nice  +2 to  +6 */
    64,     /* class 6: nice  +7 to +12 */
    16      /* class 7: nice +13 to +19 */
};

int clti_weight_to_class(u32 weight);
u64 clti_decay_interpolate(u64 load, int local_n);
u64 clti_decay_load(u64 load, int periods);
u64 clti_vruntime_delta(u64 delta_exec, u32 weight);
void clti_update_curr(cfs_rq_t *rq, int task_idx);
void clti_update_load_avg(cfs_rq_t *rq, int task_idx);
int clti_pick_next_task(const cfs_rq_t *rq);
void clti_tick(cfs_rq_t *rq);

#endif /* APEX_CFS_CLTI_H */
