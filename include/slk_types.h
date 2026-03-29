/*
 * slk_types.h — Core type definitions for the SLK kernel
 *
 * This file encodes Definition 1 (Section 2.1) of the paper:
 *   S = <id, R, args, sigma>
 *
 * DESIGN PRINCIPLE:
 *   Every type chosen here is an engineering decision that must
 *   simultaneously satisfy two constraints:
 *     1. Formal fidelity: the type must exactly match the mathematical
 *        domain declared in Definition 1.
 *     2. Embedded constraint: the type must be usable on an ARM Cortex-M
 *        processor without OS, without malloc, without FPU.
 *
 * MEMORY FOOTPRINT PER SIMPLEX (MAX_ARITY = 3):
 *   id    : uint32_t = 4 bytes
 *   R     : uint16_t = 2 bytes
 *   args  : uint32_t[3] = 12 bytes
 *   sigma : uint8_t  = 1 byte
 *   pad   : 1 byte (compiler automatic alignment)
 *   ──────────────────────────────
 *   TOTAL : 20 bytes per simplex
 *
 *   For N_max = 256 simplexes (default value):
 *   Knowledge base K = 256 * 20 = 5120 bytes = 5 KB
 *   Full kernel (code + static data) < 4 KB of code
 */

#ifndef SLK_TYPES_H
#define SLK_TYPES_H

/*
 * stdint.h is available on all modern C99 compilers,
 * including arm-none-eabi-gcc for microcontrollers.
 * It guarantees that uint32_t is exactly 32 bits on every platform.
 */
#include <stdint.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * KERNEL CONFIGURATION CONSTANTS
 *
 * These constants are COMPILATION PARAMETERS, not axioms.
 * They can be modified for the target hardware without invalidating
 * the formal proofs in Section 2.
 * ───────────────────────────────────────────────────────────────────────────*/

/*
 * MAX_ARITY — Maximum arity of a relation R in Sigma
 *
 * Justification for value 3:
 *   - Arity 1 (unary)   : property predicates. Ex: IS_ACTIVE(sensor_7)
 *   - Arity 2 (binary)  : directional relations. Ex: PRECEDES(A, B)
 *   - Arity 3 (ternary) : three-participant relations. Ex: TRANSFER(src, dst, amount)
 *
 *   Relations of arity >= 4 are theoretically possible but rare in practice.
 *   If a domain requires them, MAX_ARITY can be increased at compile time.
 *   The memory size of each simplex adapts automatically via args[MAX_ARITY].
 *
 * Link to axioms:
 *   A1.2 (Arity conformance) verifies |args| == arity(R) at runtime.
 *   MAX_ARITY is the physical upper bound of this check.
 */
#define SLK_MAX_ARITY    3

/*
 * N_MAX — Maximum size of the knowledge base K
 *
 * Justification:
 *   Axiom A1.3 (Finiteness of K) requires |K| < N_max.
 *   N_max = 256 is sufficient for most embedded applications.
 *   On STM32F4 (192 KB RAM), N_max can be increased to 4096
 *   while keeping the footprint under 100 KB.
 *
 *   IMPORTANT: Modifying N_max does not change the logical behavior
 *   of the kernel. It is a physical parameter, not an axiom.
 */
#define SLK_N_MAX        256

/*
 * NULL_ID — Sentinel value for "undefined identifier"
 *
 * Justification:
 *   Axiom A3.1 (Reference coherence) allows args[i] = NULL_ID
 *   to represent an unknown entity (open world assumption).
 *   0 is reserved as sentinel: no real simplex has id = 0.
 *   Valid identifiers start at 1.
 */
#define SLK_NULL_ID      0

/*
 * SIGMA_MAX — Maximum value of the stability index
 *
 * Link to axioms:
 *   A1.4 (Bounded stability) requires sigma in {0,...,255}.
 *   Since sigma is a uint8_t, SIGMA_MAX = 255 is a type truth,
 *   not a runtime check.
 *   This constant is declared here for documentation, not for code.
 */
#define SLK_SIGMA_MAX    255

/* ─────────────────────────────────────────────────────────────────────────────
 * DEFINITION 1: THE SLK SIMPLEX
 *   S = <id, R, args, sigma>
 * ───────────────────────────────────────────────────────────────────────────*/

/*
 * SLK_Simplex — Atomic unit of knowledge in the SLK kernel
 *
 * Formal correspondence (Definition 1, Section 2.1):
 *
 *   C field    | Formal component | Mathematical domain
 *   ───────────┼──────────────────┼──────────────────────────────────
 *   id         | id               | N (natural integer, >= 1)
 *   relation   | R                | Sigma (finite alphabet of predicates)
 *   args[]     | args             | N^k (ordered tuple, k = arity(R))
 *   sigma      | sigma            | {0,...,255} (uint8)
 *
 * NOTE ON ALIGNMENT:
 *   The C compiler may add padding bytes between fields to align
 *   memory accesses on natural CPU boundaries.
 *   On ARM Cortex-M:
 *     - uint32_t must be aligned on 4 bytes
 *     - uint16_t must be aligned on 2 bytes
 *   Field ordering (largest to smallest) minimizes padding.
 *   To force exact layout, __attribute__((packed)) can be used,
 *   but this slows memory access on some processors.
 */
typedef struct {
    /*
     * id — Globally unique identifier of the simplex
     *
     * Domain: uint32_t -> represents N in {1,...,2^32-1}
     * 0 is reserved as SLK_NULL_ID (sentinel value).
     *
     * Axiom A1.1 (Uniqueness): the registry guarantees no other
     * simplex in K has the same id.
     *
     * Axiom A3.2 (Immutability): once inserted into K, id cannot
     * be modified. In C, this is enforced by the registry access
     * policy (no direct field access from outside kernel.c).
     */
    uint32_t id;

    /*
     * relation — Predicate of relation R
     *
     * Domain: uint16_t -> index into alphabet Sigma
     * Sigma is declared in the SDK layer during initialization.
     * The kernel does not know the semantics of R — it only checks
     * that the index is valid (A1.5) and that arity is respected (A1.2).
     *
     * Choice of uint16_t: allows up to 65535 distinct relations in
     * Sigma, sufficient for any application domain.
     */
    uint16_t relation;

    /*
     * args — Ordered tuple of target identifiers
     *
     * Domain: N^k where k = arity(relation)
     *
     * FUNDAMENTAL DECISION — Tuple vs Set:
     *   args is an array (ordered tuple), not a set.
     *   This allows modeling PRECEDES(A,B) != PRECEDES(B,A).
     *   If args were a set, these two relations would be identical
     *   and the kernel would be unable to distinguish non-symmetric
     *   relations — excluding the majority of useful cases.
     *
     * Positions args[k], args[k+1], ... args[MAX_ARITY-1] beyond
     * the effective arity are set to SLK_NULL_ID by convention.
     * Axiom A1.2 guarantees that effective positions are valid.
     */
    uint32_t args[SLK_MAX_ARITY];

    /*
     * sigma — Discrete stability index
     *
     * Domain: {0,...,255} (uint8_t)
     * 0   = no evidence (assertion without support)
     * 255 = maximum certainty (established fact)
     *
     * SIGMA IS NOT A PROBABILITY:
     *   A probability must satisfy Kolmogorov axioms
     *   (P(true) = 1, P(false) = 0, additivity). sigma does not satisfy
     *   these properties — it is a discrete evidence weight, closer
     *   to a score than a probability.
     *
     *   This distinction is important: the kernel remains entirely
     *   deterministic (Axiom A2.3, confluence) even if sigma varies.
     *   sigma influences SDK layer decisions, not the kernel.
     *
     * Axiom A1.4 (Bounded stability) is structurally satisfied:
     * the uint8_t type cannot hold a value > 255.
     */
    uint8_t sigma;

} SLK_Simplex;


/* ─────────────────────────────────────────────────────────────────────────────
 * KERNEL RETURN CODES
 *
 * Each return code corresponds to the violation of a specific axiom.
 * This allows the SDK layer to know exactly which rule was violated —
 * a property of total auditability of the system.
 * ───────────────────────────────────────────────────────────────────────────*/

typedef enum {
    SLK_OK                  =  0,  /* V(S,K,A) = 1: simplex accepted                */

    /* F1 violations — Structural coherence */
    SLK_ERR_ID_ZERO         = -1,  /* A1.1: id = 0 is reserved (NULL_ID)            */
    SLK_ERR_ID_EXISTS       = -2,  /* A1.1: id already in K with different values   */
    SLK_ERR_ARITY_MISMATCH  = -3,  /* A1.2: |args| != arity(R) according to Sigma   */
    SLK_ERR_BASE_FULL       = -4,  /* A1.3: |K| >= N_max                            */
    SLK_ERR_RELATION_INVALID= -5,  /* A1.5: relation not declared in Sigma          */

    /* F2 violations — Temporal causality */
    SLK_ERR_SPONTANEOUS     = -6,  /* A2.2: modification attempt outside V()        */

    /* F3 violations — Graph topology */
    SLK_ERR_DANGLING_REF    = -7,  /* A3.1: args[i] non-null and absent from K      */

    /* F4 violations — Security */
    SLK_ERR_BOOT_CORRUPT    = -8,  /* A4.4: kernel CRC32 invalid at boot            */
    SLK_ERR_ATOMIC_FAIL     = -9,  /* A4.3: interrupted transition, corrupted state */

    /* F5 violations — Conservation */
    SLK_ERR_NULL_WRITE      = -10, /* A5.2: write attempt without going through V   */

} SLK_Status;


/* ─────────────────────────────────────────────────────────────────────────────
 * DEFINITION 2: THE ALPHABET REGISTRY ENTRY (Sigma)
 *
 * Sigma is the finite alphabet of predicates.
 * Each entry declares a predicate with its name and arity.
 * The kernel uses this information to verify A1.2 and A1.5.
 * ───────────────────────────────────────────────────────────────────────────*/

/*
 * SLK_Relation — Declaration of a predicate in the alphabet Sigma
 *
 * name  : human-readable string (for SDK logs)
 *         The kernel never reads name — it is for audit purposes only.
 * arity : arity k of the relation, in {0,...,MAX_ARITY}
 *         Arity 0: proposition without argument (pure fact). Ex: SYSTEM_OK
 *         Arity 1: unary predicate. Ex: IS_ACTIVE(x)
 *         Arity 2: binary relation. Ex: PRECEDES(x, y)
 *         Arity 3: ternary relation. Ex: TRANSFER(x, y, amount)
 */
typedef struct {
    const char *name;
    uint8_t     arity;
} SLK_Relation;


/* ─────────────────────────────────────────────────────────────────────────────
 * DEFINITION 3: THE GLOBAL KERNEL STATE
 *
 * SLK_Kernel encapsulates the complete system state at time tau.
 * This is the C equivalent of knowledge base K (Definition 2, Section 2.2).
 * ───────────────────────────────────────────────────────────────────────────*/

typedef struct {
    /*
     * base[] — The knowledge base K
     *
     * Static array of N_MAX simplexes.
     * NO DYNAMIC ALLOCATION: no malloc, no realloc.
     * The array is fully allocated at compile time (BSS/DATA segment).
     * This makes SLK viable on microcontrollers without OS.
     */
    SLK_Simplex base[SLK_N_MAX];

    /*
     * count — Number of simplexes currently in K
     *
     * Maintained invariant: 0 <= count <= N_MAX
     * Axiom A1.3 (Finiteness): count < N_MAX before any insertion.
     */
    uint32_t count;

    /*
     * tau — Inference cycle counter (logical clock)
     *
     * Axiom A2.1 (Temporal arrow):
     *   tau is strictly monotonically increasing.
     *   Incremented on EVERY call to slk_validate() that returns SLK_OK.
     *   Never decremented. Never reset during execution.
     *
     * uint64_t: at 1 million validations per second, tau overflows
     *   after 584,542 years. Considered sufficient.
     */
    uint64_t tau;

    /*
     * sigma_alphabet[] — The Sigma alphabet of predicates
     *
     * sigma_count: number of relations declared in Sigma
     * Axiom A1.5: every relation in a simplex must have
     *   an index < sigma_count.
     */
    const SLK_Relation *sigma_alphabet;
    uint16_t            sigma_count;

    /*
     * boot_crc — Reference CRC32 of the kernel, computed at boot
     *
     * Axiom A4.4 (Boot integrity):
     *   At boot, slk_init() recomputes the CRC32 of the code segment
     *   and compares it to boot_crc. If different: immediate halt.
     *   boot_crc is itself stored in .rodata (read-only).
     */
    uint32_t boot_crc;

    /*
     * is_initialized — Kernel state flag
     *
     * 0: kernel not initialized, all operations return SLK_ERR_NULL_WRITE
     * 1: kernel initialized and verified, operations authorized
     *
     * Axiom A5.2 (Read/write separation):
     *   WRITE is conditioned on is_initialized == 1.
     */
    uint8_t is_initialized;

} SLK_Kernel;


#endif /* SLK_TYPES_H */
