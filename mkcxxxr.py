#! /usr/bin/env python

def mkcxxxr(n, bw):
    if bw == 1 and (n == 0 or n == 1):
        return f"inline object*& c{'ad'[n]}r(object* x) {{ return x->c{'ad'[n]}r; }}"
    binstr = bin(n)[2:].rjust(bw, '0').replace('1', 'd').replace('0', 'a')
    print("c" + binstr + "r")
    return f"inline object*& c{binstr}r(object* x) {{ return c{binstr[0]}r(c{binstr[1:]}r(x)); }}"


with open("cxxxr.h", "w") as f:
    print('/* AUTO-GENERATED FILE */\n#include "tinobsy.hpp"', file=f)
    for bw in range(1, 7):
        for n in range(2**bw):
            print(mkcxxxr(n, bw), file=f)
