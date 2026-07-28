# Smart2Raw — a biblioteca onde dá para experimentar

Duas maneiras de alguém que não é você conferir o que o Smart2Raw faz, sem
instalar nada e sem acreditar em ninguém:

| entregável | o que é | como usar |
|---|---|---|
| `dist/smart2raw-live.html` | a biblioteca inteira compilada para WebAssembly, embutida numa página de **arquivo único** | abra no navegador (funciona por `file://`, sem servidor e sem internet) |
| `dist/s2r-probe.exe` | console de Windows x64, **sem CRT e sem DLL de runtime** — só `kernel32` | `s2r-probe.exe dados.csv --coluna 3` |

Nenhum dos dois reimplementa nada. Os dois chamam o `include/smart2raw.h` do
repositório, compilado para outro alvo. Quando a página mostra um tamanho, foi
`s2r_pool_bytes()` / `s2r_blocked_bytes()` quem respondeu; quando ela oferece um
`.s2r` para baixar, foi `s2r_blocked_save()` quem escreveu aqueles bytes — a
página só trocou o disco por um bloco de memória.

## Por que isso existe

Um artigo diz o que o formato faz. Um `.zip` de código-fonte exige que a pessoa
tenha compilador, paciência e confiança. O que faltava era a coisa no meio: um
lugar onde alguém **coloca a própria coluna** e vê a classificação, o tamanho e o
tempo de consulta medidos na máquina dela, com cada resposta conferida contra um
laço ingênuo antes de aparecer na tela.

## Como está construído

O `smart2raw.h` precisa de treze funções de libc. Para alvos que não têm libc
nenhuma (wasm32; um PE ligado sem CRT), o `src/s2r_rt.c` fornece exatamente
essas treze, em três partes:

1. **um alocador com etiquetas de fronteira e coalescência**, com cabeçalho de 64
   bytes para que todo payload volte alinhado a 64 — assim `aligned_alloc(64, n)`
   é literalmente `malloc(n)` e `free()` funciona nos dois, como na glibc;
2. as primitivas `mem*`/`str*` e um `qsort` (heapsort: sem recursão, sem pior
   caso quadrático);
3. **um `<stdio.h>` cujo `FILE` é um bloco de memória** — é isso que faz o
   `s2r_blocked_save()`/`s2r_blocked_load()` de verdade rodarem dentro do
   navegador, sem uma linha alterada.

O `src/s2r_probe.c` é o motor comum: uma coluna entra, um relatório sai. Ele
confere o que dá para conferir (round-trip valor a valor, soma pelos metadados
contra a soma ingênua, ida e volta pelo arquivo) e marca uma divergência como
defeito em vez de imprimir um número bonito.

## Construir

```sh
sh tools/build.sh        # wasm32 + página de arquivo único + binário nativo
sh tools/build_win.sh    # s2r-probe.exe (cruzado do Linux, sem SDK da Microsoft)
sh tools/verify.sh       # a verificação inteira
```

Requisitos: `clang` (com `wasm-ld`, `lld-link` e `llvm-dlltool`, que vêm no
mesmo pacote do LLVM), `python3` e `node`. Nada de emscripten, nada de npm.

## O que a verificação faz

| passo | o que garante |
|---|---|
| `probe_native` × `wasm_report.js` sobre `shapes.bin` | o relatório do wasm é **idêntico, campo por campo**, ao do build nativo com a libc de verdade — em 17 formas de coluna, incluindo sem sinal acima de 2^63, com sinal, constante e de um elemento só |
| `tools/stress.js` | 8 rodadas do lote no MESMO módulo dão o mesmo resultado e a memória fica estável (o teste do alocador) |
| `tools/page_test.js` | a página num Chromium de verdade: 10 formas, todas as barras verdes, consulta cronometrada com resultados conferidos, `.s2r` baixado e o número mágico lido de volta |
| `out/probe_hosted` | o **mesmo `probe_main_win.c`** compilado contra POSIX e executado, para que a lógica do executável seja testada e não só compilada |

O que fica sem execução automática aqui é a carga do PE e as doze chamadas ao
`kernel32` — no Windows, `s2r-probe.exe` sai com código 0 quando todas as
verificações passam, então `echo %ERRORLEVEL%` já responde.

---

Smart2Raw — Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
SPDX-License-Identifier: AGPL-3.0-or-later · ou licença comercial (LICENSING.md)
