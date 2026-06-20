# GW‑BASIC → C Transpiler

A small transpiler that reads a GW‑BASIC program (`.bas`) and emits **primitive C**.
It is built on Peter Heusch's sample project: **JavaCC** generates the parser, and a tiny
C "stack‑machine" runtime (`prolog.c` / `epilog.c`) is wrapped around the translated lines.

This document explains how the transpiler is built, how to run it, how the design works
(dynamic jumping, line ordering), and what runtime warnings it produces.

---

## 1. The pipeline at a glance

```
            JavaCC                       gcc / CLion
  TEST.BAS  ───────►  java GWBasic  ───►  program.c  ───►  ./program
 (GW-BASIC)           (transpiler)        (primitive C)     (output)
```

The transpiler does **not** execute the BASIC program. It only reads existing code and
prints C source to *stdout*. You then compile that C with any C compiler (gcc, or CLion).

A translated program is always three parts concatenated:

1. `prolog.c` — the C runtime: an operand stack, a variable table, and one C function per
   BASIC operation (`LOAD_CONST`, `PRINT`, `ADD`, `STORE`, …). It ends with `int main(void) {`.
2. **the translated lines** — each BASIC line becomes a sequence of those calls, framed by
   two C labels (`label_<n>_init:` … `label_<n>_fini:`).
3. `epilog.c` — closes `main()` and contains the `__prog_end:` label.

Because every operation is a *function call against a stack*, the generated C stays
"primitive": no expression trees, no temporaries — just a flat list of calls and `goto`s.

Example — the line `30 PRINT A` becomes:

```c
  label_30_init:
    LOAD_VAR("A");
    PRINT();
  label_30_fini:
```

---

## 2. Building the transpiler

Requirements: **JDK 11+** and **Maven** (the POM targets Java 11).
The `javacc-maven-plugin` turns `src/main/javacc/GWBasic.jj` into Java parser sources
automatically during `compile`.

```bash
cd GWBasic
mvn clean compile
```

`mvn clean` deletes the previously generated `GWBasic*.java`, `Token*.java`, etc.;
`mvn compile` regenerates and compiles them. In **CLion/IntelliJ** you can just open the
folder as a Maven project and run the Maven `compile` goal.

---

## 3. Running it (transpiling a `.bas` file)

After `mvn compile`, the main class is `de.hft_stuttgart.cpl.GWBasic`. Pass the `.bas`
file as the only argument. The transpiler **writes the generated C into the `target/`
directory** and prints a one‑line status to *stderr*:

* on success it writes `target/<name>.c`;
* on a BASIC source error it prints a clear message and writes the partial output to
  `target/<name>.c.error` (note: **not** a `.c` file), and exits with a non‑zero code.

```bash
# 1) GW-BASIC -> C     (creates target/TEST.c on success)
java -cp target/classes de.hft_stuttgart.cpl.GWBasic TEST.BAS

# 2) C -> executable   (link the math library with -lm)
gcc -Wall -o program target/TEST.c -lm

# 3) run it
./program
```

So after transpiling, `target/` contains a `.c` file only if transpilation succeeded; a
`*.c.error` file means the BASIC source was rejected (see the printed message for why).

In **CLion** you typically transpile with the command above, then open the resulting
`program.c` in a CLion C project and build/run it there.

---

## 4. "First, bring all lines into numeric order" (Manual §2.5)

The GW‑BASIC manual (§2.5, *Line Format*) says line numbers *"indicate the order in which
the program lines are stored in memory, and are also used as references when branching."*
So the very first thing `main()` does is read the whole file and sort the lines by their
numeric line number using a `TreeMap<Integer,String>` before any parsing happens.

That means a source file written out of order, e.g.

```
30 PRINT I
10 LET I=1
20 PRINT 100
```

is reordered to `10, 20, 30` and only *then* handed to the JavaCC parser. While sorting,
`main()` also records the **set of line numbers that actually exist** — this set is what
makes safe jump‑checking possible (see §5). Duplicate line numbers and lines that don't
start with a number produce a warning to *stderr* rather than a crash.

---

## 5. Dynamic jumping — forth and back

All control flow is translated to C `goto`, jumping between the per‑line
`label_<n>_init` labels. The supported jumps:

**`GOTO n`** — `goto label_n_init;`. The target `n` is checked against the set of existing
line numbers. If `n` does not exist, the transpiler does **not** emit a broken `goto`
(which would be a *C‑compiler* error); instead it emits `bad_line_number(n);`, which prints
a runtime warning and continues. This satisfies the rule *"line numbers may not exist →
your compiler or runtime must warn, a C‑compiler error is not acceptable."*

**`IF expr THEN …`** — the condition is evaluated onto the stack, then
`JUMP_IF_FALSE(if_k_else)` skips the THEN branch. `THEN` (and the optional `ELSE`) may be
either a line number (an implicit `GOTO`) or a statement.

**`GOSUB n` / `RETURN`** — the hard one, because C `goto` cannot jump to a *computed*
target. The transpiler solves it with a **return‑address stack + a dispatcher**:

* Each `GOSUB` call site gets a unique id `R`. It emits
  `gosub_push(R); goto label_n_init; ret_R:`.
* Every `RETURN` emits `goto __gosub_dispatch;`.
* At the end of the program the transpiler emits one dispatcher:

  ```c
  __gosub_dispatch:
    switch (gosub_pop()) {
      case 1: goto ret_1;
      case 2: goto ret_2;
      default: goto __prog_end;   /* RETURN without GOSUB -> warns, ends */
    }
  ```

So `RETURN` pops the id of the most recent `GOSUB` and the `switch` jumps back to the
instruction right after that `GOSUB`. This is portable, primitive C (no computed‑goto
extensions). `__prog_end` sits at the very end and the body does `goto __prog_end;` first,
so execution never falls into the dispatcher by accident.

**`WHILE`/`WEND` and `FOR`/`NEXT`** — matched with parser stacks (`whileStack`,
`forStack`) and translated to a test label at the top and a back‑edge `goto` at the bottom.
`FOR` stores its limit and step in hidden variables (`__lim_k`, `__step_k`); `FOR_CONTINUE`
re‑checks the bound each iteration and `STEP_VAR` advances the counter. Per the assignment,
WHILE/WEND and FOR/NEXT are assumed correctly nested; an unmatched `WEND`/`NEXT`, or a
`WHILE`/`FOR` left open at end of file, raises a transpile‑time error.

---

## 6. Runtime type checking and warnings

Type checking is done **at runtime**, not by the C compiler. Every value carries a tag
(`T_NUM` or `T_STR`). The runtime emits a `RUNTIME WARNING` (to *stderr*) and keeps going —
it never produces a hard C error — in each of these cases:

* **Variable used before assignment** → warns, then uses `0` (or `""` for a `name$`
  string variable).
* **Wrong type in an operation** (e.g. a string fed to `*`) → warns, treats it as `0`.
  (`+` on two strings is treated as concatenation, matching GW‑BASIC.)
* **Assigning the wrong type** to a variable (number into `A$`, or string into a numeric
  name) → warns.
* **Jump to a non‑existent line number** → warns and ignores the jump.
* **Division / modulo by zero** → warns, yields `0`.
* **`RETURN` without `GOSUB`** → warns and ends the program.

Comparisons follow GW‑BASIC's convention: true is `-1`, false is `0`.

---

## 7. Supported language subset

Statements: `LET` (the keyword is optional, so bare `A=…` also works), `PRINT`,
`WHILE`/`WEND`, `FOR`/`TO`/`STEP`/`NEXT`, `IF`/`THEN`/`ELSE`, `GOTO`, `GOSUB`/`RETURN`,
`DIM`, and multiple statements per line separated by `:`.

Expressions: `+ - * / %`, comparisons `= <> < > <= >=`, logical `AND OR NOT`, unary `-`,
parentheses, decimal and floating constants, `&H…`/`&O…` hex/octal constants, string
literals, numeric and `name$` string variables.

1D arrays and intrinsic functions:

* `DIM A(n)` allocates a one‑dimensional array with indices `0..n` (multiple arrays may be
  declared in one `DIM`, comma‑separated). `A(i)=expr` stores and `A(i)` reads an element.
  Referencing an array before `DIM` warns and defaults to `0..10`; an out‑of‑range index
  warns and is ignored (read yields `0`) — never a crash.
* `SIN(x)`, `COS(x)` — sine / cosine (radians).
* `CEIL(x)` rounds **up**, `FLOOR(x)` rounds **down**; `INT(x)` is GW‑BASIC's floor.

These functions use the C math library, which is why the generated C is linked with `-lm`.
Because `A(i)` and `SIN(i)` share the same `name(expr)` syntax, the transpiler treats the
known names `SIN COS CEIL FLOOR INT` as functions and everything else as an array.

---

## 8. Sample programs and expected output

| File | Demonstrates | stdout |
|------|--------------|--------|
| `TEST.BAS` | Heusch's original `WHILE`/`WEND` | `51 50 … 1` |
| `TEST2_GOTO.BAS` | line reordering, `IF…THEN line`, forward `GOTO` | `100 1 2 3 100` |
| `TEST3_GOSUB.BAS` | `GOSUB`/`RETURN` dispatch (two call sites) | `5 25 10 100 0` |
| `TEST4_FOR.BAS` | `FOR`, `FOR…STEP -2`, string `PRINT` | `1 2 3 4 5 10 8 6 4 2 DONE` |
| `TEST5_WARNINGS.BAS` | unassigned var + undefined jump warnings | `0` / `hello world` / `42` (+ warnings on stderr) |
| `TEST6_ARRAY_MATH.BAS` | `DIM`/array, `CEIL`/`FLOOR`/`INT`, `SIN`/`COS` | `0 1 4 9 16 25` then `3 2 -2 0 1` |
| `TEST7_BAD_SOURCE.BAS` | a **deliberate** error (`WEND` without `WHILE`) | *(no `.c`; writes `target/TEST7_BAD_SOURCE.c.error` + a clear message)* |

Programs 1–6 transpile to C that compiles cleanly under `gcc -Wall -lm` with no warnings.
Program 7 demonstrates the error path: the transpiler prints `ERROR in BASIC source: WEND
without WHILE` and produces a `.c.error` file instead of a `.c`.

---

## 8a. Self‑imposed rules and limits

The assignment lets us define our own reasonable limits as long as they are applied
consistently. The ones this project uses:

* **Number of variables:** up to **256** scalar variables and **64** arrays. Exceeding the
  limit is a clean runtime termination with a message (not a crash).
* **Variable‑name length:** the first **63** characters are significant; longer names are
  **cut off** (not a syntax error). Names follow GW‑BASIC form — start with a letter, then
  letters/digits, optional trailing `$` for strings.
* **Undeclared variables read before assignment:** **interpolated** — a numeric variable
  defaults to `0`, a `name$` to `""`, each with a runtime warning. (We chose interpolation
  rather than treating it as an error.)
* **Operand stack depth 1024, GOSUB nesting 256** — generous; overflow terminates cleanly
  with a message.
* **Array indices** run `0..n`; out‑of‑range access warns and is ignored (reads give `0`).
  An array used before `DIM` defaults to `0..10` with a warning.
* **Numbers** are represented as C `double`; `/` is real division and `%` is the remainder.
* **Comparisons** yield GW‑BASIC truth values: `-1` true, `0` false.

---

## 8b. How this meets the three evaluation requirements

**(1) The transpiler never fails except on BASIC‑source errors, and flags those clearly.**
`main()` wraps the whole parse in a `try/catch` (covering `ParseException`, JavaCC's
`TokenMgrError`, and any other `Throwable`). The generated C is first captured into a memory
buffer. On success it is written to `target/<name>.c`; on a BASIC‑source error the
transpiler prints `ERROR in BASIC source: …`, writes the partial output to
`target/<name>.c.error` (an extension that is deliberately **not** `.c`, so a failed
transpilation is obvious), and exits non‑zero. The transpiler never terminates with a Java
stack trace.

**(2) On success, the produced C always compiles.** The only constructs that could
otherwise produce a C‑compiler error are jumps to non‑existent line numbers — these are
detected during transpilation (the set of existing line numbers is known) and emitted as a
`bad_line_number(n)` runtime call instead of a dangling `goto`. Per‑line labels that are
never targeted are intentional, so the runtime disables just that cosmetic warning. All
seven samples that transpile successfully compile under `gcc -Wall -lm` with **zero**
warnings.

**(3) The compiled program never crashes.** Every runtime hazard is handled: type
mismatches, unassigned variables, undefined jumps, division/modulo by zero, and
out‑of‑range array indices all emit a `RUNTIME WARNING` and continue. Genuinely
unrecoverable conditions (operand‑stack overflow, exceeding the variable/array limits, out
of memory) call `exit(1)` **with a clear termination message** — which the requirements
explicitly allow.

---

## 9. Assumptions and limitations

Following the assignment brief, the transpiler **assumes**: `WHILE`/`WEND` and `FOR`/`NEXT`
are properly 1:1 nested, and source line numbers are strictly increasing (it sorts anyway).
It does **not** assume that referenced line numbers exist, that variables were assigned, or
that variables have the right type — all three are handled with runtime warnings as above.

Out of scope (the C runtime is intentionally minimal): multi‑dimensional arrays,
`DATA`/`READ`, `INPUT`, `DEF FN`, most other built‑in functions, and `PRINT` with multiple
`;`/`,` items. (1D arrays via `DIM`, and `SIN`/`COS`/`CEIL`/`FLOOR`/`INT`, are supported.)

---

## 10. Files

```
GWBasic/
├── pom.xml                           Maven build (runs JavaCC)
├── TEST.BAS                          Heusch's sample
├── TEST2_GOTO.BAS … TEST5_*.BAS      extra samples
└── src/main/
    ├── javacc/GWBasic.jj             grammar + transpiler (line sort, codegen, jumps)
    └── resources/
        ├── prolog.c                  C runtime + start of main()
        └── epilog.c                  end of main()
```
