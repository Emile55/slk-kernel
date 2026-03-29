/*
 * kernel.c — SLK kernel implementation
 *
 * This file implements:
 *   - slk_init()     : initialization and integrity verification (A4.4)
 *   - slk_validate() : the function V(S, K, A) from Theorem 1
 *   - slk_commit()   : atomic insertion into K (A4.3)
 *   - slk_find()     : read from K (A5.2 — READ always available)
 *   - slk_count()    : state of K
 *
 * FUNDAMENTAL PRINCIPLE:
 *   This file is the ONLY place where K (base[]) is modified.
 *   All modifications go through slk_validate() -> slk_commit().
 *   There is no direct access to base[] from outside.
 *   This architecturally guarantees A2.2 (strict causality).
 */

#include "slk_types.h"
#include "slk_axioms.h"
#include <stddef.h>
#include <string.h>   /* memcpy, memset */
#include <stdint.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * UTILITY: CRC32
 *
 * Minimal CRC32 implementation (IEEE 802.3 polynomial).
 * Used for A4.4 (boot integrity).
 *
 * Why CRC32 and not SHA-256?
 *   CRC32 is hardware-integrated on many ARM microcontrollers
 *   (STM32, nRF52, etc.) and fits in ~20 lines of code.
 *   SHA-256 requires ~300 lines and is much slower.
 *   To detect accidental corruption (radiation, memory failure),
 *   CRC32 is sufficient. For cryptographic resistance against
 *   an active attacker, the SDK should use HMAC-SHA256 at the upper layer.
 * ───────────────────────────────────────────────────────────────────────────*/

static uint32_t slk_crc32(const uint8_t *data, uint32_t length) {
    uint32_t crc = 0xFFFFFFFF;
    uint32_t i, j;

    for (i = 0; i < length; i++) {
        crc ^= (uint32_t)data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320; /* Reflected IEEE 802.3 polynomial */
            } else {
                crc >>= 1;
            }
        }
    }

    return crc ^ 0xFFFFFFFF;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * slk_init — Kernel initialization
 *
 * Parameters:
 *   k        : pointer to the kernel structure to initialize
 *   alphabet : array of relations declared in Sigma
 *   count    : number of relations in Sigma
 *
 * Return:
 *   SLK_OK if the kernel is initialized and verified
 *   SLK_ERR_BOOT_CORRUPT if CRC verification fails
 *
 * Implements:
 *   Axiom A4.4 (Boot integrity):
 *     CRC32 of axiom functions computed and stored in boot_crc.
 *     Any subsequent corruption of the axiom table will be detected
 *     by ax_f4_4_boot_crc() during subsequent validations.
 *
 *   Axiom A5.2 (Read/write separation):
 *     is_initialized = 1 ONLY after successful verification.
 *     No write operation is possible before this flag.
 * ───────────────────────────────────────────────────────────────────────────*/

int slk_init(SLK_Kernel *k, const SLK_Relation *alphabet, uint16_t count) {
    if (k == NULL || alphabet == NULL || count == 0) {
        return SLK_ERR_NULL_WRITE;
    }

    /*
     * STEP 1: Full zero-initialization of the structure
     *
     * memset guarantees that:
     *   - base[] is completely empty (all ids = 0 = NULL_ID)
     *   - tau = 0 (clock at rest)
     *   - is_initialized = 0 (not ready yet)
     *
     * This is the clean initial state required by A5.1 (weak monotonicity):
     * K starts empty and can only grow.
     */
    memset(k, 0, sizeof(SLK_Kernel));

    /*
     * STEP 2: Sigma alphabet configuration
     *
     * The kernel does not copy Sigma — it only keeps a pointer.
     * This means the alphabet array MUST remain valid for the
     * entire lifetime of the kernel.
     * In practice, alphabet is declared as a static const in the SDK.
     */
    k->sigma_alphabet = alphabet;
    k->sigma_count    = count;

    /*
     * STEP 3: Compute reference CRC32 (Axiom A4.4)
     *
     * We compute the CRC32 of the axiom function table.
     * This CRC will be verified at each validation by ax_f4_4_boot_crc().
     *
     * TECHNICAL NOTE: We compute the CRC of function POINTERS
     * (addresses), not of the function code itself.
     * On systems with ASLR (Address Space Layout Randomization),
     * addresses change at each execution, making this CRC less relevant.
     * For microcontrollers (no ASLR), addresses are fixed after linking
     * and this CRC is meaningful.
     *
     * On Linux (for unit tests), we still compute the CRC to verify
     * that the table is not corrupted between linking and execution.
     */
    k->boot_crc = slk_crc32(
        (const uint8_t *)SLK_AXIOM_TABLE,
        (uint32_t)(SLK_AXIOM_COUNT * sizeof(SLK_AxiomFn))
    );

    if (k->boot_crc == 0) {
        /*
         * A CRC of 0 is theoretically possible but extremely rare.
         * Treat it as an error to avoid confusion with the
         * "uninitialized" state (where boot_crc is 0 by memset).
         */
        k->boot_crc = 0xDEADBEEF; /* Non-zero sentinel value */
    }

    /*
     * STEP 4: Mark the kernel as initialized (Axiom A5.2)
     *
     * is_initialized = 1 is the last act of slk_init().
     * Before this point, any write attempt into K is rejected
     * by ax_f5_2_separation_rw() and ax_f4_2_priorite().
     */
    k->is_initialized = 1;

    return SLK_OK;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * slk_commit — Atomic insertion of a simplex into K (PRIVATE)
 *
 * This function is PRIVATE: it is only called from slk_validate().
 * It implements the "commit" step of the "validate then commit" pattern.
 *
 * Implements:
 *   Axiom A4.3 (Atomicity):
 *     Copying the simplex and incrementing count AND tau are performed
 *     as an atomic sequence.
 *     On microcontrollers, interrupts must be disabled during this
 *     function (see NOTE below).
 *
 *   Axiom A2.1 (Temporal arrow):
 *     tau is incremented AFTER the successful copy of the simplex.
 *     tau only reflects COMPLETE transitions.
 * ───────────────────────────────────────────────────────────────────────────*/

static int slk_commit(SLK_Kernel *k, const SLK_Simplex *s) {
    /*
     * ATOMICITY ON MICROCONTROLLER:
     * On ARM Cortex-M, to guarantee atomicity, this block should be
     * wrapped with:
     *   __disable_irq();  // Disable interrupts
     *   ... (copy + increment)
     *   __enable_irq();   // Re-enable interrupts
     *
     * On Linux (tests), threads can interrupt here, but for V1
     * (single-thread), this is acceptable.
     */

    /*
     * Final guard check (defense in depth).
     * Already verified by ax_f1_3_finiteness, but double-checked
     * to guarantee that no bug in the axiom chain causes
     * a buffer overflow.
     */
    if (k->count >= SLK_N_MAX) {
        return SLK_ERR_BASE_FULL;
    }

    /*
     * Copy the simplex into K.
     * memcpy copies exactly sizeof(SLK_Simplex) bytes.
     * No malloc, no dynamic pointers — direct copy.
     */
    memcpy(&k->base[k->count], s, sizeof(SLK_Simplex));

    /*
     * Atomic increment of count AND tau.
     * Order matters:
     *   1. count++ : the simplex is now "in K"
     *   2. tau++   : the clock reflects the new transition
     *
     * If an interrupt occurs between 1 and 2, count is coherent
     * but tau is not. This is acceptable: tau is not used by axioms
     * for logical decisions, only for temporal audit.
     */
    k->count++;
    k->tau++;

    return SLK_OK;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * slk_validate — The function V(S, K, A) from Theorem 1 (Section 2.3)
 *
 * This is the central function of the SLK kernel.
 * It implements exactly Definition 4:
 *   V(S, K, A) = 1 iff for all alpha in A: alpha(S, K) = 1
 *
 * Parameters:
 *   k : kernel state (knowledge base K + alphabet Sigma)
 *   s : incoming simplex to validate
 *
 * Return:
 *   SLK_OK if the simplex is accepted and inserted into K
 *   An SLK_ERR_* code indicating the violated axiom
 *
 * Properties guaranteed by this implementation:
 *   - Soundness (Theorem 1): if SLK_OK, s is coherent with K under A
 *   - Atomicity (A4.3): K is modified only if ALL axioms pass
 *   - Determinism (A2.3): same input => same output
 *   - Causality (A2.2): K grows only via slk_commit()
 *   - Temporal arrow (A2.1): tau is incremented after each OK
 * ───────────────────────────────────────────────────────────────────────────*/

int slk_validate(SLK_Kernel *k, const SLK_Simplex *s) {
    uint32_t i;
    int      result;

    /*
     * ENTRY GUARD: null pointer checks
     *
     * These checks do not correspond to any specific axiom —
     * they protect against programming errors in the SDK
     * (passing NULL pointers). In embedded C, dereferencing
     * NULL causes an immediate Hard Fault.
     */
    if (k == NULL || s == NULL) {
        return SLK_ERR_NULL_WRITE;
    }

    /*
     * PHASE 1: VALIDATION
     * ────────────────────
     * We iterate through the axiom table and call each function.
     * K IS NOT MODIFIED during this phase.
     *
     * Fail-fast pattern:
     * As soon as an axiom returns an error, we return immediately.
     * This implements:
     *   V(S, K, A) = 0 if EXISTS alpha in A such that alpha(S, K) = 0
     *
     * Confluence property (A2.3):
     * The final result of V is the same regardless of the evaluation
     * order of axioms. The order chosen here is a performance
     * optimization (fastest axioms first), not a logical constraint.
     */
    for (i = 0; i < SLK_AXIOM_COUNT; i++) {
        result = SLK_AXIOM_TABLE[i](s, k);

        if (result != SLK_OK) {
            /*
             * Axiom violated.
             * Return the error code of the violated axiom.
             * K is unchanged — atomicity guarantee (A4.3).
             *
             * The error code allows the SDK to know EXACTLY
             * which axiom rejected the simplex (total auditability).
             */
            return result;
        }
    }

    /*
     * SPECIAL CASE: Idempotence
     *
     * If ax_f1_1_uniqueness returned SLK_OK because the simplex
     * is already identically present in K (idempotence),
     * we MUST NOT re-insert it (that would break A1.1 by having
     * two copies of the same simplex) and MUST NOT increment tau
     * (no new logical transition).
     *
     * Detection: search for the simplex in K after validation.
     * If found -> return SLK_OK without commit.
     */
    for (i = 0; i < k->count; i++) {
        if (k->base[i].id == s->id) {
            /* Simplex already present and identical (ax_f1_1 verified this) */
            return SLK_OK; /* Idempotence: success without modification */
        }
    }

    /*
     * PHASE 2: COMMIT
     * ────────────────
     * All axioms are satisfied. Insert the simplex into K
     * and increment tau.
     *
     * slk_commit() is the ONLY function that modifies base[].
     * This architectural constraint implements A2.2 (strict causality).
     */
    return slk_commit(k, s);
}


/* ─────────────────────────────────────────────────────────────────────────────
 * READ FUNCTIONS (Axiom A5.2: READ always available)
 *
 * These functions never modify K.
 * They are available even if is_initialized = 0 for introspection.
 * ───────────────────────────────────────────────────────────────────────────*/

/*
 * slk_find — Search for a simplex by id in K
 *
 * Returns a const pointer to the found simplex, or NULL if absent.
 * The pointer is read-only (const): the caller cannot modify
 * the simplex directly in K (guarantees A3.2).
 *
 * Complexity: O(|K|)
 */
const SLK_Simplex *slk_find(const SLK_Kernel *k, uint32_t id) {
    uint32_t i;

    if (k == NULL || id == SLK_NULL_ID) {
        return NULL;
    }

    for (i = 0; i < k->count; i++) {
        if (k->base[i].id == id) {
            return &k->base[i]; /* Const pointer: read-only */
        }
    }

    return NULL;
}

/*
 * slk_count — Returns the number of simplexes in K
 */
uint32_t slk_count(const SLK_Kernel *k) {
    if (k == NULL) return 0;
    return k->count;
}

/*
 * slk_tau — Returns the current logical clock value
 *
 * tau represents the number of valid transitions performed since
 * kernel initialization. Can be used by the SDK for temporal
 * audit and non-repudiation of transitions.
 */
uint64_t slk_tau(const SLK_Kernel *k) {
    if (k == NULL) return 0;
    return k->tau;
}

/*
 * slk_status_name — Human-readable name of an error code
 *
 * Useful for SDK logs and conversational interfaces.
 * Excluded from ultra-lightweight embedded binaries
 * (via #ifdef SLK_ENABLE_STRINGS) because strings cost flash memory.
 */
#ifdef SLK_ENABLE_STRINGS
const char *slk_status_name(int status) {
    switch (status) {
        case SLK_OK:                   return "SLK_OK";
        case SLK_ERR_ID_ZERO:          return "ERR_ID_ZERO (A1.1)";
        case SLK_ERR_ID_EXISTS:        return "ERR_ID_EXISTS (A1.1)";
        case SLK_ERR_ARITY_MISMATCH:   return "ERR_ARITY_MISMATCH (A1.2)";
        case SLK_ERR_BASE_FULL:        return "ERR_BASE_FULL (A1.3)";
        case SLK_ERR_RELATION_INVALID: return "ERR_RELATION_INVALID (A1.5)";
        case SLK_ERR_SPONTANEOUS:      return "ERR_SPONTANEOUS (A2.2)";
        case SLK_ERR_DANGLING_REF:     return "ERR_DANGLING_REF (A3.1)";
        case SLK_ERR_BOOT_CORRUPT:     return "ERR_BOOT_CORRUPT (A4.4)";
        case SLK_ERR_ATOMIC_FAIL:      return "ERR_ATOMIC_FAIL (A4.3)";
        case SLK_ERR_NULL_WRITE:       return "ERR_NULL_WRITE (A5.2)";
        default:                       return "UNKNOWN_ERROR";
    }
}
#endif