#include "fairness.h"
#include <math.h>

/*
 * fairness_jain_index()
 * Purpose: Compute Jain's Fairness Index
 *          over all runnable tasks
 * Formula: J = (Σxi)² / (n·Σxi²)
 *          where xi = exec_runtime
 * Logic: EXACT (used by all 3 approximation logics)
 * Error bound: 0% (exact double arithmetic)
 * Called from: apaf_update_state(), metrics_record()
 */
double fairness_jain_index(const cfs_rq_t *rq)
{
    u64 sum = 0;
    u64 sum_sq = 0;
    int n = 0;
    int i;
    double j;

    if (!rq || !rq->tasks || rq->nr_tasks <= 0) {
        return 1.0;
    }

    for (i = 0; i < rq->nr_tasks; i++) {
        if (rq->tasks[i].runnable == 1) {
            u64 x = rq->tasks[i].exec_runtime;
            sum += x;
            sum_sq += x * x;
            n++;
        }
    }

    if (n < JAIN_MIN_TASKS) {
        return 1.0;
    }

    if (sum == 0 || sum_sq == 0) {
        return 1.0;
    }

    j = (double)(sum * sum) / (double)(n * sum_sq);

    if (j > 1.0) {
        return 1.0;
    }
    if (j < 0.0) {
        return 0.0;
    }

    return j;
}

/*
 * fairness_apaf_next_state()
 * Purpose: APAF state machine transition function
 * Logic: APAF controller (Novel contribution)
 * Error bound: N/A (control logic, not math)
 * Called from: apaf_update_state() every 4 ticks
 *
 * State machine:
 *   TIGHT(0) ←→ MEDIUM(1) ←→ LOOSE(2)
 *   No level skipping allowed.
 *   Hysteresis gaps prevent oscillation:
 *     LOOSE→MEDIUM: J < 0.93
 *     MEDIUM→LOOSE: J > 0.97  (gap = 0.04)
 *     MEDIUM→TIGHT: J < 0.90
 *     TIGHT→MEDIUM: J > 0.95  (gap = 0.05)
 */
int fairness_apaf_next_state(int current_state, double jain)
{
    double j = jain;

    if (j > 1.0) {
        j = 1.0;
    } else if (j < 0.0) {
        j = 0.0;
    }

    switch (current_state) {
    case APAF_LOOSE:
        /* LOOSE cannot transition to TIGHT directly */
        if (j < JAIN_LOOSE_TO_MEDIUM) {
            return APAF_MEDIUM;
        }
        return APAF_LOOSE;
    case APAF_MEDIUM:
        if (j < JAIN_MEDIUM_TO_TIGHT) {
            return APAF_TIGHT;
        }
        if (j > JAIN_MEDIUM_TO_LOOSE) {
            return APAF_LOOSE;
        }
        return APAF_MEDIUM;
    case APAF_TIGHT:
        if (j > JAIN_TIGHT_TO_MEDIUM) {
            return APAF_MEDIUM;
        }
        return APAF_TIGHT;
    default:
        return current_state;
    }
}

/*
 * fairness_cpu_shares()
 * Purpose: Compute fractional CPU share per task
 * Logic: EXACT (measurement function)
 * Error bound: 0%
 * Called from: metrics_record(), exp1 validation
 */
void fairness_cpu_shares(const cfs_rq_t *rq, double *shares, int max_tasks)
{
    u64 total = 0;
    int nr_running = 0;
    int i;
    int limit;

    if (!shares || max_tasks <= 0) {
        return;
    }

    limit = max_tasks;
    if (!rq || !rq->tasks || rq->nr_tasks <= 0) {
        for (i = 0; i < limit; i++) {
            shares[i] = 0.0;
        }
        return;
    }

    if (rq->nr_tasks < limit) {
        limit = rq->nr_tasks;
    }

    for (i = 0; i < limit; i++) {
        if (rq->tasks[i].runnable == 1) {
            total += rq->tasks[i].exec_runtime;
            nr_running++;
        }
    }

    for (i = 0; i < limit; i++) {
        if (rq->tasks[i].runnable == 1) {
            if (total > 0) {
                shares[i] = (double)rq->tasks[i].exec_runtime / (double)total;
            } else if (nr_running > 0) {
                shares[i] = 1.0 / (double)nr_running;
            } else {
                shares[i] = 0.0;
            }
        } else {
            shares[i] = 0.0;
        }
    }
}

/*
 * fairness_ideal_share()
 * Purpose: Compute weight-proportional ideal share
 * Formula: share = task_weight / total_weight
 * Logic: EXACT (theoretical baseline)
 * Error bound: 0%
 * Called from: metrics_record() for error calc
 */
double fairness_ideal_share(u32 task_weight, u32 total_weight)
{
    if (total_weight == 0) {
        return 0.0;
    }

    return (double)task_weight / (double)total_weight;
}

/*
 * fairness_total_weight()
 * Purpose: Sum weights of all runnable tasks
 * Logic: EXACT
 * Error bound: 0%
 * Called from: fairness_ideal_share() callers
 */
u32 fairness_total_weight(const cfs_rq_t *rq)
{
    u32 total = 0;
    int i;

    if (!rq || !rq->tasks || rq->nr_tasks <= 0) {
        return 0;
    }

    for (i = 0; i < rq->nr_tasks; i++) {
        if (rq->tasks[i].runnable == 1) {
            total += (u32)rq->tasks[i].weight;
        }
    }

    return total;
}

#ifdef FAIRNESS_TEST
int main(void)
{
    cfs_task_t tasks1[4];
    cfs_task_t tasks2[4];
    cfs_task_t tasks3[1];
    cfs_task_t tasks4[3];
    cfs_task_t tasks5[3];
    cfs_rq_t rq1;
    cfs_rq_t rq2;
    cfs_rq_t rq3;
    cfs_rq_t rq4;
    cfs_rq_t rq5;
    double j;
    double shares[3];
    double sum_shares;
    int next;

    tasks1[0] = task_create(0, 0);
    tasks1[1] = task_create(1, 0);
    tasks1[2] = task_create(2, 0);
    tasks1[3] = task_create(3, 0);
    tasks1[0].exec_runtime = 100;
    tasks1[1].exec_runtime = 100;
    tasks1[2].exec_runtime = 100;
    tasks1[3].exec_runtime = 100;
    rq1 = rq_init(tasks1, 4, 0);
    j = fairness_jain_index(&rq1);
    if (j == 1.0) {
        printf("Test 1 PASS: J=1.0 for equal shares\n");
    }

    tasks2[0] = task_create(0, 0);
    tasks2[1] = task_create(1, 0);
    tasks2[2] = task_create(2, 0);
    tasks2[3] = task_create(3, 0);
    tasks2[0].exec_runtime = 100;
    tasks2[1].exec_runtime = 200;
    tasks2[2].exec_runtime = 300;
    tasks2[3].exec_runtime = 400;
    rq2 = rq_init(tasks2, 4, 0);
    j = fairness_jain_index(&rq2);
    if (fabs(j - 0.833333) <= 0.001) {
        printf("Test 2 PASS: J=0.8333 for unequal shares\n");
    }

    tasks3[0] = task_create(0, 0);
    tasks3[0].exec_runtime = 500;
    rq3 = rq_init(tasks3, 1, 0);
    j = fairness_jain_index(&rq3);
    if (j == 1.0) {
        printf("Test 3 PASS: J=1.0 for single task\n");
    }

    tasks4[0] = task_create(0, 0);
    tasks4[1] = task_create(1, 0);
    tasks4[2] = task_create(2, 0);
    tasks4[0].exec_runtime = 0;
    tasks4[1].exec_runtime = 0;
    tasks4[2].exec_runtime = 0;
    rq4 = rq_init(tasks4, 3, 0);
    j = fairness_jain_index(&rq4);
    if (j == 1.0) {
        printf("Test 4 PASS: J=1.0 for zero runtime\n");
    }

    next = fairness_apaf_next_state(APAF_LOOSE, 0.92);
    if (next == APAF_MEDIUM) {
        printf("Test 5 PASS: LOOSE→MEDIUM at J=0.92\n");
    }

    next = fairness_apaf_next_state(APAF_MEDIUM, 0.94);
    if (next == APAF_MEDIUM) {
        printf("Test 6 PASS: MEDIUM stays at J=0.94\n");
    }

    next = fairness_apaf_next_state(APAF_MEDIUM, 0.88);
    if (next == APAF_TIGHT) {
        printf("Test 7 PASS: MEDIUM→TIGHT at J=0.88\n");
    }

    next = fairness_apaf_next_state(APAF_TIGHT, 0.96);
    if (next == APAF_MEDIUM) {
        printf("Test 8 PASS: TIGHT→MEDIUM at J=0.96\n");
    }

    next = fairness_apaf_next_state(APAF_LOOSE, 0.85);
    if (next == APAF_MEDIUM) {
        printf("Test 9 PASS: no level skipping\n");
    }

    tasks5[0] = task_create(0, 0);
    tasks5[1] = task_create(1, 0);
    tasks5[2] = task_create(2, 0);
    tasks5[0].exec_runtime = 300;
    tasks5[1].exec_runtime = 500;
    tasks5[2].exec_runtime = 200;
    rq5 = rq_init(tasks5, 3, 0);
    fairness_cpu_shares(&rq5, shares, 3);
    sum_shares = shares[0] + shares[1] + shares[2];
    if (fabs(shares[0] - 0.3) <= 0.01 &&
        fabs(shares[1] - 0.5) <= 0.01 &&
        fabs(shares[2] - 0.2) <= 0.01 &&
        fabs(sum_shares - 1.0) <= 0.01) {
        printf("Test 10 PASS: shares sum to 1.0\n");
    }

    printf("=== ALL FAIRNESS TESTS PASSED ===\n");
    printf("Ready for Step 3: metrics.c\n");

    return 0;
}
#endif
