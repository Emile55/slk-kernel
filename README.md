# SLK — Simplex Logical Kernel

**A Proven-Sound Axiomatic Constraint Validator for Autonomous Agents on Constrained Hardware**

[![License: AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](LICENSE)
[![HAL](https://img.shields.io/badge/HAL-hal--05573274-green.svg)](https://hal.science/hal-05573274)
[![Platform](https://img.shields.io/badge/platform-x86--64%20%7C%20ARM%20Cortex--A55-lightgrey.svg)]()

## What is SLK?

SLK is an axiomatic constraint validator implemented in **397 lines of ISO C99** with **zero dynamic memory allocation**. It validates proposed knowledge insertions against 16 universal axioms before accepting them into a knowledge base K.

SLK is **not** an inference engine — it does not perform chaining, unification, or resolution. It is a validation kernel: deterministic, formally specified, and portable to any C99 platform without OS dependency.

## Key Numbers (all measured)

| Metric | x86-64 Linux | ARM Cortex-A55 Android |
|--------|-------------|------------------------|
| Binary size | **2,360 bytes** | **2,314 bytes** |
| Latency per call | **0.034 µs** | **0.065 µs** |
| Throughput | **29.27 M val/s** | **15.40 M val/s** |
| Dynamic allocation | **0 bytes** | **0 bytes** |
| Unit tests | **39 / 39** | **39 / 39** |

**ProofWriter validation (train split, 85,468 instances, maxD=0 to maxD=9):**
- 956,782 validation calls
- 0 implementation errors
- Theorem 1 holds at all depth levels

## Why SLK?

| System | Scope | Proof type | x86+ARM | Zero malloc | <20KB |
|--------|-------|-----------|---------|-------------|-------|
| microKanren | Relational logic | Yes (Coq, mechanized) | No | No | Yes |
| seL4 | OS kernel | Yes (Isabelle, mechanized) | No | No | No |
| emlearn | Statistical ML | None | ARM only | Yes | Yes |
| TFLite Micro | NN inference | None | ARM only | Partial | Partial |
| LINC + Prover9 | LLM reasoning | Yes (paper) | No | No | No |
| **SLK (this work)** | **Constraint validation** | **Yes (paper proof)** | **Yes — C99** | **Yes** | **Yes (2.3KB)** |

Note: microKanren and seL4 have mechanized proofs — a stronger guarantee than SLK's current paper proof. Coq mechanization of Theorem 1 is planned future work.

## The 16 Axioms

| Family | Axioms | Guarantee |
|--------|--------|-----------|
| F1 — Structural | A1.1 Uniqueness, A1.2 Arity, A1.3 Finiteness, A1.4 Stability, A1.5 Alphabet | No duplicate ids, valid arities |
| F2 — Temporal | A2.1 Arrow, A2.2 Causality, A2.3 Confluence | Monotone clock, deterministic transitions |
| F3 — Topology | A3.1 Reference coherence, A3.2 Id immutability | No dangling references |
| F4 — Security | A4.1 Kernel immutability, A4.2 Priority, A4.3 Atomicity, A4.4 Boot integrity | Axioms in .rodata, unconditional blocking |
| F5 — Conservation | A5.1 Monotonicity, A5.2 R/W separation | No spontaneous deletion |

## Theorem 1 (Validation Correctness)

> The C99 implementation of `slk_validate()` faithfully implements the validation function V: for all inputs (S, K), `slk_validate(S, K)` returns 1 if and only if V(S, K, A) = 1.

**Proof strategy:** structural argument (code inspection) + 39 unit tests + 956,782 ProofWriter validation calls with zero errors. Mechanized proof in Coq is planned future work.

## Quick Start

```bash
git clone https://github.com/Emile55/slk-kernel
cd slk-kernel

# Linux / macOS
gcc -DSLK_ENABLE_STRINGS -I./include -I./src -O2 \
    tests/test_kernel.c src/kernel.c -o test_kernel
./test_kernel

# Windows (MinGW)
gcc -DSLK_ENABLE_STRINGS -I./include -I./src -O2 \
    tests/test_kernel.c src/kernel.c -o test_kernel.exe
./test_kernel.exe

# ARM Android (Termux)
clang -DSLK_ENABLE_STRINGS -I./include -I./src -O2 \
    tests/test_kernel.c src/kernel.c -o test_kernel
./test_kernel
```

Expected output: `39 Tests passed, 0 Tests failed`

## ProofWriter Benchmark

```bash
cd benchmarks
pip install datasets
python proofwriter_bench.py
```

Runs on the complete ProofWriter train split (85,468 instances).

## Repository Structure

```
slk-kernel/
├── src/
│   ├── kernel.c          # 397 lines — main validation logic
│   └── slk_axioms.h      # 16 axiom functions (static inline C)
├── include/
│   └── slk_types.h       # Type definitions
├── tests/
│   └── test_kernel.c     # 39 unit tests
├── benchmarks/
│   ├── proofwriter_bench.py   # ProofWriter benchmark
│   └── slk_bridge.py          # Python ctypes bridge
├── paper.md              # JOSS paper
├── paper.bib             # References
└── Makefile
```


## License

- **Kernel source code:** [AGPL-3.0](LICENSE)
- **Paper (paper.md, paper.bib):** [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)

## Paper
Preprint, 2026. DOI: 10.5281/zenodo.19425043


## Author

**Emile Fanel Gonkol**
Independent Researcher — Brazzaville, Republic of Congo
egonkol@gmail.com
ORCID: [0009-0004-3911-0300](https://orcid.org/0009-0004-3911-0300)