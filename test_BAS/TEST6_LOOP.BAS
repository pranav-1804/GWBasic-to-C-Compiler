5 PRINT "=== STAGE 1: BASIC EXPRESSIONS & MATH ==="
10 LET A = 10
20 LET B = 3
30 LET C = A + B * 2
40 PRINT "C should be 16: "
50 PRINT C

60 PRINT "=== STAGE 2: WHILE/WEND NESTING ==="
70 LET I = 1
80 WHILE I - 4
90   PRINT "While iteration: "
100  PRINT I
110  LET I = I + 1
120 WEND

130 PRINT "=== STAGE 3: FOR/NEXT WITH EXPLICIT STEP ==="
140 FOR J = 1 TO 5 STEP 2
150   PRINT "For J step 2: "
160   PRINT J
170 NEXT J

PRINT "=== STAGE 4: MISSING LINE NUMBER TEST ==="
LET K = 100
PRINT "Successfully parsed a line without an initial number: "
PRINT K

PRINT "=== STAGE 5: RUNTIME SAFETY CHECKS ==="
PRINT "Uncomment line 220 to test unassigned variable runtime error:"
PRINT "Uncomment line 240 to test type checking mismatch runtime error:"