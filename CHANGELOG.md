# Changelog

Versioning follows SemVer. Dates use the YYYY-MM-DD format.

## [3.5.0] - 2026-07-27

DOI: 10.5281/zenodo.21623772

Inclui tudo da 3.4.1 - a correcao da coluna sem sinal acima de 2^63, a suite de
fuzz diferencial e as correcoes de escopo - e acrescenta nove coisas que a
auditoria desta rodada encontrou.

A primeira delas, e a que da nome a release: **o frame of reference ganha uma
escala**.

A v3.4.0 removeu um DESLOCAMENTO que o dado nao precisava - `v = base + delta`.
Ela nao removeu uma ESCALA. Uma coluna de `{500, 1500, ... 11500}` tem amplitude
11.000 e portanto 14 bits, mas todo valor e `base + 1000*i` com `i` em 0..11:
quatro bits de indice vestindo um casaco de quatorze.

    v = base + passo * i        passo = mdc, no bloco, de (v - base)

O passo comum e o mdc dos deslocamentos, custa uma passada para achar, e dividi-lo
fora e exato por construcao. **Nao e um dicionario**: nao ha tabela de consulta nem
indirecao por valor - o mapa e uma funcao afim em forma fechada, entao toda
operacao se reescreve no dominio do indice e os bytes guardados continuam sendo os
inteiros nativos que sempre foram.

    v > t          <=>   i > (t - base) / passo          (divisao inteira)
    v em [lo,hi]   <=>   i em [teto((lo-base)/passo), piso((hi-base)/passo)]
    SUM(v)         =     n*base + passo*SUM(i)
    max(v)         =     base + passo*amplitude_i

`passo == 1` e EXATAMENTE a v3.4.0, do mesmo jeito que `base == 0` era exatamente a
v3.3. Uma coluna sem passo comum paga um mdc na construcao e nada depois.

### O que foi adicionado

- **`S2RBlocked` ganha passo por bloco.** `s2r_blocked_stride()` expoe; todo o
  resto da API e identica. A amplitude armazenada passa a ser a do INDICE, que e o
  que limita o payload, e o descarte de bloco desce o limiar para o mesmo dominio
  com uma divisao por bloco.
- **`S2RAffine`, o pool afim plano.** Uma coluna inteira costuma ter um unico passo
  - um intervalo de amostragem, uma granularidade de ponto fixo, um ID alocado em
  incrementos fixos. O pool de indices e um `S2RPool` comum, entao TODO kernel SIMD
  ja existente serve sem uma segunda implementacao:
  `s2r_affine_count_gt/lt/eq/range`, `s2r_affine_sum`, `s2r_affine_sum_if`, com e
  sem sinal. O pool de indices e sempre SEM SINAL, mesmo numa coluna com sinal: os
  deslocamentos partem do minimo, entao nao podem ser negativos. O sinal mora so na
  base.
- **`s2r_affine_detect` / `_signed`** para descobrir base e passo de um vetor cru,
  e `s2r_gcd64` publico.

### Formato

`fmt = 3` e o mesmo arquivo com um byte de classe a mais e um vetor a mais. O
escritor so emite `fmt = 3` quando algum bloco tem passo acima de 1, entao uma
coluna sem passo e **byte a byte o arquivo que a v3.4.0 escrevia** e um leitor
v3.4.0 continua abrindo. Um leitor v3.5.0 aceita os dois, e le `fmt = 2` como
"todo passo e 1" - que e o que ele significa. Um passo 0 em disco e recusado:
dividiria por zero no predicado.

### Medido

12 milhoes de elementos, 12 valores distintos espalhados de 500 a 11500,
embaralhado, `-O3 -march=native`, AVX2:

| representacao | tamanho | COUNT(x>5500) | vs pool plano |
|---|---:|---:|---:|
| pool plano u16 (v3.4.0) | 22,89 MB | 1,033 ms | 1,00x |
| `S2RBlocked` v3.4.0 (sem passo) | 22,90 MB | 1,042 ms | 0,99x |
| `S2RBlocked` v3.5.0, bloco 16384 | **11,45 MB** | **0,464 ms** | **2,23x** |
| `S2RAffine` v3.5.0 | **11,44 MB** | **0,468 ms** | **2,21x** |

Colunas sem passo comum ficam identicas ao que a v3.4.0 produzia - conferido, nao
suposto: as suites de PFOR passam sem alteracao e com as mesmas razoes que ja
afirmavam.

### O que isto NAO resolve

O regime B do benchmark de warehouse continua sendo uma derrota. Com o passo
fatorado sao 11,44 MB contra 5,72 MB do par e 0,468 ms contra 0,326 ms: a perda
cai de 4x/3,2x para **2x/1,44x**, e nao inverte. O piso e aritmetico - 12 valores
distintos precisam de log2(12) = 3,58 bits, o par usa 4, e a menor classe nativa
aqui e 8. Essa ausencia e a decisao de projeto que compra os 7,9 ms -> 0,00 ms de
materializacao, nao uma omissao.

E o ganho e condicional ao dado ter granularidade real. Medido:

| coluna | passo (mdc) | classe |
|---|---:|---|
| timestamps a cada 60 s a partir de 1,7e9 | 60 | u16 -> **u8** |
| preco em centavos, granularidade de 5 | 5 | u32 -> **u16** |
| 12 IDs de regiao arbitrarios em 500..11500 | 1 | u16 -> u16 (nada) |
| 12 codigos de erro arbitrarios em 0..40000 | 1 | u16 -> u16 (nada) |

### Auditoria de folga: quatro coisas que ainda escapavam

Depois da fatoracao afim ficou a pergunta certa - *o que MAIS estamos deixando
escapar?* - e a resposta veio de medir, nao de opinar. Quatro folgas, todas
fechadas aqui.

**1. Ordem estabelecida no pool plano.** A camada em blocos responde `count_gt`
por busca binaria desde a v3.4.0. O pool plano nao usava a ordem para nada, mesmo
tendo `s2r_is_sorted` ha versoes. A diferenca e que um bloco e imutavel depois do
build e um pool plano nao e: o fato tem de ser MANTIDO, e uma flag obsoleta
devolveria a resposta errada em silencio - a pior falha possivel. Entao:

- `S2R_FLAG_SORTED`, derrubada por TODA escrita, posta por apenas duas coisas:
  `s2r_sort()`, que estabeleceu a ordem, e `s2r_mark_sorted()`, que paga uma
  passada O(n) para verificar.
- **Apendar em ordem preserva a flag.** E o padrao de ingestao que importa:
  timestamps e ids monotonicos chegam ordenados, entao a flag sobrevive a uma
  carga inteira. Um push fora de ordem a derruba na hora.
- A flag NAO e serializada: `S2R_FLAGS_FORMAT_MASK` a mantem fora do arquivo, e
  um pool carregado comeca sem saber de nada - a direcao segura.
- `count_gt/lt/eq/range`, com e sem sinal, entram na busca binaria acima do mesmo
  limiar medido que a camada em blocos ja usava.

Medido, 8M elementos ordenados, `u8`:

| operacao | varredura SIMD | ordem conhecida | ganho |
|---|---:|---:|---:|
| `count_gt(100)` | 0,371 ms | abaixo do relogio | ~7000x |
| `count_range(50,150)` | 0,363 ms | abaixo do relogio | ~5000x |

**2. Cura atraves da fronteira do sinal.** `s2r_fit_class` estreita a LARGURA mas
nunca o SINAL, entao uma coluna declarada com sinal que nunca recebe um negativo
fica o dobro do necessario: 0..200 nao cabe em `i8` (-128..127) e vai para `i16`,
quando `u8` bastava. Schema e ORM declaram INT com sinal por padrao, o que torna
essa forma comum. `s2r_fit_class_signedness()` cura os dois. E uma funcao separada
de proposito: largar o sinal muda o contrato - depois da cura um push negativo e
RECUSADO em vez de promover - e automatico surpreenderia quem pretende empurrar um
depois. 8M valores em 0..200: **15,26 MB -> 7,63 MB**.

**3. Coluna constante carrega zero bits.** A camada em blocos ja resolvia (largura
de delta 0, sem payload). O `S2RAffine` nao: guardava um byte por elemento para
repetir o mesmo indice - exatamente o habito que a biblioteca existe para quebrar.
Agora a base e a contagem SAO a coluna: **7,63 MB -> 0 bytes de payload**, e todo
predicado responde em O(1).

**4. Os quatro predicados que faltavam na camada em blocos.** Ela respondia UM
predicado, `count_gt`, enquanto o pool plano respondia cinco. Isso e ao contrario:
o mapa de zona vale MAIS para uma consulta por faixa do que para uma de um lado
so, porque uma janela pode errar o bloco pelas duas pontas. Adicionados
`s2r_blocked_count_lt`, `_count_eq`, `_count_range` e `_sum_if`, com descarte de
zona, tratamento de bloco constante e a janela movida para o dominio do indice do
mesmo jeito que `count_gt` move o limiar. `sum_if` de uma janela que cobre o bloco
inteiro devolve a soma de zona sem ler payload nenhum.

### O pool plano ganha o mapa de zona que a camada em blocos ja tinha

A v3.4.0 deu a cada BLOCO um minimo, uma amplitude e uma soma, e passou a
responder a partir deles sem ler payload. O pool plano - o tipo que a maioria
dos chamadores de fato segura - nao carregava nada, entao um predicado que os
proprios limites do dado podiam recusar em O(1) ainda lia todo byte. A flag de
ordem da secao anterior era, sem que eu percebesse, o primeiro membro de uma
familia. Aqui vem o resto.

**Resumo (min, max, soma).** `s2r_summarize()` paga uma passada e grava os tres;
`S2R_FLAG_SUMMARY` diz se valem. Toda escrita arbitraria derruba, e **um append
mantem**, porque um elemento novo so pode alargar um minimo ou maximo e so pode
somar. Um carregamento sequencial inteiro sai resumido de graca.

Os predicados passam a responder tres casos sem tocar o payload:

    limiar >= max        ->  count_gt e 0
    limiar <  min        ->  count_gt e a coluna inteira
    janela nao intersecta ->  count_range e 0

Medido, 4M elementos `u8` numa coluna que vai so ate 200:
`count_gt(220)` de **0,1435 ms para 0,000034 ms**. O resumo custa 8,5 ms uma vez.

**Indice acumulado.** Uma coluna `u8` tem 256 valores possiveis e uma `u16` tem
65536 - numeros pequenos ao lado de n. A contagem acumulada de cada valor responde
QUALQUER faixa EXATAMENTE, em duas leituras, de uma estrutura que **nao cresce com
o dado**: 2 KB para `u8`, 512 KB para `u16`.

    count_range(lo,hi) = cum[hi+1] - cum[lo]

E a mesma ideia do mapa de zona - metadado precomputado responde sem tocar o
payload - aplicada ao dominio do VALOR em vez do dominio da POSICAO. Nao e
aproximacao nem esboco.

Medido: `count_range(50,150)` de **0,1481 ms para 0,000035 ms, 4231x**. Construcao
1,67 ms, e se paga em **11 consultas**.

Obsolescencia e o risco inteiro, entao nao fica na disciplina: o pool carrega uma
**epoca** que toda escrita avanca, o indice grava a epoca em que foi construido, e
uma consulta contra um pool alterado **recusa responder** em vez de responder
errado.

### O tamanho de bloco passa a ser classificado, nao adivinhado

A biblioteca escolhia a classe a partir do dado e pedia ao chamador para chutar o
tamanho de bloco, que e o parametro mais consequente dos dois. O padrao 256 foi
medido contra tres formas reais e e DOMINADO em duas - menor e mais rapido num
bloco maior.

`s2r_blocked_plan()` preca todo candidato a partir de UMA passada. O modelo e
exato, nao amostrado: uma varredura na granularidade fina grava min, max e passo
comum por celula, e celulas adjacentes se FUNDEM em arvore, o que e exato porque

    filho i contem  min_i + g_i * k
    logo, relativo a m = min(min_i):  (min_i - m) + g_i * k
    e o passo fundido e  mdc, sobre i, de ( min_i - m , g_i )

`s2r_blocked_choose_block()` devolve o minimo e `s2r_blocked_build_auto()` usa.
Medido em 4M elementos:

| coluna | padrao 256 | automatico |
|---|---|---|
| timestamps a cada 60 s | 4,11 MB · 0,032 ms | 4,11 MB · 0,032 ms (escolheu 256) |
| medicao 100000 +- 30 | 4,04 MB · 0,294 ms | **3,82 MB · 0,144 ms** (131072) |
| 12 distintos embaralhado | 4,04 MB · 0,224 ms | **3,82 MB · 0,149 ms** (131072) |

**Limite declarado:** a escolha automatica minimiza BYTES. Numa coluna ordenada
existe uma fronteira de tempo que ela nao enxerga - com blocos grandes a busca
binaria entra e o predicado fica ate 200x mais rapido em troca de 1,9x de memoria.
Quem quiser esse lado tem `s2r_blocked_plan()` com a fronteira inteira; a escolha
automatica nao adivinha por voce.

### "Nunca expande", e a representacao recomendada

Todo formato classico tem um regime onde EXPANDE o dado. Medido, 4M elementos,
contra a linha de base `int64` de 30,52 MB:

| coluna | int64 | **S2R** | dicionario | RLE | bitmap |
|---|---:|---:|---:|---:|---:|
| uniforme 0..200 | 30,52 | **3,81** | 3,82 | 30,37 | - |
| 12 distintos em 500..11500 | 30,52 | 3,81 | **1,91** | 27,97 | - |
| o mesmo, ordenado | 30,52 | 0,06 | 1,91 | **0,00** | - |
| booleano 0/1 | 30,52 | 3,81 | **0,48** | 15,26 | **0,48** |
| timestamps a cada 60 s | 30,52 | **7,71** | **41,01** | 30,52 | - |
| u64 aleatorio | 30,52 | **30,52** | **41,01** | 30,52 | - |
| ids em 0..1e6 | 30,52 | **15,26** | 17,03 | 30,52 | - |

Dicionario fica 34% MAIOR que o int64 cru em alta cardinalidade - o dicionario
vira do tamanho do dado. RLE nao comprime nada fora de dado ordenado. Bitmap so
existe com k=2. **O Smart2Raw nao consegue expandir**, e nao por sorte: ele
classifica por AMPLITUDE, entao o pior caso e "a amplitude pede 64 bits", que E a
entrada `int64`. A classe mais larga e a propria linha de base. Isso agora e
asserido em teste, nao afirmado em prosa.

`s2r_recommend()` devolve os tamanhos das tres formas e qual usar. Ele existe
porque **o ponto de entrada obvio e o pior**: em 4M timestamps o `S2RPool` que
qualquer um pegaria da 15,26 MB e 0,73 ms, contra 4,11 MB e 0,04 ms da forma em
blocos. A biblioteca sabia qual era melhor e nunca dizia.

### O que continua escapando, e por que

Uma coluna booleana ocupa 8x o de um bitmap e o `popcount` e ~17x mais rapido que
o nosso `count_gt` - e o README lista "flags" como caso de uso. O piso e
aritmetico: k valores distintos precisam de log2(k) bits e a menor classe nativa
e 8. Fechar isso exige classe sub-byte, o que troca a propriedade que o projeto
inteiro compra (os 7,9 ms -> 0,00 ms de materializacao) e e uma decisao de
escopo, nao um bug. Fica declarada, nao escondida.

### Testes

- **29 suites** (eram 26). Nova: `test_gaps.c`, 196 checagens, rodada TRES vezes
  com o limiar de busca binaria no padrao, forcado desligado e forcado ligado.
  A secao 1 chama TODA funcao publica que escreve num pool ordenado e exige que a
  flag tenha caido - por execucao, nao por inspecao, porque uma funcao futura que
  esquecer o clear e o unico jeito de esse recurso virar um bug silencioso.
- Busca binaria e varredura sao comparadas sobre OS MESMOS dados, em todo limiar e
  todo par ordenado, e as duas contra a referencia ingenua.

### Testes

- **29 suites** (eram 25). Nova: `test_affine.c`, 283 checagens - deteccao do
  passo, round trip, `count_gt/lt/eq/range` e `sum_if` contra a referencia escalar
  em todo limiar candidato e todo par ordenado deles (cada valor distinto e seus
  dois vizinhos imediatos, que por construcao NAO caem na malha - e onde um erro de
  arredondamento existiria), colunas com e sem sinal, extremos de u64 e perto de
  `INT64_MIN`, bloco de um elemento, serializacao nos dois formatos, o byte de
  `fmt` conferido em disco, e corrupcao byte a byte do corpo.
- As 25 suites anteriores passam sem alteracao.

---

## [3.4.1] - 2026-07-27

Uma correcao de defeito. Nenhuma API nova, nenhuma mudanca de formato, nenhuma
mudanca de estrutura - um arquivo `.s2r` escrito pela 3.4.0 continua sendo lido
identicamente, e um escrito pela 3.4.1 e lido pela 3.4.0.

### Corrigido: coluna SEM SINAL com valor acima de 2^63 devolvia dado errado

A base de cada bloco era guardada em `int64_t` e a MAIOR base era rastreada com
comparacao COM SINAL, mesmo numa coluna sem sinal:

    if(base > hi_b) hi_b = base;        /* int64_t, sempre */
    ...
    b->bcls = s2r_classify((uint64_t)hi_b);

Uma base acima de `INT64_MAX` le como negativa, entao o maximo corrente fica
pequeno demais, a classe das bases sai estreita demais, e a base e **truncada na
escrita**. A soma de bloco tinha exatamente o mesmo problema.

O caso minimo cabe em duas linhas:

    uint64_t v[2] = { 1, UINT64_MAX };
    s2r_blocked_build(&b, v, 2, 1);
    s2r_blocked_get(&b, 1)   ->  255        /* deveria ser UINT64_MAX */

Bases classificadas em 8 bits. `max` devolvia 255, `sum` devolvia 256 no lugar de
0, `count_gt(1000)` devolvia 0 no lugar de 1. **Sem erro, sem aviso, sem CRC
quebrado** - o arquivo e internamente consistente, so que com os valores errados.

Faixas sem sinal precisam de maximos sem sinal; nao ha sinal para comparar. O
conserto e trocar os dois rastreadores por `uint64_t`.

**Quem e afetado:** qualquer coluna sem sinal cuja faixa cruze 2^63 e que use a
camada em blocos - hashes de 64 bits, ids gerados por hash, timestamps em
nanossegundos de epocas distantes, mascaras de bits largas. Uma coluna com sinal
nao e afetada, e uma sem sinal abaixo de 2^63 tambem nao. O pool plano nunca foi
afetado.

### Por que 25 suites nao pegaram

Todas elas testam formas ESCOLHIDAS - faixas plausiveis, outliers plausiveis,
tamanhos de bloco plausiveis. O defeito morava numa forma que ninguem escolheria
de proposito. Uma bateria de casos escolhidos herda o ponto cego de quem
escolheu.

### Adicionado: `tests/test_fuzz_diff.c` (26a suite, 100.950 checagens)

Fuzz diferencial: 12 formas de coluna, tamanho e tamanho de bloco sorteados, tudo
conferido contra a referencia ingenua - acessores, agregados, os predicados do
pool plano e da camada em blocos, e ida e volta em arquivo. **As sementes sao
fixas**, porque um fuzz que muda a cada execucao acha o defeito uma vez e depois
some com ele. A mesma suite contra a 3.4.0 falha em 9 checagens; contra a 3.4.1,
em nenhuma, em seis sementes e 500 mil checagens.

### Correcoes de escopo na documentacao

Nenhuma linha de codigo, mas o material afirmava coisas que a medicao nao
sustenta:

- **"flags" saiu da lista de casos de uso.** Uma coluna booleana ocupa 8x o de um
  bitmap e o `popcount` e ~17x mais rapido que o nosso `count_gt`. O piso e
  aritmetico: k valores distintos precisam de log2(k) bits e a menor classe nativa
  e 8. Anunciar como alvo a forma onde mais se perde e o oposto de honestidade
  medida.
- **`benchmarks/format_matrix.c` (novo).** As sete formas de coluna contra todos
  os formatos, cada tamanho conferido por assercao antes de ser impresso. Ele
  existe porque a afirmacao abaixo passou a estar no README, e uma figura sem
  programa que a reproduza contraria a primeira regra do projeto.
- **A propriedade "nunca expande" passou a ser declarada.** Todo formato classico
  tem um regime onde EXPANDE: dicionario numa coluna de alta cardinalidade fica
  34% maior que o `int64` cru (medido: 41,01 MB contra 30,52 MB), RLE em dado
  desordenado guarda uma corrida por valor. O Smart2Raw nao consegue expandir, e
  nao por sorte - ele classifica por AMPLITUDE, entao o pior caso e "a amplitude
  pede 64 bits", que E a entrada.
- **`s2r_fit_class` nao troca o sinal.** O README prometia "cura bidirecional" sem
  a ressalva; uma coluna declarada com sinal que perdeu o ultimo negativo continua
  o dobro do necessario.
- **`S2R_BLOCK_DEFAULT` e um padrao, nao um otimo.** Medido em tres formas reais,
  256 e dominado em duas.
- **O ponto de entrada obvio nao e o melhor.** Em 4M timestamps o `S2RPool` da
  15,26 MB e 0,73 ms; a mesma coluna em blocos da 4,11 MB e 0,04 ms.
- `benchmarks/warehouse`: o dado do regime B diz representar `region_id` mas e
  uma progressao aritmetica. Ids reais seriam arbitrarios; a conclusao nao muda,
  mas o dado passa a dizer o que e.

---

## [3.4.0] - 2026-07-27

A correctness, contract and performance release. Nothing here is a feature in the
sense of "something the library could not express". Every item is either a bug that
was silently producing a wrong answer, undefined behaviour on a path whose
correctness proof requires *defined* wraparound, a place where the C core disagreed
with its own ports and its own specification, or an operation leaving a measured
multiple on the table.

---

### 1. The `.s2r` contract - the reader did not obey the spec the project wrote

- **Header `flags` were adopted verbatim from disk.** A crafted file with bit 2
  (`S2R_FLAG_EXTERNAL`) made `s2r_pool_free` skip the `free` and null the pointer,
  leaking every loaded pool - confirmed with AddressSanitizer. Bit 1
  (`S2R_FLAG_READONLY`) produced a heap pool that silently refused every mutation.
  Load and mmap now mask to the SIGNED bit, and `s2r_save_portable` masks on the
  way out, so in-memory ownership state can never round-trip through a file.
- `s2r_load_portable` / `s2r_map_open` now reject, per `SPEC_s2r_format.md`: an
  unsupported `fmt`, a nonzero reserved byte, a class/signed-flag disagreement, a
  declared count that overflows `size_t`, and a file length different from
  `16 + payload + 4`. **All of these were accepted by C and rejected by at least
  one port**, so `.s2r` was not the portable contract it claimed to be.
- **Go port:** `Load` converted the declared count to a signed `int` and multiplied
  by the element size with no range check. A header claiming
  `count = 0x8000000000000002` at class 16 wrapped to a 4-byte payload, passed the
  length and CRC checks, and decoded as a 2-element pool. The count is now derived
  from the file length by division; the declared value must agree exactly.
- **Go / JS:** the reserved byte is now checked. **JS / Python:** a signed class
  with the flag clear was accepted because `signed` was derived as the OR of class
  and flag, which made the very next agreement check unreachable.

### 2. Undefined behaviour

- `s2r_mul_scalar` at class u16: `a[i] *= v` promotes both operands to `int`, so
  `65436 * 65533` overflowed `INT_MAX`. That is the exact path lazy-carry depends
  on, and its correctness proof requires wraparound to be *defined* in `Z/2^w`.
  Now multiplies through `unsigned`. Found by adding an ASan+UBSan job to CI.
- `s2r_sum_signed`, `s2r_sum_if_signed` and `s2r_blocked_sum_signed` accumulated in
  `int64_t`; summing a large `i64` pool reaches signed overflow easily. Their
  unsigned twins have always accumulated in `uint64_t` for exactly this reason.
  All three now do the same and cast on return - two's-complement addition is
  bit-identical, so the value callers already got is unchanged, it is just defined
  by the standard now.

### 3. Predicate dispatch - `sum_fast` was the only SIMD-dispatched operation

Every filter - `count_gt`, `count_lt`, `count_eq`, `count_range`, `sum_if`, signed
and unsigned - ran scalar, and the signed variants plus unsigned `count_range` /
`sum_if` went through the `s2r_get` accessor, so the width switch sat INSIDE the
loop. On the same `u8` bytes of one pool, `sum_fast` reached 24218 Mval/s while
every filter sat near 4000.

**One kernel serves the whole family.** It reduces to a range test:

    count_gt(t) = count_range(t+1, MAX)    count_lt(t) = count_range(MIN, t-1)
    count_eq(v) = count_range(v, v)

and a range test on a w-bit lane is one WRAPPING subtract plus one unsigned
compare, because for `lo <= hi` with `span = hi - lo < 2^w`:

    v in [lo,hi]   <=>   (v - lo) mod 2^w  <=  span

The same identity serves SIGNED pools with no second kernel: two's-complement
subtraction is bit-identical to unsigned subtraction, so reducing the endpoints
modulo `2^w` and running the unsigned kernel on the raw stored bytes gives the
signed answer. `sum_if` reuses the frame of reference: `SUM(v) = SUM(v-lo) +
lo*count`, and `v-lo` for a match is narrow and unsigned, so `vpsadbw` sums it
directly even for a signed pool.

`count_gt` then earned a dedicated kernel back: serving it through the range form
needs a wrapping subtract *before* the saturating one, and that instruction cost
~28% on a bandwidth-bound 23 MB `u16` column, because `v > t` needs no rebasing -
`subs_epu(v,t) != 0` already IS the predicate. Specialised by measurement.

New: `s2r_count_gt_fast`, `s2r_count_lt_fast`, `s2r_count_eq_fast`,
`s2r_count_range_fast`, `s2r_sum_if_fast` and the five `_signed_fast` twins.
Following the `s2r_sum` / `s2r_sum_fast` precedent the existing names are
unchanged, and every `_fast` result is identical to its scalar counterpart.

Measured, 48M elements, `-O3 -march=native`, AVX2:

| operation | before | after | gain |
|---|---:|---:|---:|
| `count_gt` u8 | 2812 Mval/s | 16848 Mval/s | 6.0x |
| `count_lt` u8 | 3935 | 17658 | 4.5x |
| `count_eq` u8 | 3970 | 18088 | 4.6x |
| `count_range` u8 | 2943 | 15645 | 5.3x |
| `sum_if` u8 | 2881 | 14361 | 5.0x |
| `count_gt` u16 | 2024 | 7453 | 3.7x |
| `count_gt_signed` i8 | 1402 | 18833 | 13.4x |
| `count_range_signed` i8 | 1988 | 17033 | 8.6x |
| `sum_if_signed` i8 | 1532 | 14710 | 9.6x |

AVX2 kernels for the 8- and 16-bit classes, where the compact classes live and
where `sum_fast` also puts its effort; 32/64-bit and non-x86 fall back to typed
hoisted loops. An explicit NEON path is deliberately NOT claimed - it could not be
measured here.

### 4. The block-wise (PFOR) layer

**Frame of reference.** A block's class came from its **maximum alone**:

    block = { 9000000000, 9000000001, ... }  ->  classify(max) = u64

even though the block spans 1. Blocks now store values relative to their own
minimum:

| column (4M elements, block 256) | single width | PFOR | gain |
|---|---:|---:|---:|
| unix timestamps, +-50 s per block | 15.26 MB | 3.89 MB | **3.92x** |
| sequential IDs from 9e9 | 30.52 MB | 3.95 MB | **7.73x** |
| measurement 100000 +- 30 | 15.26 MB | 3.89 MB | **3.92x** |
| constant column | 7.63 MB | 0.04 MB | **170.67x** |
| sensor 20.00..25.00 C | 7.63 MB | 7.67 MB | 0.99x |
| uniform 0..200 (baseline already 0) | 3.81 MB | 3.84 MB | 0.99x |
| random u64 (no structure) | 15.26 MB | 15.33 MB | 1.00x |

The last three rows matter as much as the first three: **the old behaviour is the
special case `base = 0`**, so data that was already optimal stays optimal and
unstructured data is untouched. The existing PFOR suites pass unchanged with the
same ratios they asserted before. A block whose values are all equal now has delta
width 0 and stores **no payload at all**, which is where the 170x comes from, and
`s2r_blocked_sum_fast` now works on signed blocks, which it previously refused.

**Zone statistics.** Each block keeps its true delta span and its sum, so
`s2r_blocked_sum`, `_max` and `_min` are O(nblocks) walks over metadata that never
touch the payload - SUM measured **114x faster** (377.6 us to 3.3 us on 8M
elements). The predicate skip tightens from a *width* bound to the *real* range: a
block of 100..150 was previously treated as reaching 355 because one byte can hold
that. Cost is ~7% of the payload, and every bookkeeping array is stored in the
smallest class that fits its own range.

**Sorted blocks, and the gate they needed.** A non-decreasing block can answer
`count_gt` by binary search. Shipping that unconditionally would have been a
**regression**, which only measurement revealed - on a sawtooth column where every
block straddles the threshold and both sides hold identical payload:

| block size | binary search vs vectorised scan |
|---:|---:|
| 64 | **0.67x** - a loss |
| 256 | 1.21x |
| 1024 | 3.26x |
| 4096 | 10.78x |
| 65536 | **140.30x** |

`log2(n)` dependent probes lose to a sequential, prefetchable, vectorised scan
until the block is big enough, so the search is gated on
`S2R_BLK_BSEARCH_MIN_BYTES` (default 512 = 8 cache lines, overridable). That
removes the small-block loss (0.67x to 1.15x) and keeps the large-block win.

What the sorted flag does *not* buy is worth stating: on a **globally** sorted
column the zone map already resolves nearly every block. It pays when blocks are
individually ordered but the column is not - the shape of time-partitioned or
per-group data.

**Serialization** (ROADMAP: "block-wise `.s2r` serialization"). `s2r_blocked_save`
/ `s2r_blocked_load`, `fmt = 2`, canonical little-endian, CRC32 over **metadata and
payload** so a corrupted zone map is caught, exact file length required. The class
byte is 0, so a v3.3 reader rejects the file on both the class and fmt checks -
correct, because a blocked column is not a flat pool; the reverse is refused too.
On 8M sequential IDs from 9e9 the file is 8.31 MB against 61.04 MB of raw `int64`.

**A bug this found.** The first `s2r_blocked_count_gt` wrote the skip test as
`base + span <= thr`. At delta width 8, `span` is `UINT64_MAX` and the addition
wraps, so blocks that *did* contain matches were silently skipped - `count_gt`
returned 0 where the answer was 30048. Testing `base > thr` first makes
`thr - base` safe and removes the addition entirely.

### 5. SIMD reach

- **ARM SVE2 was unreachable dead code.** SVE implies NEON on AArch64 and the NEON
  block came first in `s2r_sum_fast`, so on real hardware the SVE kernels were
  never called - only the x86 emulation test, which forces SVE2 without NEON, ran
  them. SVE2 is now tested first, behind a `svcntb() > 16` guard so NEON keeps a
  128-bit vector, where the two are at parity.
- **The SVE kernels were also 8x too narrow.** An extending load of one byte per
  64-bit lane consumes `svcntd()` = VL/64 bytes per iteration - 2 bytes at VL=128
  against NEON's fixed 16 - so they lost to NEON on every shipping SVE machine and
  only reached parity at VL=1024. The header comment claiming "at 128-bit SVE the
  width equals NEON" described the intent, not the code. Rewritten around UDOT
  (`svdot_u32` / `svdot_u64`) against a vector of ones, SVE's answer to `vpsadbw`,
  which consumes `svcntb()` = VL/8 bytes per iteration.
- **RVV had the identical problem**: `u8mf8`/`u16mf4` into a `u64m1` accumulator is
  VLEN/64 elements per iteration. Now `u8m1`/`u16m2` into `u64m8` - VLEN/8, 8x
  wider, same overflow-free accumulation and tail-undisturbed policy.
- Both paths remain EXPERIMENTAL: logic validated by emulation, hardware pending.

### Tools, bindings and benchmarks

- `s2r agg sum` on a signed pool used `(int64_t)s2r_sum()`, which only works at
  class 64: `{-1,-128,-5,100}` as `i8` summed to 734 instead of -34. `count-gt` and
  `count-range` parsed the threshold with `strtoull` and called the unsigned
  kernels, so `count-gt 0` over that pool answered 4 instead of 1. Every
  aggregation is now sign-aware, matching `min`/`max`, which already were, and the
  CLI calls the `_fast` variants.
- `s2r info` read `count` from offset 8 for every magic, but pre-3.3 files keep it
  at offset 12, so legacy files reported a nonsense count and expected size; the
  format label was hardcoded to v3.3. Both layouts are handled now.
- `tools/s2r.c`: unchecked `malloc`/`realloc`/`s2r_pool_init` and discarded push
  return codes could produce a silently truncated `.s2r`.
- **ctypes binding**: `signed` was a Python-side attribute that `push()` flipped on
  the first negative value while the C pool stayed unsigned. The signed push was
  rejected, the return code discarded, and the value vanished - after which every
  read used the signed accessors on bytes stored as unsigned
  (`Pool(S2R_8); push(200); push(-1)` left one element reading back as -56).
  Signedness is now read from the pool, a negative push into an unsigned pool
  raises, and push/save/scalar return codes are checked. Added
  `s2r_capi_sum_signed` and `s2r_capi_is_signed` to the C ABI.
- **Python port**: `set()` widened the class before validating the index, so an
  out-of-range `set` changed `byte_length` on its way to raising `IndexError`;
  `get()` accepted negative indices that the JS and Go ports reject; a corrupt
  class byte raised a bare `ValueError` instead of `S2RFormatError`.
- `benchmarks/maestro/` shipped a duplicate of `include/smart2raw.h`, contradicting
  its own README, and the lookup could never reach the repo (`../include` resolves
  to `benchmarks/include/`), so the stale vendored copy was always used. The search
  now tries `../../include/` first; the duplicate is gone.
- New `benchmarks/warehouse/`: Smart2Raw against the dictionary + bit-packing + RLE
  encoding family, with the peer implemented at its best (sorted-dictionary
  predicate pushdown, SIMD nibble unpacker) and our side calling the shipped
  library. It documents where we win, where we tie and where we lose.

### Tests and CI

- **25 suites** (was 17). New: `test_format_hardening.c` (40 checks on the `.s2r`
  contract including the mmap reader), `test_filters_simd.c` (**142,952 checks** -
  for the 8-bit classes it sweeps EVERY threshold and EVERY ordered pair of range
  endpoints against the scalar reference), `test_pfor_frame.c` (**5,472 checks** -
  accessors, aggregates, zone stats, per-block sorted flags, serialization round
  trips, byte-flip corruption, truncation, trailing bytes), plus vector-length
  sweeps running the same SVE/RVV code at VL = 128..1024 bits.
- The PFOR suite runs three times with different binary-search gate settings -
  default, forced off, forced on - because two code paths that must agree are only
  proven to agree if both are executed.
- New `tools/test_cli.sh` (19 checks) and `bindings/python/test_binding.py`
  (16 tests); neither had any coverage before.
- **CI could not fail.** The QEMU job wrote `/tmp/$t | tail -1` without `pipefail`,
  so the pipeline exit status was `tail`'s and a failing or crashing test produced
  a green check - and that is the only job exercising big-endian. Added an
  ASan+UBSan job, jobs for the three ports, and a conformance job; none of the
  ports was built by CI before.

## [3.3.7] - 2026-06-09
- AVX-512: added a dedicated `s2r__sum_u8_avx512` path (uses `_mm512_sad_epu8`, 64 bytes/iter) selected at runtime via `__builtin_cpu_supports("avx512bw")`, ahead of the AVX2 path. Compiled AND run on an AVX-512 Xeon here; result is bit-identical to scalar/AVX2, and measured at ~1.17x (cache-resident) to ~1.30x (memory-bound) over AVX2 and ~12-14x over scalar for u8 sums.
- AVX-512 u16: a u16 AVX-512 kernel was implemented and benchmarked, but it came out SLOWER than the AVX2 u16 path (8 vs 16 elements/iter), so u16 deliberately stays on AVX2. Honest call by measurement; the u16 AVX-512 kernel was removed rather than shipped unused.
- Experimental: RISC-V Vector (RVV 1.0) path for `s2r_sum_fast` (u8/u16), gated by `__riscv_v_intrinsic` (built with `-march=rv64gcv`). Written to the RVV 1.0 C intrinsics; the vector-length-agnostic logic is validated on x86 via an emulation shim (`tests/rvv_emu`, suite "RISC-V RVV emulated"), but it has NOT yet been compiled or run on a RISC-V toolchain/hardware. Scalar fallback unchanged. To be promoted from experimental after an `rv64gcv` build plus a QEMU/hardware run.
- Experimental: ARM SVE2 path for `s2r_sum_fast` (u8/u16), gated by `__ARM_FEATURE_SVE2`. Written to the SVE ACLE intrinsics; logic validated on x86 via an emulation shim (`tests/sve2_emu`, suite "ARM SVE2 emulated"), but NOT yet compiled/run on SVE hardware. Note: at 128-bit SVE the width equals NEON, so the expected gain is marginal. To be promoted after a real SVE build + QEMU/hardware run.
- Test suite is now 17 suites, 0 failures (added the RVV and SVE2 emulated-logic suites).

## [3.3.6] - 2026-06-05
- Analytics v2: added compact integer `sort`, `is_sorted`, `unique_sorted`, `nunique` and `value_counts` primitives to the C core.
- Mirrored the same analytics-v2 API in the Go, JavaScript and Python ports.
- Added C, Go, JavaScript and Python tests for sorted signed/unsigned pools, distinct counts and value-frequency maps.

## [3.3.5] - 2026-05-31
- Fix: class promotion with an empty pool (`count==0`) did not readjust the
  capacity to the already-allocated buffer, which could overflow when the first
  value required a class larger than the initial one (e.g., init I8, first value
  -5e9). Affected push_adaptive / push_signed_adaptive / ensure_fits / reclass.
  Found with AddressSanitizer; regression test added (test_regressao, item 6).

## [3.3.4] - 2026-05-31
- Signed PFOR: `s2r_blocked_build_signed` / `get_signed` / `sum_signed`.
- SIMD-accelerated block-wise sum: `s2r_blocked_sum_fast` (each block reuses the
  vpsadbw/NEON dispatch in its native type). Measured ~6.5-6.7x over scalar.

## [3.3.3] - 2026-05-31
- Block-wise width (PFOR): `S2RBlocked` (`build`/`get`/`sum`/`max`/`bytes`/`free`),
  unsigned. Each block chooses its class; an outlier inflates only its own block
  (~3.7x memory recovered under localized outliers, measured).

## [3.3.2] - 2026-05-31
- Analytics module merged into the single header: bidirectional / self-healing
  width (`s2r_remove_swap`, `s2r_fit_class`), `S2RTracked` (min/max O(1) on push)
  and group-by on the compact data (`s2r_histogram_u8`, `s2r_group_sum_u8u32`).
- Header banner/changelog updated; module index.

## [3.3.1] - 2026-05-31
- Signed lazy-carry arithmetic (`s2r_add/mul_scalar_signed_safe`,
  `S2RDeferredSigned`); NEON path (ARM); big-endian mmap via copy-on-write
  (on-disk file left intact).

## [3.3.0] - 2026-05-31
- Auto-adaptive push (`s2r_push_adaptive`); SIMD with runtime dispatch
  (AVX2 vpsadbw, scalar fallback; `s2r_sum_fast`); zero-copy mmap
  (`s2r_map_open`/`close`); portable I/O (canonical LE + CRC32).

## [3.2.1] - 2026-05-31
- `s2r_stddev` fixed (robust sqrt, no math.h); aligned allocation via
  `aligned_alloc` (C11); signed-aware aggregations/filters/statistics.

## [3.2.0]
- Signed integers (S2R_I8..I64); promote/demote; statistics; range queries;
  `push_many`/`transform`; `S2R_FOREACH`; `s2r_info`.
