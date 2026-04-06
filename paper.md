---
title: 'SLK: Simplex Logical Kernel — A Proven-Sound Axiomatic Constraint Validator for Autonomous Agents on Constrained Hardware'
tags:
  - C99
  - embedded systems
  - formal methods
  - constraint validation
  - autonomous agents
  - ARM
  - zero dynamic allocation
authors:
  - name: Emile Fanel Gonkol
    orcid: 0009-0004-3911-0300
    affiliation: 1
affiliations:
  - name: Independent Researcher, Brazzaville, Republic of Congo
    index: 1
date: 31 March 2026
bibliography: paper.bib
---

# Summary

The Simplex Logical Kernel (SLK) is an axiomatic constraint validator implemented in 397 lines of ISO C99 with zero dynamic memory allocation. SLK validates proposed knowledge insertions against 16 universal axioms partitioned into five families: structural coherence, temporal causality, graph topology, security and integrity, and information conservation. The kernel binary occupies 2,360 bytes on x86-64 Linux and 2,314 bytes on ARM Cortex-A55 Android — both measured. SLK executes a single validation call in 0.034 µs on x86-64 and 0.065 µs on ARM Cortex-A55, achieving 29.27 and 15.40 million validations per second respectively.

SLK is not an inference engine: it does not perform forward/backward chaining, unification, or resolution. It validates that proposed simplex insertions satisfy all 16 constraints simultaneously before accepting them into the knowledge base K. This deterministic, formally specified validation layer is designed for deployment on constrained hardware — microcontrollers, ARM IoT devices, and smartphones — without OS dependency or Docker sandboxing.

# Statement of Need

Autonomous AI agents deployed in safety-critical contexts — medical devices, industrial control, autonomous vehicles — lack formal mechanisms to guarantee that their actions satisfy invariant safety rules. Existing sandboxing approaches (Docker, VMs) address deployment isolation but not action-level safety, and impose memory footprints of 500MB to 2GB incompatible with constrained hardware.

SLK addresses this gap by providing a sub-3KB, zero-allocation constraint validation kernel that can run bare-metal on ARM without an operating system. The 16 axioms are placed in read-only memory (Axiom A4.1), making them structurally immutable at runtime even if the calling application layer is compromised. Domain-specific safety rules are injected as pluggable K-Vaults without modifying the kernel binary.

# Validation

Theorem 1 (Validation Correctness) establishes that `slk_validate()` faithfully implements the formal validation function V: the kernel accepts a simplex if and only if it satisfies all 16 axioms simultaneously. This is established by a paper proof strategy and corroborated by:

- 39 unit tests exercising each axiom independently with positive and negative cases (all passing on x86-64 Linux, Windows, and ARM Cortex-A55 Android)
- Zero implementation errors across 956,782 validation calls on the complete ProofWriter train split [@tafjord2021proofwriter] (85,468 instances, maxD=0 to maxD=9)

Proposition 1 establishes that the 16 axioms are mutually non-redundant: removing any single axiom creates a class of structurally invalid simplexes that V would incorrectly accept.

# Performance

| Platform | Binary size | Latency | Throughput | Dynamic allocation |
|----------|------------|---------|------------|-------------------|
| x86-64 Linux (GCC 12, -O2) | 2,360 bytes | 0.034 µs | 29.27 M/s | 0 bytes |
| ARM Cortex-A55 Android (clang, Termux) | 2,314 bytes | 0.065 µs | 15.40 M/s | 0 bytes |

All measurements performed on physical hardware. ARM measurements on Tecno Spark Go 2020 (ARM Cortex-A55 octa-core) via Termux.

# Acknowledgements

The author thanks Peter Clark (Allen AI) for endorsement and feedback on the formal foundations of this work.

# References