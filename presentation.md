# GW‑BASIC → C Transpiler
### Compiler Construction project — built on Peter Heusch's JavaCC sample

A tool that reads a GW‑BASIC program (`.bas`) and emits **primitive C**, which is then
compiled by an ordinary C compiler (gcc / CLion).

---

## 1. The problem

* GW‑BASIC is an old line‑numbered language: programs jump around with `GOTO`, `GOSUB`,
  `IF…THEN <line>`.
* Goal: **translate** (not interpret) such a program into plain C, so it can be compiled and
  run as a native executable.
* Tooling: **JavaCC** generates the parser; a small **C runtime** (`prolog.c` / `epilog.c`)
  supplies the operations the generated code calls.

---

## 2. The pipeline

```
            JavaCC                         gcc / CLion
  TEST.BAS  ───────►  java GWBasic  ───►  target/TEST.c  ───►  ./program
 (GW-BASIC)          (our transpiler)      (primitive C)        (output)
```

A translated program is always three parts concatenated:

1. **`prolog.c`** — the C runtime: an operand stack, a variable table, an array table, and
   one C function per BASIC operation (`LOAD_CONST`, `PRINT`, `ADD`, `STORE`, …). Ends with
   `int main(void) {`.
2. **the translated lines** — each BASIC line becomes a flat list of those calls, framed by
   two C labels: `label_<n>_init:` … `label_<n>_fini:`.
3. **`epilog.c`** — closes `main()`; holds the `__prog_end:` label.

Because every operation is a *function call against a stack*, the generated C stays
"primitive": no expression trees, no temporaries — just calls and `goto`s.

> Example — `30 PRINT A` becomes:
> ```c
>   label_30_init:
>     LOAD_VAR("A");
>     PRINT();
>   label_30_fini:
> ```

---

## 3. Where we started

Heusch's sample supported only: `PRINT`, `LET`, `WHILE`/`WEND`, integer constants, and
`+ - * / %`. Its C runtime was intentionally **incomplete** — `stack_push`, `stack_pop`,
`LOAD_VAR`, and `STORE` were empty stubs.

Our job: complete the runtime and grow the language, while meeting strict correctness rules.

---

## 4. What we implemented (overview)

| Area | Added |
|------|-------|
| **Program preparation** | Numeric line‑ordering pass (Manual §2.5) |
| **Control flow** | `GOTO`, `GOSUB`/`RETURN`, `IF…THEN/ELSE`, `FOR/TO/STEP/NEXT` |
| **Data** | 1D arrays (`DIM`, `A(i)` load/store), `name$` strings |
| **Operators** | comparisons `= <> < > <= >=`, logical `AND OR NOT`, unary `-`, floats, hex/octal |
| **Functions** | `SIN`, `COS`, `CEIL` (round up), `FLOOR` (round down), `INT` |
| **Runtime safety** | full stack machine, variable table, **runtime type checking**, warnings |
| **Robust output** | writes `target/<name>.c` on success, `.c.error` on bad source |

---

## 5. How: bring lines into numeric order (Manual §2.5)

The manual says line numbers *"indicate the order in which program lines are stored… and are
used as references when branching."*

So `main()` reads the whole file into a `TreeMap<Integer,String>` (auto‑sorted by line
number) **before** parsing, and records the **set of line numbers that exist**.

```
30 PRINT I          10 LET I=1
10 LET I=1    ─►    20 PRINT 100      ─►  fed to the parser in order
20 PRINT 100        30 PRINT I            + {10,20,30} = valid jump targets
```

That set is what makes safe jump‑checking possible (next slide). Duplicate / number‑less
lines produce a warning, not a crash.

---

## 6. How: dynamic jumping "forth and back"

All control flow becomes C `goto` between the per‑line `label_<n>_init` labels.

* **`GOTO n`** → `goto label_n_init;`. If `n` doesn't exist we emit `bad_line_number(n);`
  (a runtime warning) instead of a broken `goto` — so the C **always compiles**.
* **`IF expr THEN …`** → evaluate condition, `JUMP_IF_FALSE(if_k_else)`; the `THEN`/`ELSE`
  target may be a line number (implicit `GOTO`) or a statement.
* **`WHILE`/`WEND`, `FOR`/`NEXT`** → a test label at the top, a back‑edge `goto` at the
  bottom (matched with parser stacks).

---

## 7. How: GOSUB / RETURN (the hard one)

C `goto` cannot jump to a *computed* target, so we use a **return‑address stack + a
dispatcher**:

* Each `GOSUB` site gets a unique id `R`: `gosub_push(R); goto label_n_init; ret_R:`
* Every `RETURN` does `goto __gosub_dispatch;`
* One dispatcher at the end maps the id back to the call site:

```c
__gosub_dispatch:
  switch (gosub_pop()) {
    case 1: goto ret_1;
    case 2: goto ret_2;
    default: goto __prog_end;   /* RETURN without GOSUB -> warns, ends */
  }
```

Portable, primitive C — no compiler extensions (no computed `goto`).

---

## 8. How: 1D arrays and math functions

* **Arrays** — `DIM A(n)` allocates indices `0..n` in a separate array table.
  `A(i)=expr` → `STORE_ARR`, `A(i)` → `LOAD_ARR`. Used before `DIM`? defaults to `0..10`
  with a warning. Out‑of‑range index? warns and is ignored — never a crash.
* **Functions** — `SIN`, `COS` (radians), `CEIL` (round up), `FLOOR` (round down),
  `INT` (= floor). Implemented via the C math library → link with `-lm`.
* `A(i)` and `SIN(i)` share `name(expr)` syntax, so the transpiler treats the known names
  `SIN COS CEIL FLOOR INT` as functions and everything else as an array.

---

## 9. How: runtime type checking & warnings

Type checking happens **at runtime**, not in the C compiler. Every value carries a tag
(`T_NUM` / `T_STR`). The runtime prints a `RUNTIME WARNING` and **keeps going** for:

* variable used before assignment → uses `0` (or `""` for `name$`)
* wrong type in an operation → treated as `0` (`+` on two strings = concatenation)
* assigning the wrong type to a variable
* jump to a non‑existent line, division/modulo by zero, out‑of‑range index
* `RETURN` without `GOSUB`

Unrecoverable cases (stack overflow, variable/array limits, out of memory) terminate with a
**clear message** — allowed by the rules.

---

## 10. How: robust transpiler output

`main()` captures the generated C into a buffer and wraps parsing in `try/catch`
(`ParseException`, `TokenMgrError`, any `Throwable`):

* **success** → `target/<name>.c`
* **BASIC source error** → clear message + `target/<name>.c.error` (extension is *not* `.c`),
  exit non‑zero

So a failed transpilation is instantly recognisable, and the transpiler never crashes with a
stack trace — it only ever rejects bad BASIC input.

---

## 11. Self‑imposed rules (applied consistently)

* **Variables:** up to 256 scalars, 64 arrays (overflow = clean termination).
* **Name length:** first 63 chars significant; longer names are cut off.
* **Undeclared variables:** interpolated to `0` / `""` with a warning.
* **Operand stack 1024, GOSUB depth 256.**
* **Numbers** are C `double`; `/` real division, `%` remainder.
* **Truth values** follow GW‑BASIC: `-1` = true, `0` = false.

---

## 12. Meeting the three evaluation requirements

1. **Transpiler fails only on bad BASIC source — and says so.** Errors print a clear message
   and leave the partial code as `*.c.error`; success writes `*.c`.
2. **Successful output always compiles.** Undefined jumps become runtime warnings, not
   dangling `goto`s. All valid samples build under `gcc -Wall -lm` with **zero** warnings.
3. **The compiled program never crashes.** Every hazard warns and continues; unrecoverable
   states terminate with a message.

---

## 12a. Requirement-by-requirement classification

**R1 — transpiler fails only on bad BASIC source; clear message; failed code kept as
non‑`.c`.**
`main()` wraps the whole parse in `try/catch` (`ParseException`, `TokenMgrError`, any
`Throwable`) and captures generated C into a buffer first.
→ success writes `target/<name>.c`; a BASIC error prints `ERROR in BASIC source: <msg>` and
writes `target/<name>.c.error` (extension is `.error`, **not** `.c`), then exits non‑zero.
A missing input file is reported cleanly. The transpiler never dies with a stack trace.

**R2 — successful output always compiles.**
The only thing that could break the C compile — a jump to a line that does not exist — is
caught at transpile time and emitted as `bad_line_number(n)` (a runtime call) instead of a
dangling `goto`. Intentional unused per‑line labels are silenced with one
`#pragma GCC diagnostic ignored "-Wunused-label"`.
→ all six valid samples compile under `gcc -Wall -lm` with **zero** warnings/errors.

**R3 — compiled program never crashes; clean termination is allowed.**
Every hazard warns and continues: unassigned variable, wrong type, bad jump,
division/modulo by zero, out‑of‑range index, `RETURN` without `GOSUB`. Unrecoverable states
(stack overflow, variable/array limits, out of memory, negative `DIM`) `exit(1)` **with a
clear message**.

**R4 — self‑defined rules, applied consistently.**

| Rule | Our decision |
|------|--------------|
| (a) Number of variables | 256 scalars + 64 arrays; overflow = clean termination |
| (b) Variable‑name length | first **63** chars significant (≥ 8); longer names **cut off**, not a syntax error |
| (c) Undeclared variable read first | **interpolated** → `0` (numeric) or `""` (`name$`), with a runtime warning |
| (d) Other rules | operand stack 1024; GOSUB depth 256; numbers are `double` (`/` real, `%` remainder); arrays `0..n` with auto‑`DIM(10)`; truth `-1`/`0` |

---

## 12b. Classification of extra functionality

The baseline sample had only `PRINT`, `LET`, `WHILE`/`WEND`, integer constants and
`+ - * / %`, with an **incomplete** runtime. Everything below was added by us.

| Category | Added on top of the sample |
|----------|----------------------------|
| **Control flow** | `GOTO` (validated), `GOSUB`/`RETURN` (return‑id + dispatcher), `IF…THEN/ELSE` (line or statement), `FOR/TO/STEP/NEXT` |
| **Data & types** | 1D arrays (`DIM`, `A(i)` load/store, bounds + auto‑DIM), `name$` strings, string concatenation, tagged runtime values with type checking |
| **Operators & literals** | relational `= <> < > <= >=`, logical `AND OR NOT`, unary `-`, floats, `&H`/`&O` hex/octal |
| **Functions** | `SIN`, `COS`, `CEIL` (round up), `FLOOR` (round down), `INT` (floor) |
| **Engineering / correctness** | numeric line‑ordering pass (§2.5), multiple statements per line (`:`), optional `LET`, full success/error output scheme, complete crash‑safe C runtime |

---

## 13. Demo — sample programs

| File | Demonstrates | Output |
|------|--------------|--------|
| `TEST.BAS` | Heusch's `WHILE`/`WEND` | `51 … 1` |
| `TEST2_GOTO.BAS` | line reorder, `IF…THEN line`, `GOTO` | `100 1 2 3 100` |
| `TEST3_GOSUB.BAS` | `GOSUB`/`RETURN` dispatch | `5 25 10 100 0` |
| `TEST4_FOR.BAS` | `FOR`, `STEP -2`, strings | `1 2 3 4 5 10 8 6 4 2 DONE` |
| `TEST5_WARNINGS.BAS` | unassigned var + bad jump warnings | `0` / `hello world` / `42` |
| `TEST6_ARRAY_MATH.BAS` | arrays, `CEIL`/`FLOOR`/`INT`, `SIN`/`COS` | `0 1 4 9 16 25` then `3 2 -2 0 1` |
| `TEST7_BAD_SOURCE.BAS` | deliberate error (`WEND` without `WHILE`) | *no `.c`; writes `.c.error`* |

```bash
mvn clean compile
java -cp target/classes de.hft_stuttgart.cpl.GWBasic TEST6_ARRAY_MATH.BAS
gcc -Wall -o program target/TEST6_ARRAY_MATH.c -lm && ./program
```

---

## 14. Use cases

* **Teaching compiler construction** — a complete, readable example of lexing, parsing,
  code generation, control‑flow lowering, and a runtime library.
* **Retro / legacy code** — run old GW‑BASIC listings as native executables without a BASIC
  interpreter.
* **A pattern for "old language → C"** — the line‑ordering pass, `goto`‑based control flow,
  GOSUB dispatcher, and tagged‑value runtime transfer to other simple source languages.
* **Foundation to extend** — the stack‑machine design makes adding statements/functions a
  matter of one grammar rule + one C function.

---

## 15. Limitations & future work

Out of scope today: multi‑dimensional arrays, `DATA`/`READ`, `INPUT`, `DEF FN`, most other
built‑ins, and `PRINT` with multiple `;`/`,` items.

Natural next steps: more intrinsics (`ABS`, `SQR`, `TAN`, `LEN`, `MID$`), `INPUT` for
interactivity, multi‑dimensional arrays, and a constant‑folding optimisation pass.

---

## 16. Summary

* Extended Heusch's sample into a working **GW‑BASIC → C transpiler**.
* Implemented line ordering, full dynamic jumping (`GOTO`/`GOSUB`/`IF`/`FOR`), 1D arrays,
  `SIN`/`COS`/`CEIL`/`FLOOR`, and a complete, type‑checked C runtime.
* Designed for the three correctness rules: **the transpiler doesn't crash, the C compiles,
  and the program doesn't crash.**
