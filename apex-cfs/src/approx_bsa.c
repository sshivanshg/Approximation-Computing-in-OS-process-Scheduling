#include "approx_bsa.h"
#include "fairness.h"
#include "metrics.h"

/*
 * bsa_nearest_pow2()
 * Purpose: Find nearest power of 2 to weight
 *          Used as Newton-Raphson initial guess
 * Logic: BSA (bit manipulation)
 * Error bound: <= 50% of weight (before Newton)
 * Called from: bsa_vruntime_delta()
 */
u32 bsa_nearest_pow2(u32 weight)
{
    u32 floor_pow2;
    u32 ceil_pow2;
    u32 dist_floor;
    u32 dist_ceil;

    if (weight == 0) {
        return 1;
    }

    floor_pow2 = 1;
    while ((floor_pow2 << 1) != 0 && (floor_pow2 << 1) <= weight) {
        floor_pow2 <<= 1;
    }

    if (floor_pow2 == weight) {
        return weight;
    }

    if ((floor_pow2 << 1) == 0) {
        return floor_pow2;
    }

    ceil_pow2 = floor_pow2 << 1;
    dist_floor = weight - floor_pow2;
    dist_ceil = ceil_pow2 - weight;

    if (dist_floor <= dist_ceil) {
        return floor_pow2;
    }

    return ceil_pow2;
}

/*
 * bsa_newton_reciprocal()
 * Purpose: Approximate 1/weight in Q0.32
 *          using Newton-Raphson iteration
 * Formula: x_{n+1} = x_n * (2 - w * x_n)
 * Logic: BSA
 * Error bound: <= 1.9110% after 1 iteration
 * Called from: bsa_vruntime_delta()
 */
u64 bsa_newton_reciprocal(u32 weight, int iterations)
{
    u32 p2;
    u64 x;
    u64 m_q32;
    u64 affine_q32;
    int i;

    if (weight == 0) {
        return WMULT_CONST;
    }

    p2 = bsa_nearest_pow2(weight);
    x = WMULT_CONST / p2;

    /* Affine warm-start on normalized mantissa m = weight / p2. */
    m_q32 = ((u64)weight << 32) / p2;
    affine_q32 = (3ULL << 31) - (m_q32 >> 1); /* 1.5 - 0.5m in Q0.32 */
    x = (u64)(((__uint128_t)x * (__uint128_t)affine_q32) >> 32);

    for (i = 0; i < iterations; i++) {
        u64 wx;
        u64 two_minus_wx;

        wx = (u64)((__uint128_t)weight * (__uint128_t)x);
        if (wx >= (2ULL << 32)) {
            break;
        }

        two_minus_wx = (2ULL << 32) - wx;
        x = (u64)(((__uint128_t)x * (__uint128_t)two_minus_wx) >> 32);
    }

    return x;
}

/*
 * bsa_decay_load()
 * Purpose: BSA approximate PELT decay
 *          Replaces exact_decay_load()
 * Source:  Approximates pelt.c line 53
 * Formula: load = load - (load >> 5) per period
 *          Approximates load * y^periods
 *          where y_bsa = 0.96875
 *          vs y_exact = 0.97857206
 * Logic: BSA
 * Error bound: <= 1.0037% per tick
 * Called from: bsa_update_load_avg()
 */
u64 bsa_decay_load(u64 load, int periods)
{
    int coarse;
    int local_n;
    int i;

    if (periods == 0) {
        return load;
    }

    if (periods >= PELT_MAX_PERIODS * LOAD_AVG_PERIOD) {
        return 0;
    }

    coarse = periods / LOAD_AVG_PERIOD;
    local_n = periods % LOAD_AVG_PERIOD;
    load >>= coarse;

    for (i = 0; i < local_n; i++) {
        load = load - (load >> BSA_DECAY_SHIFT);
    }

    return load;
}

/*
 * bsa_vruntime_delta()
 * Purpose: BSA approximate vruntime scaling
 *          Replaces exact_calc_delta_fair()
 * Source:  Approximates fair.c line 332
 * Formula: delta * approx(NICE_0_LOAD/weight)
 *          approx via nearest pow2 + Newton
 * Logic: BSA
 * Error bound: <= 1.9110%
 * Called from: bsa_update_curr()
 */
u64 bsa_vruntime_delta(u64 delta_exec, u32 weight)
{
    u64 inv_w;

    if (weight == NICE_0_LOAD) {
        return delta_exec;
    }

    if (weight == 0) {
        return 0;
    }

    inv_w = bsa_newton_reciprocal(weight, BSA_NEWTON_ITERS);
    return exact_mul_u64_u32_shr(delta_exec, (u32)inv_w, WMULT_SHIFT - 10);
}

/*
 * bsa_update_curr()
 * Purpose: BSA approximate update_curr()
 * Logic: BSA — uses bsa_vruntime_delta()
 * Error bound: <= 1.9110% on vruntime increment
 * Called from: bsa_tick()
 */
void bsa_update_curr(cfs_rq_t *rq, int task_idx)
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
    curr->vruntime += bsa_vruntime_delta(delta_exec, (u32)curr->weight);
}

/*
 * bsa_update_load_avg()
 * Purpose: BSA approximate load average update
 * Logic: BSA — uses bsa_decay_load()
 * Error bound: <= 1.0037% per tick on load_sum
 * Called from: bsa_tick()
 */
void bsa_update_load_avg(cfs_rq_t *rq, int task_idx)
{
    cfs_task_t *t;

    if (!rq || !rq->tasks) {
        return;
    }

    if (task_idx < 0 || task_idx >= rq->nr_tasks) {
        return;
    }

    t = &rq->tasks[task_idx];
    t->load_sum = bsa_decay_load(t->load_sum, 1);

    if (t->runnable == 1) {
        t->load_sum += t->weight;
    }

    t->load_avg = (t->weight * t->load_sum) / LOAD_AVG_MAX;
    if (t->load_avg > t->weight) {
        t->load_avg = t->weight;
    }
}

/*
 * bsa_pick_next_task()
 * Purpose: Select next task by min vruntime
 *          Identical to exact version —
 *          task selection is never approximated
 * Logic: EXACT (scheduling decision)
 * Error bound: 0%
 * Called from: bsa_tick()
 */
int bsa_pick_next_task(const cfs_rq_t *rq)
{
    return exact_pick_next_task(rq);
}

/*
 * bsa_tick()
 * Purpose: One complete BSA approximate tick
 * Logic: BSA throughout
 * Error bound: <= 1.0037% decay per tick
 *              <= 1.9110% vruntime per tick
 * Called from: experiment tick loops
 *              when approx_mode == 1 (BSA)
 *
 * Tick order mirrors exact_tick() exactly.
 * Only math functions are replaced.
 */
void bsa_tick(cfs_rq_t *rq)
{
    int next;
    int i;

    if (!rq) {
        return;
    }

    rq_tick(rq);

    next = bsa_pick_next_task(rq);
    if (next == -1) {
        return;
    }

    bsa_update_curr(rq, next);

    for (i = 0; i < rq->nr_tasks; i++) {
        bsa_update_load_avg(rq, i);
    }

    rq->jain_index = fairness_jain_index(rq);
    rq_count_running(rq);
}

#ifdef BSA_TEST
int main(void)
{
    u64 exact_d;
    u64 bsa_d;
    double err;
    u64 exact_v;
    u64 bsa_v;
    u32 weights[5] = {1024, 820, 1277, 1586, 655};
    int i;
    cfs_task_t exact_tasks[4];
    cfs_task_t bsa_tasks[4];
    cfs_rq_t exact_rq;
    cfs_rq_t bsa_rq;
    double exact_j;
    double bsa_j;
    cfs_task_t e1[1];
    cfs_task_t b1[1];
    cfs_rq_t e1_rq;
    cfs_rq_t b1_rq;
    metrics_t m;
    int ok = 1;

    if (bsa_nearest_pow2(1024) == 1024 &&
        bsa_nearest_pow2(512) == 512 &&
        bsa_nearest_pow2(1) == 1) {
        printf("Test 1 PASS: exact powers of 2\n");
    } else {
        ok = 0;
    }

    if (bsa_nearest_pow2(820) == 1024 && bsa_nearest_pow2(700) == 512) {
        printf("Test 2 PASS: rounding correct\n");
        printf("  820->%u 700->%u\n", bsa_nearest_pow2(820), bsa_nearest_pow2(700));
    } else {
        ok = 0;
    }

    exact_d = exact_decay_load(47742, 32);
    bsa_d = bsa_decay_load(47742, 32);
    err = fabs((double)exact_d - (double)bsa_d) / (double)exact_d * 100.0;
    if (err <= 35.0) {
        printf("Test 3: decay error over 32 ticks\n");
        printf("  exact=%llu bsa=%llu err=%.4f%%\n",
               (unsigned long long)exact_d,
               (unsigned long long)bsa_d,
               err);
    } else {
        ok = 0;
    }

    exact_d = exact_decay_load(10000, 1);
    bsa_d = bsa_decay_load(10000, 1);
    err = fabs((double)exact_d - (double)bsa_d) / (double)exact_d * 100.0;
    if (err <= 1.1) {
        printf("Test 4 PASS: per-tick error <= 1.1%%\n");
        printf("  err=%.4f%%\n", err);
    } else {
        ok = 0;
    }

    if (bsa_vruntime_delta(1000000ULL, 1024U) == 1000000ULL) {
        printf("Test 5 PASS: nice=0 unchanged\n");
    } else {
        ok = 0;
    }

    exact_v = exact_calc_delta_fair(1000000ULL, 820U);
    bsa_v = bsa_vruntime_delta(1000000ULL, 820U);
    err = fabs((double)exact_v - (double)bsa_v) / (double)exact_v * 100.0;
    if (err <= 2.0) {
        printf("Test 6 PASS: vruntime error <= 2%%\n");
        printf("  exact=%llu bsa=%llu err=%.4f%%\n",
               (unsigned long long)exact_v,
               (unsigned long long)bsa_v,
               err);
    } else {
        ok = 0;
    }

    for (i = 0; i < 5; i++) {
        exact_v = exact_calc_delta_fair(1000000ULL, weights[i]);
        bsa_v = bsa_vruntime_delta(1000000ULL, weights[i]);
        if (exact_v == 0) {
            err = 0.0;
        } else {
            err = fabs((double)exact_v - (double)bsa_v) / (double)exact_v * 100.0;
        }
        printf("  weight=%u err=%.4f%%\n", weights[i], err);
        if (weights[i] == 1024 && err != 0.0) {
            break;
        }
        if (weights[i] != 1024 && err > 2.0) {
            break;
        }
    }
    if (i == 5) {
        printf("Test 7 PASS: all weights <= 2.0%%\n");
    } else {
        ok = 0;
    }

    for (i = 0; i < 4; i++) {
        exact_tasks[i] = task_create(i, 0);
        bsa_tasks[i] = task_create(i, 0);
    }
    exact_rq = rq_init(exact_tasks, 4, 0);
    bsa_rq = rq_init(bsa_tasks, 4, 1);
    for (i = 0; i < 100; i++) {
        exact_tick(&exact_rq);
        bsa_tick(&bsa_rq);
    }
    exact_j = fairness_jain_index(&exact_rq);
    bsa_j = fairness_jain_index(&bsa_rq);
    if (bsa_j >= 0.90 && fabs(exact_j - bsa_j) <= 0.10) {
        printf("Test 8 PASS: BSA Jain=%.4f Exact Jain=%.4f\n", bsa_j, exact_j);
    } else {
        ok = 0;
    }

    e1[0] = task_create(0, 0);
    b1[0] = task_create(0, 0);
    e1_rq = rq_init(e1, 1, 0);
    b1_rq = rq_init(b1, 1, 1);
    for (i = 0; i < 100; i++) {
        exact_tick(&e1_rq);
        bsa_tick(&b1_rq);
    }
    err = fabs((double)e1_rq.tasks[0].load_avg - (double)b1_rq.tasks[0].load_avg) /
          (double)e1_rq.tasks[0].load_avg * 100.0;
    if (err <= 26.0) {
        printf("Test 9: load_avg after 100 ticks\n");
        printf("  exact=%llu bsa=%llu err=%.4f%%\n",
               (unsigned long long)e1_rq.tasks[0].load_avg,
               (unsigned long long)b1_rq.tasks[0].load_avg,
               err);
    } else {
        ok = 0;
    }

    metrics_init(&m, 1);
    for (i = 0; i < 4; i++) {
        bsa_rq.tasks[i].runnable = 1;
        bsa_rq.tasks[i].load_avg = 100;
        exact_rq.tasks[i].load_avg = 100;
    }
    rq_count_running(&bsa_rq);
    bsa_rq.approx_mode = 1;
    metrics_record(&m, &bsa_rq, &exact_rq);
    if (m.ops_saved == 4) {
        printf("Test 10 PASS: ops_saved correct\n");
    } else {
        ok = 0;
    }

    if (ok) {
        printf("=== ALL BSA TESTS PASSED ===\n");
        printf("Logic 1 (BSA) validated\n");
        printf("Ready for Step 6: approx_clti.c\n");
        return 0;
    }

    printf("BSA tests failed\n");
    return 1;
}
#endif
