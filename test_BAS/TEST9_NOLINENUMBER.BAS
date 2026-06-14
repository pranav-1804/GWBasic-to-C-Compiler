PRINT "--- TESTING OPTIONAL LINE NUMBERS ---"
LET X = 100
LET Y = 200
LET Z = X + Y
PRINT "The sum Z (should be 300) is calculated without line numbers:"
PRINT Z
FOR K = 1 TO 2
PRINT "Looping inside non-numbered block..."
NEXT K
PRINT "--- OPTIONAL LINE NUMBERS SUCCESS ---"