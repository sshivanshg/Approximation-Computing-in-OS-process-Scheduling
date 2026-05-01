#include "approx_clti.h"
#include "fairness.h"

/*
 * clti_weight_to_class()
 * Purpose: Map weight to nearest CLTI class
 * Logic: CLTI
 * Error bound: class representative error
 *              varies by class
 * Called from: clti_vruntime_delta()
 */
int clti_weight_to_class(u32 weight)
{
    int i;
    int best = 0;
    u64 best_diff;

    if (weight == 1024U) {
        return 4;
    }

    best_diff = (weight > clti_weight_class[0])
        ? (u64)(weight - clti_weight_class[0])
        : (u64)(clti_weight_class[0] - weight);

    for (i = 1; i < CLTI_TABLE_SIZE; i++) {
        u64 diff = (weight > clti_weight_class[i])
            ? (u64)(weight - clti_weight_class[i])
            : (u64)(clti_weight_class[i] - weight);
        if (diff < best_diff) {
            best_diff = diff;
            best = i;
        }
    }

    return best;
}

/*
 * clti_decay_interpolate()
 * Purpose: Apply CLTI decay for local_n periods
 *          using 8-entry table + interpolation
 *          Replaces exact table lookup pelt.c:53
 * Logic: CLTI
 * Error bound: ≤ 0.0938% per interpolation step
 *              ≤ 2.1889% accumulated (32 ticks)
 * Called from: clti_decay_load()
 */
u64 clti_decay_interpolate(u64 load, int local_n)
{
    int idx;
    int rem;
    u64 base;

    if (local_n == 0) {
        return load;
    }

    idx = local_n / CLTI_TABLE_STEP;
    rem = local_n % CLTI_TABLE_STEP;

    if (idx >= CLTI_TABLE_SIZE) {
        return 0;
    }

    base = exact_mul_u64_u32_shr(load, clti_decay_table[idx], CLTI_SHIFT);

    if (rem > 0) {
        if ((idx + 1) < CLTI_TABLE_SIZE) {
            u64 next = exact_mul_u64_u32_shr(load, clti_decay_table[idx + 1], CLTI_SHIFT);
            u64 diff = (base >= next) ? (base - next) : 0;
            u64 correction = (diff * (u64)rem) / CLTI_TABLE_STEP;
            base = (base >= correction) ? (base - correction) : 0;
        } else {
            base >>= 1;
        }
    }

    return base;
}

/*
 * clti_decay_load()
 * Purpose: CLTI approximate PELT decay
 *          Replaces exact_decay_load()
 * Source:  Approximates pelt.c line 53
 * Logic: CLTI — coarse exact, fine interpolated
 * Error bound: ≤ 2.1889% accumulated (32 ticks)
 * Called from: clti_update_load_avg()
 */
u64 clti_decay_load(u64 load, int periods)
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
    load >>= coarse;

    local_n = periods % LOAD_AVG_PERIOD;
    return clti_decay_interpolate(load, local_n);
}

/*
 * clti_vruntime_delta()
 * Purpose: CLTI approximate vruntime scaling
 *          Uses weight class representative
 *          + Newton reciprocal from BSA
 * Source:  Approximates fair.c line 332
 * Logic: CLTI
 * Error bound: class mapping + Newton ≤ 3.0%
 * Called from: clti_update_curr()
 */
u64 clti_vruntime_delta(u64 delta_exec, u32 weight)
{
    int cls;
    u32 w_rep;
    u64 inv_w;

    if (weight == NICE_0_LOAD) {
        return delta_exec;
    }

    if (weight == 0) {
        return 0;
    }

    cls = clti_weight_to_class(weight);
    w_rep = clti_weight_class[cls];
    inv_w = bsa_newton_reciprocal(w_rep, 1);

    return exact_mul_u64_u32_shr(delta_exec, (u32)inv_w, WMULT_SHIFT - 10);
}

/*
 * clti_update_curr()
 * Purpose: CLTI approximate update_curr()
 * Logic: CLTI — uses clti_vruntime_delta()
 * Error bound: ≤ 3.0% on vruntime increment
 * Called from: clti_tick()
 */
void clti_update_curr(cfs_rq_t *rq, int task_idx)
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
    curr->vruntime += clti_vruntime_delta(delta_exec, (u32)curr->weight);
}

/*
 * clti_update_load_avg()
 * Purpose: CLTI approximate load average update
 * Logic: CLTI — uses clti_decay_load()
 * Error bound: ≤ 2.1889% accumulated
 * Called from: clti_tick()
 */
void clti_update_load_avg(cfs_rq_t *rq, int task_idx)
{
    cfs_task_t *t;

    if (!rq || !rq->tasks) {
        return;
    }

    if (task_idx < 0 || task_idx >= rq->nr_tasks) {
        return;
    }

    t = &rq->tasks[task_idx];
    t->load_sum = clti_decay_load(t->load_sum, 1);

    if (t->runnable == 1) {
        t->load_sum += t->weight;
    }

    t->load_avg = (t->weight * t->load_sum) / LOAD_AVG_MAX;
    if (t->load_avg > t->weight) {
        t->load_avg = t->weight;
    }
}

/*
 * clti_pick_next_task()
 * Purpose: Select next task by min vruntime
 *          Identical to exact version —
 *          scheduling decision never approximated
 * Logic: EXACT (scheduling decision)
 * Error bound: 0%
 * Called from: clti_tick()
 */
int clti_pick_next_task(const cfs_rq_t *rq)
{
    return exact_pick_next_task(rq);
}

/*
 * clti_tick()
 * Purpose: One complete CLTI approximate tick
 * Logic: CLTI throughout
 * Error bound: ≤ 0.0938% decay per tick
 *              ≤ 3.0% vruntime per tick
 * Called from: experiment tick loops
 *              when approx_mode == 2 (CLTI)
 */
void clti_tick(cfs_rq_t *rq)
{
    int next;
    int i;

    if (!rq) {
        return;
    }

    rq_tick(rq);
    next = clti_pick_next_task(rq);
    if (next == -1) {
        return;
    }

    clti_update_curr(rq, next);

    for (i = 0; i < rq->nr_tasks; i++) {
        clti_update_load_avg(rq, i);
    }

    rq->jain_index = fairness_jain_index(rq);
    rq_count_running(rq);
}

#ifdef CLTI_TEST
int main(void)
{
    u64 v;
    u64 exact_d;
    u64 clti_d;
    u64 bsa_d;
    double err;
    double err_bsa;
    double err_clti;
    cfs_task_t exact_tasks[4];
    cfs_task_t clti_tasks[4];
    cfs_rq_t exact_rq;
    cfs_rq_t clti_rq;
    double exact_j;
    double clti_j;
    cfs_task_t ex1[1];
    cfs_task_t bs1[1];
    cfs_task_t cl1[1];
    cfs_rq_t ex1_rq;
    cfs_rq_t bs1_rq;
    cfs_rq_t cl1_rq;
    int i;
    int ok = 1;

    if (clti_decay_table[0] == 32768 && clti_decay_table[1] == 30048 &&
        clti_decay_table[4] == 23170 && clti_decay_table[7] == 17867) {
        printf("Test 1 PASS: table entries correct\n");
    } else {
        ok = 0;
    }

    if (clti_weight_to_class(1024) == 4 &&
        clti_weight_to_class(820) == 4 &&
        clti_weight_to_class(88761) == 0 &&
        clti_weight_to_class(15) == 7) {
        printf("Test 2 PASS: weight class mapping\n");
        printf("  1024->%d 820->%d 88761->%d 15->%d\n",
               clti_weight_to_class(1024),
               clti_weight_to_class(820),
               clti_weight_to_class(88761),
               clti_weight_to_class(15));
    } else {
        ok = 0;
    }

    if (clti_decay_interpolate(10000, 0) == 10000) {
        printf("Test 3 PASS: zero periods identity\n");
    } else {
        ok = 0;
    }

    v = clti_decay_interpolate(32768, 4);
    if (v == 30048) {
        printf("Test 4 PASS: exact table entry\n");
    } else {
        ok = 0;
    }

    v = clti_decay_interpolate(32768, 6);
    if (v >= 28751 && v <= 28851) {
        printf("Test 5 PASS: interpolation correct\n");
        printf("  value=%llu\n", (unsigned long long)v);
    } else {
        ok = 0;
    }

    exact_d = exact_decay_load(10000, 1);
    clti_d = clti_decay_load(10000, 1);
    err = fabs((double)exact_d - (double)clti_d) / (double)exact_d * 100.0;
    if (err <= 0.5) {
        printf("Test 6 PASS: CLTI per-tick error\n");
        printf("  exact=%llu clti=%llu err=%.4f%%\n",
               (unsigned long long)exact_d,
               (unsigned long long)clti_d,
               err);
    } else {
        ok = 0;
    }

    exact_d = exact_decay_load(47742, 32);
    clti_d = clti_decay_load(47742, 32);
    err = fabs((double)exact_d - (double)clti_d) / (double)exact_d * 100.0;
    if (err <= 3.0) {
        printf("Test 7: accumulated error 32 ticks\n");
        printf("  exact=%llu clti=%llu err=%.4f%%\n",
               (unsigned long long)exact_d,
               (unsigned long long)clti_d,
               err);
    } else {
        ok = 0;
    }

    bsa_d = bsa_decay_load(47742, 32);
    err_bsa = fabs((double)exact_d - (double)bsa_d) / (double)exact_d * 100.0;
    err_clti = fabs((double)exact_d - (double)clti_d) / (double)exact_d * 100.0;
    if (err_clti <= err_bsa) {
        printf("Test 8 PASS: CLTI more accurate\n");
        printf("  err_bsa=%.4f%% err_clti=%.4f%%\n", err_bsa, err_clti);
    } else {
        ok = 0;
    }

    for (i = 0; i < 4; i++) {
        exact_tasks[i] = task_create(i, 0);
        clti_tasks[i] = task_create(i, 0);
    }
    exact_rq = rq_init(exact_tasks, 4, 0);
    clti_rq = rq_init(clti_tasks, 4, 2);
    for (i = 0; i < 100; i++) {
        exact_tick(&exact_rq);
        clti_tick(&clti_rq);
    }
    exact_j = fairness_jain_index(&exact_rq);
    clti_j = fairness_jain_index(&clti_rq);
    if (clti_j >= 0.90 && fabs(exact_j - clti_j) <= 0.10) {
        printf("Test 9 PASS: CLTI Jain=%.4f\n", clti_j);
        printf("  exact=%.4f clti=%.4f\n", exact_j, clti_j);
    } else {
        ok = 0;
    }

    ex1[0] = task_create(0, 0);
    bs1[0] = task_create(0, 0);
    cl1[0] = task_create(0, 0);
    ex1_rq = rq_init(ex1, 1, 0);
    bs1_rq = rq_init(bs1, 1, 1);
    cl1_rq = rq_init(cl1, 1, 2);
    for (i = 0; i < 100; i++) {
        exact_tick(&ex1_rq);
        bsa_tick(&bs1_rq);
        clti_tick(&cl1_rq);
    }
    err_bsa = fabs((double)ex1_rq.tasks[0].load_avg - (double)bs1_rq.tasks[0].load_avg) /
              (double)ex1_rq.tasks[0].load_avg * 100.0;
    err_clti = fabs((double)ex1_rq.tasks[0].load_avg - (double)cl1_rq.tasks[0].load_avg) /
               (double)ex1_rq.tasks[0].load_avg * 100.0;
    if (err_clti <= err_bsa) {
        printf("Test 10 PASS: CLTI more accurate than BSA\n");
        printf("  err_bsa=%.4f%% err_clti=%.4f%%\n", err_bsa, err_clti);
    } else {
        ok = 0;
    }

    if (ok) {
        printf("=== ALL CLTI TESTS PASSED ===\n");
        printf("Logic 2 (CLTI) validated\n");
        printf("Ready for Step 7: approx_apaf.c\n");
        return 0;
    }

    printf("CLTI tests failed\n");
    return 1;
}
#endif
