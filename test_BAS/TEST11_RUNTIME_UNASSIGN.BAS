10 PRINT "--- TESTING UNASSIGNED VARIABLE TRAP ---"
20 LET VALIDVAR = 50
30 PRINT VALIDVAR
40 PRINT "The next line attempts to print MYSTERYVAR which was never set."
50 PRINT "Your executable should print a Runtime Error and exit cleanly here:"
60 PRINT MYSTERYVAR
70 PRINT "If you see this line, your runtime safety check FAILED!"