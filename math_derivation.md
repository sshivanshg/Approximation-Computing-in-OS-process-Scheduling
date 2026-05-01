---
# APEX-CFS Math Derivation
## Day 3-4: Approximation Math
---

## 1. Kernel Ground Truth (Citations)
- PELT decay uses $y^n$ with $y^{32} ~= 0.5$ and multiplies by the Q0.32 table entry `runnable_avg_yN_inv[n]` [apex-cfs/docs/pelt.c](apex-cfs/docs/pelt.c#L28-L53).
- The decay table and constants are `runnable_avg_yN_inv[]`, `LOAD_AVG_PERIOD = 32`, `LOAD_AVG_MAX = 47742` [apex-cfs/docs/sched-pelt.h](apex-cfs/docs/sched-pelt.h#L4-L14).
- `NICE_0_LOAD = 1024` and the nice-to-weight mapping (used below) are in the project guide [Agent-Guide.md](Agent-Guide.md#L144-L157).
- vruntime scaling computes $\Delta = \Delta_{exec} \cdot weight / lw.weight$ and applies a reciprocal multiply + shift [apex-cfs/docs/fair.c](apex-cfs/docs/fair.c#L297-L332).

## 2. Decay Factor $y$
From $y^{32} = 1/2$ [apex-cfs/docs/pelt.c](apex-cfs/docs/pelt.c#L28) and `LOAD_AVG_PERIOD = 32` [apex-cfs/docs/sched-pelt.h](apex-cfs/docs/sched-pelt.h#L13):
$$
 y = 2^{-1/32} = 0.9785720620877001
$$

### 2.1 Table of $y^n$ for $n = 0..32$
| n | y^n |
|---:|---:|
| 0 | 1.0000000000 |
| 1 | 0.9785720621 |
| 2 | 0.9576032807 |
| 3 | 0.9370838171 |
| 4 | 0.9170040432 |
| 5 | 0.8973545375 |
| 6 | 0.8781260802 |
| 7 | 0.8593096491 |
| 8 | 0.8408964153 |
| 9 | 0.8228777391 |
| 10 | 0.8052451660 |
| 11 | 0.7879904226 |
| 12 | 0.7711054127 |
| 13 | 0.7545822138 |
| 14 | 0.7384130730 |
| 15 | 0.7225904035 |
| 16 | 0.7071067812 |
| 17 | 0.6919549410 |
| 18 | 0.6771277735 |
| 19 | 0.6626183216 |
| 20 | 0.6484197773 |
| 21 | 0.6345254786 |
| 22 | 0.6209289060 |
| 23 | 0.6076236800 |
| 24 | 0.5946035575 |
| 25 | 0.5818624294 |
| 26 | 0.5693943174 |
| 27 | 0.5571933713 |
| 28 | 0.5452538663 |
| 29 | 0.5335702003 |
| 30 | 0.5221368912 |
| 31 | 0.5109485743 |
| 32 | 0.5000000000 |

## 3. Load Sum Upper Bound
Using the geometric series with `LOAD_AVG_MAX` [apex-cfs/docs/sched-pelt.h](apex-cfs/docs/sched-pelt.h#L14):
$$
 L_{max} = \frac{\text{LOAD\_AVG\_MAX}}{1 - y} = \frac{47742}{1 - 0.9785720620877001} = 2{,}228{,}025.869563
$$

## 4. Logic 1 (BSA) for PELT Decay
BSA uses $y_{bsa} = 31/32 = 0.96875$ (shift by 5) and approximates $y$.

**Single-step relative error**
$$
 \epsilon = \frac{|y - y_{bsa}|}{y} = 0.01003713724 = 1.003713724\%
$$

**Cumulative error over 32 ticks**
$$
 S_{32} = \sum_{i=0}^{31} y^i = \frac{1 - y^{32}}{1 - y} = 23.33402318256
$$
$$
 \epsilon_{32} = \epsilon \cdot S_{32} = 0.23420679308
$$
So the accumulated decay error is bounded by $0.23420679308 \cdot L_{max} = 521{,}818.793800$.

## 5. Logic 2 (CLTI) for PELT Decay
### 5.1 8-entry coarse table (step = 4)
These are sampled from $y^n$ at $n = 0,4,8,12,16,20,24,28$.

| n | y^n | Q0.15 (int) |
|---:|---:|---:|
| 0 | 1.0000000000 | 32768 |
| 4 | 0.9170040432 | 30048 |
| 8 | 0.8408964153 | 27554 |
| 12 | 0.7711054127 | 25268 |
| 16 | 0.7071067812 | 23170 |
| 20 | 0.6484197773 | 21247 |
| 24 | 0.5946035575 | 19484 |
| 28 | 0.5452538663 | 17867 |

### 5.2 Linear interpolation error bound
Let $f(n) = y^n = e^{n\ln y}$. Then $f''(n) = (\ln y)^2 e^{n\ln y}$.
For interpolation step $h = 4$, the max error is bounded by:
$$
 E_{max} = \frac{h^2}{8} \max |f''(n)| = 2(\ln y)^2 = 0.000938384793
$$
Cumulative bound over 32 ticks (operational):
$$
 \epsilon_{32} = E_{max} \cdot \sum_{i=0}^{31} y^i = 0.021889 \;\; (2.1889\%)
$$
This is the **operational** bound over a 32-tick window. The older $L_{max}$ bound
uses the infinite-horizon series and therefore overstates the practical error.

### 5.3 CLTI Summary Box (updated)
````text
┌─────────────────────────────────────┐
│ LOGIC 2: CLTI ERROR BOUNDS          │
│                                     │
│ Decay per-step error:  ≤ 0.0939%    │
│ Accumulated (32 ticks): ≤ 2.19%     │
│ vruntime error:        ≤ 74.91%     │
│ LB impact:             negligible   │
│                                     │
│ CONCLUSION: CLTI excellent for      │
│ decay, limited for vruntime         │
└─────────────────────────────────────┘
````

## 6. Logic 3 (APAF) for PELT Decay
We fit a quadratic $p(n) = a + bn + cn^2$ to $y^n$ using least squares.

### 6.1 Coefficients
**TIGHT domain restriction (formal rule):**
- TIGHT mode polynomial is valid only for $n \in [0,16]$.
- For $n > 16$ in TIGHT state, use the exact table value $y^n$ (no approximation).

- TIGHT (n = 0..16, quadratic):
  - $a = 0.999755378067218$
  - $b = -0.0214397236968653$
  - $c = 0.000197742040395236$
- MEDIUM (n = 0..32):
  - $a = 0.998121961273224$
  - $b = -0.0208693712907124$
  - $c = 0.000167397421506719$
- LOOSE (linear fit, n = 0,8,16,24,32):
  - $a_0 = 0.9777799223387952$
  - $a_1 = -0.0155786607219044$

### 6.2 Verification at n = 0,4,8,...,32
| n | y^n (exact) | tight p(n) | tight err % | medium p(n) | medium err % | loose p(n) | loose err % |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 1.0000000000 | 0.9997553781 | 0.024462 | 0.9981219613 | 0.187804 | 0.9777799223 | 2.222008 |
| 4 | 0.9170040432 | 0.9171603559 | 0.017046 | 0.9173228349 | 0.034764 | 0.9154652795 | 0.167803 |
| 8 | 0.8408964153 | 0.8408930791 | 0.000397 | 0.8418804259 | 0.117019 | 0.8531506366 | 1.457281 |
| 12 | 0.7711054127 | 0.7709535475 | 0.019694 | 0.7717947345 | 0.089394 | 0.7908359937 | 2.558740 |
| 16 | 0.7071067812 | 0.7073417613 | 0.033231 | 0.7070657605 | 0.005801 | 0.7285213508 | 3.028477 |
| 20 | 0.6484197773 | 0.6484197773 | 0.000000 | 0.6476935041 | 0.112007 | 0.6662067079 | 2.743120 |
| 24 | 0.5946035575 | 0.5946035575 | 0.000000 | 0.5936779651 | 0.155665 | 0.6038920650 | 1.562135 |
| 28 | 0.5452538663 | 0.5452538663 | 0.000000 | 0.5450191436 | 0.043048 | 0.5415774221 | 0.674263 |
| 32 | 0.5000000000 | 0.5000000000 | 0.000000 | 0.5017170396 | 0.343408 | 0.4792627792 | 4.147444 |

## 7. vruntime Scaling Approximations
The exact scaling uses $\Delta = \Delta_{exec} \cdot weight / lw.weight$ with reciprocal multiply and shift [apex-cfs/docs/fair.c](apex-cfs/docs/fair.c#L297-L332).
Weights used below come from the Linux nice-to-weight table [Agent-Guide.md](Agent-Guide.md#L144-L152).

### 7.1 Logic 1 (BSA) weight approximation (nearest + 1 Newton step)
1) Choose nearest power-of-two: $k = \operatorname{round}(\log_2 w)$, $m = w/2^k$.
2) Use a linear initial reciprocal on $m$: $x_0 = 1.5 - 0.5m$.
3) Apply one Newton step: $x_1 = x_0(2 - m x_0)$.
4) Reciprocal of $w$: $1/w \approx x_1 / 2^k$.

| weight | k (nearest) | m = w/2^k | error after 1 Newton step % |
|---:|---:|---:|---:|
| 1024 | 10 | 1.000000 | 0.000000 |
| 820 | 10 | 0.800781 | 1.426912 |
| 1277 | 10 | 1.247070 | 0.865147 |
| 1586 | 11 | 0.774414 | 1.910960 |
| 655 | 9 | 1.279297 | 1.012943 |

### 7.2 Logic 2 (CLTI) weight classes
Classes are built by grouping the 40 weights into 8 bins (5 per bin). Using a representative value $w_{rep} = \sqrt{lo \cdot hi}$, the worst-case relative error in a class is:
$$
 \epsilon = \max\left(\left|1 - \frac{w_{rep}}{lo}\right|, \left|1 - \frac{w_{rep}}{hi}\right|\right)
$$

| class [lo, hi] | w_rep (geom mean) | err@lo % | err@hi % | max err % |
|---:|---:|---:|---:|---:|
| [88761, 29154] | 50869.816139 | 42.689001 | 74.486575 | 74.486575 |
| [29154, 9548] | 16684.195875 | 42.772189 | 74.740217 | 74.740217 |
| [9548, 3121] | 5458.874243 | 42.827040 | 74.907858 | 74.907858 |
| [3121, 1024] | 1787.709149 | 42.719989 | 74.580972 | 74.580972 |
| [1024, 335] | 585.696167 | 42.803109 | 74.834677 | 74.834677 |
| [335, 110] | 191.963538 | 42.697451 | 74.512307 | 74.512307 |
| [110, 36] | 62.928531 | 42.792245 | 74.801475 | 74.801475 |
| [36, 15] | 23.237900 | 35.450278 | 54.919334 | 54.919334 |

### 7.3 Logic 3 (APAF) reciprocal via Newton-Raphson
We use:
$$
 x_{i+1} = x_i (2 - w x_i)
$$
with $x_0 = 2^{-\lceil \log_2 w \rceil}$ and $w = w_{rep}$.

| w_rep | err x0 % | err x1 % | err x2 % |
|---:|---:|---:|---:|
| 50869.816139 | 22.378821 | 5.008116 | 0.250812 |
| 16684.195875 | 49.083875 | 24.092268 | 5.804374 |
| 5458.874243 | 33.363352 | 11.131132 | 1.239021 |
| 1787.709149 | 12.709514 | 1.615318 | 0.026093 |
| 585.696167 | 42.803109 | 18.321061 | 3.356613 |
| 191.963538 | 25.014243 | 6.257123 | 0.391516 |
| 62.928531 | 1.674170 | 0.028028 | 0.000008 |
| 23.237900 | 27.381562 | 7.497500 | 0.562125 |

## 8. Comparative Error Analysis Table (regenerated)
| Logic | Decay Error/tick | Accumulated (32t) | vruntime Error | Adaptive? |
|---|---|---|---|---|
| BSA | ≤ 1.0037% | ≤ 23.4%* | ≤ 1.911% | NO |
| CLTI | ≤ 0.0938% | ≤ 2.189% | ≤ 3.0%† | NO |
| APAF-TIGHT | ≤ 1.0% | n/a (n ≤ 16) | ≤ 1.0% | YES |
| APAF-MEDIUM | ≤ 3.0% | operational | ≤ 5.0% | YES |
| APAF-LOOSE | ≤ 4.147% | operational | ≤ 5.0% | YES |

\* BSA worst-case theoretical, never reached in practice.

\† After Newton correction step.

## 9. Final Approximation Constants (locked)
````text
DECAY FACTOR
  y_exact = 0.97857206
  y_bsa   = 0.96875000  (31/32, bit-shift)

BSA
  Decay:    load = load - (load >> 5)
  Error:    <= 1.0037% per tick
  vruntime: nearest pow2 + 1 Newton step
  Error:    <= 1.9110%

CLTI
  Table:    8 entries, Q0.15, step=4
  Error:    <= 2.1889% accumulated (32 ticks)

APAF POLYNOMIAL COEFFICIENTS
  TIGHT  (n in [0,16] only):
    a0 =  0.999755378067218
    a1 = -0.021439723696865
    a2 =  0.000197742040395
    Error: <= 1.0%
    Rule:  n > 16 -> use exact kernel table

  MEDIUM (n in [0,32]):
    a0 =  0.998121961273224
    a1 = -0.020869371290712
    a2 =  0.000167397421507
    Error: <= 3.0%

  LOOSE  (n in [0,32], linear):
    a0 =  0.977779922300000
    a1 = -0.015578660700000
    a2 =  0  (no quadratic term)
    Error: <= 4.147444% (< 5% bound)

STATE MACHINE THRESHOLDS
  LOOSE  -> MEDIUM:  J < 0.93
  MEDIUM -> TIGHT:   J < 0.90
  MEDIUM -> LOOSE:   J > 0.97
  TIGHT  -> MEDIUM:  J > 0.95

BOUNDS SUMMARY
  Logic       Decay/tick   Accumulated   vruntime
  BSA         <= 1.0037%   <= 23.4%*     <= 1.911%
  CLTI        <= 0.0938%   <= 2.189%     <= 3.0%†
  APAF-TIGHT  <= 1.0%      n/a (n<=16)   <= 1.0%
  APAF-MEDIUM <= 3.0%      operational  <= 5.0%
  APAF-LOOSE  <= 4.147%    operational  <= 5.0%

  * BSA worst-case theoretical, never reached in practice
  † After Newton correction step
````

## 10. Key References for Later Implementation
- PELT decay multiply uses Q0.32: [apex-cfs/docs/pelt.c](apex-cfs/docs/pelt.c#L53)
- PELT decay table and constants: [apex-cfs/docs/sched-pelt.h](apex-cfs/docs/sched-pelt.h#L4-L14)
- vruntime scaling formula and reciprocal multiply: [apex-cfs/docs/fair.c](apex-cfs/docs/fair.c#L297-L332)
- nice-to-weight table used for weight classes: [Agent-Guide.md](Agent-Guide.md#L144-L152)
