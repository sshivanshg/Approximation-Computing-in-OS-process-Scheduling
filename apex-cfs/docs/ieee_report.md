# APEX-CFS: Adaptive Approximate Computing for Linux CFS Scheduler with Fairness-Aware Error Budget Control

Vivek and Shivansh

## Abstract
Linux CFS performs exact fixed-point math for load tracking at pelt.c line 53, a hot path executed about 300,000 times per second for 100 tasks. This overhead motivates approximation inside scheduler internals. APEX-CFS introduces three approximation logics (BSA, CLTI, APAF) targeting PELT decay and vruntime scaling while preserving fairness. The novel contribution is the APAF controller, the first self-tuning approximation layer inside CFS math that adjusts error budgets using real-time Jain's Fairness Index. Across equal-weight workloads, all logics maintain Jain=1.0000 for N=10, 50, and 100 tasks. In mixed workloads, APAF matches exact CPU allocation (48.00% vs 48.00%) while CLTI degrades interactive share to 28.45%. Under a dynamic spike, the controller reacts in 3 ticks (3 ms). Error-bound verification shows 3/3 bounds satisfied over 1000 ticks. These results demonstrate that approximate computing is feasible inside the OS scheduler with formal error guarantees.

## I. Introduction
Modern OS kernels make millions of scheduling decisions per second. Linux CFS uses PELT for load tracking, performing exponential decay at pelt.c line 53 and calling it roughly 300,000 times per second for 100 tasks. Each call uses fixed-point Q0.32 arithmetic with the 32-entry runnable_avg_yN_inv[] table, making this path a dominant computational hotspot.

Approximate computing trades precision for efficiency. Prior work such as MEANTIME, Pliant, and Taufique et al. reports 40-46% energy reduction with less than 5% accuracy loss at the application layer, but core scheduler internals remain untouched. The OS kernel is still treated as a black box in most approximation research, as noted in the Agrawal et al. survey.

APEX-CFS introduces three approximation logics for CFS internal computations. BSA replaces the Q0.32 multiply with a single bit-shift, CLTI uses an 8-entry interpolated table, and APAF uses adaptive polynomials with a fairness feedback controller. All logic targets pelt.c line 53 and fair.c line 332, directly addressing the computational core of CFS.

The remainder of this paper is organized as follows: Section II reviews related work, Section III describes the methodology and approximation logic, Section IV presents experimental results, Section V discusses future scope, and Section VI concludes.

## II. Literature Review
MEANTIME (Farrell and Hoffmann, USENIX ATC 2016) proposes an OS runtime for energy and timeliness on Linux/ARM. It reports about 46% energy reduction with less than 2% accuracy loss, but operates strictly at the application level. It does not modify kernel scheduler math.

Pliant (Kulkarni et al., HPCA 2019) introduces cloud runtime co-scheduling with approximation to improve resource efficiency. It maintains web-service QoS with only 2.1% quality loss by orchestrating application-level approximation. The kernel scheduler remains unchanged.

Agrawal et al. (IEEE ICRC 2016) provide a broad survey of approximation techniques including loop perforation, which can deliver 50% speedup in favorable cases. The survey explicitly identifies the OS kernel as a largely ignored area for approximation. It motivates kernel-level exploration but does not address scheduler internals.

Cano-Camarero et al. (IEEE ISVLSI 2017) present the first kernel-level approximate computing work by introducing approximate memory support in Linux. Their modifications are limited to memory subsystems and do not affect the scheduler. The CFS math path is untouched.

Taufique et al. (ACM TRETS 2025) design a mobile HMP resource manager that reduces deadline misses by 25% with 2.2% accuracy loss. The work focuses on DVFS and resource management rather than scheduler fairness. It provides no analysis of scheduler-level error propagation.

Despite diverse techniques, no existing work modifies core OS scheduler math. The computational hotspot — PELT exponential decay performing about 300,000 multiplications per second — remains unexplored in approximate computing research. APEX-CFS addresses this gap directly.

## III. Methodology
### A. System Architecture
APEX-CFS follows a three-layer design: (1) an approximation layer with three logics (BSA, CLTI, APAF), (2) a fairness monitor that computes Jain's Index every 4 ticks, and (3) an error budget controller that changes approximation state. The primary kernel target is pelt.c line 53 for PELT decay, the secondary target is fair.c line 332 for vruntime scaling, and the tertiary target is fair.c line 9947 for the load balancer.

### B. Logic 1: BSA
BSA replaces the decay table multiply with a shift: load = load - (load >> 5), approximating y=0.96875 versus the exact y=0.97857206. The per-tick error is:

$\varepsilon = |0.97857 - 0.96875| / 0.97857 = 1.0037\%$

For vruntime, BSA uses the nearest power of two with one Newton iteration, bounded by 1.9110% error. BSA is the computationally cheapest logic because one instruction replaces a table lookup.

### C. Logic 2: CLTI
CLTI uses an 8-entry decay table with step size 4 and linear interpolation between entries. The error bound from the second derivative is:

$E_{max} = h^2/8 \times \max|f''(n)| = 2 \times (\ln y)^2 = 0.0938\%$

CLTI also maps weights into 8 power-of-two representatives. This produces the lowest decay error, but the weight class mapping distorts vruntime under mixed workloads. Experiment 2 confirms this with a 28.45% interactive CPU share versus the correct 48.00%.

### D. Logic 3: APAF (Novel Contribution)
APAF uses polynomial approximation evaluated via Horner's method:

$p(n) = a_0 + n(a_1 + n a_2)$

The three modes use fitted coefficients:

- TIGHT (n in [0,16]): a0=0.999755, a1=-0.021440, a2=0.000198, error ≤ 1.0%
- MEDIUM (n in [0,32]): a0=0.998122, a1=-0.020869, a2=0.000167, error ≤ 3.0%
- LOOSE (linear, n in [0,32]): a0=0.977780, a1=-0.015579, a2=0, error ≤ 4.147%

For vruntime, APAF uses Newton-Raphson reciprocals: two iterations in TIGHT/MEDIUM (≤1.0%), one iteration in LOOSE (≤5.0%).

### E. APAF Controller (Novel Contribution)
The APAF controller monitors Jain's Fairness Index $J=(\sum x_i)^2/(n\sum x_i^2)$ every APAF_MONITOR_INTERVAL=4 ticks. Based on J, the controller transitions between TIGHT, MEDIUM, and LOOSE states with hysteresis rules: LOOSE→MEDIUM when J<0.93, MEDIUM→TIGHT when J<0.90, MEDIUM→LOOSE when J>0.97, and TIGHT→MEDIUM when J>0.95. This embeds fairness feedback directly into scheduler math.

The state machine is provably non-oscillating. LOOSE→MEDIUM requires J<0.93 while MEDIUM→LOOSE requires J>0.97, producing a 0.04 hysteresis gap that prevents rapid toggling. The MEDIUM↔TIGHT gap is 0.05, enforcing stability before moving between tighter and looser states.

### F. Theoretical vs Operational Error Bounds
The master error formula yields:

$$
|\Delta L| \le \varepsilon \cdot L_{max} / (1-y)
\le 0.01004 \cdot 47742 / 0.02143
\approx 46.9\% \text{ of } L_{max}
$$

This theoretical worst-case bound is never approached in practice for three reasons: (1) natural decay reset, as load decays to zero for blocked tasks and breaks error accumulation; (2) contribution interruption, as each runnable tick adds an exact contribution term and interrupts error propagation; and (3) APAF controller intervention, which tightens the error budget when J<0.93 before errors accumulate. Experiment 4 confirms this: maximum observed error is 0.9996% (BSA), 0.0728% (CLTI), and 0.1177% (APAF) across 1000 ticks, all well below theoretical bounds.

## IV. Results and Evaluation
### A. Experiment 1: Equal-Weight Fairness
Experiment 1 evaluates equal-weight tasks (nice=0) for N=10, 50, and 100 over 1000 ticks. The goal is to verify that approximation does not reduce fairness when all tasks are symmetric.

All three logics maintain Jain=1.0000 regardless of task count, confirming that approximation does not degrade scheduling fairness for equal-weight workloads. BSA shows higher load_avg error from accumulated decay drift, but it does not affect CPU allocation decisions.

Table I: Equal-Weight Fairness (1000 ticks)

| Logic | N=10 err% | N=50 err% | N=100 err% | Jain |
|-------|-----------|-----------|------------|------|
| BSA   | 31.45     | 31.45     | 31.45      | 1.0000 |
| CLTI  | 3.02      | 3.02      | 3.02       | 1.0000 |
| APAF  | 1.18      | 1.56      | 1.57       | 1.0000 |

### B. Experiment 2: Mixed-Weight Protection
Experiment 2 models a mixed workload with one interactive task (nice=-10, weight=9548) and ten batch tasks (nice=0, weight=1024) over 2000 ticks. The goal is to measure interactive CPU share and latency under approximation.

CLTI's weight-class approximation reduces interactive CPU share from 48.00% to 28.45%, a 40.6% degradation. This shows that decay accuracy alone is insufficient and vruntime scaling accuracy is equally critical. APAF's Newton-Raphson reciprocal preserves exact CPU allocation at 48.00%, matching ground truth.

Table II: Mixed Workload (2000 ticks)

| Logic | CPU Share% | MaxWait | AvgWait | BatchJain |
|-------|-----------|---------|---------|-----------|
| EXACT | 48.00     | 5       | 1.00    | 1.0000    |
| BSA   | 48.05     | 5       | 1.00    | 1.0000    |
| CLTI  | 28.45     | 5       | 3.02    | 1.0000    |
| APAF  | 48.00     | 5       | 1.00    | 1.0000    |

### C. Experiment 3: Controller Dynamics
Experiment 3 applies a three-phase spike: 5 tasks (ticks 0-499), 55 tasks (ticks 500-1499), and 15 tasks (ticks 1500-2000). This isolates APAF and exact baselines to measure controller response.

The controller relaxes to LOOSE in Phase 1 with avgJ=0.9946 and reacts to the spike within 3 ticks (3 ms), within one APAF monitor interval. Phase 2 is dominated by TIGHT with avgJ=0.3327, showing the controller tightening when contention rises.

Phase 3 recovery takes 435 ticks. This reflects vruntime convergence rather than controller latency: tasks activated during Phase 2 accumulated unequal exec_runtimes, requiring CFS to redistribute CPU time before fairness recovered. APAF correctly maintains TIGHT during this convergence window.

Table III: APAF Phase Summary

| Phase | Tasks | Dominant | AvgJain | Ticks |
|-------|-------|----------|---------|-------|
| 1     | 5     | LOOSE    | 0.9946  | 0-499 |
| 2     | 55    | TIGHT    | 0.3327  | 500-1499 |
| 3     | 15    | TIGHT    | 0.8335  | 1500-2000 |

### D. Experiment 4: Error Bound Verification
Experiment 4 validates theoretical bounds for each logic across 1000 ticks with 20 mixed-nice tasks. Every tick is checked against the derived decay bounds.

All three logics maintain errors within theoretical bounds with zero violations. APAF's maximum observed error of 0.1177% is far below its 4.1474% theoretical bound, reflecting the controller's preference for TIGHT mode (98.9% of ticks) under mixed workloads.

Table IV: Error Bound Verification

| Logic | Theoretical | MaxObserved | Violations | Verified |
|-------|-------------|-------------|------------|----------|
| BSA   | 1.0037%     | 0.9996%     | 0          | YES      |
| CLTI  | 0.0938%     | 0.0728%     | 0          | YES      |
| APAF  | 4.1474%     | 0.1177%     | 0          | YES      |

Table V: Error Distribution (1000 ticks each)

| Logic | <1%  | 1-3% | 3-5% | >5% |
|-------|------|------|------|-----|
| BSA   | 1000 | 0    | 0    | 0   |
| CLTI  | 1000 | 0    | 0    | 0   |
| APAF  | 1000 | 0    | 0    | 0   |

APAF state distribution: TIGHT 989 ticks (98.9%), MEDIUM 11 ticks (1.1%).

## V. Future Scope
The userspace simulation validates the approach. Phase 3 will implement APEX-CFS as a Linux 6.1 kernel patch targeting pelt.c line 53 and fair.c line 332 directly. A /proc/sys/kernel/cfs_approx_mode interface will allow runtime control.

Current work approximates per-CPU scheduling. Extending approximation to the load balancer (fair.c line 9947) would amplify savings across NUMA domains. The 1.05x imbalance margin for LOOSE provides a starting point for safe migration policies.

Replacing simulation metrics with hardware PMU counters (perf, RAPL) would enable energy measurement, validating the 40-46% energy reduction observed in related work.

The APAF state machine uses fixed thresholds. A lightweight online learning approach could tune thresholds per workload type, improving adaptation speed beyond the current 3-tick reaction.

## VI. Conclusion
This paper presented APEX-CFS, the first adaptive approximate computing framework for Linux CFS scheduler internals. Three logics were designed, mathematically analyzed, and empirically validated. BSA provides simplicity at 1.0037% error. CLTI provides 0.0938% decay error but fails mixed-weight scenarios. APAF provides adaptive control, matching exact CPU allocation in all tested workloads with 3-tick spike detection.

The APAF controller is the first implementation of feedback-controlled approximation inside CFS scheduling math. Experiment 4 confirms mathematical error bounds hold across 1000 ticks with zero violations. This work demonstrates that the OS kernel scheduler — previously identified as an unexplored area in approximate computing literature — can be safely approximated with formal fairness guarantees.

## References
[1] A. Farrell and H. Hoffmann, "MEANTIME: Achieving Both Minimal Energy and Timeliness with Approximate Computing," in Proc. USENIX ATC, 2016.

[2] N. Kulkarni et al., "Pliant: Leveraging Approximation to Improve Datacenter Resource Efficiency," in Proc. HPCA, 2019.

[3] A. Agrawal et al., "Approximate Computing: Challenges and Opportunities," in Proc. IEEE ICRC, 2016.

[4] B. Cano-Camarero et al., "Introducing Approximate Memory Support in Linux Kernel," in Proc. IEEE ISVLSI, 2017.

[5] Z. Taufique et al., "Exploiting Approximation for Run-time Resource Management of Concurrent Applications," ACM Trans. Reconfigurable Technol. Syst., 2025.

[6] P. Turner, "Per-entity load tracking," LWN.net, 2013. [Online]. Available: https://lwn.net/Articles/531853/

[7] Linux Kernel Source, "Completely Fair Scheduler Implementation," kernel/sched/fair.c and kernel/sched/pelt.c, Linux v6.1, 2022.

[8] V. and S., "APEX-CFS: Adaptive Approximate Computing for Linux CFS Scheduler," Phase 2 Report, 2026.
