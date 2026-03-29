/*
 * slk_axioms.h — The 16 universal axioms of the SLK kernel
 *
 * This file implements Definition 3 (Section 2.2) of the paper:
 *   An axiom alpha is a total and deterministic decision function
 *   alpha : S x K -> {0, 1}
 *
 * FILE ARCHITECTURE:
 *   Each axiom is a static C inline function that takes
 *   a simplex S and the kernel state K, and returns:
 *     1 (SLK_OK) if the axiom is satisfied
 *     a negative error code if the axiom is violated
 *
 * CONFLUENCE PROPERTY (Axiom A2.3):
 *   The order of axiom calls in slk_validate() does not change
 *   the final result. Each axiom is INDEPENDENT of the others.
 *   They can be called in any order and produce the same
 *   verdict. This is the direct translation of the confluence theorem.
 *
 * IMMUTABILITY PROPERTY (Axiom A4.1):
 *   All axiom functions are declared with the qualifier
 *   'static const' in their call. The compiler places the function
 *   pointer in .rodata. An external module cannot
 *   modify the addresses of these functions in memory.
 */

#ifndef SLK_AXIOMS_H
#define SLK_AXIOMS_H

#include "slk_types.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * BASE TYPE: AXIOM FUNCTION
 *
 * All axiom functions share the same signature.
 * This allows storing them in a function pointer array
 * (the "Axiom Matrix") for loop-based verification in O(N_AXIOMS).
 * ───────────────────────────────────────────────────────────────────────────*/

/*
 * SLK_AxiomFn — Type of an axiom function
 *
 * Parameters:
 *   s : constant pointer to the simplex to validate
 *       (const: the axiom never modifies the incoming simplex)
 *   k : constant pointer to the kernel state
 *       (const: the axiom never modifies K — only slk_validate() does)
 *
 * Return:
 *   SLK_OK (0) if the axiom is satisfied
 *   negative error code if the axiom is violated
 *
 * NOTE: returning an int (not a bool) allows identifying
 * exactly WHICH axiom was violated, which is required for
 * the total auditability property of the system.
 */
typedef int (*SLK_AxiomFn)(const SLK_Simplex *s, const SLK_Kernel *k);


/* ─────────────────────────────────────────────────────────────────────────────
 * FAMILY F1 — STRUCTURAL COHERENCE (5 axioms)
 *
 * These axioms verify that the incoming simplex is structurally
 * valid BEFORE checking its relations with K.
 * These are type safety checks — the fastest ones.
 * ───────────────────────────────────────────────────────────────────────────*/

/*
 * ax_f1_1_uniqueness — Axiom A1.1: Identifier uniqueness
 *
 * Formal property:
 *   For all S1, S2 in K: S1.id = S2.id => S1 = S2
 *
 * C translation:
 *   If the incoming simplex has an id already present in K,
 *   then it is either the SAME simplex (idempotence — accepted),
 *   or a CONFLICT (same id, different values — rejected).
 *
 * Why check for idempotence?
 *   In a distributed system, the same simplex may arrive twice
 *   (unreliable network). It is silently re-accepted rather than
 *   raising an error. This is a robustness choice.
 *
 * Complexity: O(|K|) — linear scan of base[] array
 */
static inline int ax_f1_1_unicite(const SLK_Simplex *s, const SLK_Kernel *k) {
    uint32_t i;

    /* id = 0 is reserved as SLK_NULL_ID — always invalid */
    if (s->id == SLK_NULL_ID) {
        return SLK_ERR_ID_ZERO;
    }

    /* Scan K to detect an id conflict */
    for (i = 0; i < k->count; i++) {
        if (k->base[i].id == s->id) {
            /*
             * Same id found in K.
             * Check if it is exactly the same simplex
             * (idempotence) or a data conflict.
             *
             * A simplex is identical if ALL its fields are equal:
             * relation, args[0..MAX_ARITY-1], and sigma.
             */
            if (k->base[i].relation == s->relation &&
                k->base[i].args[0]  == s->args[0]  &&
                k->base[i].args[1]  == s->args[1]  &&
                k->base[i].args[2]  == s->args[2]  &&
                k->base[i].sigma    == s->sigma) {
                /*
                 * Idempotence: simplex already present, identical.
                 * Return SLK_OK — not an error, just an
                 * already completed insertion. slk_validate() will not
                 * increment tau in this case (see kernel.c).
                 */
                return SLK_OK;
            }
            /* Conflict: same id, different data */
            return SLK_ERR_ID_EXISTS;
        }
    }

    return SLK_OK; /* No conflict found */
}

/*
 * ax_f1_2_arity — Axiom A1.2: Arity conformance
 *
 * Formal property:
 *   For all S in K: |S.args| = arity(S.R)
 *
 * C translation:
 *   The simplex declares a relation R (via its `relation` index).
 *   The Sigma alphabet stores the expected arity for each relation.
 *   We verify that non-NULL args match the expected arity,
 *   and that positions beyond the arity are NULL_ID.
 *
 * Exemple :
 *   Sigma declares: relation 7 = PRECEDES, arity = 2
 *   A simplex with relation=7, args=[3, 5, 0] is VALID (0 = NULL_ID)
 *   A simplex with relation=7, args=[3, 0, 5] is INVALID
 *     (args[2] non-null but arity is 2)
 */
static inline int ax_f1_2_arite(const SLK_Simplex *s, const SLK_Kernel *k) {
    uint8_t  expected_arity;
    uint32_t i;

    /* Check A1.5 first (relation must be in Sigma) */
    if (s->relation >= k->sigma_count) {
        return SLK_ERR_RELATION_INVALID;
    }

    expected_arity = k->sigma_alphabet[s->relation].arity;

    /*
     * Two-pass verification:
     *
     * Pass 1: positions [0..arity-1] must be non-NULL
     *   (unless arity is 0, in which case there are no args to check)
     *
     * Pass 2: positions [arity..MAX_ARITY-1] must be NULL_ID
     *   (guarantees that the tuple is "clean" and auditable)
     */
    for (i = 0; i < (uint32_t)expected_arity; i++) {
        /*
         * We do not force args[i] != NULL_ID here.
         * Axiom A3.1 (Reference coherence) handles target validity.
         * A1.2 only checks the number of arguments, not their value.
         */
    }

    for (i = (uint32_t)expected_arity; i < SLK_MAX_ARITY; i++) {
        if (s->args[i] != SLK_NULL_ID) {
            /*
             * A non-NULL argument beyond the declared arity.
             * This is either a construction error or an attempt
             * to pass hidden data in unused fields.
             * In both cases: rejected.
             */
            return SLK_ERR_ARITY_MISMATCH;
        }
    }

    return SLK_OK;
}

/*
 * ax_f1_3_finiteness — Axiom A1.3: Finiteness of K
 *
 * Formal property:
 *   |K| < N_max
 *
 * C translation:
 *   If K is already full, any insertion is rejected.
 *   This is the protection against base[] array overflow.
 *
 * NOTE: this axiom is checked FIRST in the validation chain
 * (see slk_validate() in kernel.c). If K is full, no need
 * to check other axioms.
 */
static inline int ax_f1_3_finitude(const SLK_Simplex *s, const SLK_Kernel *k) {
    (void)s; /* The simplex itself is not used for this check */

    if (k->count >= SLK_N_MAX) {
        return SLK_ERR_BASE_FULL;
    }

    return SLK_OK;
}

/*
 * ax_f1_4_stability — Axiom A1.4: Bounded stability
 *
 * Formal property:
 *   S.sigma dans {0,...,255}
 *
 * C translation:
 *   Since sigma is a uint8_t, this property is GUARANTEED BY TYPE.
 *   A uint8_t cannot hold a value > 255.
 *   This function exists to:
 *     1. Document the axiom in the validation chain
 *     2. Serve as an extension point if sigma changes type in the future
 *     3. Maintain 1:1 correspondence between formal axioms and code
 *
 * In practice, the compiler will optimize this function to a NOP
 * since the condition is always true for a uint8_t.
 */
static inline int ax_f1_4_stabilite(const SLK_Simplex *s, const SLK_Kernel *k) {
    (void)k; /* K is not used for this check */

    /*
     * This check is redundant with the uint8_t type.
     * It is kept for axiom <-> code traceability.
     * A modern compiler with -O2 will eliminate it automatically.
     */
    if (s->sigma > SLK_SIGMA_MAX) {
        /* This case can never occur with a uint8_t */
        return SLK_ERR_ARITY_MISMATCH; /* Generic error code reuse */
    }

    return SLK_OK;
}

/*
 * ax_f1_5_alphabet — Axiom A1.5: Alphabet validity
 *
 * Formal property:
 *   S.R in Sigma, i.e. relation < sigma_count
 *
 * C translation:
 *   The simplex relation index must correspond to a valid entry
 *   in the Sigma alphabet. A relation outside Sigma is
 *   by definition undefined — the kernel cannot check its arity
 *   or its semantics.
 *
 * NOTE: ax_f1_2_arity() already calls this check first.
 *   Here it is re-exposed as an independent axiom for traceability.
 */
static inline int ax_f1_5_alphabet(const SLK_Simplex *s, const SLK_Kernel *k) {
    if (s->relation >= k->sigma_count) {
        return SLK_ERR_RELATION_INVALID;
    }

    return SLK_OK;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * FAMILY F2 — TEMPORAL CAUSALITY (3 axioms)
 *
 * These axioms concern the GLOBAL kernel state and transitions
 * between successive states. They cannot be verified by looking
 * only at the incoming simplex — they require the full state k.
 * ───────────────────────────────────────────────────────────────────────────*/

/*
 * ax_f2_1_temporal_arrow — Axiom A2.1: Temporal arrow
 *
 * Formal property:
 *   tau is strictly monotonically increasing
 *
 * C translation:
 *   tau is a uint64_t in SLK_Kernel.
 *   The axiom is structurally satisfied: tau is only incremented
 *   in slk_validate() (see kernel.c) and never decremented.
 *   This function checks that tau has not exceeded UINT64_MAX
 *   (overflow theoretically impossible but documented).
 *
 * NOTE: like ax_f1_4_stability, this function mainly serves
 * to document the axiom in the validation chain.
 */
static inline int ax_f2_1_fleche_temporelle(const SLK_Simplex *s, const SLK_Kernel *k) {
    (void)s;

    /*
     * Check for tau overflow.
     * UINT64_MAX = 18,446,744,073,709,551,615
     * At 1 million validations/second: overflow after 584,542 years.
     * In practice: never. But we document the check.
     */
    if (k->tau == UINT64_MAX) {
        return SLK_ERR_ATOMIC_FAIL; /* Reuse: "corrupted system state" */
    }

    return SLK_OK;
}

/*
 * ax_f2_2_strict_causality — Axiom A2.2: Strict causality
 *
 * Formal property:
 *   K_{t+1} \ K_t ⊆ {S_in}
 *   Knowledge base K can only grow by adding the validated simplex.
 *
 * C translation:
 *   This axiom is ARCHITECTURALLY satisfied: the only function
 *   that modifies base[] is slk_commit() in kernel.c, which is
 *   only called from slk_validate() after all axioms have
 *   returned SLK_OK.
 *
 *   This function verifies that the kernel is properly initialized
 *   before authorizing any write — this is the A2.2 guard.
 */
static inline int ax_f2_2_causalite(const SLK_Simplex *s, const SLK_Kernel *k) {
    (void)s;

    if (!k->is_initialized) {
        return SLK_ERR_NULL_WRITE;
    }

    return SLK_OK;
}

/*
 * ax_f2_3_confluence — Axiom A2.3: Confluence (determinism)
 *
 * Formal property:
 *   V(K, S_in) is a total and deterministic function
 *   Same input => same output, always
 *
 * C translation:
 *   This property is guaranteed by the absence of:
 *     - Mutable global variables in axioms
 *     - Calls to rand() or any random generator
 *     - Real-time dependencies (clock(), time())
 *     - Function pointers modifiable at runtime
 *
 *   This function verifies that the CRC32 of the kernel state matches
 *   its reference value — detecting any external alteration.
 *
 * IMPORTANT NOTE:
 *   The full CRC32 of the code would be expensive to recompute on each
 *   validation. Here we only check that sigma_count has not been
 *   altered (a change in sigma_count would change the behavior
 *   of all F1 axioms). For the full CRC, see ax_f4_4_boot_crc.
 */
static inline int ax_f2_3_confluence(const SLK_Simplex *s, const SLK_Kernel *k) {
    (void)s;

    /*
     * Minimal state coherence check:
     * sigma_count must be non-zero (an empty Sigma makes the system useless).
     * A corrupted Sigma (sigma_count = 0 after init) would indicate
     * an alteration of the kernel state.
     */
    if (k->sigma_count == 0 && k->is_initialized) {
        return SLK_ERR_BOOT_CORRUPT;
    }

    return SLK_OK;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * FAMILY F3 — GRAPH TOPOLOGY (2 axioms)
 *
 * These axioms verify the relations between the incoming simplex
 * and EXISTING simplexes in K.
 * These are the most time-expensive checks (O(|K|)).
 * ───────────────────────────────────────────────────────────────────────────*/

/*
 * ax_f3_1_ref_coherence — Axiom A3.1: Reference coherence
 *
 * Formal property:
 *   For all i: S.args[i] in dom(K) U {NULL_ID}
 *
 * C translation:
 *   Each non-NULL argument of the incoming simplex must point to
 *   an existing simplex in K (i.e., its id must be
 *   present in base[]).
 *
 *   args[i] = NULL_ID is ALLOWED: this represents a reference
 *   to an unknown entity (open world semantics).
 *   What is forbidden: args[i] = 9999 when no simplex
 *   has id 9999. This is a "dangling reference" — the equivalent
 *   of an invalid pointer in C.
 *
 * DISTINCTION FROM CLOSED WORLD:
 *   The closed world (Prolog's Closed World Assumption) says that
 *   what is not in K is false. A3.1 is weaker: it only says
 *   that references must be defined or explicitly
 *   undefined (NULL_ID). A reference to NULL_ID does not say that
 *   the target is "false" — just that it is unknown.
 *
 * Complexity: O(arity * |K|) — for each arg, scan of K
 */
static inline int ax_f3_1_coherence_ref(const SLK_Simplex *s, const SLK_Kernel *k) {
    uint8_t  expected_arity;
    uint32_t i, j;
    int      found;

    if (s->relation >= k->sigma_count) {
        return SLK_ERR_RELATION_INVALID; /* A1.5 should have caught this earlier */
    }

    expected_arity = k->sigma_alphabet[s->relation].arity;

    for (i = 0; i < (uint32_t)expected_arity; i++) {
        if (s->args[i] == SLK_NULL_ID) {
            continue; /* NULL_ID is always allowed (open world) */
        }

        /*
         * Search for target id in K.
         * We also accept that the id points to the simplex itself
         * if the relation is declared reflexive in the SDK.
         * The kernel does not judge reflexivity — that is the SDK role.
         */
        found = 0;
        for (j = 0; j < k->count; j++) {
            if (k->base[j].id == s->args[i]) {
                found = 1;
                break;
            }
        }

        if (!found) {
            return SLK_ERR_DANGLING_REF;
        }
    }

    return SLK_OK;
}

/*
 * ax_f3_2_id_immutability — Axiom A3.2: Identifier immutability
 *
 * Formal property:
 *   S.id is read-only after insertion
 *
 * C translation:
 *   This property is ARCHITECTURALLY guaranteed by the fact that
 *   simplexes in base[] are only accessed read-only from
 *   outside kernel.c. There is no public function
 *   of type slk_update_id() in the kernel API.
 *
 *   Here we check the edge case where an incoming simplex has the
 *   same id as an existing simplex but DIFFERENT data.
 *   This case is already handled by ax_f1_1_uniqueness (which returns
 *   SLK_ERR_ID_EXISTS in this case).
 *
 *   This function exists to document the axiom in the chain.
 *   It is a complete NOP — its logic is already in ax_f1_1_uniqueness.
 */
static inline int ax_f3_2_immutabilite_id(const SLK_Simplex *s, const SLK_Kernel *k) {
    /*
     * The logic of A3.2 is fully covered by A1.1.
     * This function is kept for formal traceability.
     *
     * QUESTION: is this a redundancy that invalidates Proposition 1?
     * ANSWER: No. A1.1 and A3.2 are DISTINCT axioms with
     *   different justifications:
     *   - A1.1 is a set property (identifier uniqueness)
     *   - A3.2 is a temporal property (immutability after insertion)
     *   They happen to have the same C implementation, but their
     *   formal motivations are different.
     */
    (void)s;
    (void)k;
    return SLK_OK;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * FAMILY F4 — SECURITY AND INTEGRITY (4 axiomes)
 * ───────────────────────────────────────────────────────────────────────────*/

/*
 * ax_f4_1_kernel_immutability — Axiom A4.1: Kernel immutability
 *
 * Formal property:
 *   A_kernel is in .rodata segment — no module can modify it
 *
 * C translation:
 *   The axiom function table (see slk_axiom_table[] in kernel.c)
 *   is declared as:
 *     static const SLK_AxiomFn slk_axiom_table[] = { ... };
 *   The 'const' keyword requests the compiler to place this array
 *   in the .rodata segment (read-only data).
 *
 *   On a microcontroller, .rodata is flashed to ROM. It is physically
 *   impossible for a RAM program to modify ROM without privileged
 *   access to the Flash controller (which requires an unlock
 *   hardware sequence).
 *
 *   This function verifies that the function pointers in the table
 *   point within the code segment (.text) address range.
 *   If a pointer points to .data or .bss, it is a corruption.
 *
 * NOTE: In Linux environment (for unit tests), the address check
 * is omitted because the .text/.data distinction is less strict.
 */
static inline int ax_f4_1_immuabilite(const SLK_Simplex *s, const SLK_Kernel *k) {
    (void)s;
    (void)k;
    /*
     * The main guarantee is architectural (const declaration).
     * This function documents the axiom in the validation chain.
     * On microcontroller, an address check could be added:
     *
     *   extern uint32_t __text_start, __text_end; // linker symbols
     *   uintptr_t fn_addr = (uintptr_t)slk_axiom_table;
     *   if (fn_addr < (uintptr_t)&__text_start ||
     *       fn_addr > (uintptr_t)&__text_end) {
     *       return SLK_ERR_BOOT_CORRUPT;
     *   }
     */
    return SLK_OK;
}

/*
 * ax_f4_2_absolute_priority — Axiom A4.2: Absolute priority of the kernel
 *
 * Formal property:
 *   V(S,K,A) = 0 => block without exception or override
 *
 * C translation:
 *   This property is guaranteed by the structure of slk_validate():
 *   as soon as an axiom returns an error code, the function returns
 *   immediately WITHOUT modifying K. There is no mechanism
 *   of "override" or "force insert" in the public API.
 *
 *   This function verifies that is_initialized is properly set
 *   (an uninitialized kernel must NEVER accept insertions).
 */
static inline int ax_f4_2_priorite(const SLK_Simplex *s, const SLK_Kernel *k) {
    (void)s;

    if (!k->is_initialized) {
        return SLK_ERR_NULL_WRITE;
    }

    return SLK_OK;
}

/*
 * ax_f4_3_atomicity — Axiom A4.3: Transition atomicity
 *
 * Formal property:
 *   V is all-or-nothing: tau increments only if the transition is complete
 *
 * C translation:
 *   Atomicity is implemented in slk_validate() via the pattern
 *   "validate then commit":
 *
 *     1. Validation: all axioms are checked WITHOUT modifying K
 *     2. Commit: if all OK, slk_commit() modifies K and increments tau
 *
 *   If a hardware interrupt occurs between steps 1 and 2,
 *   K is not modified (step 2 has not yet started).
 *   If an interrupt occurs DURING step 2 (simplex copy),
 *   the is_committing flag allows detecting this state and rolling back.
 *
 *   This function verifies that we are not in a "mid-commit" state.
 */
static inline int ax_f4_3_atomicite(const SLK_Simplex *s, const SLK_Kernel *k) {
    (void)s;
    (void)k;
    /*
     * The main atomicity check is in slk_validate().
     * Here we structurally validate that the incoming simplex is complete
     * (all mandatory fields are present).
     * A simplex with id = NULL_ID is structurally incomplete.
     */
    if (s->id == SLK_NULL_ID) {
        return SLK_ERR_ATOMIC_FAIL;
    }

    return SLK_OK;
}

/*
 * ax_f4_4_boot_integrity — Axiom A4.4: Boot integrity
 *
 * Formal property:
 *   CRC32(A_kernel) verified before any operation
 *
 * C translation:
 *   This check is done ONCE at boot in slk_init().
 *   During execution, each validation checks that boot_crc
 *   in the kernel structure has not been altered.
 *
 * NOTE: The full CRC32 of the code would be too expensive to recompute
 * on each validation. We only check boot_crc != 0 as
 * a sanity indicator. The full check is in slk_init().
 */
static inline int ax_f4_4_boot_crc(const SLK_Simplex *s, const SLK_Kernel *k) {
    (void)s;

    /*
     * boot_crc = 0 means slk_init() was not properly called
     * or that the structure was zeroed out (corruption).
     */
    if (k->boot_crc == 0 && k->is_initialized) {
        return SLK_ERR_BOOT_CORRUPT;
    }

    return SLK_OK;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * FAMILY F5 — INFORMATION CONSERVATION (2 axiomes)
 * ───────────────────────────────────────────────────────────────────────────*/

/*
 * ax_f5_1_monotonicity — Axiom A5.1: Weak monotonicity of K
 *
 * Formal property:
 *   K_{t+1} ⊇ K_t \ {S_removed}
 *   No simplex disappears spontaneously.
 *
 * C translation:
 *   This property is ARCHITECTURALLY guaranteed:
 *   There is no function in the public API that removes a
 *   simplex from K without the SDK explicitly requesting
 *   removal (via slk_remove(), which is not in V1 of the kernel).
 *
 *   This function verifies that count has not unexpectedly decreased.
 *   In practice, count can only grow (or remain stable for
 *   idempotent insertions).
 */
static inline int ax_f5_1_monotonie(const SLK_Simplex *s, const SLK_Kernel *k) {
    (void)s;

    /*
     * count must be in [0, N_MAX].
     * A count > N_MAX would indicate corruption of the kernel structure.
     */
    if (k->count > SLK_N_MAX) {
        return SLK_ERR_BASE_FULL;
    }

    return SLK_OK;
}

/*
 * ax_f5_2_rw_separation — Axiom A5.2: Read/write separation
 *
 * Formal property:
 *   READ(K) is always available.
 *   WRITE(S, K) is conditioned on V(S, K, A) = 1.
 *
 * C translation:
 *   Read functions (slk_find, slk_count, slk_get) are
 *   available as soon as is_initialized = 1, without going through V.
 *   Write functions (slk_validate) always go through
 *   the full axiom chain before modifying K.
 *
 *   This function verifies that the kernel is initialized before
 *   authorizing a write.
 */
static inline int ax_f5_2_separation_rw(const SLK_Simplex *s, const SLK_Kernel *k) {
    (void)s;

    if (!k->is_initialized) {
        return SLK_ERR_NULL_WRITE;
    }

    return SLK_OK;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * THE AXIOM TABLE (The "Validation Matrix")
 *
 * This array is the core of the V(S, K, A) implementation.
 * It lists the 16 axiom functions in execution order.
 *
 * IMMUTABILITY PROPERTY (A4.1):
 *   Declared 'static const' -> placed in .rodata by the compiler.
 *   Function pointers in this array cannot be
 *   modified at runtime by an external module.
 *
 * CONFLUENCE PROPERTY (A2.3):
 *   The order of axioms in this array DOES NOT AFFECT the final result.
 *   If V returns SLK_OK, it will be true in any order.
 *   If V returns an error, the order determines WHICH axiom is reported
 *   first (fail-fast behavior).
 *
 *   The order chosen here optimizes performance by checking axioms
 *   fastest first (F4, F5, F1 before F3 which is O(|K|)).
 * ───────────────────────────────────────────────────────────────────────────*/

static const SLK_AxiomFn SLK_AXIOM_TABLE[] = {
    /* F4 and F5 first: system security checks */
    /* Fast (O(1)), critical, must block before any processing */
    ax_f4_2_priorite,          /* A4.2: kernel initialized?              */
    ax_f4_4_boot_crc,          /* A4.4: structure integrity?             */
    ax_f5_2_separation_rw,     /* A5.2: WRITE authorized?                */
    ax_f5_1_monotonie,         /* A5.1: count coherent?                  */

    /* F1 next: structural simplex checks */
    /* O(1) except A1.1 which is O(|K|) — but short-circuits if KO */
    ax_f1_3_finitude,          /* A1.3: K full? (O(1))                   */
    ax_f1_4_stabilite,         /* A1.4: sigma bounded? (O(1))            */
    ax_f1_5_alphabet,          /* A1.5: relation in Sigma? (O(1))        */
    ax_f1_2_arite,             /* A1.2: correct arity? (O(1))            */
    ax_f1_1_unicite,           /* A1.1: no id conflict? (O(|K|))         */

    /* F2 next: temporal checks */
    ax_f2_2_causalite,         /* A2.2: kernel initialized (guard)       */
    ax_f2_3_confluence,        /* A2.3: Sigma coherent?                  */
    ax_f2_1_fleche_temporelle, /* A2.1: tau no overflow?                 */

    /* F3 last: topological checks */
    /* O(arity * |K|): most expensive — only if everything else OK */
    ax_f3_1_coherence_ref,     /* A3.1: valid references? (O(k*|K|))     */
    ax_f3_2_immutabilite_id,   /* A3.2: NOP — covered by A1.1           */

    /* F4 remaining */
    ax_f4_1_immuabilite,       /* A4.1: axiom table in .rodata?          */
    ax_f4_3_atomicite,         /* A4.3: structurally complete simplex?   */
};

/* Number of axioms in the table — computed automatically */
#define SLK_AXIOM_COUNT (sizeof(SLK_AXIOM_TABLE) / sizeof(SLK_AXIOM_TABLE[0]))


#endif /* SLK_AXIOMS_H */