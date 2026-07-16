/* tavernroll — a tabletop dice roller for your terminal
 * Parses standard dice notation and rolls with a fair, seedable engine.
 */
#include "dice.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

#define VERSION "1.0.0"

/* ---- palette (tavern amber on ink) ---- */
static int use_color = 1;
#define C_RESET  (use_color ? "\x1b[0m"  : "")
#define C_AMBER  (use_color ? "\x1b[38;5;179m" : "") /* primary */
#define C_INK    (use_color ? "\x1b[38;5;250m" : "") /* body */
#define C_DIM    (use_color ? "\x1b[38;5;244m" : "") /* dropped dice */
#define C_CRIT   (use_color ? "\x1b[38;5;114m" : "") /* max face */
#define C_FUMBLE (use_color ? "\x1b[38;5;174m" : "") /* natural 1 */
#define C_BOLD   (use_color ? "\x1b[1m"   : "")

static void print_logo(void) {
    printf("%s   /\\   %s\n", C_AMBER, C_RESET);
    printf("%s  /  \\  %s %stavernroll%s  %sv%s%s\n",
           C_AMBER, C_RESET, C_BOLD, C_RESET, C_DIM, VERSION, C_RESET);
    printf("%s  \\  /  %s %sroll the bones%s\n", C_AMBER, C_RESET, C_DIM, C_RESET);
    printf("%s   \\/   %s\n", C_AMBER, C_RESET);
}

static void usage(void) {
    print_logo();
    printf("\n%sUsage:%s tavernroll [options] <expression>\n\n", C_BOLD, C_RESET);
    printf("%sExpressions%s\n", C_BOLD, C_RESET);
    printf("  2d6+3        two six-siders plus three\n");
    printf("  1d20         a single d20\n");
    printf("  4d6kh3       roll 4d6, keep the highest 3 (ability scores)\n");
    printf("  2d20kl1      keep the lowest of two d20 (disadvantage)\n");
    printf("  4d6dl1       roll 4d6, drop the lowest\n");
    printf("  3d6!         exploding sixes\n");
    printf("  6x(4d6kh3)   repeat a roll six times\n");
    printf("  adv / dis    shorthand for advantage / disadvantage\n\n");
    printf("%sOptions%s\n", C_BOLD, C_RESET);
    printf("  -s, --seed N   deterministic seed (reproducible rolls)\n");
    printf("  -n, --stats    show the odds for the expression\n");
    printf("      --trials N  sample size for --stats (default 100000)\n");
    printf("      --no-color  plain output\n");
    printf("  -h, --help     this help\n");
    printf("  -v, --version  version\n");
}

/* Parentheses are only sugar around a repeat, e.g. 6x(4d6kh3). Drop the
 * paren characters in place so the "6x" prefix and inner grammar survive. */
static void strip_parens(char *s) {
    char *w = s;
    for (char *r = s; *r; r++)
        if (*r != '(' && *r != ')') *w++ = *r;
    *w = '\0';
}

static void print_die(int v, int sides, int kept, int exploded) {
    const char *col = C_INK;
    if (!kept)           col = C_DIM;
    else if (v >= sides) col = C_CRIT;
    else if (v == 1)     col = C_FUMBLE;
    if (kept) printf("%s%d%s%s", col, v, exploded ? "!" : "", C_RESET);
    else      printf("%s(%d)%s", col, v, C_RESET);
}

static void print_roll(const TrExpr *expr, TrRng *rng) {
    TrDiceRoll details[TR_MAX_TERMS];
    int nd = TR_MAX_TERMS;
    int total = tr_eval(expr, rng, details, &nd);

    printf("  %s=> %s%s%d%s", C_DIM, C_BOLD, C_AMBER, total, C_RESET);
    if (nd > 0) {
        printf("   %s", C_DIM);
        int di = 0;
        for (int i = 0; i < expr->nterms; i++) {
            const TrTerm *t = &expr->terms[i];
            if (t->is_const) {
                printf("%s%s%d%s", C_RESET, t->sign < 0 ? "-" : "+", t->cvalue, C_DIM);
                continue;
            }
            if (i > 0) printf("%s %s ", C_DIM, t->sign < 0 ? "-" : "+");
            printf("%s[%s", C_DIM, C_RESET);
            const TrDiceRoll *r = &details[di++];
            for (int k = 0; k < r->n; k++) {
                if (k) printf(" ");
                print_die(r->values[k], r->sides, r->kept[k], r->exploded[k]);
            }
            printf("%s]%s", C_DIM, C_RESET);
        }
        printf("%s", C_RESET);
    }
    printf("\n");
}

static void run_stats(const TrExpr *expr, uint64_t seed, long trials) {
    TrRng rng; tr_rng_seed(&rng, seed);
    int mn = 1 << 30, mx = -(1 << 30);
    double sum = 0, sumsq = 0;
    long counts_size = 4096;
    long *hist = calloc(counts_size, sizeof(long));
    long offset = 2048;
    for (long i = 0; i < trials; i++) {
        int v = tr_eval(expr, &rng, NULL, NULL);
        if (v < mn) mn = v;
        if (v > mx) mx = v;
        sum += v; sumsq += (double)v * v;
        long bucket = v + offset;
        if (bucket >= 0 && bucket < counts_size) hist[bucket]++;
    }
    double mean = sum / trials;
    double var = sumsq / trials - mean * mean;
    double sd = var > 0 ? sqrt(var) : 0;

    printf("  %s%sodds over %ld rolls%s\n", C_BOLD, C_AMBER, trials, C_RESET);
    printf("  %smin%s %d   %smax%s %d   %smean%s %.2f   %sstd%s %.2f\n",
           C_DIM, C_RESET, mn, C_DIM, C_RESET, mx,
           C_DIM, C_RESET, mean, C_DIM, C_RESET, sd);

    /* find peak for bar scaling */
    long peak = 1;
    for (int v = mn; v <= mx; v++) { long c = hist[v + offset]; if (c > peak) peak = c; }
    for (int v = mn; v <= mx; v++) {
        long c = hist[v + offset];
        double pct = 100.0 * c / trials;
        int bars = (int)(28.0 * c / peak + 0.5);
        printf("  %s%4d%s %s", C_INK, v, C_RESET, C_AMBER);
        for (int b = 0; b < bars; b++) printf("#");
        printf("%s %s%.1f%%%s\n", C_RESET, C_DIM, pct, C_RESET);
    }
    free(hist);
}

int main(int argc, char **argv) {
    uint64_t seed = 0;
    int have_seed = 0;
    int stats = 0;
    long trials = 100000;
    char expr_buf[512] = {0};

    if (!isatty(1)) use_color = 0;
    if (getenv("NO_COLOR")) use_color = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 0; }
        if (!strcmp(a, "-v") || !strcmp(a, "--version")) { printf("tavernroll %s\n", VERSION); return 0; }
        if (!strcmp(a, "--no-color")) { use_color = 0; continue; }
        if (!strcmp(a, "-n") || !strcmp(a, "--stats")) { stats = 1; continue; }
        if (!strcmp(a, "-s") || !strcmp(a, "--seed")) {
            if (i + 1 >= argc) { fprintf(stderr, "seed needs a value\n"); return 2; }
            seed = strtoull(argv[++i], NULL, 10); have_seed = 1; continue;
        }
        if (!strcmp(a, "--trials")) {
            if (i + 1 >= argc) { fprintf(stderr, "--trials needs a value\n"); return 2; }
            trials = strtol(argv[++i], NULL, 10);
            if (trials < 1) trials = 1;
            if (trials > 10000000) trials = 10000000;
            continue;
        }
        /* accumulate expression tokens */
        if (expr_buf[0]) strncat(expr_buf, " ", sizeof(expr_buf) - strlen(expr_buf) - 1);
        strncat(expr_buf, a, sizeof(expr_buf) - strlen(expr_buf) - 1);
    }

    if (!expr_buf[0]) { usage(); return 0; }

    if (!have_seed) {
        seed = ((uint64_t)time(NULL) << 20) ^ (uint64_t)getpid() ^ 0x9e3779b9u;
    }

    strip_parens(expr_buf);

    TrExpr expr;
    if (tr_parse(expr_buf, &expr) != 0) {
        fprintf(stderr, "%scan't read that:%s %s\n", C_FUMBLE, C_RESET, expr.error);
        fprintf(stderr, "try:  tavernroll 2d6+3   (see --help)\n");
        return 1;
    }

    if (stats) {
        run_stats(&expr, seed, trials);
        return 0;
    }

    TrRng rng; tr_rng_seed(&rng, seed);
    for (int r = 0; r < expr.repeat; r++)
        print_roll(&expr, &rng);

    return 0;
}
