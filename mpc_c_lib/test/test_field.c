/*
 * test_field.c  --  Unit tests for mpc_field.c
 *
 * All expected values were computed by hand (shown in comments) and
 * independently verified with Python:
 *
 *   p = 2**31 - 1          # 2147483647
 *   pow(a, p-2, p)         # modular inverse via Fermat's little theorem
 *
 * Run:
 *   ./test_field
 *
 * A clean run prints "ALL FIELD TESTS PASSED" and exits 0.
 * Any failure prints the failing case and exits 1.
 */

#include "mpc_field.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define P  ((uint32_t)MPC_PRIME)   /* 2147483647 */

/* ------------------------------------------------------------------ */
/*  Test harness                                                        */
/* ------------------------------------------------------------------ */

static int _failures = 0;

static void check(const char *name, field_t got, field_t expected) {
    if (got == expected) {
        printf("  PASS  %s = %u\n", name, got);
    } else {
        printf("  FAIL  %s: got %u, expected %u\n", name, got, expected);
        _failures++;
    }
}

/* ------------------------------------------------------------------ */
/*  field_add tests                                                     */
/* ------------------------------------------------------------------ */

static void test_add(void) {
    printf("\n--- field_add ---\n");

    /* Basic */
    check("add(0, 0)",     field_add(0, 0),         0);
    check("add(3, 5)",     field_add(3, 5),          8);
    check("add(100, 200)", field_add(100, 200),      300);

    /* Wrap-around at p */
    /* (p-1) + 1 = p ≡ 0 mod p */
    check("add(p-1, 1)",   field_add(P-1, 1),        0);

    /* (p-1) + 2 = 1 */
    check("add(p-1, 2)",   field_add(P-1, 2),        1);

    /* (p-1) + (p-1) = p-2  (because 2(p-1) = 2p-2 ≡ p-2 mod p) */
    check("add(p-1, p-1)", field_add(P-1, P-1),      P-2);
}

/* ------------------------------------------------------------------ */
/*  field_sub tests                                                     */
/* ------------------------------------------------------------------ */

static void test_sub(void) {
    printf("\n--- field_sub ---\n");

    check("sub(5, 3)",     field_sub(5, 3),          2);
    check("sub(3, 3)",     field_sub(3, 3),          0);
    check("sub(0, 0)",     field_sub(0, 0),          0);

    /* Underflow: 3 - 5 = -2 ≡ p-2 mod p */
    check("sub(3, 5)",     field_sub(3, 5),          P-2);

    /* 0 - 1 = -1 ≡ p-1 mod p */
    check("sub(0, 1)",     field_sub(0, 1),          P-1);

    /* (p-1) - (p-1) = 0 */
    check("sub(p-1, p-1)", field_sub(P-1, P-1),      0);
}

/* ------------------------------------------------------------------ */
/*  field_mul tests                                                     */
/* ------------------------------------------------------------------ */

static void test_mul(void) {
    printf("\n--- field_mul ---\n");

    check("mul(0, 0)",     field_mul(0, 0),          0);
    check("mul(1, 1)",     field_mul(1, 1),          1);
    check("mul(2, 3)",     field_mul(2, 3),          6);
    check("mul(1000, 1000)", field_mul(1000, 1000),  1000000);

    /*
     * Overflow test: (p-1) * (p-1) mod p
     * (p-1)^2 = p^2 - 2p + 1 ≡ 1 mod p
     * Python: (2147483646 * 2147483646) % 2147483647 == 1
     */
    check("mul(p-1, p-1)", field_mul(P-1, P-1),      1);

    /*
     * (p-1) * 2 mod p = 2p - 2 mod p = p - 2
     * Python: (2147483646 * 2) % 2147483647 == 2147483645
     */
    check("mul(p-1, 2)",   field_mul(P-1, 2),        P-2);

    /*
     * Large values that would overflow if done in 32 bits.
     * a = 1073741824 (= 2^30), b = 2 => product = 2^31 = p+1 ≡ 1 mod p
     * Python: (1073741824 * 2) % 2147483647 == 1
     */
    check("mul(2^30, 2)",  field_mul(1073741824U, 2U), 1);
}

/* ------------------------------------------------------------------ */
/*  field_inv tests                                                     */
/* ------------------------------------------------------------------ */

static void test_inv(void) {
    printf("\n--- field_inv ---\n");

    /*
     * Verify: a * inv(a) ≡ 1 mod p for each test case.
     *
     * Pre-computed with Python pow(a, p-2, p):
     *   inv(1) = 1
     *   inv(2) = 1073741824   (= (p+1)/2)
     *   inv(3) = 1431655765   check: 3*1431655765 = 4294967295 = 2p+1 ≡ 1
     *   inv(p-1) = p-1        because (p-1)^2 ≡ 1 mod p, so p-1 is its own inverse
     */
    check("inv(1)",        field_inv(1),              1);
    check("inv(2)",        field_inv(2),              1073741824U);
    check("inv(3)",        field_inv(3),              1431655765U);
    check("inv(p-1)",      field_inv(P-1),            P-1);

    /* Verify by multiplication: a * inv(a) must equal 1 */
    field_t a = 123456789U;
    field_t inv_a = field_inv(a);
    check("mul(a, inv(a))", field_mul(a, inv_a),      1);

    /* inv(0) must return 0 (documented sentinel) */
    check("inv(0)",        field_inv(0),              0);
}

/* ------------------------------------------------------------------ */
/*  field_rand tests                                                    */
/* ------------------------------------------------------------------ */

static void test_rand(void) {
    printf("\n--- field_rand ---\n");

    int in_range = 1;
    uint32_t nonzero_count = 0;
    const int N = 10000;

    for (int i = 0; i < N; i++) {
        field_t r = field_rand();
        if (r >= (uint32_t)P) {
            printf("  FAIL  field_rand returned out-of-range value: %u\n", r);
            in_range = 0;
            _failures++;
            break;
        }
        if (r != 0) nonzero_count++;
    }

    if (in_range) {
        printf("  PASS  all %d random values in [0, p-1]\n", N);
    }

    /*
     * Statistical sanity: with 10000 draws, the probability of seeing
     * zero nonzero values is (1/p)^10000, which is effectively impossible.
     * If nonzero_count == 0, the RNG is broken.
     */
    if (nonzero_count == 0) {
        printf("  FAIL  field_rand returned 0 for all %d calls (RNG broken?)\n", N);
        _failures++;
    } else {
        printf("  PASS  %u/%d values are non-zero (RNG not stuck at 0)\n",
               nonzero_count, N);
    }
}

/* ------------------------------------------------------------------ */
/*  main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== Field arithmetic tests (p = %u) ===\n", P);

    test_add();
    test_sub();
    test_mul();
    test_inv();
    test_rand();

    printf("\n");
    if (_failures == 0) {
        printf("ALL FIELD TESTS PASSED\n");
        return 0;
    } else {
        printf("FAILED: %d test(s) failed\n", _failures);
        return 1;
    }
}
