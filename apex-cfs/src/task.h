#ifndef APEX_CFS_TASK_H
#define APEX_CFS_TASK_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t  u8;
typedef int64_t  s64;

/* Kernel constants (exact copies) */
#define NICE_0_LOAD         1024
#define LOAD_AVG_PERIOD     32
#define LOAD_AVG_MAX        47742
#define FIXED_POINT_SHIFT   32
#define TICK_NS             1000000ULL  /* 1ms in nanoseconds */
#define WMULT_SHIFT         32
#define SCHED_CAPACITY_SHIFT 10

/* PELT decay table — y^n where y = 0.97857206 */
/* Stored as fixed-point: value = y^n * 2^32    */
static const u32 runnable_avg_yN_inv[32] = {
    0xffffffff, 0xfa83b2da, 0xf5257d14, 0xefe4b99a,
    0xeac0c6e6, 0xe5b906e6, 0xe0ccdeeb, 0xdbfbb796,
    0xd744fcc9, 0xd2a81d91, 0xce248c14, 0xc9b9bd85,
    0xc5672a10, 0xc12c4cc9, 0xbd08a39e, 0xb8fbaf46,
    0xb504f333, 0xb123f581, 0xad583ee9, 0xa9a15ab4,
    0xa5fed6a9, 0xa2704302, 0x9ef5325f, 0x9b8d39b9,
    0x9837f050, 0x94f4efa8, 0x91c3d373, 0x8ea4398a,
    0x8b95c1e3, 0x88980e80, 0x85aac367, 0x82cd8698
};

/* Nice to weight mapping (from kernel sched/core.c) */
static const int nice_to_weight[40] = {
 /* -20 */ 88761, 71755, 56483, 46273, 36291,
 /* -15 */ 29154, 23254, 18705, 14949, 11916,
 /* -10 */  9548,  7620,  6100,  4904,  3906,
 /*  -5 */  3121,  2501,  1991,  1586,  1277,
 /*   0 */  1024,   820,   655,   526,   423,
 /*   5 */   335,   272,   215,   172,   137,
 /*  10 */   110,    87,    70,    56,    45,
 /*  15 */    36,    29,    23,    18,    15,
};

/* APAF state constants */
#define APAF_TIGHT   0
#define APAF_MEDIUM  1
#define APAF_LOOSE   2

/* APAF coefficients (locked) */
#define APAF_A0_TIGHT  0.999755378067218
#define APAF_A1_TIGHT -0.021439723696865
#define APAF_A2_TIGHT  0.000197742040395

#define APAF_A0_MEDIUM  0.998121961273224
#define APAF_A1_MEDIUM -0.020869371290712
#define APAF_A2_MEDIUM  0.000167397421507

#define APAF_A0_LOOSE  0.977779922300000
#define APAF_A1_LOOSE -0.015578660700000
#define APAF_A2_LOOSE  0.0

/* Jain index thresholds */
#define JAIN_LOOSE_TO_MEDIUM  0.93
#define JAIN_MEDIUM_TO_TIGHT  0.90
#define JAIN_MEDIUM_TO_LOOSE  0.97
#define JAIN_TIGHT_TO_MEDIUM  0.95

/* Task representation */
typedef struct {
    int     id;
    int     nice;           /* -20 to +19 */
    u64     weight;         /* derived from nice */
    u64     vruntime;       /* virtual runtime in ns */
    u64     load_avg;       /* PELT load average */
    u64     load_sum;       /* PELT load sum */
    u64     exec_runtime;   /* total CPU time */
    int     runnable;       /* 1 = runnable, 0 = blocked */
} cfs_task_t;

/* Runqueue representation */
typedef struct {
    cfs_task_t  *tasks;
    int          nr_tasks;
    int          nr_running;
    u64          clock;         /* current time in ns */
    u64          tick;          /* tick counter (1ms each) */
    int          approx_mode;   /* 0=exact,1=BSA,2=CLTI,3=APAF */
    int          apaf_state;    /* TIGHT=0, MEDIUM=1, LOOSE=2 */
    double       jain_index;    /* current fairness */
} cfs_rq_t;

/* Metrics snapshot */
typedef struct {
    u64     tick;
    int     logic_used;
    double  jain_index;
    double  max_error_pct;
    double  avg_error_pct;
    u64     ops_saved;
    int     apaf_state;         /* only for Logic 3 */
} metrics_t;

/* task.c function declarations */
cfs_task_t task_create(int id, int nice);
void task_set_runnable(cfs_task_t *t, int runnable);
u32 task_get_weight(const cfs_task_t *t);

cfs_rq_t rq_init(cfs_task_t *tasks, int nr_tasks, int approx_mode);
void rq_tick(cfs_rq_t *rq);
int rq_count_running(const cfs_rq_t *rq);

void task_print(const cfs_task_t *t);
void rq_print(const cfs_rq_t *rq);

#endif /* APEX_CFS_TASK_H */
