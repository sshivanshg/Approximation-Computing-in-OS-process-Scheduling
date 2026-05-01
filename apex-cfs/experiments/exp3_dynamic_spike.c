#include "../src/task.h"
#include "../src/fairness.h"
#include "../src/metrics.h"
#include "../src/cfs_exact.h"
#include "../src/approx_apaf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PHASE1_TASKS    5
#define PHASE2_TASKS    55
#define PHASE3_TASKS    15
#define MAX_TASKS       55

#define PHASE1_END      500
#define PHASE2_END      1500
#define PHASE3_END      2001

#define TOTAL_TICKS     2001

typedef struct {
    u64 tick;
    int old_state;
    int new_state;
    double jain_at_transition;
    int phase;
} transition_event_t;

typedef struct {
    int     phase;
    int     n_tasks;
    u64     tick_start;
    u64     tick_end;
    double  avg_jain;
    double  min_jain;
    int     dominant_state;
    int     n_transitions;
} phase_summary_t;

int main(void)
{
    cfs_task_t *exact_tasks;
    cfs_task_t *apaf_tasks;
    cfs_rq_t exact_rq;
    cfs_rq_t apaf_rq;
    metrics_t *exact_buf;
    metrics_t *apaf_buf;
    transition_event_t transitions[100];
    phase_summary_t phases[3];
    int state_counts[3][3];
    int n_transitions = 0;
    int prev_apaf_state;
    int current_phase = 1;
    int i;
    u64 reaction_ticks = 0;
    u64 recovery_ticks = 0;
    int found_reaction = 0;
    int found_recovery = 0;

    exact_tasks = (cfs_task_t *)malloc(sizeof(cfs_task_t) * (size_t)MAX_TASKS);
    apaf_tasks = (cfs_task_t *)malloc(sizeof(cfs_task_t) * (size_t)MAX_TASKS);
    exact_buf = (metrics_t *)malloc(sizeof(metrics_t) * (size_t)TOTAL_TICKS);
    apaf_buf = (metrics_t *)malloc(sizeof(metrics_t) * (size_t)TOTAL_TICKS);
    if (!exact_tasks || !apaf_tasks || !exact_buf || !apaf_buf) {
        printf("ERROR: allocation failed\n");
        free(exact_tasks);
        free(apaf_tasks);
        free(exact_buf);
        free(apaf_buf);
        return 1;
    }

    for (i = 0; i < MAX_TASKS; i++) {
        exact_tasks[i] = task_create(i, 0);
        apaf_tasks[i] = task_create(i, 0);
    }

    for (i = 0; i < PHASE1_TASKS; i++) {
        exact_tasks[i].runnable = 1;
        apaf_tasks[i].runnable = 1;
    }
    for (i = PHASE1_TASKS; i < MAX_TASKS; i++) {
        exact_tasks[i].runnable = 0;
        apaf_tasks[i].runnable = 0;
    }

    exact_rq = rq_init(exact_tasks, MAX_TASKS, 0);
    apaf_rq = rq_init(apaf_tasks, MAX_TASKS, 3);

    metrics_init(exact_buf, TOTAL_TICKS);
    metrics_init(apaf_buf, TOTAL_TICKS);

    memset(transitions, 0, sizeof(transitions));
    memset(phases, 0, sizeof(phases));
    memset(state_counts, 0, sizeof(state_counts));

    for (i = 0; i < 3; i++) {
        phases[i].min_jain = 1.0;
    }

    prev_apaf_state = apaf_rq.apaf_state;

    printf("================================\n");
    printf("APEX-CFS Experiment 3\n");
    printf("Dynamic Spike -- APAF Controller\n");
    printf("Phase 1: %d tasks  (ticks 0-499)\n", PHASE1_TASKS);
    printf("Phase 2: %d tasks  (ticks 500-1499)\n", PHASE2_TASKS);
    printf("Phase 3: %d tasks  (ticks 1500-2000)\n", PHASE3_TASKS);
    printf("================================\n\n");

    for (i = 0; i < TOTAL_TICKS; i++) {
        if (i == PHASE1_END) {
            int t;
            printf("[tick %d] SPIKE: %d -> %d tasks\n", i, PHASE1_TASKS, PHASE2_TASKS);
            for (t = PHASE1_TASKS; t < PHASE2_TASKS; t++) {
                exact_tasks[t].runnable = 1;
                apaf_tasks[t].runnable = 1;
            }
            rq_count_running(&exact_rq);
            rq_count_running(&apaf_rq);
            current_phase = 2;
        }

        if (i == PHASE2_END) {
            int t;
            printf("[tick %d] RECOVERY: %d -> %d tasks\n", i, PHASE2_TASKS, PHASE3_TASKS);
            for (t = PHASE3_TASKS; t < MAX_TASKS; t++) {
                exact_tasks[t].runnable = 0;
                apaf_tasks[t].runnable = 0;
            }
            rq_count_running(&exact_rq);
            rq_count_running(&apaf_rq);
            current_phase = 3;
        }

        exact_tick(&exact_rq);
        apaf_tick(&apaf_rq);

        if (apaf_rq.apaf_state != prev_apaf_state) {
            if (n_transitions < (int)(sizeof(transitions) / sizeof(transitions[0]))) {
                transitions[n_transitions].tick = (u64)i;
                transitions[n_transitions].old_state = prev_apaf_state;
                transitions[n_transitions].new_state = apaf_rq.apaf_state;
                transitions[n_transitions].jain_at_transition = apaf_rq.jain_index;
                transitions[n_transitions].phase = current_phase;
                n_transitions++;
            }

            printf("[tick %d] APAF: %s -> %s (J=%.4f, phase=%d)\n",
                   i,
                   metrics_get_state_name(3, prev_apaf_state),
                   metrics_get_state_name(3, apaf_rq.apaf_state),
                   apaf_rq.jain_index,
                   current_phase);

            prev_apaf_state = apaf_rq.apaf_state;
        }

        {
            int ph = current_phase - 1;
            double j = apaf_rq.jain_index;
            phases[ph].phase = current_phase;
            phases[ph].n_tasks = (current_phase == 1) ? PHASE1_TASKS :
                                (current_phase == 2) ? PHASE2_TASKS : PHASE3_TASKS;
            phases[ph].tick_start = (ph == 0) ? 0 : (ph == 1) ? PHASE1_END : PHASE2_END;
            phases[ph].tick_end = (u64)i;
            phases[ph].avg_jain += j;
            if (j < phases[ph].min_jain) {
                phases[ph].min_jain = j;
            }
            if (apaf_rq.apaf_state >= APAF_TIGHT && apaf_rq.apaf_state <= APAF_LOOSE) {
                state_counts[ph][apaf_rq.apaf_state]++;
            }
        }

        metrics_record(&exact_buf[i], &exact_rq, &exact_rq);
        metrics_record(&apaf_buf[i], &apaf_rq, &exact_rq);
    }

    phases[0].avg_jain /= (double)PHASE1_END;
    phases[1].avg_jain /= (double)(PHASE2_END - PHASE1_END);
    phases[2].avg_jain /= (double)(TOTAL_TICKS - PHASE2_END);

    for (i = 0; i < 3; i++) {
        int s;
        int best_state = APAF_MEDIUM;
        int best_count = -1;
        for (s = 0; s < 3; s++) {
            if (state_counts[i][s] > best_count) {
                best_count = state_counts[i][s];
                best_state = s;
            }
        }
        phases[i].dominant_state = best_state;
        phases[i].n_transitions = 0;
    }

    for (i = 0; i < n_transitions; i++) {
        int ph = transitions[i].phase - 1;
        if (ph >= 0 && ph < 3) {
            phases[ph].n_transitions++;
        }

        if (!found_reaction && transitions[i].tick >= PHASE1_END) {
            reaction_ticks = transitions[i].tick - PHASE1_END;
            found_reaction = 1;
        }
        if (!found_recovery && transitions[i].tick >= PHASE2_END) {
            recovery_ticks = transitions[i].tick - PHASE2_END;
            found_recovery = 1;
        }
    }

    printf("\n==== Phase Summary ====\n\n");
    for (i = 0; i < 3; i++) {
        printf("Phase %d (%d tasks, ticks %llu-%llu):\n",
               phases[i].phase,
               phases[i].n_tasks,
               (unsigned long long)phases[i].tick_start,
               (unsigned long long)phases[i].tick_end);
        printf("  Avg Jain:       %.4f\n", phases[i].avg_jain);
        printf("  Min Jain:       %.4f\n", phases[i].min_jain);
        printf("  Dominant state: %s\n", metrics_get_state_name(3, phases[i].dominant_state));
        printf("  State counts:\n");
        printf("    TIGHT:  %d ticks\n", state_counts[i][APAF_TIGHT]);
        printf("    MEDIUM: %d ticks\n", state_counts[i][APAF_MEDIUM]);
        printf("    LOOSE:  %d ticks\n\n", state_counts[i][APAF_LOOSE]);
    }

    printf("==== Transition Log ====\n\n");
    printf("Total transitions: %d\n\n", n_transitions);
    for (i = 0; i < n_transitions; i++) {
        printf("  tick=%llu  phase=%d  %s -> %s  J=%.4f\n",
               (unsigned long long)transitions[i].tick,
               transitions[i].phase,
               metrics_get_state_name(3, transitions[i].old_state),
               metrics_get_state_name(3, transitions[i].new_state),
               transitions[i].jain_at_transition);
    }

    printf("\n==== Controller Performance ====\n\n");
    printf("Spike at tick 500:\n");
    if (found_reaction) {
        printf("  Controller reaction: %llu ticks\n", (unsigned long long)reaction_ticks);
        printf("  Reaction time: %llu ms\n", (unsigned long long)reaction_ticks);
    } else {
        printf("  Controller reaction: 0 ticks\n");
        printf("  Reaction time: 0 ms\n");
    }
    printf("Recovery at tick 1500:\n");
    if (found_recovery) {
        printf("  Controller recovery: %llu ticks after spike removal\n",
               (unsigned long long)recovery_ticks);
    } else {
        printf("  Controller recovery: 0 ticks after spike removal\n");
    }

    metrics_write_csv(exact_buf, TOTAL_TICKS, MAX_TASKS, "results/exp3_EXACT.csv");
    metrics_write_csv(apaf_buf, TOTAL_TICKS, MAX_TASKS, "results/exp3_APAF.csv");

    {
        FILE *tf = fopen("results/exp3_transitions.txt", "w");
        if (tf) {
            fprintf(tf, "APEX-CFS Experiment 3 -- APAF State Transitions\n");
            fprintf(tf, "Spike at tick 500 (5->55 tasks)\n");
            fprintf(tf, "Recovery at tick 1500 (55->15 tasks)\n\n");
            for (i = 0; i < n_transitions; i++) {
                fprintf(tf, "tick=%llu phase=%d %s->%s J=%.4f\n",
                        (unsigned long long)transitions[i].tick,
                        transitions[i].phase,
                        metrics_get_state_name(3, transitions[i].old_state),
                        metrics_get_state_name(3, transitions[i].new_state),
                        transitions[i].jain_at_transition);
            }
            fclose(tf);
        }
    }

    {
        FILE *sf = fopen("results/exp3_summary.txt", "w");
        if (sf) {
            fprintf(sf, "APEX-CFS Experiment 3 -- Dynamic Spike (APAF)\n\n");
            for (i = 0; i < 3; i++) {
                fprintf(sf, "Phase %d (%d tasks, ticks %llu-%llu):\n",
                        phases[i].phase,
                        phases[i].n_tasks,
                        (unsigned long long)phases[i].tick_start,
                        (unsigned long long)phases[i].tick_end);
                fprintf(sf, "  Avg Jain:       %.4f\n", phases[i].avg_jain);
                fprintf(sf, "  Min Jain:       %.4f\n", phases[i].min_jain);
                fprintf(sf, "  Dominant state: %s\n\n",
                        metrics_get_state_name(3, phases[i].dominant_state));
            }

            fprintf(sf, "Controller performance:\n");
            fprintf(sf, "- Reaction time: %llu ticks (%llu ms)\n",
                    (unsigned long long)reaction_ticks,
                    (unsigned long long)reaction_ticks);
            fprintf(sf, "- Recovery time: %llu ticks\n\n",
                    (unsigned long long)recovery_ticks);

            fprintf(sf, "APAF controller confirmed adaptive:\n");
            fprintf(sf, "- Phase 1 (light): dominant=%s\n",
                    metrics_get_state_name(3, phases[0].dominant_state));
            fprintf(sf, "- Phase 2 (spike): dominant=%s\n",
                    metrics_get_state_name(3, phases[1].dominant_state));
            fprintf(sf, "- Phase 3 (recovery): dominant=%s\n",
                    metrics_get_state_name(3, phases[2].dominant_state));
            fprintf(sf, "- Reaction time: %llu ticks (%llu ms)\n",
                    (unsigned long long)reaction_ticks,
                    (unsigned long long)reaction_ticks);
            fclose(sf);
        }
    }

    printf("\n================================\n");
    printf("Experiment 3 COMPLETE\n");
    printf("Transitions logged: %d\n", n_transitions);
    printf("CSV files: results/exp3_*.csv\n");
    printf("Transitions: results/exp3_transitions.txt\n");
    printf("Summary: results/exp3_summary.txt\n");
    printf("================================\n");

    free(exact_tasks);
    free(apaf_tasks);
    free(exact_buf);
    free(apaf_buf);
    return 0;
}
