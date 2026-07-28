#!/usr/bin/env python3
"""Uma amostra determinística com quatro colunas de perfis bem diferentes."""
import random, sys
random.seed(7)
n = 400000
path = sys.argv[1] if len(sys.argv) > 1 else "out/amostra.csv"
with open(path, "w") as f:
    f.write("id,leitura,ts,saldo_centavos\n")
    for i in range(n):
        f.write("%d,%d,%d,%d\n" % (i, random.randint(0, 200),
                                   1700000000 + i*60, 1000 + random.randint(0, 400)*25))
print("%s: %d linhas, 4 colunas" % (path, n))
