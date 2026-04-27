CC      = gcc
CFLAGS  = -Wall -Werror -std=c11 -g -O0
CFLAGS += -I./src
LDFLAGS = -lm

SRC_BASE = src/task.c \
           src/fairness.c \
           src/metrics.c

SRC_EXACT = src/cfs_exact.c

SRC_APPROX = src/approx_bsa.c \
             src/approx_clti.c \
             src/approx_apaf.c

SRC_ALL = $(SRC_BASE) $(SRC_EXACT) $(SRC_APPROX)

EXP1_SRC = experiments/exp1_equal_weight.c
EXP2_SRC = experiments/exp2_mixed_load.c
EXP3_SRC = experiments/exp3_dynamic_spike.c
EXP4_SRC = experiments/exp4_error_bound.c

EXP1_BIN = results/exp1
EXP2_BIN = results/exp2
EXP3_BIN = results/exp3
EXP4_BIN = results/exp4

all: $(EXP1_BIN) $(EXP2_BIN) \
     $(EXP3_BIN) $(EXP4_BIN)

exp1: $(EXP1_BIN)

exp2: $(EXP2_BIN)

$(EXP1_BIN): $(SRC_ALL) $(EXP1_SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(EXP2_BIN): $(SRC_ALL) $(EXP2_SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(EXP3_BIN): $(SRC_ALL) $(EXP3_SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(EXP4_BIN): $(SRC_ALL) $(EXP4_SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test_task:
	$(CC) $(CFLAGS) -DTASK_TEST \
	    src/task.c \
	    -o results/test_task $(LDFLAGS)
	./results/test_task

test_fairness:
	$(CC) $(CFLAGS) -DFAIRNESS_TEST \
	    src/task.c src/fairness.c \
	    -o results/test_fairness $(LDFLAGS)
	./results/test_fairness

test_metrics:
	$(CC) $(CFLAGS) -DMETRICS_TEST \
	    src/task.c src/fairness.c \
	    src/metrics.c \
	    -o results/test_metrics $(LDFLAGS)
	./results/test_metrics

test_exact:
	$(CC) $(CFLAGS) -DEXACT_TEST \
	    src/task.c src/fairness.c \
	    src/metrics.c src/cfs_exact.c \
	    -o results/test_exact $(LDFLAGS)
	./results/test_exact

test_bsa:
	$(CC) $(CFLAGS) -DBSA_TEST \
	    src/task.c src/fairness.c \
	    src/metrics.c src/cfs_exact.c \
	    src/approx_bsa.c \
	    -o results/test_bsa $(LDFLAGS)
	./results/test_bsa

test_clti:
	$(CC) $(CFLAGS) -DCLTI_TEST \
	    src/task.c src/fairness.c \
	    src/metrics.c src/cfs_exact.c \
	    src/approx_bsa.c src/approx_clti.c \
	    -o results/test_clti $(LDFLAGS)
	./results/test_clti

test_apaf:
	$(CC) $(CFLAGS) -DAPAF_TEST \
	    src/task.c src/fairness.c \
	    src/metrics.c src/cfs_exact.c \
	    src/approx_bsa.c src/approx_clti.c \
	    src/approx_apaf.c \
	    -o results/test_apaf $(LDFLAGS)
	./results/test_apaf

test_all: test_task test_fairness \
          test_metrics test_exact \
          test_bsa test_clti test_apaf
	@echo "=== ALL UNIT TESTS PASSED ==="

run_all: all
	@echo "--- Running Experiment 1 ---"
	./$(EXP1_BIN)
	@echo "--- Running Experiment 2 ---"
	./$(EXP2_BIN)
	@echo "--- Running Experiment 3 ---"
	./$(EXP3_BIN)
	@echo "--- Running Experiment 4 ---"
	./$(EXP4_BIN)
	@echo "=== ALL EXPERIMENTS COMPLETE ==="
	@echo "Results in: results/"

clean:
	rm -f results/exp1 results/exp2 \
	      results/exp3 results/exp4 \
	      results/test_task \
	      results/test_fairness \
	      results/test_metrics \
	      results/test_exact \
	      results/test_bsa \
	      results/test_clti \
	      results/test_apaf \
	      results/*.csv

help:
	@echo "APEX-CFS Build System"
	@echo "====================="
	@echo "make          - Build all experiments"
	@echo "make run_all  - Build and run all"
	@echo "make test_all - Run all unit tests"
	@echo "make test_task     - Test task.c"
	@echo "make test_fairness - Test fairness.c"
	@echo "make test_metrics  - Test metrics.c"
	@echo "make test_exact    - Test cfs_exact.c"
	@echo "make test_bsa      - Test approx_bsa.c"
	@echo "make test_clti     - Test approx_clti.c"
	@echo "make test_apaf     - Test approx_apaf.c"
	@echo "make clean         - Remove binaries"
	@echo ""
	@echo "Experiments:"
	@echo "  results/exp1 - Equal weight tasks"
	@echo "  results/exp2 - Mixed workload"
	@echo "  results/exp3 - Dynamic spike (APAF)"
	@echo "  results/exp4 - Error bound verify"

.PHONY: all clean help run_all test_all \
	exp1 \
	exp2 \
        test_task test_fairness \
        test_metrics test_exact \
        test_bsa test_clti test_apaf
