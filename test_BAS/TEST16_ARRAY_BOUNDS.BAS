10 PRINT "--- ARRAY OUT-OF-BOUNDS TRAP ---"
20 DIM A(5)
30 LET A(3) = 99
40 PRINT A(3)
50 LET A(10) = 1
60 PRINT "If you see this line, the array-bounds check FAILED!"
