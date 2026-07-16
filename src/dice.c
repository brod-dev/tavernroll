#include "dice.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

/* ---- PRNG: splitmix64 to seed xoroshiro128+ ---- */
static uint64_t splitmix64(uint64_t *x) {
    uint64_t z = (*x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

void tr_rng_seed(TrRng *rng, uint64_t seed) {
    uint64_t sm = seed ? seed : 0x123456789ABCDEFULL;
    rng->s0 = splitmix64(&sm);
    rng->s1 = splitmix64(&sm);
    if (rng->s0 == 0 && rng->s1 == 0) rng->s1 = 1;
}

static uint64_t rng_next(TrRng *rng) {
    uint64_t s0 = rng->s0, s1 = rng->s1;
    uint64_t r = s0 + s1;
    s1 ^= s0;
    rng->s0 = rotl(s0, 55) ^ s1 ^ (s1 << 14);
    rng->s1 = rotl(s1, 36);
    return r;
}

/* Unbiased die roll in [1, sides] via rejection sampling. */
int tr_rng_die(TrRng *rng, int sides) {
    if (sides <= 1) return sides < 1 ? 0 : 1;
    uint64_t range = (uint64_t)sides;
    uint64_t limit = UINT64_MAX - (UINT64_MAX % range);
    uint64_t x;
    do { x = rng_next(rng); } while (x >= limit);
    return (int)(x % range) + 1;
}

/* ---- Parser ---- */
/* case-insensitive 3-char prefix match, portable under -std=c11 */
static int ci_eq3(const char *p, const char *w) {
    for (int i = 0; i < 3; i++) {
        int a = tolower((unsigned char)p[i]);
        int b = tolower((unsigned char)w[i]);
        if (a != b) return 0;
    }
    return 1;
}

static void set_err(TrExpr *e, const char *msg) {
    snprintf(e->error, sizeof(e->error), "%s", msg);
}

/* Parse a non-negative integer, advancing *p. Returns -1 if none. */
static int parse_int(const char **p) {
    const char *s = *p;
    if (!isdigit((unsigned char)*s)) return -1;
    long v = 0;
    while (isdigit((unsigned char)*s)) {
        v = v * 10 + (*s - '0');
        if (v > 1000000) v = 1000000;
        s++;
    }
    *p = s;
    return (int)v;
}

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

int tr_parse(const char *input, TrExpr *out) {
    memset(out, 0, sizeof(*out));
    out->repeat = 1;
    out->error[0] = '\0';
    if (!input) { set_err(out, "empty expression"); return -1; }

    const char *p = skip_ws(input);
    if (*p == '\0') { set_err(out, "empty expression"); return -1; }

    /* adv / dis shorthand -> 2d20kh1 / 2d20kl1 */
    if (ci_eq3(p, "adv") && (p[3] == '\0' || isspace((unsigned char)p[3]))) {
        input = "2d20kh1";  p = input;
    } else if (ci_eq3(p, "dis") && (p[3] == '\0' || isspace((unsigned char)p[3]))) {
        input = "2d20kl1";  p = input;
    }

    /* optional leading repeat: N x ... */
    {
        const char *save = p;
        int n = parse_int(&p);
        const char *q = skip_ws(p);
        if (n >= 0 && (*q == 'x' || *q == 'X')) {
            if (n < 1 || n > 1000) { set_err(out, "repeat count must be 1-1000"); return -1; }
            out->repeat = n;
            p = q + 1;
            p = skip_ws(p);
        } else {
            p = save; /* not a repeat prefix */
        }
    }

    int sign = 1;
    int first = 1;
    while (1) {
        p = skip_ws(p);
        if (*p == '\0') break;

        if (*p == '+') { sign = 1; p++; first = 0; continue; }
        if (*p == '-') { sign = -1; p++; first = 0; continue; }

        if (out->nterms >= TR_MAX_TERMS) { set_err(out, "expression too long"); return -1; }
        TrTerm *t = &out->terms[out->nterms];
        memset(t, 0, sizeof(*t));
        t->sign = sign;

        int lead = parse_int(&p);            /* dice count or constant */
        if (*p == 'd' || *p == 'D') {
            p++;
            int sides = parse_int(&p);
            if (sides < 1) { set_err(out, "die needs a face count, e.g. d6"); return -1; }
            if (sides > 100000) { set_err(out, "die has too many faces"); return -1; }
            t->count = (lead < 0) ? 1 : lead;
            if (t->count < 1) { set_err(out, "dice count must be at least 1"); return -1; }
            if (t->count > TR_MAX_DICE) { set_err(out, "too many dice in one term"); return -1; }
            t->sides = sides;

            /* optional keep/drop */
            if ((p[0] == 'k' || p[0] == 'd') && (p[1] == 'h' || p[1] == 'l')) {
                int drop = (p[0] == 'd');
                int high = (p[1] == 'h');
                p += 2;
                int kn = parse_int(&p);
                if (kn < 0) kn = 1;
                if (kn > t->count) kn = t->count;
                t->keep_n = kn;
                if (drop)      t->keep_mode = high ? TR_DROP_HIGH : TR_DROP_LOW;
                else           t->keep_mode = high ? TR_KEEP_HIGH : TR_KEEP_LOW;
            }
            if (*p == '!') { t->explode = 1; p++; }
            out->nterms++;
        } else {
            /* constant term */
            if (lead < 0) { set_err(out, "unexpected character in expression"); return -1; }
            t->is_const = 1;
            t->cvalue = lead;
            out->nterms++;
        }
        sign = 1;
        first = 0;
        (void)first;
    }

    if (out->nterms == 0) { set_err(out, "no dice or values found"); return -1; }
    return 0;
}

/* ---- Evaluation ---- */
void tr_roll_term(const TrTerm *t, TrRng *rng, TrDiceRoll *out) {
    memset(out, 0, sizeof(*out));
    out->sides = t->sides;
    out->n = t->count;
    for (int i = 0; i < t->count; i++) {
        int v = tr_rng_die(rng, t->sides);
        int exploded = 0;
        if (t->explode && t->sides > 1) {
            int rolls = 0;
            int last = v;
            while (last == t->sides && rolls < TR_EXPLODE_CAP) {
                last = tr_rng_die(rng, t->sides);
                v += last;
                exploded = 1;
                rolls++;
            }
        }
        out->values[i] = v;
        out->exploded[i] = exploded;
        out->kept[i] = 1; /* default keep all */
    }

    /* keep/drop: operate on per-die values */
    if (t->keep_mode != TR_KEEP_NONE) {
        /* order indices by value */
        int idx[TR_MAX_DICE];
        for (int i = 0; i < t->count; i++) idx[i] = i;
        for (int i = 0; i < t->count; i++)
            for (int j = i + 1; j < t->count; j++)
                if (out->values[idx[j]] > out->values[idx[i]]) {
                    int tmp = idx[i]; idx[i] = idx[j]; idx[j] = tmp;
                } /* idx now sorted high->low */

        for (int i = 0; i < t->count; i++) out->kept[i] = 0;
        int k = t->keep_n;
        if (t->keep_mode == TR_KEEP_HIGH) {
            for (int i = 0; i < k; i++) out->kept[idx[i]] = 1;
        } else if (t->keep_mode == TR_KEEP_LOW) {
            for (int i = 0; i < k; i++) out->kept[idx[t->count - 1 - i]] = 1;
        } else if (t->keep_mode == TR_DROP_HIGH) {
            for (int i = 0; i < t->count; i++) out->kept[i] = 1;
            for (int i = 0; i < k; i++) out->kept[idx[i]] = 0;
        } else if (t->keep_mode == TR_DROP_LOW) {
            for (int i = 0; i < t->count; i++) out->kept[i] = 1;
            for (int i = 0; i < k; i++) out->kept[idx[t->count - 1 - i]] = 0;
        }
    }

    int sub = 0;
    for (int i = 0; i < t->count; i++)
        if (out->kept[i]) sub += out->values[i];
    out->subtotal = sub;
}

int tr_eval(const TrExpr *expr, TrRng *rng,
            TrDiceRoll *details, int *ndetails) {
    int total = 0;
    int dcount = 0;
    for (int i = 0; i < expr->nterms; i++) {
        const TrTerm *t = &expr->terms[i];
        if (t->is_const) {
            total += t->sign * t->cvalue;
            continue;
        }
        TrDiceRoll r;
        tr_roll_term(t, rng, &r);
        total += t->sign * r.subtotal;
        if (details && ndetails && dcount < *ndetails) {
            details[dcount++] = r;
        }
    }
    if (ndetails) *ndetails = dcount;
    return total;
}
