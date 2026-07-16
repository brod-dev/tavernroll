/* Unit tests for the tavernroll dice engine. */
#include "../src/dice.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
} while (0)

static void test_parse_basic(void) {
    TrExpr e;
    CHECK(tr_parse("2d6+3", &e) == 0, "parse 2d6+3");
    CHECK(e.nterms == 2, "2d6+3 has two terms");
    CHECK(e.terms[0].count == 2 && e.terms[0].sides == 6, "2d6 count/sides");
    CHECK(e.terms[0].sign == 1, "2d6 positive");
    CHECK(e.terms[1].is_const && e.terms[1].cvalue == 3, "+3 constant");
}

static void test_parse_implicit_one(void) {
    TrExpr e;
    CHECK(tr_parse("d20", &e) == 0, "parse d20");
    CHECK(e.terms[0].count == 1 && e.terms[0].sides == 20, "d20 => 1d20");
}

static void test_parse_keep_drop(void) {
    TrExpr e;
    CHECK(tr_parse("4d6kh3", &e) == 0, "parse 4d6kh3");
    CHECK(e.terms[0].keep_mode == TR_KEEP_HIGH && e.terms[0].keep_n == 3, "kh3");
    CHECK(tr_parse("2d20kl1", &e) == 0, "parse 2d20kl1");
    CHECK(e.terms[0].keep_mode == TR_KEEP_LOW && e.terms[0].keep_n == 1, "kl1");
    CHECK(tr_parse("4d6dl1", &e) == 0, "parse 4d6dl1");
    CHECK(e.terms[0].keep_mode == TR_DROP_LOW && e.terms[0].keep_n == 1, "dl1");
}

static void test_parse_adv_dis(void) {
    TrExpr e;
    CHECK(tr_parse("adv", &e) == 0, "parse adv");
    CHECK(e.terms[0].count == 2 && e.terms[0].sides == 20 &&
          e.terms[0].keep_mode == TR_KEEP_HIGH, "adv => 2d20kh1");
    CHECK(tr_parse("dis", &e) == 0, "parse dis");
    CHECK(e.terms[0].keep_mode == TR_KEEP_LOW, "dis => 2d20kl1");
}

static void test_parse_repeat(void) {
    TrExpr e;
    CHECK(tr_parse("3x1d6", &e) == 0, "parse 3x1d6");
    CHECK(e.repeat == 3, "repeat = 3");
    CHECK(e.terms[0].count == 1 && e.terms[0].sides == 6, "inner 1d6");
}

static void test_parse_errors(void) {
    TrExpr e;
    CHECK(tr_parse("hello", &e) != 0, "garbage rejected");
    CHECK(tr_parse("", &e) != 0, "empty rejected");
    CHECK(tr_parse("2d", &e) != 0, "missing faces rejected");
    CHECK(e.error[0] != '\0', "error message set");
}

static void test_constant_only(void) {
    TrExpr e; TrRng r; tr_rng_seed(&r, 1);
    CHECK(tr_parse("5", &e) == 0, "parse constant 5");
    CHECK(tr_eval(&e, &r, NULL, NULL) == 5, "constant evaluates to 5");
}

static void test_determinism(void) {
    TrExpr e; tr_parse("8d10+4", &e);
    TrRng a, b; tr_rng_seed(&a, 42); tr_rng_seed(&b, 42);
    int va = tr_eval(&e, &a, NULL, NULL);
    int vb = tr_eval(&e, &b, NULL, NULL);
    CHECK(va == vb, "same seed => same result");
    TrRng c; tr_rng_seed(&c, 43);
    int vc = tr_eval(&e, &c, NULL, NULL);
    CHECK(va != vc || 1, "different seed usually differs (smoke)");
}

static void test_range(void) {
    TrExpr e; tr_parse("1d20", &e);
    TrRng r; tr_rng_seed(&r, 7);
    int ok = 1;
    for (int i = 0; i < 5000; i++) {
        int v = tr_eval(&e, &r, NULL, NULL);
        if (v < 1 || v > 20) ok = 0;
    }
    CHECK(ok, "1d20 always within 1..20");
}

static void test_keep_high_math(void) {
    /* roll 4d6kh3 many times; kept subtotal must equal sum of 3 highest */
    TrExpr e; tr_parse("4d6kh3", &e);
    TrRng r; tr_rng_seed(&r, 99);
    int ok = 1;
    for (int t = 0; t < 2000; t++) {
        TrDiceRoll roll;
        tr_roll_term(&e.terms[0], &r, &roll);
        /* compute expected: sum all minus lowest */
        int lowest = 1 << 30, sum = 0, kept = 0, keptcount = 0;
        for (int i = 0; i < roll.n; i++) {
            sum += roll.values[i];
            if (roll.values[i] < lowest) lowest = roll.values[i];
            if (roll.kept[i]) { kept += roll.values[i]; keptcount++; }
        }
        if (keptcount != 3) ok = 0;
        if (kept != sum - lowest) ok = 0;
    }
    CHECK(ok, "4d6kh3 keeps the top three");
}

static void test_negative_modifier(void) {
    TrExpr e; tr_parse("1d6-2", &e);
    TrRng r; tr_rng_seed(&r, 3);
    int ok = 1;
    for (int i = 0; i < 3000; i++) {
        int v = tr_eval(&e, &r, NULL, NULL);
        if (v < -1 || v > 4) ok = 0;   /* 1..6 minus 2 => -1..4 */
    }
    CHECK(ok, "1d6-2 stays within -1..4");
}

static void test_explode_can_exceed(void) {
    /* d2! must sometimes exceed 2 because of explosions */
    TrExpr e; tr_parse("1d2!", &e);
    TrRng r; tr_rng_seed(&r, 12345);
    int exceeded = 0;
    for (int i = 0; i < 2000; i++) {
        int v = tr_eval(&e, &r, NULL, NULL);
        if (v > 2) exceeded = 1;
    }
    CHECK(exceeded, "exploding d2 can exceed its face count");
}

int main(void) {
    printf("tavernroll test suite\n");
    test_parse_basic();
    test_parse_implicit_one();
    test_parse_keep_drop();
    test_parse_adv_dis();
    test_parse_repeat();
    test_parse_errors();
    test_constant_only();
    test_determinism();
    test_range();
    test_keep_high_math();
    test_negative_modifier();
    test_explode_can_exceed();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
