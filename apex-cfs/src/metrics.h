#ifndef APEX_CFS_METRICS_H
#define APEX_CFS_METRICS_H

#include "task.h"
#include "fairness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Maximum ticks storable in one run */
#define METRICS_MAX_TICKS     10000

/* CSV column header — never change this */
#define METRICS_CSV_HEADER \
  "tick,logic,n_tasks,jain_index," \
  "max_error_pct,avg_error_pct," \
  "ops_saved,apaf_state\n"

/* Logic name strings for CSV output */
#define LOGIC_NAME_EXACT  "EXACT"
#define LOGIC_NAME_BSA    "BSA"
#define LOGIC_NAME_CLTI   "CLTI"
#define LOGIC_NAME_APAF   "APAF"

/* metrics.c function declarations */
void metrics_init(metrics_t *buf, int n);
void metrics_record(metrics_t *entry, const cfs_rq_t *approx_rq, const cfs_rq_t *exact_rq);
void metrics_write_csv(const metrics_t *buf, int n_entries, int n_tasks, const char *filename);
void metrics_print_summary(const metrics_t *buf, int n_entries, int n_tasks);
const char *metrics_get_logic_name(int mode);
const char *metrics_get_state_name(int mode, int state);

#endif /* APEX_CFS_METRICS_H */
