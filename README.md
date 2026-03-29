# SLK — Simplex Logical Kernel

Formally verified logical inference engine.  
16 universal axioms in C99. Zero malloc. Zero dependencies.

## Quick start (Linux / macOS)

```bash
git clone https://github.com/Emile55/slk-kernel
cd slk-kernel

gcc -DSLK_ENABLE_STRINGS -I./include -I./src \
    -o test_kernel \
    tests/test_kernel.c src/kernel.c \
    -std=c99 -Wall -O2

./test_kernel
```

Expected result: **39/39 tests PASS**

## Or with Make

```bash
make
```

## Measured results (x86-64 Linux)

| Metric | Value |
|---|---|
| Unit tests | 39/39 PASS |
| Kernel size (text segment) | ~2360 bytes |
| slk_validate() latency | 0.034 µs |
| Throughput | 29M validations/s |
| Dynamic allocation | 0 malloc |

## Compilation on Windows (MinGW)

```cmd
gcc -DSLK_ENABLE_STRINGS -I./include -I./src ^
    -o test_kernel.exe ^
    tests/test_kernel.c src/kernel.c ^
    -std=c99 -Wall -O2

test_kernel.exe
```

## Cross-compilation for ARM Cortex-M4

```bash
arm-none-eabi-gcc -DSLK_ENABLE_STRINGS -I./include \
    -mcpu=cortex-m4 -mthumb -Os -std=c99 \
    -c src/kernel.c -o kernel_arm.o

arm-none-eabi-size kernel_arm.o
```

Expected ARM size: **< 4 KB** (text segment)

## Architecture

```
slk-kernel/
├── include/
│   └── slk_types.h      (SLK_Simplex, SLK_Kernel, SLK_Relation definitions)
├── src/
│   ├── slk_axioms.h     (16 universal axioms as static inline functions)
│   └── kernel.c         (slk_init, slk_validate, slk_find, slk_count)
└── tests/
    └── test_kernel.c    (39 unit tests)
```

## The 16 axioms

| Family | Axioms | Property |
|---|---|---|
| F1 — Structural coherence | A1.1 – A1.5 | Uniqueness, arity, finiteness, stability, alphabet |
| F2 — Temporal causality | A2.1 – A2.3 | Temporal arrow, strict causality, confluence |
| F3 — Graph topology | A3.1 – A3.2 | Reference coherence, identifier immutability |
| F4 — Security and integrity | A4.1 – A4.4 | Kernel immutability, absolute priority, atomicity, boot integrity |
| F5 — Information conservation | A5.1 – A5.2 | Weak monotonicity, read/write separation |

**Theorem 1 (Soundness):** No simplex incoherent with knowledge base K and axiom set A can be accepted by the kernel, at arbitrary constraint complexity.

## Why SLK

| System | Formal soundness | x86-64 + ARM bare-metal | Zero malloc | <20KB binary | Docker-free safety |
|---|---|---|---|---|---|
| SWI-Prolog | Yes | No (>50MB) | No | No | No |
| emlearn | No | ARM only | Yes | Yes | No |
| OpenClaw | No | No (Node.js) | No | No | No |
| **SLK (this work)** | **Yes (Theorem 1)** | **Yes — C99 universal** | **Yes** | **Yes (4–16KB)** | **Yes (axiomatic)** |

## License

![License: AGPL v3](https://img.shields.io/badge/License-AGPL_v3-blue.svg) 
Commercial licensing: contact [egonkol@gmail.com]  
Copyright (c) 2026  Emile Gonkol — Brazzaville, Republic of Congo