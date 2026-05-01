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

typedef struct {
    int     n_tasks;
    int     logic;
    int     ticks;
    double  final_jain;
    double  min_jain;
    double  avg_jain;
    double  max_error_pct;
    double  avg_error_pct;
    u64     total_ops_saved;
    int     final_apaf_state;
    u64     final_load_avg;
} exp1_result_t;

exp1_result_t run_experiment(int n_tasks, int logic, int ticks)
{
    exp1_result_t result;
    cfs_task_t *exact_tasks;
    cfs_task_t *approx_tasks;
    cfs_rq_t exact_rq;
    cfs_rq_t approx_rq;
    metrics_t *mbuf;
    double sum_jain = 0.0;
    double min_jain = 1.0;
    double max_err = 0.0;
    double sum_err = 0.0;
    u64 total_ops = 0;
    int i;
    char filename[64];

    memset(&result, 0, sizeof(result));
    result.n_tasks = n_tasks;
    result.logic = logic;
    result.ticks = ticks;
    result.min_jain = 0.0;

    exact_tasks = (cfs_task_t *)malloc(sizeof(cfs_task_t) * (size_t)n_tasks);
    approx_tasks = (cfs_task_t *)malloc(sizeof(cfs_task_t) * (size_t)n_tasks);
    mbuf = (metrics_t *)malloc(sizeof(metrics_t) * (size_t)ticks);
    if (!exact_tasks || !approx_tasks || !mbuf) {
        printf("ERROR: allocation failed for N=%d logic=%d\n", n_tasks, logic);
        free(exact_tasks);
        free(approx_tasks);
        free(mbuf);
        return result;
    }

    for (i = 0; i < n_tasks; i++) {
        exact_tasks[i] = task_create(i, 0);
        approx_tasks[i] = task_create(i, 0);
    }

    exact_rq = rq_init(exact_tasks, n_tasks, 0);
    approx_rq = rq_init(approx_tasks, n_tasks, logic);
    metrics_init(mbuf, ticks);

    for (i = 0; i < ticks; i++) {
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

        metrics_record(&mbuf[i], &approx_rq, &exact_rq);
    }

    for (i = 0; i < ticks; i++) {
        sum_jain += mbuf[i].jain_index;
        sum_err += mbuf[i].avg_error_pct;
        total_ops += mbuf[i].ops_saved;

        if (mbuf[i].jain_index < min_jain) {
            min_jain = mbuf[i].jain_index;
        }
        if (mbuf[i].max_error_pct > max_err) {
            max_err = mbuf[i].max_error_pct;
        }
    }

    result.n_tasks = n_tasks;
    result.logic = logic;
    result.ticks = ticks;
    result.final_jain = approx_rq.jain_index;
    result.min_jain = min_jain;
    result.avg_jain = sum_jain / (double)ticks;
    result.max_error_pct = max_err;
    result.avg_error_pct = sum_err / (double)ticks;
    result.total_ops_saved = total_ops;
    result.final_apaf_state = approx_rq.apaf_state;
    result.final_load_avg = approx_tasks[0].load_avg;

    snprintf(filename, sizeof(filename), "results/exp1_n%d_%s.csv", n_tasks, metrics_get_logic_name(logic));
    metrics_write_csv(mbuf, ticks, n_tasks, filename);

    free(mbuf);
    free(exact_tasks);
    free(approx_tasks);
    return result;
}

int main(void)
{
    int ticks = 1000;
    int n_values[3] = {10, 50, 100};
    int logics[4] = {0, 1, 2, 3};
    const char *logic_names[4] = {"EXACT", "BSA", "CLTI", "APAF"};
    exp1_result_t all_results[3][4];
    FILE *summary;
    int ni;
    int li;

    printf("================================\n");
    printf("APEX-CFS Experiment 1\n");
    printf("Equal Weight Tasks\n");
    printf("Ticks: %d\n", ticks);
    printf("================================\n\n");

    for (ni = 0; ni < 3; ni++) {
        int n = n_values[ni];
        exp1_result_t results[4];

        printf("--- N=%d tasks ---\n", n);
        for (li = 0; li < 4; li++) {
            int logic = logics[li];
            results[logic] = run_experiment(n, logic, ticks);
            all_results[ni][logic] = results[logic];

            printf("  %s: Jain=%.4f err=%.4f%% ops_saved=%llu\n",
                   logic_names[logic],
                   results[logic].final_jain,
                   results[logic].max_error_pct,
                   (unsigned long long)results[logic].total_ops_saved);
        }

        printf("\n  Results Table (N=%d, %d ticks):\n", n, ticks);
        printf("  %-8s %-10s %-10s %-10s %-12s %-8s\n",
               "Logic", "Jain(fin)", "Jain(min)", "MaxErr%", "Ops_Saved", "State");
        printf("  %-8s %-10s %-10s %-10s %-12s %-8s\n",
               "-----", "---------", "---------", "-------", "---------", "-----");

        for (li = 0; li < 4; li++) {
            int logic = logics[li];
            char state_str[16];

            if (logic == 3) {
                strcpy(state_str, metrics_get_state_name(3, results[logic].final_apaf_state));
            } else {
                strcpy(state_str, "N/A");
            }

            printf("  %-8s %-10.4f %-10.4f %-10.4f %-12llu %-8s\n",
                   logic_names[logic],
                   results[logic].final_jain,
                   results[logic].min_jain,
                   results[logic].max_error_pct,
                   (unsigned long long)results[logic].total_ops_saved,
                   state_str);
        }
        printf("\n");
    }

    summary = fopen("results/exp1_summary.txt", "w");
    if (!summary) {
        printf("ERROR: failed to write results/exp1_summary.txt\n");
        return 1;
    }

    fprintf(summary, "APEX-CFS Experiment 1 -- Equal Weight Tasks\n");
    fprintf(summary, "Ticks: 1000\n");
    fprintf(summary, "N values tested: 10, 50, 100\n");
    fprintf(summary, "Logics tested: EXACT, BSA, CLTI, APAF\n\n");

    for (ni = 0; ni < 3; ni++) {
        int n = n_values[ni];

        fprintf(summary, "--- N=%d tasks ---\n", n);
        fprintf(summary, "  Results Table (N=%d, %d ticks):\n", n, ticks);
        fprintf(summary, "  %-8s %-10s %-10s %-10s %-12s %-8s\n",
                "Logic", "Jain(fin)", "Jain(min)", "MaxErr%", "Ops_Saved", "State");
        fprintf(summary, "  %-8s %-10s %-10s %-10s %-12s %-8s\n",
                "-----", "---------", "---------", "-------", "---------", "-----");

        for (li = 0; li < 4; li++) {
            int logic = logics[li];
            char state_str[16];

            if (logic == 3) {
                strcpy(state_str, metrics_get_state_name(3, all_results[ni][logic].final_apaf_state));
            } else {
                strcpy(state_str, "N/A");
            }

            fprintf(summary, "  %-8s %-10.4f %-10.4f %-10.4f %-12llu %-8s\n",
                    logic_names[logic],
                    all_results[ni][logic].final_jain,
                    all_results[ni][logic].min_jain,
                    all_results[ni][logic].max_error_pct,
                    (unsigned long long)all_results[ni][logic].total_ops_saved,
                    state_str);
        }
        fprintf(summary, "\n");
    }
    fclose(summary);

    for (ni = 0; ni < 3; ni++) {
        for (li = 0; li < 4; li++) {
            if (all_results[ni][li].final_jain < 0.85) {
                printf("WARNING: Jain below 0.85 (N=%d logic=%s jain=%.4f)\n",
                       all_results[ni][li].n_tasks,
                       logic_names[li],
                       all_results[ni][li].final_jain);
            }
        }

        if (all_results[ni][3].total_ops_saved <= all_results[ni][0].total_ops_saved) {
            printf("WARNING: APAF ops_saved not greater than EXACT at N=%d\n", all_results[ni][3].n_tasks);
        }
    }

    printf("\n================================\n");
    printf("Experiment 1 COMPLETE\n");
    printf("CSV files written to results/\n");
    printf("Summary: results/exp1_summary.txt\n");
    printf("================================\n");

    return 0;
}
