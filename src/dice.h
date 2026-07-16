/* tavernroll — dice notation engine
 * Public interface for parsing and evaluating tabletop dice expressions.
 */
#ifndef TAVERNROLL_DICE_H
#define TAVERNROLL_DICE_H

#include <stdint.h>

#define TR_MAX_TERMS   64
#define TR_MAX_DICE    512
#define TR_EXPLODE_CAP 64   /* safety cap on chained explosions per die */

/* Deterministic PRNG (splitmix64 seeded xorshift128+) so results are
 * reproducible across platforms given the same --seed. */
typedef struct {
    uint64_t s0, s1;
} TrRng;

void   tr_rng_seed(TrRng *rng, uint64_t seed);
int    tr_rng_die(TrRng *rng, int sides); /* uniform 1..sides */

/* Keep/drop selection modes for a dice term. */
typedef enum {
    TR_KEEP_NONE = 0,
    TR_KEEP_HIGH,   /* khN */
    TR_KEEP_LOW,    /* klN */
    TR_DROP_HIGH,   /* dhN */
    TR_DROP_LOW     /* dlN */
} TrKeepMode;

typedef struct {
    int        sign;      /* +1 or -1 */
    int        is_const;  /* 1 => flat modifier, value in cvalue */
    int        cvalue;    /* constant value when is_const */
    int        count;     /* number of dice */
    int        sides;     /* faces per die */
    TrKeepMode keep_mode;
    int        keep_n;    /* argument to keep/drop */
    int        explode;   /* 1 => dice explode on max face */
} TrTerm;

typedef struct {
    int   repeat;                 /* leading NxN repeat count */
    int   nterms;
    TrTerm terms[TR_MAX_TERMS];
    char  error[128];             /* empty on success */
} TrExpr;

/* Per-die detail captured for pretty printing. */
typedef struct {
    int values[TR_MAX_DICE];
    int kept[TR_MAX_DICE];
    int exploded[TR_MAX_DICE];
    int n;
    int sides;
    int subtotal;
} TrDiceRoll;

/* Parse an expression string. Returns 0 on success, -1 on error
 * (message written to out->error). */
int tr_parse(const char *input, TrExpr *out);

/* Evaluate one instance of the expression, returning the grand total.
 * If details/ndetails are non-NULL, fills up to *ndetails dice-term rolls. */
int tr_eval(const TrExpr *expr, TrRng *rng,
            TrDiceRoll *details, int *ndetails);

/* Evaluate a single dice term into a detailed roll. */
void tr_roll_term(const TrTerm *t, TrRng *rng, TrDiceRoll *out);

#endif
