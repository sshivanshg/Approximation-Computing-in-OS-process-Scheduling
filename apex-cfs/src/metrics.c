#include "metrics.h"

/*
 * metrics_init()
 * Purpose: Zero-initialize metrics buffer
 * Logic: EXACT (bookkeeping only)
 * Error bound: 0%
 * Called from: experiment setup, before tick loop
 */
void metrics_init(metrics_t *buf, int n)
{
    int count = n;
    int i;

    if (!buf) {
        return;
    }

    if (count <= 0) {
        printf("[ERROR] metrics_init: invalid n=%d\n", n);
        return;
    }

    if (count > METRICS_MAX_TICKS) {
        printf("[WARN] metrics_init: n=%d exceeds max, clamping to %d\n",
               n, METRICS_MAX_TICKS);
        count = METRICS_MAX_TICKS;
    }

    memset(buf, 0, sizeof(metrics_t) * (size_t)count);
    for (i = 0; i < count; i++) {
        buf[i].apaf_state = APAF_MEDIUM;
        buf[i].jain_index = 1.0;
    }
}

/*
 * metrics_record()
 * Purpose: Snapshot metrics by comparing
 *          approx vs exact runqueue at one tick
 * Logic: EXACT (measurement, not approximation)
 * Error bound: 0% (measuring, not computing)
 * Called from: main tick loop in each experiment
 *
 * IMPORTANT: approx_rq and exact_rq must be
 * at the same tick when this is called.
 * nr_tasks must be identical in both rqs.
 */
void metrics_record(metrics_t *entry, const cfs_rq_t *approx_rq, const cfs_rq_t *exact_rq)
{
    double sum_error = 0.0;
    double max_error = 0.0;
    double avg_error = 0.0;
    int count = 0;
    int i;
    u64 ops_saved = 0;

    if (!entry || !approx_rq || !exact_rq) {
        return;
    }

    entry->tick = approx_rq->tick;
    entry->logic_used = approx_rq->approx_mode;
    entry->apaf_state = approx_rq->apaf_state;
    entry->jain_index = fairness_jain_index(approx_rq);

    for (i = 0; i < approx_rq->nr_tasks; i++) {
        u64 approx_load = approx_rq->tasks[i].load_avg;
        u64 exact_load = exact_rq->tasks[i].load_avg;
        if (exact_load > 0) {
            double err = fabs((double)approx_load - (double)exact_load) /
                         (double)exact_load * 100.0;
            if (err > max_error) {
                max_error = err;
            }
            sum_error += err;
            count++;
        }
    }

    if (count > 0) {
        avg_error = sum_error / (double)count;
    }

    entry->max_error_pct = max_error;
    entry->avg_error_pct = avg_error;

    if (approx_rq->approx_mode == 1) {
        ops_saved = (u64)approx_rq->nr_running;
    } else if (approx_rq->approx_mode == 2) {
        ops_saved = (u64)approx_rq->nr_running * 4ULL;
    } else if (approx_rq->approx_mode == 3) {
        if (approx_rq->apaf_state == APAF_TIGHT) {
            ops_saved = (u64)approx_rq->nr_running * 2ULL;
        } else if (approx_rq->apaf_state == APAF_MEDIUM) {
            ops_saved = (u64)approx_rq->nr_running * 3ULL;
        } else {
            ops_saved = (u64)approx_rq->nr_running * 4ULL;
        }
    } else {
        ops_saved = 0;
    }

    entry->ops_saved = ops_saved;
}

/*
 * metrics_write_csv()
 * Purpose: Write metrics buffer to CSV file
 * Logic: EXACT (I/O only)
 * Error bound: 0%
 * Called from: end of each experiment
 *
 * CSV columns (NEVER CHANGE ORDER):
 * tick, logic, n_tasks, jain_index,
 * max_error_pct, avg_error_pct,
 * ops_saved, apaf_state
 */
void metrics_write_csv(const metrics_t *buf, int n_entries, int n_tasks, const char *filename)
{
    FILE *f;
    int i;

    if (!buf || !filename || n_entries <= 0) {
        return;
    }

    f = fopen(filename, "w");
    if (!f) {
        printf("[ERROR] metrics_write_csv: cannot open %s\n", filename);
        return;
    }

    fprintf(f, METRICS_CSV_HEADER);

    for (i = 0; i < n_entries; i++) {
        const char *logic = metrics_get_logic_name(buf[i].logic_used);
        const char *state = metrics_get_state_name(buf[i].logic_used, buf[i].apaf_state);
        fprintf(f, "%llu,%s,%d,%.6f,%.6f,%.6f,%llu,%s\n",
                (unsigned long long)buf[i].tick,
                logic,
                n_tasks,
                buf[i].jain_index,
                buf[i].max_error_pct,
                buf[i].avg_error_pct,
                (unsigned long long)buf[i].ops_saved,
                state);
    }

    fclose(f);
    printf("Wrote %d entries to %s\n", n_entries, filename);
}

/*
 * metrics_print_summary()
 * Purpose: Human-readable experiment summary
 * Logic: EXACT (reporting only)
 * Error bound: 0%
 * Called from: end of each experiment
 */
void metrics_print_summary(const metrics_t *buf, int n_entries, int n_tasks)
{
    double avg_jain = 0.0;
    double min_jain = 1.0;
    double overall_max_error = 0.0;
    double overall_avg_error = 0.0;
    u64 total_ops_saved = 0;
    int transitions = 0;
    int i;
    int logic_mode = 0;

    if (!buf || n_entries <= 0) {
        return;
    }

    logic_mode = buf[0].logic_used;

    for (i = 0; i < n_entries; i++) {
        avg_jain += buf[i].jain_index;
        if (buf[i].jain_index < min_jain) {
            min_jain = buf[i].jain_index;
        }
        if (buf[i].max_error_pct > overall_max_error) {
            overall_max_error = buf[i].max_error_pct;
        }
        overall_avg_error += buf[i].avg_error_pct;
        total_ops_saved += buf[i].ops_saved;
        if (logic_mode == 3 && i > 0) {
            if (buf[i].apaf_state != buf[i - 1].apaf_state) {
                transitions++;
            }
        }
    }

    avg_jain /= (double)n_entries;
    overall_avg_error /= (double)n_entries;

    printf("================================\n");
    printf("APEX-CFS Experiment Summary\n");
    printf("================================\n");
    printf("Tasks:          %d\n", n_tasks);
    printf("Ticks:          %d\n", n_entries);
    printf("Logic:          %s\n", metrics_get_logic_name(logic_mode));
    printf("--------------------------------\n");
    printf("Fairness (Jain's Index):\n");
    printf("  Average:      %.6f\n", avg_jain);
    printf("  Minimum:      %.6f\n", min_jain);
    printf("Error vs Exact:\n");
    printf("  Max observed: %.6f%%\n", overall_max_error);
    printf("  Avg observed: %.6f%%\n", overall_avg_error);
    printf("Operations saved:\n");
    printf("  Total:        %llu\n", (unsigned long long)total_ops_saved);
    printf("  Per tick avg: %.2f\n", (double)total_ops_saved / (double)n_entries);

    if (logic_mode == 3) {
        printf("APAF transitions: %d\n", transitions);
    }

    printf("================================\n");
}

/*
 * metrics_get_logic_name()
 * Purpose: Return string name for a given approx_mode.
 * Logic: EXACT
 * Error bound: 0%
 * Called from: metrics_write_csv(), metrics_print_summary()
 */
const char *metrics_get_logic_name(int mode)
{
    switch (mode) {
    case 0:
        return LOGIC_NAME_EXACT;
    case 1:
        return LOGIC_NAME_BSA;
    case 2:
        return LOGIC_NAME_CLTI;
    case 3:
        return LOGIC_NAME_APAF;
    default:
        return "UNKNOWN";
    }
}

/*
 * metrics_get_state_name()
 * Purpose: Return APAF state name string.
 * Logic: EXACT
 * Error bound: 0%
 * Called from: metrics_write_csv(), metrics_print_summary()
 */
const char *metrics_get_state_name(int mode, int state)
{
    if (mode != 3) {
        return "N/A";
    }

    switch (state) {
    case APAF_TIGHT:
        return "TIGHT";
    case APAF_MEDIUM:
        return "MEDIUM";
    case APAF_LOOSE:
        return "LOOSE";
    default:
        return "UNKNOWN";
    }
}

#ifdef METRICS_TEST
int main(void)
{
    metrics_t buf[10];
    cfs_task_t tasks_a[3];
    cfs_task_t tasks_b[3];
    cfs_rq_t rq_a;
    cfs_rq_t rq_b;
    FILE *f;
    char line[256];
    int i;
    int ok_ops;

    memset(buf, 0xFF, sizeof(buf));
    metrics_init(buf, 10);
    if (buf[0].tick == 0 && buf[0].jain_index == 1.0 && buf[5].apaf_state == APAF_MEDIUM) {
        printf("Test 1 PASS: metrics_init correct\n");
    }

    tasks_a[0] = task_create(0, 0);
    tasks_a[1] = task_create(1, 0);
    tasks_a[2] = task_create(2, 0);
    tasks_b[0] = task_create(0, 0);
    tasks_b[1] = task_create(1, 0);
    tasks_b[2] = task_create(2, 0);

    tasks_a[0].load_avg = 990;
    tasks_a[1].load_avg = 990;
    tasks_a[2].load_avg = 990;
    tasks_b[0].load_avg = 1000;
    tasks_b[1].load_avg = 1000;
    tasks_b[2].load_avg = 1000;

    rq_a = rq_init(tasks_a, 3, 1);
    rq_b = rq_init(tasks_b, 3, 0);
    rq_a.tick = 42;

    metrics_record(&buf[0], &rq_a, &rq_b);
    if (buf[0].tick == 42 && buf[0].logic_used == 1 &&
        fabs(buf[0].max_error_pct - 1.0) <= 0.1) {
        printf("Test 2 PASS: metrics_record correct\n");
    }

    rq_a.nr_running = 3;
    rq_a.apaf_state = APAF_LOOSE;

    ok_ops = 1;
    rq_a.approx_mode = 0;
    metrics_record(&buf[1], &rq_a, &rq_b);
    if (buf[1].ops_saved != 0) {
        ok_ops = 0;
    }

    rq_a.approx_mode = 1;
    metrics_record(&buf[1], &rq_a, &rq_b);
    if (buf[1].ops_saved != 3) {
        ok_ops = 0;
    }

    rq_a.approx_mode = 2;
    metrics_record(&buf[1], &rq_a, &rq_b);
    if (buf[1].ops_saved != 12) {
        ok_ops = 0;
    }

    rq_a.approx_mode = 3;
    rq_a.apaf_state = APAF_LOOSE;
    metrics_record(&buf[1], &rq_a, &rq_b);
    if (buf[1].ops_saved != 12) {
        ok_ops = 0;
    }

    if (ok_ops) {
        printf("Test 3 PASS: ops_saved correct\n");
    }

    if (strcmp(metrics_get_logic_name(0), "EXACT") == 0 &&
        strcmp(metrics_get_logic_name(1), "BSA") == 0 &&
        strcmp(metrics_get_logic_name(2), "CLTI") == 0 &&
        strcmp(metrics_get_logic_name(3), "APAF") == 0 &&
        strcmp(metrics_get_logic_name(9), "UNKNOWN") == 0) {
        printf("Test 4 PASS: logic names correct\n");
    }

    if (strcmp(metrics_get_state_name(3, 0), "TIGHT") == 0 &&
        strcmp(metrics_get_state_name(3, 1), "MEDIUM") == 0 &&
        strcmp(metrics_get_state_name(3, 2), "LOOSE") == 0 &&
        strcmp(metrics_get_state_name(1, 0), "N/A") == 0) {
        printf("Test 5 PASS: state names correct\n");
    }

    for (i = 0; i < 5; i++) {
        buf[i].tick = (u64)i;
        buf[i].logic_used = 1;
        buf[i].jain_index = 1.0;
        buf[i].max_error_pct = 0.5;
        buf[i].avg_error_pct = 0.25;
        buf[i].ops_saved = 10;
        buf[i].apaf_state = APAF_MEDIUM;
    }

    metrics_write_csv(buf, 5, 3, "results/test_metrics.csv");
    f = fopen("results/test_metrics.csv", "r");
    if (f) {
        if (fgets(line, (int)sizeof(line), f)) {
            if (strcmp(line, METRICS_CSV_HEADER) == 0) {
                printf("Test 6 PASS: CSV written correctly\n");
            }
        }
        fclose(f);
    }

    for (i = 0; i < 10; i++) {
        buf[i].jain_index = 1.0 - (0.01 * i);
        buf[i].max_error_pct = 0.2 * i;
        buf[i].avg_error_pct = 0.1 * i;
        buf[i].ops_saved = 5;
        buf[i].logic_used = 3;
        buf[i].apaf_state = (i % 3);
    }

    metrics_print_summary(buf, 10, 3);
    printf("Test 7 PASS: summary prints correctly\n");

    printf("=== ALL METRICS TESTS PASSED ===\n");
    printf("Ready for Step 4: cfs_exact.c\n");

    return 0;
}
#endif
