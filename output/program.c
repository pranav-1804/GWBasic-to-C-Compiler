/* ============================================================
 *  GW-BASIC  ->  C   transpiler runtime  (prolog.c)
 *  This file is emitted verbatim at the TOP of every generated
 *  program, before the translated BASIC lines.
 *
 *  Design: the transpiler turns each BASIC expression/statement
 *  into a sequence of calls against a small stack machine that
 *  is implemented here.  Values are TAGGED so that type checking
 *  happens at *runtime* (as required by the assignment), and the
 *  runtime emits a WARNING - never a C-compiler error - when it
 *  meets an unassigned variable, a wrong type, or a jump to a
 *  line number that does not exist.
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Every BASIC line gets a pair of labels so any line can be a jump target.
 * Lines that are never jumped to therefore have "unused" labels - that is by
 * design, so we silence the cosmetic warning (it is not an error). */
#pragma GCC diagnostic ignored "-Wunused-label"

/* ---------- Tagged value ---------- */
typedef enum { T_NUM, T_STR } VType;
typedef struct { VType type; double num; char *str; } Value;

static Value make_num(double d) { Value v; v.type = T_NUM; v.num = d;  v.str = NULL; return v; }
static Value make_str(char *s)  { Value v; v.type = T_STR; v.num = 0;  v.str = s;    return v; }

/* ---------- Operand stack ---------- */
#define STACK_MAX 1024
static Value vstack[STACK_MAX];
static int   vsp = 0;

static void stack_push(Value v) {
    if (vsp >= STACK_MAX) { fprintf(stderr, "RUNTIME ERROR: operand stack overflow\n"); exit(1); }
    vstack[vsp++] = v;
}
static Value stack_pop(void) {
    if (vsp <= 0) { fprintf(stderr, "RUNTIME ERROR: operand stack underflow\n"); exit(1); }
    return vstack[--vsp];
}

/* require a numeric operand; warn (do not crash) on type mismatch */
static double as_num(Value v, const char *op) {
    if (v.type != T_NUM) {
        fprintf(stderr, "RUNTIME WARNING: operator '%s' expected a number but got a string; using 0\n", op);
        return 0.0;
    }
    return v.num;
}

/* ---------- Variable table ---------- */
#define VAR_MAX 256
typedef struct { char name[64]; int assigned; Value val; } VarSlot;
static VarSlot vars[VAR_MAX];
static int     var_count = 0;

static int is_string_var(const char *name) {       /* GW-BASIC: A$ is a string variable */
    size_t n = strlen(name);
    return n > 0 && name[n - 1] == '$';
}
static VarSlot *var_find(const char *name) {
    for (int i = 0; i < var_count; i++)
        if (strcmp(vars[i].name, name) == 0) return &vars[i];
    return NULL;
}
static VarSlot *var_intern(const char *name) {
    VarSlot *s = var_find(name);
    if (s) return s;
    if (var_count >= VAR_MAX) { fprintf(stderr, "RUNTIME ERROR: too many variables\n"); exit(1); }
    s = &vars[var_count++];
    strncpy(s->name, name, sizeof(s->name) - 1);
    s->name[sizeof(s->name) - 1] = '\0';
    s->assigned = 0;
    s->val = is_string_var(name) ? make_str(strdup("")) : make_num(0);
    return s;
}

/* ---------- Loads / stores ---------- */
void LOAD_CONST(double value) { stack_push(make_num(value)); }

void LOAD_STR(const char *literal) {            /* literal still carries its surrounding quotes */
    size_t n = strlen(literal);
    char *s;
    if (n >= 2 && literal[0] == '"' && literal[n - 1] == '"') {
        s = (char *)malloc(n - 1);
        memcpy(s, literal + 1, n - 2);
        s[n - 2] = '\0';
    } else {
        s = strdup(literal);
    }
    stack_push(make_str(s));
}

void LOAD_VAR(const char *name) {
    VarSlot *s = var_find(name);
    if (!s || !s->assigned) {                   /* unassigned variable -> warn, use default */
        fprintf(stderr, "RUNTIME WARNING: variable '%s' used before assignment; using %s\n",
                name, is_string_var(name) ? "\"\"" : "0");
        stack_push(is_string_var(name) ? make_str(strdup("")) : make_num(0));
        return;
    }
    stack_push(s->val);
}

void STORE(const char *name) {
    Value v = stack_pop();
    if (is_string_var(name) && v.type != T_STR)
        fprintf(stderr, "RUNTIME WARNING: assigning a number to string variable '%s'\n", name);
    if (!is_string_var(name) && v.type != T_NUM)
        fprintf(stderr, "RUNTIME WARNING: assigning a string to numeric variable '%s'\n", name);
    VarSlot *s = var_intern(name);
    s->val = v;
    s->assigned = 1;
}

/* ---------- Arithmetic ---------- */
void ADD(void) {
    Value b = stack_pop(), a = stack_pop();
    if (a.type == T_STR && b.type == T_STR) {           /* "+"  also concatenates strings */
        size_t n = strlen(a.str) + strlen(b.str) + 1;
        char *s = (char *)malloc(n);
        strcpy(s, a.str); strcat(s, b.str);
        stack_push(make_str(s));
        return;
    }
    stack_push(make_num(as_num(a, "+") + as_num(b, "+")));
}
void SUB(void) { Value b = stack_pop(), a = stack_pop(); stack_push(make_num(as_num(a, "-") - as_num(b, "-"))); }
void MUL(void) { Value b = stack_pop(), a = stack_pop(); stack_push(make_num(as_num(a, "*") * as_num(b, "*"))); }
void DIV(void) {
    Value b = stack_pop(), a = stack_pop();
    double db = as_num(b, "/");
    if (db == 0) { fprintf(stderr, "RUNTIME WARNING: division by zero; using 0\n"); stack_push(make_num(0)); return; }
    stack_push(make_num(as_num(a, "/") / db));
}
void MOD(void) {
    Value b = stack_pop(), a = stack_pop();
    double db = as_num(b, "%");
    if (db == 0) { fprintf(stderr, "RUNTIME WARNING: modulo by zero; using 0\n"); stack_push(make_num(0)); return; }
    double da = as_num(a, "%");
    stack_push(make_num(da - (double)((long)(da / db)) * db));   /* remainder; avoids libm */
}

/* ---------- Comparisons  (GW-BASIC: true = -1, false = 0) ---------- */
static int cmp_vals(Value a, Value b) {                 /* -1 / 0 / 1 */
    if (a.type == T_STR && b.type == T_STR) { int r = strcmp(a.str, b.str); return r < 0 ? -1 : (r > 0 ? 1 : 0); }
    double x = as_num(a, "compare"), y = as_num(b, "compare");
    return x < y ? -1 : (x > y ? 1 : 0);
}
void CMP_LT(void) { Value b = stack_pop(), a = stack_pop(); stack_push(make_num(cmp_vals(a, b) <  0 ? -1 : 0)); }
void CMP_GT(void) { Value b = stack_pop(), a = stack_pop(); stack_push(make_num(cmp_vals(a, b) >  0 ? -1 : 0)); }
void CMP_LE(void) { Value b = stack_pop(), a = stack_pop(); stack_push(make_num(cmp_vals(a, b) <= 0 ? -1 : 0)); }
void CMP_GE(void) { Value b = stack_pop(), a = stack_pop(); stack_push(make_num(cmp_vals(a, b) >= 0 ? -1 : 0)); }
void CMP_EQ(void) { Value b = stack_pop(), a = stack_pop(); stack_push(make_num(cmp_vals(a, b) == 0 ? -1 : 0)); }
void CMP_NE(void) { Value b = stack_pop(), a = stack_pop(); stack_push(make_num(cmp_vals(a, b) != 0 ? -1 : 0)); }

/* ---------- Logical operators (bitwise on integers, GW-BASIC style) ---------- */
void LOGIC_AND(void) { long b = (long)as_num(stack_pop(), "AND"), a = (long)as_num(stack_pop(), "AND"); stack_push(make_num((double)(a & b))); }
void LOGIC_OR (void) { long b = (long)as_num(stack_pop(), "OR"),  a = (long)as_num(stack_pop(), "OR");  stack_push(make_num((double)(a | b))); }
void LOGIC_NOT(void) { long a = (long)as_num(stack_pop(), "NOT"); stack_push(make_num((double)(~a))); }

/* unary minus */
void NEG(void) { Value a = stack_pop(); stack_push(make_num(-as_num(a, "unary -"))); }

/* truth value for conditional jumps */
int pop_truth(void) {
    Value v = stack_pop();
    if (v.type == T_STR) { fprintf(stderr, "RUNTIME WARNING: string used as a condition; treated as false\n"); return 0; }
    return v.num != 0;
}

/* ---------- PRINT ---------- */
void PRINT(void) {
    Value v = stack_pop();
    if (v.type == T_STR) { printf("%s\n", v.str); return; }
    double d = v.num;
    if (d == (long)d) printf("%ld\n", (long)d);     /* whole numbers print without a decimal point */
    else              printf("%g\n", d);
}

/* ---------- FOR / NEXT helpers ---------- */
void FOR_CONTINUE(const char *var, const char *lim, const char *step) {
    double v = var_intern(var)->val.num;
    double l = var_intern(lim)->val.num;
    double s = var_intern(step)->val.num;
    int cont = (s >= 0) ? (v <= l) : (v >= l);
    stack_push(make_num(cont ? -1 : 0));
}
void STEP_VAR(const char *var, const char *step) {
    VarSlot *sv = var_intern(var);
    double s = var_intern(step)->val.num;
    sv->val = make_num(sv->val.num + s);
    sv->assigned = 1;
}

/* ---------- GOSUB / RETURN return-address stack ---------- */
#define GOSUB_MAX 256
static int gosub_stack[GOSUB_MAX];
static int gsp = 0;
void gosub_push(int rid) {
    if (gsp >= GOSUB_MAX) { fprintf(stderr, "RUNTIME ERROR: GOSUB nested too deep\n"); exit(1); }
    gosub_stack[gsp++] = rid;
}
int gosub_pop(void) {
    if (gsp <= 0) { fprintf(stderr, "RUNTIME WARNING: RETURN without GOSUB; ignored\n"); return -1; }
    return gosub_stack[--gsp];
}

/* ---------- undefined GOTO/GOSUB target ---------- */
void bad_line_number(int n) { fprintf(stderr, "RUNTIME WARNING: jump to undefined line %d; ignored\n", n); }

/* ---------- jump macros ---------- */
#define JUMP(label)          goto label
#define JUMP_IF_FALSE(label) if (!pop_truth()) goto label

int main(void) {
  label_10_init:
    LOAD_CONST(51);
    STORE("A");
  label_10_fini:
  label_20_init:
  while_20_start:
    LOAD_VAR("A");
    JUMP_IF_FALSE(while_20_end);
  label_20_fini:
  label_30_init:
    LOAD_VAR("A");
    PRINT();
  label_30_fini:
  label_40_init:
    LOAD_VAR("A");
    LOAD_CONST(1);
    SUB();
    STORE("A");
  label_40_fini:
  label_50_init:
    JUMP(while_20_start);
  while_20_end:
  label_50_fini:
    JUMP(__prog_end);
  __prog_end: ;
    return 0;
}
/* ===== end of generated GW-BASIC program ===== */
