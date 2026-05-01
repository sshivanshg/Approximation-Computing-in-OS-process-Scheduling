---
# APEX-CFS Kernel Source Annotation
## Linux 6.1 LTS | pelt.c + fair.c
---

## 1. pelt.c — Function Index
| Function | Lines | Purpose | Touches load decay? | Touches vruntime? |
|---|---|---|---|---|
| `decay_load` | [apex-cfs/docs/pelt.c](apex-cfs/docs/pelt.c#L31-L55) | Compute $val \cdot y^n$ using table + shifts for PELT decay. | YES | NO |
| `__accumulate_pelt_segments` | [apex-cfs/docs/pelt.c](apex-cfs/docs/pelt.c#L57-L78) | Build partial geometric-sum segments used for PELT sums. | YES | NO |
| `accumulate_sum` | [apex-cfs/docs/pelt.c](apex-cfs/docs/pelt.c#L101-L149) | Update PELT sums (load/runnable/util) with decay and current contributions. | YES | NO |
| `___update_load_sum` | [apex-cfs/docs/pelt.c](apex-cfs/docs/pelt.c#L179-L229) | Time-delta handling and calls into `accumulate_sum`. | YES | NO |
| `___update_load_avg` | [apex-cfs/docs/pelt.c](apex-cfs/docs/pelt.c#L256-L267) | Convert decayed sums into averages using a divider. | YES | NO |
| `__update_load_avg_blocked_se` | [apex-cfs/docs/pelt.c](apex-cfs/docs/pelt.c#L295-L304) | Update blocked sched_entity averages. | YES | NO |
| `__update_load_avg_se` | [apex-cfs/docs/pelt.c](apex-cfs/docs/pelt.c#L306-L318) | Update sched_entity averages on enqueue/dequeue. | YES | NO |
| `__update_load_avg_cfs_rq` | [apex-cfs/docs/pelt.c](apex-cfs/docs/pelt.c#L320-L333) | Update cfs_rq averages. | YES | NO |
| `update_rt_rq_load_avg` | [apex-cfs/docs/pelt.c](apex-cfs/docs/pelt.c#L346-L359) | Update RT rq load avg via PELT. | YES | NO |
| `update_dl_rq_load_avg` | [apex-cfs/docs/pelt.c](apex-cfs/docs/pelt.c#L372-L385) | Update DL rq load avg via PELT. | YES | NO |
| `update_thermal_load_avg` | [apex-cfs/docs/pelt.c](apex-cfs/docs/pelt.c#L403-L415) | Update thermal load avg via PELT. | YES | NO |
| `update_irq_load_avg` | [apex-cfs/docs/pelt.c](apex-cfs/docs/pelt.c#L430-L468) | Update IRQ load avg via PELT with scaled running time. | YES | NO |

## 2. pelt.c — Line-by-Line Arithmetic Annotation
| Line | Exact code | Variable computed | Formula | Precision | Approx candidate? | If YES, why |
|---|---|---|---|---|---|---|
| [pelt.c L35](apex-cfs/docs/pelt.c#L35) | `if (unlikely(n > LOAD_AVG_PERIOD * 63))` | bounds check | $n > 63 \cdot \text{LOAD_AVG_PERIOD}$ | u64 | NO | bounds guard |
| [pelt.c L49](apex-cfs/docs/pelt.c#L49) | `val >>= local_n / LOAD_AVG_PERIOD;` | `val` | $val = val \gg \lfloor n/\text{PERIOD} \rfloor$ | u64 | NO | shifts integral to exact decay chunking |
| [pelt.c L50](apex-cfs/docs/pelt.c#L50) | `local_n %= LOAD_AVG_PERIOD;` | `local_n` | $n = n \bmod \text{PERIOD}$ | u32 | NO | index normalization |
| [pelt.c L53](apex-cfs/docs/pelt.c#L53) | `val = mul_u64_u32_shr(val, runnable_avg_yN_inv[local_n], 32);` | `val` | $val = val \cdot y^{n} \cdot 2^{-32}$ | fixed-point Q0.32 | YES | core decay multiply, tolerant to small error |
| [pelt.c L64](apex-cfs/docs/pelt.c#L64) | `c1 = decay_load((u64)d1, periods);` | `c1` | $c1 = d1 \cdot y^{p}$ | u64->u32 | NO | depends on exact decay for sum correctness |
| [pelt.c L75](apex-cfs/docs/pelt.c#L75) | `c2 = LOAD_AVG_MAX - decay_load(LOAD_AVG_MAX, periods) - 1024;` | `c2` | $c2 = \text{MAX} - \text{MAX} \cdot y^{p} - 1024$ | u32/u64 | NO | exact sum component |
| [pelt.c L77](apex-cfs/docs/pelt.c#L77) | `return c1 + c2 + c3;` | return value | $c1 + c2 + c3$ | u32 | NO | aggregation step |
| [pelt.c L108](apex-cfs/docs/pelt.c#L108) | `delta += sa->period_contrib;` | `delta` | $\Delta = \Delta + \text{period\_contrib}$ | u64 | NO | time alignment |
| [pelt.c L109](apex-cfs/docs/pelt.c#L109) | `periods = delta / 1024;` | `periods` | $p = \lfloor \Delta/1024 \rfloor$ | u64 | NO | exact period counting |
| [pelt.c L115](apex-cfs/docs/pelt.c#L115) | `sa->load_sum = decay_load(sa->load_sum, periods);` | `load_sum` | $S = S \cdot y^{p}$ | u64 | NO | used by averages; keep exact |
| [pelt.c L116](apex-cfs/docs/pelt.c#L116) | `sa->runnable_sum = decay_load(sa->runnable_sum, periods);` | `runnable_sum` | $R = R \cdot y^{p}$ | u64 | NO | same reasoning |
| [pelt.c L118](apex-cfs/docs/pelt.c#L118) | `sa->util_sum = decay_load((u64)(sa->util_sum), periods);` | `util_sum` | $U = U \cdot y^{p}$ | u32->u64 | NO | same reasoning |
| [pelt.c L123](apex-cfs/docs/pelt.c#L123) | `delta %= 1024;` | `delta` | $\Delta = \Delta \bmod 1024$ | u64 | NO | period remainder |
| [pelt.c L135](apex-cfs/docs/pelt.c#L135) | `contrib = __accumulate_pelt_segments(periods,` | `contrib` | $\text{segments}(p, 1024-\text{period\_contrib}, \Delta)$ | u32 | NO | exact segment sum |
| [pelt.c L136](apex-cfs/docs/pelt.c#L136) | `1024 - sa->period_contrib, delta);` | `contrib` | $d1 = 1024 - \text{period\_contrib}$ | u32 | NO | correct remainder math |
| [pelt.c L141](apex-cfs/docs/pelt.c#L141) | `sa->load_sum += load * contrib;` | `load_sum` | $S = S + (load \cdot contrib)$ | u64 | NO | accumulator accuracy |
| [pelt.c L143](apex-cfs/docs/pelt.c#L143) | `sa->runnable_sum += runnable * contrib << SCHED_CAPACITY_SHIFT;` | `runnable_sum` | $R = R + (runnable \cdot contrib) \ll k$ | u64 | NO | scale alignment |
| [pelt.c L145](apex-cfs/docs/pelt.c#L145) | `sa->util_sum += contrib << SCHED_CAPACITY_SHIFT;` | `util_sum` | $U = U + contrib \ll k$ | u64 | NO | scale alignment |
| [pelt.c L185](apex-cfs/docs/pelt.c#L185) | `delta = now - sa->last_update_time;` | `delta` | $\Delta = now - last$ | u64 | NO | time delta |
| [pelt.c L199](apex-cfs/docs/pelt.c#L199) | `delta >>= 10;` | `delta` | $\Delta = \Delta \gg 10$ | u64 | NO | ns->1024ns unit conversion |
| [pelt.c L203](apex-cfs/docs/pelt.c#L203) | `sa->last_update_time += delta << 10;` | `last_update_time` | $t = t + (\Delta \ll 10)$ | u64 | NO | time reconstruction |
| [pelt.c L264](apex-cfs/docs/pelt.c#L264) | `sa->load_avg = div_u64(load * sa->load_sum, divider);` | `load_avg` | $\text{load\_avg} = (load \cdot load\_sum)/divider$ | u64->ulong | NO | final average output |
| [pelt.c L265](apex-cfs/docs/pelt.c#L265) | `sa->runnable_avg = div_u64(sa->runnable_sum, divider);` | `runnable_avg` | $\text{runnable\_avg} = runnable\_sum/divider$ | u64->ulong | NO | final average output |
| [pelt.c L266](apex-cfs/docs/pelt.c#L266) | `WRITE_ONCE(sa->util_avg, sa->util_sum / divider);` | `util_avg` | $\text{util\_avg} = util\_sum/divider$ | u32->ulong | NO | final average output |
| [pelt.c L439](apex-cfs/docs/pelt.c#L439) | `running = cap_scale(running, arch_scale_freq_capacity(cpu_of(rq)));` | `running` | $running \cdot f_{cap}$ | u64 | NO | capacity scaling accuracy |
| [pelt.c L440](apex-cfs/docs/pelt.c#L440) | `running = cap_scale(running, arch_scale_cpu_capacity(cpu_of(rq)));` | `running` | $running \cdot c_{cap}$ | u64 | NO | capacity scaling accuracy |
| [pelt.c L453](apex-cfs/docs/pelt.c#L453) | `ret = ___update_load_sum(rq->clock - running, &rq->avg_irq,` | `ret` | $ret = f(clock - running)$ | u64 | NO | exact time alignment |
| [pelt.c L457](apex-cfs/docs/pelt.c#L457) | `ret += ___update_load_sum(rq->clock, &rq->avg_irq,` | `ret` | $ret = ret + f(clock)$ | int | NO | accumulator |

## 3. pelt.c — Key Findings
### 3.1 runnable_avg_yN_inv[] Array
- Definition line: [sched-pelt.h](apex-cfs/docs/sched-pelt.h#L4-L11)
- Values (32 entries): `0xffffffff, 0xfa83b2da, 0xf5257d14, 0xefe4b99a, 0xeac0c6e6, 0xe5b906e6, 0xe0ccdeeb, 0xdbfbb796, 0xd744fcc9, 0xd2a81d91, 0xce248c14, 0xc9b9bd85, 0xc5672a10, 0xc12c4cc9, 0xbd08a39e, 0xb8fbaf46, 0xb504f333, 0xb123f581, 0xad583ee9, 0xa9a15ab4, 0xa5fed6a9, 0xa2704302, 0x9ef5325f, 0x9b8d39b9, 0x9837f050, 0x94f4efa8, 0x91c3d373, 0x8ea4398a, 0x8b95c1e3, 0x88980e80, 0x85aac367, 0x82cd8698` [sched-pelt.h](apex-cfs/docs/sched-pelt.h#L4-L11)
- Entry count: 32 (matches `LOAD_AVG_PERIOD`) [sched-pelt.h](apex-cfs/docs/sched-pelt.h#L13)
- Mathematical meaning: entry[$n$] = $y^n$ in fixed-point; used as $val \cdot y^n$ in `decay_load` [pelt.c](apex-cfs/docs/pelt.c#L53)
- Fixed-point format: Q0.32 (shift by 32 bits) [pelt.c](apex-cfs/docs/pelt.c#L53)

### 3.2 ___update_load_avg() Breakdown
- Signature: `static __always_inline void ___update_load_avg(struct sched_avg *sa, unsigned long load)` [pelt.c](apex-cfs/docs/pelt.c#L256-L257)
- Parameters:
  - `sa`: per-entity or per-rq PELT accumulator (sums and averages)
  - `load`: weight-scaled load input (entity or rq)
- Decay multiplication happens in `decay_load` at [pelt.c](apex-cfs/docs/pelt.c#L53); `___update_load_avg` consumes decayed sums and applies a divider
- Line where multiplication inside `___update_load_avg` happens: `load * sa->load_sum` at [pelt.c](apex-cfs/docs/pelt.c#L264)
- Result stored in: `sa->load_avg` at [pelt.c](apex-cfs/docs/pelt.c#L264)
- Callers:
  - `__update_load_avg_blocked_se` [pelt.c](apex-cfs/docs/pelt.c#L295-L300)
  - `__update_load_avg_se` [pelt.c](apex-cfs/docs/pelt.c#L306-L314)
  - `__update_load_avg_cfs_rq` [pelt.c](apex-cfs/docs/pelt.c#L320-L328)
  - `update_rt_rq_load_avg` [pelt.c](apex-cfs/docs/pelt.c#L346-L354)
  - `update_dl_rq_load_avg` [pelt.c](apex-cfs/docs/pelt.c#L372-L380)
  - `update_thermal_load_avg` [pelt.c](apex-cfs/docs/pelt.c#L403-L410)
  - `update_irq_load_avg` [pelt.c](apex-cfs/docs/pelt.c#L430-L464)

### 3.3 accumulate_sum() Breakdown
- Signature: `static __always_inline u32 accumulate_sum(u64 delta, struct sched_avg *sa, unsigned long load, unsigned long runnable, int running)` [pelt.c](apex-cfs/docs/pelt.c#L101-L103)
- Loop: none (0 iterations); only conditional blocks [pelt.c](apex-cfs/docs/pelt.c#L111-L138)
- Arithmetic inside:
  - Periods calculation and modulus [pelt.c](apex-cfs/docs/pelt.c#L108-L123)
  - Decays of sums [pelt.c](apex-cfs/docs/pelt.c#L115-L118)
  - Contributions accumulation [pelt.c](apex-cfs/docs/pelt.c#L141-L146)
- Most expensive line: `sa->load_sum = decay_load(sa->load_sum, periods);` [pelt.c](apex-cfs/docs/pelt.c#L115) due to table-based multiply/shift in `decay_load` [pelt.c](apex-cfs/docs/pelt.c#L53)

### 3.4 The Decay Formula (Mathematical Form)
- Code basis: y^32 = 0.5 [pelt.c](apex-cfs/docs/pelt.c#L27-L30), `LOAD_AVG_PERIOD=32` [sched-pelt.h](apex-cfs/docs/sched-pelt.h#L13)
- Decay factor: $y = 2^{-1/32} \approx 0.9785720621$
- Table encoding: $runnable\_avg\_yN\_inv[n] \approx y^n \cdot 2^{32}$ [sched-pelt.h](apex-cfs/docs/sched-pelt.h#L4-L11)
- Applied by: `mul_u64_u32_shr(..., 32)` [pelt.c](apex-cfs/docs/pelt.c#L53)
- Formula:
$$
load\_avg(t) = u_0 + \sum_{i=1}^{\infty} u_i y^i
$$
At period boundary (rollover):
$$
load\_avg \leftarrow u_0 + y \cdot load\_avg
$$

## 4. fair.c — update_curr() Annotation
Note: fair.c has 12251 lines total; only the 3 functions listed in task scope are analyzed. See [apex-cfs/docs/fair.c](apex-cfs/docs/fair.c).

**Function**
- Name: `update_curr`
- Lines: [apex-cfs/docs/fair.c](apex-cfs/docs/fair.c#L882-L920)
- Calls pelt.c function: NO

**Arithmetic lines**
| Line | Exact code | Variable | Meaning in CFS | Type | Precision concern? |
|---|---|---|---|---|---|
| [fair.c L891](apex-cfs/docs/fair.c#L891) | `delta_exec = now - curr->exec_start;` | `delta_exec` | CPU time since last exec_start | u64 | NO (time delta)
| [fair.c L905](apex-cfs/docs/fair.c#L905) | `curr->sum_exec_runtime += delta_exec;` | `sum_exec_runtime` | Total executed runtime | u64 | NO
| [fair.c L908](apex-cfs/docs/fair.c#L908) | `curr->vruntime += calc_delta_fair(delta_exec, curr);` | `vruntime` | Virtual runtime increment scaled by weight | u64 | YES (scaling precision)

**Thing 1 — update_curr() specific**
- `delta_exec` computed at [fair.c](apex-cfs/docs/fair.c#L891) from `now - curr->exec_start`
- Units: nanoseconds via `rq_clock_task` (scheduler clock) at [fair.c](apex-cfs/docs/fair.c#L885)
- `vruntime` update at [fair.c](apex-cfs/docs/fair.c#L908)
- Scaling function: `calc_delta_fair` [fair.c](apex-cfs/docs/fair.c#L694-L700)
- Weight parameter carried by `curr->load.weight`, set via `reweight_task` using `sched_prio_to_weight` [fair.c](apex-cfs/docs/fair.c#L3333-L3341)

## 5. fair.c — calc_delta_fair() Annotation
**Function**
- Name: `calc_delta_fair`
- Lines: [apex-cfs/docs/fair.c](apex-cfs/docs/fair.c#L694-L700)
- Calls pelt.c function: NO
- Full call signature used: `delta = __calc_delta(delta, NICE_0_LOAD, &se->load);` [fair.c](apex-cfs/docs/fair.c#L696-L697)

**Arithmetic lines**
| Line | Exact code | Variable | Meaning in CFS | Type | Precision concern? |
|---|---|---|---|---|---|
| [fair.c L696](apex-cfs/docs/fair.c#L696) | `if (unlikely(se->load.weight != NICE_0_LOAD))` | condition | Weight differs from default | unsigned long | NO |
| [fair.c L697](apex-cfs/docs/fair.c#L697) | `delta = __calc_delta(delta, NICE_0_LOAD, &se->load);` | `delta` | Scale by weight ratio | u64 | YES (fixed-point reciprocal)

**Thing 2 — calc_delta_fair() specific**
- Formula implemented by helper:
  - Comment: $\Delta = \Delta\_{exec} \cdot weight / lw\!.weight$ [fair.c](apex-cfs/docs/fair.c#L296-L299)
- `NICE_0_LOAD` definition: [apex-cfs/docs/sched.h](apex-cfs/docs/sched.h#L167)
- Weight parameter: `se->load.weight` [fair.c](apex-cfs/docs/fair.c#L697)
- Division method: reciprocal multiply + shift
  - Reciprocal computed: `lw->inv_weight = WMULT_CONST / w;` [fair.c](apex-cfs/docs/fair.c#L286-L293)
  - Multiply + shift: `return mul_u64_u32_shr(delta_exec, fact, shift);` [fair.c](apex-cfs/docs/fair.c#L332)
- Exact line where `NICE_0_LOAD / weight` effect is produced: reciprocal generation at [fair.c](apex-cfs/docs/fair.c#L286-L293) and final multiply/shift at [fair.c](apex-cfs/docs/fair.c#L332)

## 6. fair.c — update_load_avg() Annotation
**Function**
- Name: `update_load_avg`
- Lines: [apex-cfs/docs/fair.c](apex-cfs/docs/fair.c#L4173-L4213)
- Calls pelt.c function(s):
  - `__update_load_avg_se(now, cfs_rq, se)` [fair.c](apex-cfs/docs/fair.c#L4182-L4183) -> [pelt.c](apex-cfs/docs/pelt.c#L306-L318)
  - `update_cfs_rq_load_avg(now, cfs_rq)` [fair.c](apex-cfs/docs/fair.c#L4185) -> [pelt.c](apex-cfs/docs/pelt.c#L320-L333)

**Arithmetic lines**
- None directly in this function; it delegates arithmetic to pelt.c and helper functions.

**Thing 3 — update_load_avg() specific**
- Current time passed as `now = cfs_rq_clock_pelt(cfs_rq)` [fair.c](apex-cfs/docs/fair.c#L4175)
- Load update per task: `__update_load_avg_se(now, cfs_rq, se)` [fair.c](apex-cfs/docs/fair.c#L4182-L4183)
- Load update per runqueue: `update_cfs_rq_load_avg(now, cfs_rq)` [fair.c](apex-cfs/docs/fair.c#L4185)
- Result stored in task struct: `se->avg.load_avg` (type `unsigned long`) [apex-cfs/docs/include-linux-sched.h](apex-cfs/docs/include-linux-sched.h#L493-L502)

**Thing 4 — Weight-to-vruntime scaling (__calc_delta)**
- Function: `__calc_delta` [fair.c](apex-cfs/docs/fair.c#L308-L332)
- Exact formula (from comment):
$$
\Delta = \Delta\_{exec} \cdot weight / lw.weight
$$
with fixed-point reciprocal: [fair.c](apex-cfs/docs/fair.c#L296-L299)
- Implementation uses multiplication then shift: `mul_u64_u32_shr(delta_exec, fact, shift)` [fair.c](apex-cfs/docs/fair.c#L332)
- Inputs:
  - `delta_exec`: u64 time delta [fair.c](apex-cfs/docs/fair.c#L308)
  - `weight`: unsigned long from `sched_prio_to_weight` via `reweight_task` [fair.c](apex-cfs/docs/fair.c#L3338)
- Overflow protection:
  - Adjusts `shift` based on high bits of `fact` [fair.c](apex-cfs/docs/fair.c#L317-L330)

## 7. Approximation Targets
### 7.1 Target 1 — PELT Decay (pelt.c line 53)
- File: [apex-cfs/docs/pelt.c](apex-cfs/docs/pelt.c#L53)
- Function name: `decay_load` [apex-cfs/docs/pelt.c](apex-cfs/docs/pelt.c#L31-L55)
- Exact line number: 53
- Exact code: `val = mul_u64_u32_shr(val, runnable_avg_yN_inv[local_n], 32);` [apex-cfs/docs/pelt.c](apex-cfs/docs/pelt.c#L53)
- Calls/sec estimate (100 tasks): ~300k/s (100 tasks * 1kHz * 3 decays per tick)
- Current precision: fixed-point Q0.32 [apex-cfs/docs/pelt.c](apex-cfs/docs/pelt.c#L53)
- Proposed approximation: BSA bit-shift
- Why safe to approximate: decay factor $y$ close to 1, and error amortizes over geometric decay

### 7.2 Target 2 — vruntime Scaling (fair.c line 332)
- File: [apex-cfs/docs/fair.c](apex-cfs/docs/fair.c#L332)
- Function name: `__calc_delta` [apex-cfs/docs/fair.c](apex-cfs/docs/fair.c#L308-L332)
- Exact line number: 332
- Exact code: `return mul_u64_u32_shr(delta_exec, fact, shift);` [apex-cfs/docs/fair.c](apex-cfs/docs/fair.c#L332)
- Calls/sec estimate (100 tasks): ~1k/s per CPU (update_curr tick-based) [apex-cfs/docs/fair.c](apex-cfs/docs/fair.c#L882-L920)
- Current precision: fixed-point reciprocal (WMULT_SHIFT=32) [apex-cfs/docs/fair.c](apex-cfs/docs/fair.c#L276-L332)
- Proposed approximation: Fixed-point reciprocal
- Why safe to approximate: proportional errors in vruntime scaling are bounded and can be corrected by fairness feedback

### 7.3 Target 3 — Load Balancer (fair.c line 9947)
- File: [apex-cfs/docs/fair.c](apex-cfs/docs/fair.c#L9888-L9967)
- Function name: `find_busiest_group` [apex-cfs/docs/fair.c](apex-cfs/docs/fair.c#L9888-L10012)
- Exact line number where load comparison happens: 9947
- Exact code: `if (local->avg_load >= busiest->avg_load)` [apex-cfs/docs/fair.c](apex-cfs/docs/fair.c#L9947)
- How it uses load_avg values: compares group average loads to decide if pulling tasks is warranted
- Why approximation error here is tolerable: decision is thresholded and repeated; small load errors rarely change the chosen branch

## 8. Q&A — 5 Critical Questions
**Q1: What is the exact value of the decay factor y? Where is it encoded? How is it represented without floating point?**
- $y = 2^{-1/32} \approx 0.9785720621$ from $y^{32}=0.5$ [pelt.c](apex-cfs/docs/pelt.c#L27-L30) and `LOAD_AVG_PERIOD=32` [sched-pelt.h](apex-cfs/docs/sched-pelt.h#L13)
- Encoded in `runnable_avg_yN_inv[]` as fixed-point $y^n \cdot 2^{32}$ [sched-pelt.h](apex-cfs/docs/sched-pelt.h#L4-L11)
- Applied by `mul_u64_u32_shr(..., 32)` (Q0.32) [pelt.c](apex-cfs/docs/pelt.c#L53)

**Q2: Entry[0], Entry[31], and why 32 entries?**
- Entry[0] represents $y^0 = 1$ in Q0.32, stored as `0xffffffff` [sched-pelt.h](apex-cfs/docs/sched-pelt.h#L4-L5)
- Entry[31] represents $y^{31}$ in Q0.32, stored as `0x82cd8698` [sched-pelt.h](apex-cfs/docs/sched-pelt.h#L9-L10)
- 32 entries because `LOAD_AVG_PERIOD=32`, and the table covers $n < \text{PERIOD}$ [sched-pelt.h](apex-cfs/docs/sched-pelt.h#L13)

**Q3: In calc_delta_fair(), is NICE_0_LOAD/weight computed by division or reciprocal multiply?**
- Reciprocal multiply + shift, not direct division: `lw->inv_weight = WMULT_CONST / w;` [fair.c](apex-cfs/docs/fair.c#L286-L293) and `mul_u64_u32_shr(delta_exec, fact, shift)` [fair.c](apex-cfs/docs/fair.c#L332)

**Q4: How does nice=-5 get more CPU than nice=0? Trace nice to vruntime.**
- Nice to weight table: `sched_prio_to_weight[40]` [apex-cfs/docs/sched.h](apex-cfs/docs/sched.h#L2105)
- Weight is scaled and stored into `se->load.weight` in `reweight_task`: `weight = scale_load(sched_prio_to_weight[prio]);` [fair.c](apex-cfs/docs/fair.c#L3338)
- `calc_delta_fair` uses `se->load.weight` to scale `delta_exec` [fair.c](apex-cfs/docs/fair.c#L694-L697)
- `__calc_delta` applies the reciprocal multiply/shift using `lw->inv_weight` [fair.c](apex-cfs/docs/fair.c#L308-L332)
- Result updates `curr->vruntime` in `update_curr` [fair.c](apex-cfs/docs/fair.c#L908)

**Q5: Worst-case impact of 1% error in decay multiplication after 32 ticks**
Using $|\Delta L| \le |L_0| \cdot |\epsilon|/(1-y)$ with $y=2^{-1/32}$ [pelt.c](apex-cfs/docs/pelt.c#L27-L30), [sched-pelt.h](apex-cfs/docs/sched-pelt.h#L13):
$$
|\Delta L| \le |L_0| \cdot 0.01 / (1-2^{-1/32})
$$
Numerically, $1-2^{-1/32} \approx 0.02142794$, so:
$$
|\Delta L| \le 0.4666 \cdot |L_0|
$$

## 9. Summary Table
| Target | File | Line | Operation | Times/sec | Safe to Approx? |
|--------|------|------|-----------|-----------|-----------------|
| PELT decay | [apex-cfs/docs/pelt.c](apex-cfs/docs/pelt.c#L53) | 53 | load * y^n (Q0.32) | ~300k/s | YES |
| vruntime scale | [apex-cfs/docs/fair.c](apex-cfs/docs/fair.c#L332) | 332 | delta_exec * inv_weight >> shift | ~1k/s | YES |
| LB imbalance | [apex-cfs/docs/fair.c](apex-cfs/docs/fair.c#L9947) | 9947 | load_avg comparison | ~10/s | YES |
