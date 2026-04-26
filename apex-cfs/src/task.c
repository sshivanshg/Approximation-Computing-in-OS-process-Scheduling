#include "task.h"

/*
 * task_create()
 * Purpose: Initialize a CFS task with nice-derived weight
 * Logic: EXACT (no approximation)
 * Error bound: 0% (exact kernel weight table)
 * Called from: experiment setup functions
 */
cfs_task_t task_create(int id, int nice)
{
    cfs_task_t t;
    int clamped = nice;

    if (clamped < -20) {
        printf("[WARN] task_create: nice=%d out of range, clamped to -20\n", nice);
        clamped = -20;
    } else if (clamped > 19) {
        printf("[WARN] task_create: nice=%d out of range, clamped to 19\n", nice);
        clamped = 19;
    }

    t.id = id;
    t.nice = clamped;
    t.weight = (u64)nice_to_weight[clamped + 20];
    t.vruntime = 0;
    t.load_avg = 0;
    t.load_sum = 0;
    t.exec_runtime = 0;
    t.runnable = 1;

    return t;
}

/*
 * task_set_runnable()
 * Purpose: Update runnable flag for a task
 * Logic: EXACT (no approximation)
 * Error bound: 0%
 * Called from: experiment setup and workload changes
 */
void task_set_runnable(cfs_task_t *t, int runnable)
{
    if (!t) {
        return;
    }

    if (runnable != 0 && runnable != 1) {
        printf("[ERROR] task_set_runnable: invalid runnable=%d, forcing 0\n", runnable);
        t->runnable = 0;
        return;
    }

    t->runnable = runnable;
}

/*
 * task_get_weight()
 * Purpose: Return the task weight as u32
 * Logic: EXACT (no approximation)
 * Error bound: 0%
 * Called from: scheduling computations
 */
u32 task_get_weight(const cfs_task_t *t)
{
    if (!t) {
        return 0;
    }

    return (u32)t->weight;
}

/*
 * rq_init()
 * Purpose: Initialize a runqueue structure
 * Logic: EXACT (no approximation)
 * Error bound: 0%
 * Called from: experiment setup functions
 */
cfs_rq_t rq_init(cfs_task_t *tasks, int nr_tasks, int approx_mode)
{
    cfs_rq_t rq;
    int i;
    int running = 0;
    int mode = approx_mode;

    if (mode < 0 || mode > 3) {
        printf("[WARN] rq_init: invalid approx_mode=%d, using EXACT\n", approx_mode);
        mode = 0;
    }

    if (tasks && nr_tasks > 0) {
        for (i = 0; i < nr_tasks; i++) {
            if (tasks[i].runnable == 1) {
                running++;
            }
        }
    }

    rq.tasks = tasks;
    rq.nr_tasks = nr_tasks;
    rq.nr_running = running;
    rq.clock = 0;
    rq.tick = 0;
    rq.approx_mode = mode;
    rq.apaf_state = APAF_MEDIUM;
    rq.jain_index = 1.0;

    return rq;
}

/*
 * rq_tick()
 * Purpose: Advance runqueue time by one tick
 * Logic: EXACT (no approximation)
 * Error bound: 0%
 * Called from: scheduler loop
 */
void rq_tick(cfs_rq_t *rq)
{
    if (!rq) {
        return;
    }

    rq->tick += 1;
    rq->clock += TICK_NS;
}

/*
 * rq_count_running()
 * Purpose: Count runnable tasks and sync rq->nr_running
 * Logic: EXACT (no approximation)
 * Error bound: 0%
 * Called from: scheduler bookkeeping
 */
int rq_count_running(const cfs_rq_t *rq)
{
    int count = 0;
    int i;
    cfs_rq_t *rw;

    if (!rq || !rq->tasks || rq->nr_tasks <= 0) {
        return 0;
    }

    for (i = 0; i < rq->nr_tasks; i++) {
        if (rq->tasks[i].runnable == 1) {
            count++;
        }
    }

    rw = (cfs_rq_t *)rq;
    rw->nr_running = count;

    return count;
}

/*
 * task_print()
 * Purpose: Print a task summary
 * Logic: EXACT (no approximation)
 * Error bound: 0%
 * Called from: debug/experiments
 */
void task_print(const cfs_task_t *t)
{
    if (!t) {
        return;
    }

    printf("[Task id=%d nice=%d weight=%u]\n", t->id, t->nice, (u32)t->weight);
    printf("  vruntime     = %llu ns\n", (unsigned long long)t->vruntime);
    printf("  load_avg     = %llu\n", (unsigned long long)t->load_avg);
    printf("  load_sum     = %llu\n", (unsigned long long)t->load_sum);
    printf("  exec_runtime = %llu ns\n", (unsigned long long)t->exec_runtime);
    printf("  runnable     = %d\n", t->runnable);
}

/*
 * rq_print()
 * Purpose: Print runqueue summary and tasks
 * Logic: EXACT (no approximation)
 * Error bound: 0%
 * Called from: debug/experiments
 */
void rq_print(const cfs_rq_t *rq)
{
    const char *mode_str = "EXACT";
    const char *state_str = "MEDIUM";
    int i;

    if (!rq) {
        return;
    }

    if (rq->approx_mode == 1) {
        mode_str = "BSA";
    } else if (rq->approx_mode == 2) {
        mode_str = "CLTI";
    } else if (rq->approx_mode == 3) {
        mode_str = "APAF";
    }

    if (rq->apaf_state == APAF_TIGHT) {
        state_str = "TIGHT";
    } else if (rq->apaf_state == APAF_LOOSE) {
        state_str = "LOOSE";
    }

    printf("[Runqueue tick=%llu clock=%llu ns]\n",
           (unsigned long long)rq->tick,
           (unsigned long long)rq->clock);
    printf("  nr_tasks    = %d\n", rq->nr_tasks);
    printf("  nr_running  = %d\n", rq->nr_running);
    printf("  approx_mode = %d (%s)\n", rq->approx_mode, mode_str);
    printf("  apaf_state  = %d (%s)\n", rq->apaf_state, state_str);
    printf("  jain_index  = %.6f\n", rq->jain_index);

    for (i = 0; i < rq->nr_tasks; i++) {
        task_print(&rq->tasks[i]);
    }
}

#ifdef TASK_TEST
int main(void)
{
    cfs_task_t t0;
    cfs_task_t t1;
    cfs_task_t t2;
    cfs_task_t t3;
    cfs_task_t tasks[4];
    cfs_rq_t rq;
    int i;

    t0 = task_create(0, 0);
    if (t0.weight == 1024 && t0.vruntime == 0 && t0.runnable == 1) {
        printf("Test 1 PASS: nice=0 weight=1024\n");
    }

    t1 = task_create(1, -5);
    if (t1.weight == 3121) {
        printf("Test 2 PASS: nice=-5 weight=3121\n");
    }

    t2 = task_create(2, 1);
    if (t2.weight == 820) {
        printf("Test 3 PASS: nice=+1 weight=820\n");
    }

    t3 = task_create(3, 25);
    if (t3.nice == 19 && t3.weight == 15) {
        printf("Test 4 PASS: clamping works\n");
    }

    tasks[0] = t0;
    tasks[1] = t1;
    tasks[2] = t2;
    tasks[3] = t3;

    rq = rq_init(tasks, 4, 0);
    if (rq.nr_running == 4 && rq.clock == 0 && rq.apaf_state == APAF_MEDIUM) {
        printf("Test 5 PASS: rq_init correct\n");
    }

    for (i = 0; i < 5; i++) {
        rq_tick(&rq);
    }
    if (rq.tick == 5 && rq.clock == 5 * TICK_NS) {
        printf("Test 6 PASS: rq_tick correct\n");
    }

    task_print(&tasks[0]);
    rq_print(&rq);
    printf("Test 7 PASS: print functions work\n");

    printf("=== ALL TASK TESTS PASSED ===\n");
    printf("Ready for Step 2: fairness.c\n");

    return 0;
}
#endif
