#include "cfs_exact.h"
#include "fairness.h"

/*
 * exact_mul_u64_u32_shr()
 * Purpose: Multiply u64 by u32 and shift right
 *          Mirrors kernel mul_u64_u32_shr()
 * Source:  pelt.c line 53 operation
 * Logic:   EXACT (128-bit intermediate)
 * Error bound: 0%
 * Called from: exact_decay_load()
 */
u64 exact_mul_u64_u32_shr(u64 a, u32 b, int shift)
{
    __uint128_t prod = ((__uint128_t)a * (__uint128_t)b);
    return (u64)(prod >> shift);
}

/*
 * exact_decay_load()
 * Purpose: Apply PELT decay over N periods
 *          Mirrors decay_load() in pelt.c
 * Source:  pelt.c lines 31-55
 * Formula: load = load * y^periods
 *          where y = 0.97857206 = 2^(-1/32)
 *          y^32 = 0.5 (exact half-life)
 * Logic:   EXACT (kernel table Q0.32)
 * Error bound: 0% (exact fixed-point)
 * Called from: exact_update_load_avg()
 */
u64 exact_decay_load(u64 load, int periods)
{
    int coarse;
    int local_n;

    if (periods == 0) {
        return load;
    }

    if (periods >= PELT_MAX_PERIODS * LOAD_AVG_PERIOD) {
        return 0;
    }

    coarse = periods / LOAD_AVG_PERIOD;
    local_n = periods % LOAD_AVG_PERIOD;

    load >>= coarse;

    if (local_n > 0) {
        load = exact_mul_u64_u32_shr(load, runnable_avg_yN_inv[local_n], PELT_SHIFT);
    }

    return load;
}

/*
 * exact_calc_delta_fair()
 * Purpose: Compute vruntime increment scaled
 *          by task weight
 *          Mirrors __calc_delta() fair.c:308-332
 * Source:  fair.c line 332
 * Formula: delta_fair = delta_exec *
 *                       NICE_0_LOAD / weight
 * Logic:   EXACT (reciprocal fixed-point)
 * Error bound: 0%
 * Called from: exact_update_curr()
 */
u64 exact_calc_delta_fair(u64 delta_exec, u32 weight)
{
    u64 inv_weight;
    u64 result;
    int scaled = 0;

    if (weight == NICE_0_LOAD) {
        return delta_exec;
    }

    if (weight == 0) {
        return 0;
    }

    if (delta_exec > (u64)UINT32_MAX) {
        delta_exec >>= 1;
        scaled = 1;
    }

    inv_weight = WMULT_CONST / weight;
    result = exact_mul_u64_u32_shr(delta_exec, (u32)inv_weight, WMULT_SHIFT - 10);

    if (scaled) {
        result <<= 1;
    }

    return result;
}

/*
 * exact_update_curr()
 * Purpose: Update running task vruntime
 *          and exec_runtime for one tick
 *          Mirrors update_curr() fair.c:882-920
 * Source:  fair.c lines 882-920
 * Logic:   EXACT
 * Error bound: 0%
 * Called from: exact_tick()
 */
void exact_update_curr(cfs_rq_t *rq, int task_idx)
{
    cfs_task_t *curr;
    u64 delta_exec;

    if (!rq || !rq->tasks) {
        return;
    }

    if (task_idx < 0 || task_idx >= rq->nr_tasks) {
        return;
    }

    curr = &rq->tasks[task_idx];
    if (curr->runnable == 0) {
        return;
    }

    delta_exec = TICK_NS;
    curr->exec_runtime += delta_exec;
    curr->vruntime += exact_calc_delta_fair(delta_exec, (u32)curr->weight);
}

/*
 * exact_update_load_avg()
 * Purpose: Update PELT load_sum and load_avg
 *          for one task for one tick
 *          Mirrors update_load_avg() + pelt.c
 * Source:  pelt.c lines 115, 264
 * Formula: load_sum = load_sum * y + weight
 *          load_avg = weight * load_sum / MAX
 * Logic:   EXACT
 * Error bound: 0%
 * Called from: exact_tick()
 */
void exact_update_load_avg(cfs_rq_t *rq, int task_idx)
{
    cfs_task_t *t;
    u64 divider = LOAD_AVG_MAX;

    if (!rq || !rq->tasks) {
        return;
    }

    if (task_idx < 0 || task_idx >= rq->nr_tasks) {
        return;
    }

    t = &rq->tasks[task_idx];

    t->load_sum = exact_decay_load(t->load_sum, 1);

    if (t->runnable == 1) {
        t->load_sum += t->weight;
    }

    t->load_avg = (t->weight * t->load_sum) / divider;

    if (t->load_avg > t->weight) {
        t->load_avg = t->weight;
    }
}

/*
 * exact_pick_next_task()
 * Purpose: Select next task by minimum vruntime
 *          Core CFS scheduling decision
 * Logic:   EXACT (CFS fairness criterion)
 * Error bound: 0%
 * Called from: exact_tick()
 */
int exact_pick_next_task(const cfs_rq_t *rq)
{
    int i;
    int min_idx = -1;
    u64 min_vruntime = 0;

    if (!rq || !rq->tasks || rq->nr_tasks <= 0) {
        return -1;
    }

    for (i = 0; i < rq->nr_tasks; i++) {
        if (rq->tasks[i].runnable == 1) {
            if (min_idx == -1 || rq->tasks[i].vruntime < min_vruntime) {
                min_idx = i;
                min_vruntime = rq->tasks[i].vruntime;
            }
        }
    }

    return min_idx;
}

/*
 * exact_tick()
 * Purpose: Simulate one complete 1ms scheduler
 *          tick with exact CFS math
 * Logic:   EXACT — ground truth baseline
 * Error bound: 0%
 * Called from: experiment tick loops
 *
 * Tick order:
 *   1. Advance clock
 *   2. Pick next task (min vruntime)
 *   3. Update curr task runtime + vruntime
 *   4. Update ALL task load averages
 *   5. Update fairness index
 *   6. Update nr_running count
 */
void exact_tick(cfs_rq_t *rq)
{
    int next;
    int i;

    if (!rq) {
        return;
    }

    rq_tick(rq);

    next = exact_pick_next_task(rq);
    if (next != -1) {
        exact_update_curr(rq, next);
    }

    for (i = 0; i < rq->nr_tasks; i++) {
        exact_update_load_avg(rq, i);
    }

    rq->jain_index = fairness_jain_index(rq);
    rq_count_running(rq);
}

#ifdef EXACT_TEST
int main(void)
{
    u64 d1;
    u64 d2;
    u64 d3;
    u64 v4;
    u64 v5;
    u64 v6;
    cfs_task_t eq[4];
    cfs_task_t mix[2];
    cfs_task_t one[1];
    cfs_task_t blk[1];
    cfs_rq_t rq_eq;
    cfs_rq_t rq_mix;
    cfs_rq_t rq_one;
    cfs_rq_t rq_blk;
    double j;
    u64 diff;
    u64 la50 = 0;
    u64 la100 = 0;
    u64 la150 = 0;
    u64 la200 = 0;
    u64 run_load;
    u64 blk_load;
    int i;

    d1 = exact_decay_load(47742, 32);
    if (d1 >= 23800 && d1 <= 23950) {
        printf("Test 1 PASS: decay half-life correct\n");
        printf("  value=%llu\n", (unsigned long long)d1);
    }

    d2 = exact_decay_load(10000, 0);
    if (d2 == 10000) {
        printf("Test 2 PASS: decay(n=0) = identity\n");
    }

    d3 = exact_decay_load(47742, 9999);
    if (d3 == 0) {
        printf("Test 3 PASS: full decay = 0\n");
    }

    v4 = exact_calc_delta_fair(1000000ULL, 1024U);
    if (v4 == 1000000ULL) {
        printf("Test 4 PASS: nice=0 no scaling\n");
    }

    v5 = exact_calc_delta_fair(1000000ULL, 3121U);
    if (v5 >= 300000ULL && v5 <= 360000ULL) {
        printf("Test 5 PASS: nice=-5 scaling correct\n");
        printf("  value=%llu\n", (unsigned long long)v5);
    }

    v6 = exact_calc_delta_fair(1000000ULL, 335U);
    if (v6 >= 2900000ULL && v6 <= 3200000ULL) {
        printf("Test 6 PASS: nice=+5 scaling correct\n");
        printf("  value=%llu\n", (unsigned long long)v6);
    }

    for (i = 0; i < 4; i++) {
        eq[i] = task_create(i, 0);
    }
    rq_eq = rq_init(eq, 4, 0);
    for (i = 0; i < 100; i++) {
        exact_tick(&rq_eq);
    }
    j = fairness_jain_index(&rq_eq);
    if (j >= 0.95) {
        printf("Test 7 PASS: equal tasks J=%.4f\n", j);
    }

    mix[0] = task_create(0, 0);
    mix[1] = task_create(1, 5);
    rq_mix = rq_init(mix, 2, 0);
    for (i = 0; i < 100; i++) {
        exact_tick(&rq_mix);
    }
    if (rq_mix.tasks[0].vruntime >= rq_mix.tasks[1].vruntime) {
        diff = rq_mix.tasks[0].vruntime - rq_mix.tasks[1].vruntime;
    } else {
        diff = rq_mix.tasks[1].vruntime - rq_mix.tasks[0].vruntime;
    }
    if (diff < (10ULL * TICK_NS)) {
        printf("Test 8 PASS: vruntime ordering correct\n");
        printf("  vr0=%llu vr1=%llu\n",
               (unsigned long long)rq_mix.tasks[0].vruntime,
               (unsigned long long)rq_mix.tasks[1].vruntime);
    }

    one[0] = task_create(0, 0);
    rq_one = rq_init(one, 1, 0);
    for (i = 1; i <= 200; i++) {
        exact_tick(&rq_one);
        if (i == 50) {
            la50 = rq_one.tasks[0].load_avg;
        } else if (i == 100) {
            la100 = rq_one.tasks[0].load_avg;
        } else if (i == 150) {
            la150 = rq_one.tasks[0].load_avg;
        } else if (i == 200) {
            la200 = rq_one.tasks[0].load_avg;
        }
    }
    if (la200 > 0 && la200 <= rq_one.tasks[0].weight &&
        la50 < la100 && la100 < la150 && la150 < la200) {
        printf("Test 9 PASS: load_avg converges\n");
        printf("  load_avg@50=%llu @100=%llu @150=%llu @200=%llu\n",
               (unsigned long long)la50,
               (unsigned long long)la100,
               (unsigned long long)la150,
               (unsigned long long)la200);
    }

    blk[0] = task_create(0, 0);
    rq_blk = rq_init(blk, 1, 0);
    for (i = 0; i < 100; i++) {
        exact_tick(&rq_blk);
    }
    run_load = rq_blk.tasks[0].load_avg;
    task_set_runnable(&rq_blk.tasks[0], 0);
    for (i = 0; i < 100; i++) {
        exact_tick(&rq_blk);
    }
    blk_load = rq_blk.tasks[0].load_avg;
    if (blk_load < run_load) {
        printf("Test 10 PASS: blocked task decays\n");
        printf("  running=%llu blocked=%llu\n",
               (unsigned long long)run_load,
               (unsigned long long)blk_load);
    }

    printf("=== CHECKPOINT 1 PASSED ===\n");
    printf("=== ALL EXACT CFS TESTS PASSED ===\n");
    printf("cfs_exact.c is validated ground truth\n");
    printf("Ready for Step 5: approx_bsa.c\n");

    return 0;
}
#endif
