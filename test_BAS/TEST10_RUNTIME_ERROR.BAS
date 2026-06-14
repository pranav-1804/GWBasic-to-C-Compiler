10 PRINT "--- TESTING RUNTIME TYPE SAFETY TRAP ---"
20 LET STRVAR = "HELLO"
30 LET NUMVAR = 45
40 PRINT "Attempting to add a string literal to a numeric variable value:"
50 LET INVALIDCOMBINATION = STRVAR + NUMVAR
60 PRINT "If you see this line, your runtime type validation safety check FAILED!"