#include "../src/task.h"
#include "../src/fairness.h"
#include "../src/metrics.h"
#include "../src/cfs_exact.h"
#include "../src/approx_bsa.h"
#include "../src/approx_clti.h"
#include "../src/approx_apaf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N_BATCH           10
#define N_TOTAL           11
#define INTERACTIVE       0
#define INTERACTIVE_NICE  -10
#define BATCH_NICE         0
#define INTERACTIVE_ON     5
#define INTERACTIVE_OFF    5

typedef struct {
    int     logic;
    int     ticks;
    u64     interactive_exec_runtime;
    double  interactive_cpu_share;
    u64     max_wait_ticks;
    double  avg_wait_ticks;
    int     total_wait_events;
    double  batch_avg_jain;
    double  batch_throughput;
    double  final_jain;
    double  max_error_pct;
    double  avg_error_pct;
    u64     total_ops_saved;
    int     final_apaf_state;
} exp2_result_t;

exp2_result_t run_exp2(int logic, int ticks)
{
    exp2_result_t result;
    cfs_task_t *exact_tasks;
    cfs_task_t *approx_tasks;
    cfs_rq_t exact_rq;
    cfs_rq_t approx_rq;
    metrics_t *mbuf;
    u64 max_wait = 0;
    u64 sum_wait = 0;
    int wait_events = 0;
    u64 current_wait = 0;
    int was_blocked = 1;
    double sum_err = 0.0;
    double max_err = 0.0;
    u64 total_ops = 0;
    int i;
    char filename[64];

    memset(&result, 0, sizeof(result));
    result.logic = logic;
    result.ticks = ticks;

    exact_tasks = (cfs_task_t *)malloc(sizeof(cfs_task_t) * (size_t)N_TOTAL);
    approx_tasks = (cfs_task_t *)malloc(sizeof(cfs_task_t) * (size_t)N_TOTAL);
    mbuf = (metrics_t *)malloc(sizeof(metrics_t) * (size_t)ticks);
    if (!exact_tasks || !approx_tasks || !mbuf) {
        printf("ERROR: allocation failed for logic=%d\n", logic);
        free(exact_tasks);
        free(approx_tasks);
        free(mbuf);
        return result;
    }

    exact_tasks[INTERACTIVE] = task_create(INTERACTIVE, INTERACTIVE_NICE);
    approx_tasks[INTERACTIVE] = task_create(INTERACTIVE, INTERACTIVE_NICE);
    for (i = 1; i < N_TOTAL; i++) {
        exact_tasks[i] = task_create(i, BATCH_NICE);
        approx_tasks[i] = task_create(i, BATCH_NICE);
    }

    exact_rq = rq_init(exact_tasks, N_TOTAL, 0);
    approx_rq = rq_init(approx_tasks, N_TOTAL, logic);
    metrics_init(mbuf, ticks);

    for (i = 0; i < ticks; i++) {
        int cycle_pos = i % (INTERACTIVE_ON + INTERACTIVE_OFF);
        int j;
        u64 min_vrt = UINT64_MAX;
        int min_idx = -1;

        if (cycle_pos < INTERACTIVE_ON) {
            task_set_runnable(&exact_tasks[INTERACTIVE], 1);
            task_set_runnable(&approx_tasks[INTERACTIVE], 1);
            if (was_blocked == 1) {
                current_wait = 0;
                was_blocked = 0;
            }
        } else {
            task_set_runnable(&exact_tasks[INTERACTIVE], 0);
            task_set_runnable(&approx_tasks[INTERACTIVE], 0);
            was_blocked = 1;
            current_wait = 0;
        }

        exact_tick(&exact_rq);
        switch (logic) {
        case 0:
            exact_tick(&approx_rq);
            break;
        case 1:
            bsa_tick(&approx_rq);
            break;
        case 2:
            clti_tick(&approx_rq);
            break;
        case 3:
            apaf_tick(&approx_rq);
            break;
        default:
            exact_tick(&approx_rq);
            break;
        }

        if (approx_tasks[INTERACTIVE].runnable == 1) {
            for (j = 0; j < N_TOTAL; j++) {
                if (approx_tasks[j].runnable == 1 && approx_tasks[j].vruntime < min_vrt) {
                    min_vrt = approx_tasks[j].vruntime;
                    min_idx = j;
                }
            }

            if (min_idx != INTERACTIVE) {
                current_wait++;
                if (current_wait > max_wait) {
                    max_wait = current_wait;
                }
            } else {
                if (current_wait > 0) {
                    sum_wait += current_wait;
                    wait_events++;
                }
                current_wait = 0;
            }
        }

        metrics_record(&mbuf[i], &approx_rq, &exact_rq);
        rq_count_running(&exact_rq);
        rq_count_running(&approx_rq);
    }

    for (i = 0; i < ticks; i++) {
        sum_err += mbuf[i].avg_error_pct;
        total_ops += mbuf[i].ops_saved;
        if (mbuf[i].max_error_pct > max_err) {
            max_err = mbuf[i].max_error_pct;
        }
    }

    {
        double b_sum = 0.0;
        double b_sq = 0.0;
        for (i = 1; i < N_TOTAL; i++) {
            double rt = (double)approx_tasks[i].exec_runtime;
            b_sum += rt;
            b_sq += rt * rt;
        }
        if (b_sq > 0.0) {
            result.batch_avg_jain = (b_sum * b_sum) / (double)(N_BATCH * b_sq);
        } else {
            result.batch_avg_jain = 1.0;
        }
        result.batch_throughput = b_sum / (double)N_BATCH;
    }

    {
        u64 total_rt = 0;
        for (i = 0; i < N_TOTAL; i++) {
            total_rt += approx_tasks[i].exec_runtime;
        }
        if (total_rt > 0) {
            result.interactive_cpu_share =
                (double)approx_tasks[INTERACTIVE].exec_runtime / (double)total_rt;
        } else {
            result.interactive_cpu_share = 0.0;
        }
    }

    result.interactive_exec_runtime = approx_tasks[INTERACTIVE].exec_runtime;
    result.max_wait_ticks = max_wait;
    result.avg_wait_ticks = wait_events > 0 ? (double)sum_wait / (double)wait_events : 0.0;
    result.total_wait_events = wait_events;
    result.final_jain = approx_rq.jain_index;
    result.max_error_pct = max_err;
    result.avg_error_pct = sum_err / (double)ticks;
    result.total_ops_saved = total_ops;
    result.final_apaf_state = approx_rq.apaf_state;

    snprintf(filename, sizeof(filename), "results/exp2_%s.csv", metrics_get_logic_name(logic));
    metrics_write_csv(mbuf, ticks, N_TOTAL, filename);

    free(mbuf);
    free(exact_tasks);
    free(approx_tasks);
    return result;
}

int main(void)
{
    int ticks = 2000;
    int logics[4] = {0, 1, 2, 3};
    const char *logic_names[4] = {"EXACT", "BSA", "CLTI", "APAF"};
    exp2_result_t results[4];
    FILE *summary;
    int i;

    printf("================================\n");
    printf("APEX-CFS Experiment 2\n");
    printf("Mixed Workload\n");
    printf("1 interactive (nice=-10) + 10 batch (nice=0)\n");
    printf("Ticks: %d\n", ticks);
    printf("Interactive: ON %d ticks, OFF %d ticks\n", INTERACTIVE_ON, INTERACTIVE_OFF);
    printf("================================\n\n");

    for (i = 0; i < 4; i++) {
        int logic = logics[i];
        results[logic] = run_exp2(logic, ticks);

        printf("%s:\n", logic_names[logic]);
        printf("  Interactive CPU share: %.4f%%\n", results[logic].interactive_cpu_share * 100.0);
        printf("  Max wait (tail latency): %llu ticks\n", (unsigned long long)results[logic].max_wait_ticks);
        printf("  Avg wait: %.2f ticks\n", results[logic].avg_wait_ticks);
        printf("  Batch Jain: %.4f\n", results[logic].batch_avg_jain);
        printf("  Max error: %.4f%%\n", results[logic].max_error_pct);
        printf("  Ops saved: %llu\n", (unsigned long long)results[logic].total_ops_saved);
        if (logic == 3) {
            printf("  APAF state: %s\n", metrics_get_state_name(3, results[logic].final_apaf_state));
        }
        printf("\n");
    }

    printf("Comparison Table (2000 ticks):\n");
    printf("%-8s %-12s %-10s %-10s %-10s %-10s\n",
           "Logic", "CPU_Share%", "MaxWait", "AvgWait", "BatchJain", "MaxErr%");
    printf("%-8s %-12s %-10s %-10s %-10s %-10s\n",
           "-----", "----------", "-------", "-------", "---------", "-------");

    for (i = 0; i < 4; i++) {
        int logic = logics[i];
        printf("%-8s %-12.4f %-10llu %-10.2f %-10.4f %-10.4f\n",
               logic_names[logic],
               results[logic].interactive_cpu_share * 100.0,
               (unsigned long long)results[logic].max_wait_ticks,
               results[logic].avg_wait_ticks,
               results[logic].batch_avg_jain,
               results[logic].max_error_pct);
    }

    summary = fopen("results/exp2_summary.txt", "w");
    if (!summary) {
        printf("ERROR: failed to write results/exp2_summary.txt\n");
        return 1;
    }

    fprintf(summary, "APEX-CFS Experiment 2 -- Mixed Workload\n");
    fprintf(summary, "Ticks: 2000\n");
    fprintf(summary, "Workload: 1 interactive (nice=-10) + 10 batch (nice=0)\n");
    fprintf(summary, "Interactive pattern: 5 on / 5 off\n\n");
    fprintf(summary, "Comparison Table (2000 ticks):\n");
    fprintf(summary, "%-8s %-12s %-10s %-10s %-10s %-10s\n",
            "Logic", "CPU_Share%", "MaxWait", "AvgWait", "BatchJain", "MaxErr%");
    fprintf(summary, "%-8s %-12s %-10s %-10s %-10s %-10s\n",
            "-----", "----------", "-------", "-------", "---------", "-------");
    for (i = 0; i < 4; i++) {
        int logic = logics[i];
        fprintf(summary, "%-8s %-12.4f %-10llu %-10.2f %-10.4f %-10.4f\n",
                logic_names[logic],
                results[logic].interactive_cpu_share * 100.0,
                (unsigned long long)results[logic].max_wait_ticks,
                results[logic].avg_wait_ticks,
                results[logic].batch_avg_jain,
                results[logic].max_error_pct);
    }

    {
        double exact_share = results[0].interactive_cpu_share * 100.0;
        double apaf_share = results[3].interactive_cpu_share * 100.0;
        double share_diff = apaf_share - exact_share;
        const char *fair_ok = "YES";
        if (results[0].batch_avg_jain < 0.90 || results[1].batch_avg_jain < 0.90 ||
            results[2].batch_avg_jain < 0.90 || results[3].batch_avg_jain < 0.90) {
            fair_ok = "NO";
        }

        fprintf(summary, "\nKey Findings:\n");
        fprintf(summary, "- Interactive task CPU share (EXACT):    %.2f%%\n", exact_share);
        fprintf(summary, "- Interactive task CPU share (APAF):     %.2f%%\n", apaf_share);
        fprintf(summary, "- Share difference EXACT vs APAF: %.2f%%\n", share_diff);
        fprintf(summary, "- Max wait ticks (EXACT): %llu\n",
                (unsigned long long)results[0].max_wait_ticks);
        fprintf(summary, "- Max wait ticks (APAF):  %llu\n",
                (unsigned long long)results[3].max_wait_ticks);
        fprintf(summary, "- Batch fairness preserved: %s\n", fair_ok);
    }

    fclose(summary);

    for (i = 0; i < 4; i++) {
        if (results[i].interactive_cpu_share <= 0.0) {
            printf("WARNING: interactive CPU share is 0 (logic=%s)\n", logic_names[i]);
        }
    }
    if (results[3].max_wait_ticks > results[0].max_wait_ticks * 2) {
        printf("WARNING: APAF max_wait > 2x EXACT (APAF=%llu EXACT=%llu)\n",
               (unsigned long long)results[3].max_wait_ticks,
               (unsigned long long)results[0].max_wait_ticks);
    }

    printf("\n================================\n");
    printf("Experiment 2 COMPLETE\n");
    printf("CSV files written to results/\n");
    printf("Summary: results/exp2_summary.txt\n");
    printf("================================\n");

    return 0;
}
