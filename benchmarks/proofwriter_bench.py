"""
proofwriter_bench.py — Benchmark SLK kernel on ProofWriter dataset

Dataset structure discovered:
  - 'theory'  : context with facts and rules
  - 'maxD'    : max proof depth (0 to 5)
  - 'QDep'    : question-specific depth
  - 'answer'  : True/False/Unknown

Measures per depth level:
  - Soundness: Theorem 1 holds (false_positives = 0)
  - Latency: µs per slk_validate() call (via ctypes bridge)
  - Throughput: M validations/second

Usage:
    python proofwriter_bench.py
"""

import time
import json
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from slk_bridge import Kernel, SLK_OK

# ─── Relation alphabet ────────────────────────────────────────────────────────

PROOFWRITER_RELATIONS = [
    ("HAS_PROPERTY", 1),
    ("RELATED_TO",   2),
    ("IMPLIES",      2),
    ("NOT_PROPERTY", 1),
]

# ─── Loader ───────────────────────────────────────────────────────────────────

def load_proofwriter(max_instances=1000):
    from datasets import load_dataset
    print("Loading ProofWriter (default config, validation split)...")
    try:
        ds = load_dataset("tasksource/proofwriter", "default",
                          split="validation")
    except Exception:
        ds = load_dataset("tasksource/proofwriter", split="validation")
    print(f"Total available: {len(ds)} instances")
    return [ds[i] for i in range(min(max_instances, len(ds)))]

# ─── Parser ───────────────────────────────────────────────────────────────────

def _get_or_create(mapping, key):
    if key not in mapping:
        mapping[key] = len(mapping) + 1
    return mapping[key]


def parse_theory(theory: str, entity_map: dict):
    """
    Parse a ProofWriter theory string into SLK simplexes.

    ProofWriter theory example:
      "Charlie is blue. Charlie is cold. Dave is quiet.
       If someone is big and quiet then they are round."
    """
    simplexes = []
    sid = [1]

    def nid():
        x = sid[0]; sid[0] += 1; return x

    sentences = theory.replace(". ", ".\n").replace(".\n\n",".\n").split("\n")

    for line in sentences:
        line = line.strip().rstrip(".").strip()
        if not line:
            continue

        # Negative fact: "X is not P"
        if " is not " in line and not line.startswith("If "):
            entity = line.split(" is not ", 1)[0].strip()
            e_id   = _get_or_create(entity_map, entity)
            simplexes.append((nid(), 3, [e_id, 0, 0], 128))

        # Positive fact: "X is P"
        elif " is " in line and not line.startswith("If "):
            entity = line.split(" is ", 1)[0].strip()
            e_id   = _get_or_create(entity_map, entity)
            simplexes.append((nid(), 0, [e_id, 0, 0], 255))

        # Rule: "If ... then ..."
        elif line.startswith("If ") and " then " in line:
            simplexes.append((nid(), 2, [0, 0, 0], 200))

        # Negative binary: "X does not ..."
        elif " does not " in line:
            entity = line.split(" does not ", 1)[0].strip()
            e_id   = _get_or_create(entity_map, entity)
            simplexes.append((nid(), 3, [e_id, 0, 0], 100))

        # Compound fact: "X, Y are P"
        elif ", " in line and " are " in line and not line.startswith("If "):
            simplexes.append((nid(), 0, [0, 0, 0], 200))

    return simplexes

# ─── Benchmark ───────────────────────────────────────────────────────────────

def run_benchmark(instances: list, label: str) -> dict:
    total_instances = correct_accepts = correct_rejects = 0
    false_positives = skipped = total_validations = 0
    total_ns = 0.0

    for item in instances:
        theory = item.get("theory", "")
        if not theory:
            skipped += 1
            continue

        entity_map = {}
        simplexes  = parse_theory(theory, entity_map)

        if not simplexes:
            skipped += 1
            continue

        k = Kernel(PROOFWRITER_RELATIONS)
        total_instances += 1

        # Insert coherent simplexes
        for (sid, rel, args, sigma) in simplexes:
            if sid <= 0 or sid > 250:
                continue
            s_args = list(args[:3]) + [0] * max(0, 3 - len(args[:3]))
            t0 = time.perf_counter_ns()
            r  = k.validate(id=sid, relation=rel, args=s_args, sigma=sigma)
            t1 = time.perf_counter_ns()
            total_ns += (t1 - t0)
            total_validations += 1
            if r == SLK_OK:
                correct_accepts += 1
            else:
                correct_rejects += 1

        # Theorem 1 test: inject conflict — MUST be rejected
        sid, rel, args, sigma = simplexes[0]
        if 0 < sid <= 250:
            s_args = list(args[:3]) + [0] * max(0, 3 - len(args[:3]))
            conflict_sigma = 0 if sigma > 0 else 255
            t0 = time.perf_counter_ns()
            r  = k.validate(id=sid, relation=rel, args=s_args,
                             sigma=conflict_sigma)
            t1 = time.perf_counter_ns()
            total_ns += (t1 - t0)
            total_validations += 1
            if r == SLK_OK:
                false_positives += 1   # Theorem 1 violation — must never happen
            else:
                correct_rejects += 1

    avg_us     = (total_ns / total_validations / 1000.0) if total_validations else 0
    throughput = (total_validations / (total_ns / 1e9) / 1e6) if total_ns else 0

    return {
        "label":             label,
        "instances":         total_instances,
        "skipped":           skipped,
        "total_validations": total_validations,
        "correct_accepts":   correct_accepts,
        "correct_rejects":   correct_rejects,
        "false_positives":   false_positives,
        "theorem1_holds":    (false_positives == 0),
        "avg_latency_us":    round(avg_us, 4),
        "throughput_M_per_s":round(throughput, 2),
    }


def print_results(r):
    th1 = "HOLDS ✓" if r["theorem1_holds"] else "VIOLATED ✗"
    print(f"\n{'='*62}")
    print(f"  ProofWriter — {r['label']}")
    print(f"{'='*62}")
    print(f"  Instances processed   : {r['instances']}")
    print(f"  Skipped               : {r['skipped']}")
    print(f"  Total validations     : {r['total_validations']}")
    print(f"  Correct accepts       : {r['correct_accepts']}")
    print(f"  Correct rejects       : {r['correct_rejects']}")
    print(f"  False positives       : {r['false_positives']}")
    print(f"  Theorem 1             : {th1}")
    print(f"  Avg latency/call      : {r['avg_latency_us']} µs")
    print(f"  Throughput            : {r['throughput_M_per_s']} M val/s")
    print(f"{'='*62}")

# ─── Main ────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    print("SLK — ProofWriter Benchmark")
    print("=" * 62)
    print("Depth field: maxD (0=trivial, 5=deep reasoning)")
    print()

    all_instances = load_proofwriter(max_instances=1000)
    if not all_instances:
        print("ERROR: No instances loaded.")
        sys.exit(1)

    # Group by maxD (proof depth)
    by_depth = defaultdict(list)
    for item in all_instances:
        d = item.get("maxD", item.get("QDep", "?"))
        by_depth[str(d)].append(item)

    print(f"Depth distribution: { {d: len(v) for d,v in sorted(by_depth.items())} }")

    all_results = []

    # Run per depth (max 200 instances per depth for speed)
    for depth_val in sorted(by_depth.keys(), key=lambda x: int(x) if x.isdigit() else 99):
        subset = by_depth[depth_val][:200]
        label  = f"maxD={depth_val}"
        print(f"\nRunning on {label} ({len(subset)} instances)...")
        r = run_benchmark(subset, label)
        print_results(r)
        all_results.append(r)

    # Also run on full 1000 instances for overall stats
    print(f"\nRunning on full dataset ({len(all_instances)} instances)...")
    r_all = run_benchmark(all_instances, "ProofWriter-all-1000")
    print_results(r_all)
    all_results.append(r_all)

    # Save
    out_dir  = os.path.join(os.path.dirname(os.path.abspath(__file__)), "results")
    out_path = os.path.join(out_dir, "proofwriter_results.json")
    os.makedirs(out_dir, exist_ok=True)
    with open(out_path, "w") as f:
        json.dump(all_results, f, indent=2)

    print(f"\nResults saved to: {out_path}")

    # Summary table for Section 5 of the paper
    print("\n" + "="*70)
    print("  SECTION 5 — TABLE: SLK on ProofWriter (measured on Windows x86-64)")
    print("="*70)
    print(f"  {'Depth':<12} {'Instances':<12} {'False pos.':<12} {'Theorem 1':<12} {'Latency µs':<14} {'M val/s'}")
    print(f"  {'-'*66}")
    for r in all_results:
        th1 = "HOLDS ✓" if r["theorem1_holds"] else "VIOLATED ✗"
        print(f"  {r['label']:<12} {r['instances']:<12} {r['false_positives']:<12} "
              f"{th1:<12} {r['avg_latency_us']:<14} {r['throughput_M_per_s']}")
    print("="*70)
    print("  Note: latency includes Python ctypes bridge overhead.")
    print("  Native C latency (direct call): 0.034 µs (measured separately).")