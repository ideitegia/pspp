set decimal=dot.
title 'Demonstrate REGRESSION procedure'.
/*      run this syntax file with the command:
/*                 pspp example.stat
/*
/*      Output is written to the file "pspp.list".
/*
/*      (This comment will not appear in the output.)

data list / v0 to v2 1-33 (10).
begin data.
 0.65377128  7.735648 -23.97588
-0.13087553  6.142625 -19.63854
 0.34880368  7.651430 -25.26557
 0.69249021  6.125125 -16.57090
-0.07368178  8.245789 -25.80001
-0.34404919  6.031540 -17.56743
 0.75981559  9.832291 -28.35977
-0.46958313  5.343832 -16.79548
-0.06108490  8.838262 -29.25689
 0.56154863  6.200189 -18.58219
end data.
list.
regression /variables=v0 v1 v2 /statistics defaults /dependent=v2 /method=enter.

