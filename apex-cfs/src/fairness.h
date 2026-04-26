#ifndef APEX_CFS_FAIRNESS_H
#define APEX_CFS_FAIRNESS_H

#include "task.h"

/* Jain's index thresholds for APAF controller */
#define JAIN_LOOSE_TO_MEDIUM   0.93
#define JAIN_MEDIUM_TO_TIGHT   0.90
#define JAIN_MEDIUM_TO_LOOSE   0.97
#define JAIN_TIGHT_TO_MEDIUM   0.95

/* Minimum tasks needed for meaningful index */
#define JAIN_MIN_TASKS         2

/* fairness.c function declarations */
double fairness_jain_index(const cfs_rq_t *rq);
int fairness_apaf_next_state(int current_state, double jain);
void fairness_cpu_shares(const cfs_rq_t *rq, double *shares, int max_tasks);
double fairness_ideal_share(u32 task_weight, u32 total_weight);
u32 fairness_total_weight(const cfs_rq_t *rq);

#endif /* APEX_CFS_FAIRNESS_H */
