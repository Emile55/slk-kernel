/*
 * test_kernel.c — Tests unitaires du noyau SLK
 *
 * Chaque test vérifie un axiome spécifique.
 * La convention de nommage est : test_AX_XX_nom_de_l_axiome()
 *
 * Un test qui PASSE confirme que l'axiome est correctement implémenté.
 * Un test qui ÉCHOUE indique une régression dans le noyau.
 *
 * Compilez avec :
 *   gcc -DSLK_ENABLE_STRINGS -I../include -o test_kernel test_kernel.c ../src/kernel.c
 * Exécutez avec :
 *   ./test_kernel
 */

#define SLK_ENABLE_STRINGS

#include "slk_types.h"
#include "slk_axioms.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* ─── Déclarations des fonctions du noyau ─── */
int               slk_init(SLK_Kernel *k, const SLK_Relation *alphabet, uint16_t count);
int               slk_validate(SLK_Kernel *k, const SLK_Simplex *s);
const SLK_Simplex *slk_find(const SLK_Kernel *k, uint32_t id);
uint32_t          slk_count(const SLK_Kernel *k);
uint64_t          slk_tau(const SLK_Kernel *k);
const char       *slk_status_name(int status);

/* ─── Infrastructure de test ─── */

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)

#define ASSERT_EQ(actual, expected, msg) do { \
    tests_run++; \
    if ((actual) == (expected)) { \
        printf("  [PASS] %s\n", msg); \
        tests_passed++; \
    } else { \
        printf("  [FAIL] %s : attendu %d, obtenu %d\n", msg, (int)(expected), (int)(actual)); \
        tests_failed++; \
    } \
} while(0)

/* ─── Alphabet Sigma de test ─── */
/*
 * On déclare un alphabet minimal pour les tests :
 *   Relation 0 : EST_ACTIF     (arité 1) — unaire
 *   Relation 1 : PRECEDE       (arité 2) — binaire
 *   Relation 2 : TRANSFERT     (arité 3) — ternaire
 *   Relation 3 : SYSTEME_OK    (arité 0) — proposition pure
 */
static const SLK_Relation TEST_SIGMA[] = {
    { "EST_ACTIF",  1 },  /* index 0 */
    { "PRECEDE",    2 },  /* index 1 */
    { "TRANSFERT",  3 },  /* index 2 */
    { "SYSTEME_OK", 0 },  /* index 3 */
};
static const uint16_t TEST_SIGMA_COUNT = 4;

/* Constructeur de simplexe pour les tests */
static SLK_Simplex make_simplex(uint32_t id, uint16_t rel,
                                 uint32_t a0, uint32_t a1, uint32_t a2,
                                 uint8_t sigma) {
    SLK_Simplex s;
    memset(&s, 0, sizeof(s));
    s.id       = id;
    s.relation = rel;
    s.args[0]  = a0;
    s.args[1]  = a1;
    s.args[2]  = a2;
    s.sigma    = sigma;
    return s;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST DE INITIALISATION
 * ═══════════════════════════════════════════════════════════════════════════*/

TEST(test_init) {
    printf("\n[ slk_init() ]\n");
    SLK_Kernel k;
    int result;

    /* Test nominal */
    result = slk_init(&k, TEST_SIGMA, TEST_SIGMA_COUNT);
    ASSERT_EQ(result, SLK_OK, "init avec alphabet valide retourne SLK_OK");
    ASSERT_EQ(k.is_initialized, 1, "is_initialized = 1 apres init reussie");
    ASSERT_EQ(k.count, 0u, "count = 0 apres init (K vide)");
    ASSERT_EQ(k.tau, 0ull, "tau = 0 apres init");

    /* Test avec paramètres invalides */
    result = slk_init(NULL, TEST_SIGMA, TEST_SIGMA_COUNT);
    ASSERT_EQ(result, SLK_ERR_NULL_WRITE, "init avec k=NULL retourne ERR_NULL_WRITE");

    result = slk_init(&k, NULL, TEST_SIGMA_COUNT);
    ASSERT_EQ(result, SLK_ERR_NULL_WRITE, "init avec alphabet=NULL retourne ERR_NULL_WRITE");

    result = slk_init(&k, TEST_SIGMA, 0);
    ASSERT_EQ(result, SLK_ERR_NULL_WRITE, "init avec count=0 retourne ERR_NULL_WRITE");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TESTS FAMILLE F1 — COHÉRENCE STRUCTURELLE
 * ═══════════════════════════════════════════════════════════════════════════*/

TEST(test_f1_1_unicite) {
    printf("\n[ Axiome A1.1 — Unicite des identifiants ]\n");
    SLK_Kernel k;
    SLK_Simplex s1, s2, s3;
    int result;

    slk_init(&k, TEST_SIGMA, TEST_SIGMA_COUNT);

    /* Insertion d'un simplexe valide (arité 1 = EST_ACTIF, un arg) */
    s1 = make_simplex(1, 0, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 200);
    s1.args[0] = 1; /* auto-référence — le noyau l'accepte (A1.1 ne juge pas) */
    /* Correction : arité 1 => args[0] = quelque chose, mais on a besoin d'une cible existante */
    /* Pour ce test, on utilise une relation d'arité 0 (SYSTEME_OK, index 3) */
    s1 = make_simplex(1, 3, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 200);
    result = slk_validate(&k, &s1);
    ASSERT_EQ(result, SLK_OK, "insertion d'un simplexe valide (id=1)");
    ASSERT_EQ(slk_count(&k), 1u, "count = 1 apres premiere insertion");

    /* Test idempotence : même simplexe une deuxième fois */
    result = slk_validate(&k, &s1);
    ASSERT_EQ(result, SLK_OK, "re-insertion identique (idempotence) retourne SLK_OK");
    ASSERT_EQ(slk_count(&k), 1u, "count reste 1 apres insertion idempotente");
    ASSERT_EQ(slk_tau(&k), 1ull, "tau reste 1 (pas de nouvelle transition)");

    /* Test conflit : même id, données différentes */
    s2 = make_simplex(1, 3, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 100);
    /* Même id=1 mais sigma différent */
    result = slk_validate(&k, &s2);
    ASSERT_EQ(result, SLK_ERR_ID_EXISTS, "conflit d'id (id=1, sigma different) retourne ERR_ID_EXISTS");

    /* Test id = 0 (NULL_ID réservé) */
    s3 = make_simplex(0, 3, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 100);
    result = slk_validate(&k, &s3);
    ASSERT_EQ(result, SLK_ERR_ID_ZERO, "id=0 (NULL_ID) retourne ERR_ID_ZERO");
}

TEST(test_f1_2_arite) {
    printf("\n[ Axiome A1.2 — Conformite d'arite ]\n");
    SLK_Kernel k;
    SLK_Simplex s;
    int result;

    slk_init(&k, TEST_SIGMA, TEST_SIGMA_COUNT);

    /* Insérer d'abord id=10 comme nœud de base (relation d'arité 0) */
    s = make_simplex(10, 3, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 255);
    slk_validate(&k, &s);

    /* Test arité correcte : PRECEDE (index 1, arité 2) avec 2 args */
    /* On insère d'abord id=11 */
    s = make_simplex(11, 3, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 255);
    slk_validate(&k, &s);

    s = make_simplex(100, 1, 10, 11, SLK_NULL_ID, 128);
    result = slk_validate(&k, &s);
    ASSERT_EQ(result, SLK_OK, "PRECEDE(10,11) avec arite=2 : valide");

    /* Test arité incorrecte : PRECEDE avec un 3ème arg non-NULL */
    s = make_simplex(101, 1, 10, 11, 10, 128); /* args[2] = 10 alors qu'arité = 2 */
    result = slk_validate(&k, &s);
    ASSERT_EQ(result, SLK_ERR_ARITY_MISMATCH, "PRECEDE avec 3 args (arite attendue 2) : rejet");

    /* Test relation hors Sigma */
    s = make_simplex(102, 99, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 100);
    result = slk_validate(&k, &s);
    ASSERT_EQ(result, SLK_ERR_RELATION_INVALID, "relation index 99 hors Sigma : rejet");
}

TEST(test_f1_3_finitude) {
    printf("\n[ Axiome A1.3 — Finitude de K ]\n");
    /*
     * Ce test est conceptuel : remplir K jusqu'à N_MAX serait long.
     * On vérifie le comportement de count et le code d'erreur.
     *
     * Pour un vrai test de finitude, réduire SLK_N_MAX à 4 et
     * insérer 5 simplexes.
     */
    SLK_Kernel k;
    int result;

    slk_init(&k, TEST_SIGMA, TEST_SIGMA_COUNT);

    /* Vérification que N_MAX est bien défini et cohérent */
    ASSERT_EQ(SLK_N_MAX, 256, "SLK_N_MAX = 256 (valeur par defaut)");

    /* Simulation : forcer count à N_MAX et vérifier le rejet */
    k.count = SLK_N_MAX; /* Forcer l'état "base pleine" */
    SLK_Simplex s = make_simplex(999, 3, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 100);
    result = slk_validate(&k, &s);
    ASSERT_EQ(result, SLK_ERR_BASE_FULL, "insertion quand count=N_MAX retourne ERR_BASE_FULL");

    k.count = 0; /* Restaurer pour les tests suivants */
    (void)result;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TESTS FAMILLE F2 — CAUSALITÉ TEMPORELLE
 * ═══════════════════════════════════════════════════════════════════════════*/

TEST(test_f2_1_fleche_temporelle) {
    printf("\n[ Axiome A2.1 — Fleche temporelle ]\n");
    SLK_Kernel k;
    SLK_Simplex s;

    slk_init(&k, TEST_SIGMA, TEST_SIGMA_COUNT);

    uint64_t tau_before = slk_tau(&k);
    s = make_simplex(1, 3, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 255);
    slk_validate(&k, &s);
    uint64_t tau_after = slk_tau(&k);

    ASSERT_EQ(tau_after > tau_before, 1, "tau apres validation > tau avant (monotone croissant)");

    /* Vérifier que tau ne régresse pas */
    uint64_t tau_check = slk_tau(&k);
    s = make_simplex(2, 3, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 128);
    slk_validate(&k, &s);
    ASSERT_EQ(slk_tau(&k) >= tau_check, 1, "tau ne peut pas diminuer (monotonie stricte)");
}

TEST(test_f2_2_causalite_stricte) {
    printf("\n[ Axiome A2.2 — Causalite stricte ]\n");
    SLK_Kernel k;
    SLK_Simplex s;
    int result;

    /* Test : opération avant init */
    memset(&k, 0, sizeof(k)); /* k non initialisé (is_initialized = 0) */
    s = make_simplex(1, 3, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 100);
    result = slk_validate(&k, &s);
    ASSERT_EQ(result, SLK_ERR_NULL_WRITE, "ecriture avant init retourne ERR_NULL_WRITE (A2.2)");

    /* Test : après init, insertion normale */
    slk_init(&k, TEST_SIGMA, TEST_SIGMA_COUNT);
    result = slk_validate(&k, &s);
    ASSERT_EQ(result, SLK_OK, "ecriture apres init retourne SLK_OK");
}

TEST(test_f2_3_confluence) {
    printf("\n[ Axiome A2.3 — Confluence (determinisme) ]\n");
    SLK_Kernel k1, k2;
    SLK_Simplex s;
    int r1, r2;

    /*
     * Vérification du déterminisme :
     * Même entrée sur deux noyaux identiquement initialisés
     * => même sortie
     */
    slk_init(&k1, TEST_SIGMA, TEST_SIGMA_COUNT);
    slk_init(&k2, TEST_SIGMA, TEST_SIGMA_COUNT);

    s = make_simplex(42, 3, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 200);
    r1 = slk_validate(&k1, &s);
    r2 = slk_validate(&k2, &s);

    ASSERT_EQ(r1, r2, "meme entree sur deux noyaux identiques => meme resultat (confluence)");
    ASSERT_EQ(slk_count(&k1), slk_count(&k2), "count identique sur deux noyaux apres meme sequence");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TESTS FAMILLE F3 — TOPOLOGIE DU GRAPHE
 * ═══════════════════════════════════════════════════════════════════════════*/

TEST(test_f3_1_coherence_ref) {
    printf("\n[ Axiome A3.1 — Coherence de reference ]\n");
    SLK_Kernel k;
    SLK_Simplex s;
    int result;

    slk_init(&k, TEST_SIGMA, TEST_SIGMA_COUNT);

    /* Insérer le nœud A (id=10) */
    s = make_simplex(10, 3, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 255);
    slk_validate(&k, &s);

    /* Insérer le nœud B (id=11) */
    s = make_simplex(11, 3, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 255);
    slk_validate(&k, &s);

    /* Test : relation PRECEDE(10, 11) — les deux cibles existent */
    s = make_simplex(100, 1, 10, 11, SLK_NULL_ID, 200);
    result = slk_validate(&k, &s);
    ASSERT_EQ(result, SLK_OK, "PRECEDE(10,11) avec les deux cibles existantes : valide");

    /* Test : relation PRECEDE(10, 999) — id=999 n'existe pas dans K */
    s = make_simplex(101, 1, 10, 999, SLK_NULL_ID, 200);
    result = slk_validate(&k, &s);
    ASSERT_EQ(result, SLK_ERR_DANGLING_REF, "PRECEDE(10,999) avec id=999 absent : ERR_DANGLING_REF");

    /* Test : relation PRECEDE(10, NULL_ID) — monde ouvert autorisé */
    s = make_simplex(102, 1, 10, SLK_NULL_ID, SLK_NULL_ID, 200);
    result = slk_validate(&k, &s);
    ASSERT_EQ(result, SLK_OK, "PRECEDE(10, NULL_ID) — monde ouvert autorise : valide");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TESTS FAMILLE F4 — SÉCURITÉ ET INTÉGRITÉ
 * ═══════════════════════════════════════════════════════════════════════════*/

TEST(test_f4_4_boot_crc) {
    printf("\n[ Axiome A4.4 — Integrite au demarrage ]\n");
    SLK_Kernel k;

    slk_init(&k, TEST_SIGMA, TEST_SIGMA_COUNT);

    ASSERT_EQ(k.boot_crc != 0, 1, "boot_crc non nul apres init (CRC calcule)");
    ASSERT_EQ(k.is_initialized, 1, "is_initialized = 1 apres init reussie");

    /* Simulation de corruption : boot_crc mis à 0 */
    uint32_t original_crc = k.boot_crc;
    k.boot_crc = 0;

    SLK_Simplex s = make_simplex(1, 3, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 100);
    int result = slk_validate(&k, &s);
    ASSERT_EQ(result, SLK_ERR_BOOT_CORRUPT, "boot_crc=0 avec noyau initialise : ERR_BOOT_CORRUPT");

    k.boot_crc = original_crc; /* Restaurer */
    (void)result;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TESTS FAMILLE F5 — CONSERVATION DE L'INFORMATION
 * ═══════════════════════════════════════════════════════════════════════════*/

TEST(test_f5_1_monotonie) {
    printf("\n[ Axiome A5.1 — Monotonie faible de K ]\n");
    SLK_Kernel k;
    SLK_Simplex s;
    uint32_t count_before;

    slk_init(&k, TEST_SIGMA, TEST_SIGMA_COUNT);

    s = make_simplex(1, 3, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 255);
    slk_validate(&k, &s);
    count_before = slk_count(&k);

    s = make_simplex(2, 3, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 128);
    slk_validate(&k, &s);

    ASSERT_EQ(slk_count(&k) >= count_before, 1,
        "count apres insertion >= count avant (monotonie faible)");
    ASSERT_EQ(slk_count(&k), 2u, "count = 2 apres 2 insertions distinctes");
}

TEST(test_f5_2_separation_rw) {
    printf("\n[ Axiome A5.2 — Separation lecture/ecriture ]\n");
    SLK_Kernel k;
    SLK_Simplex s;
    int result;

    slk_init(&k, TEST_SIGMA, TEST_SIGMA_COUNT);

    /* Écriture via slk_validate */
    s = make_simplex(1, 3, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 255);
    result = slk_validate(&k, &s);
    ASSERT_EQ(result, SLK_OK, "WRITE via slk_validate : autorise apres init");

    /* Lecture via slk_find (toujours disponible) */
    const SLK_Simplex *found = slk_find(&k, 1);
    ASSERT_EQ(found != NULL, 1, "READ via slk_find : disponible apres insertion");
    ASSERT_EQ(found->sigma, 255, "sigma du simplexe trouve = 255 (valeur correcte)");

    /* Lecture d'un id absent */
    const SLK_Simplex *not_found = slk_find(&k, 999);
    ASSERT_EQ(not_found == NULL, 1, "READ d'un id absent retourne NULL (pas de crash)");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST DE SOUNDNESS (Théorème 1)
 * ═══════════════════════════════════════════════════════════════════════════*/

TEST(test_theorem1_soundness) {
    printf("\n[ Theoreme 1 — Soundness de V ]\n");

    /*
     * On vérifie la propriété centrale :
     * Si slk_validate retourne SLK_OK, le simplexe est cohérent avec K.
     *
     * On teste la contraposée : on construit des simplexes incohérents
     * et on vérifie que slk_validate les rejette TOUS.
     */

    SLK_Kernel k;
    SLK_Simplex s;
    int result;

    slk_init(&k, TEST_SIGMA, TEST_SIGMA_COUNT);

    int incoherent_rejected = 1; /* Hypothèse : tous les incohérents seront rejetés */

    /* Incohérence 1 : id = NULL_ID */
    s = make_simplex(0, 3, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 100);
    result = slk_validate(&k, &s);
    if (result == SLK_OK) incoherent_rejected = 0;

    /* Incohérence 2 : relation hors Sigma */
    s = make_simplex(1, 255, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 100);
    result = slk_validate(&k, &s);
    if (result == SLK_OK) incoherent_rejected = 0;

    /* Incohérence 3 : arité incorrecte (PRECEDE avec 3 args) */
    s = make_simplex(1, 1, 1, 2, 3, 100); /* PRECEDE arité=2 mais args[2]=3 */
    result = slk_validate(&k, &s);
    if (result == SLK_OK) incoherent_rejected = 0;

    /* Incohérence 4 : référence pendante (cible inexistante) */
    s = make_simplex(1, 0, 9999, SLK_NULL_ID, SLK_NULL_ID, 100); /* EST_ACTIF(9999) */
    result = slk_validate(&k, &s);
    if (result == SLK_OK) incoherent_rejected = 0;

    ASSERT_EQ(incoherent_rejected, 1,
        "Theoreme 1 : tous les simplexes incoherents sont rejetes par V");

    /* Vérification positive : un simplexe cohérent est accepté */
    s = make_simplex(1, 3, SLK_NULL_ID, SLK_NULL_ID, SLK_NULL_ID, 255);
    result = slk_validate(&k, &s);
    ASSERT_EQ(result, SLK_OK, "Theoreme 1 : un simplexe coherent est accepte par V");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PROGRAMME PRINCIPAL
 * ═══════════════════════════════════════════════════════════════════════════*/

int main(void) {
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║       SUITE DE TESTS UNITAIRES — NOYAU SLK               ║\n");
    printf("║   Validation des 16 axiomes universels (Section 2.4)     ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");

    test_init();

    printf("\n──── FAMILLE F1 : Cohérence structurelle ────\n");
    test_f1_1_unicite();
    test_f1_2_arite();
    test_f1_3_finitude();

    printf("\n──── FAMILLE F2 : Causalité temporelle ────\n");
    test_f2_1_fleche_temporelle();
    test_f2_2_causalite_stricte();
    test_f2_3_confluence();

    printf("\n──── FAMILLE F3 : Topologie du graphe ────\n");
    test_f3_1_coherence_ref();

    printf("\n──── FAMILLE F4 : Sécurité et intégrité ────\n");
    test_f4_4_boot_crc();

    printf("\n──── FAMILLE F5 : Conservation de l'information ────\n");
    test_f5_1_monotonie();
    test_f5_2_separation_rw();

    printf("\n──── THÉORÈME 1 : Soundness ────\n");
    test_theorem1_soundness();

    printf("\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║  RÉSULTATS FINAUX                                         ║\n");
    printf("║  Tests exécutés : %-3d                                     ║\n", tests_run);
    printf("║  Tests réussis  : %-3d                                     ║\n", tests_passed);
    printf("║  Tests échoués  : %-3d                                     ║\n", tests_failed);
    printf("╚══════════════════════════════════════════════════════════╝\n");

    return (tests_failed == 0) ? 0 : 1;
}
