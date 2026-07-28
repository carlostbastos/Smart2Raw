/* Os dois símbolos que o compilador da Microsoft espera do CRT e que este
 * executável, por não ter CRT, precisa fornecer sozinho.
 *
 *   _fltused  - marca "este módulo usa ponto flutuante"
 *   __chkstk  - sonda a pilha quando um quadro passa de uma página. Contrato:
 *               tamanho em RAX, NÃO mexe em RSP (quem chama subtrai), só pode
 *               tocar RCX e RAX, que ele mesmo salva.
 *
 * Smart2Raw - Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
 * SPDX-License-Identifier: AGPL-3.0-or-later */
int _fltused = 1;

__asm__(
".text\n"
".globl __chkstk\n"
"__chkstk:\n"
"  pushq %rcx\n"
"  pushq %rax\n"
"  cmpq $0x1000, %rax\n"
"  leaq 24(%rsp), %rcx\n"
"  jb 1f\n"
"2:\n"
"  subq $0x1000, %rcx\n"
"  testq %rcx, (%rcx)\n"
"  subq $0x1000, %rax\n"
"  cmpq $0x1000, %rax\n"
"  ja 2b\n"
"1:\n"
"  subq %rax, %rcx\n"
"  testq %rcx, (%rcx)\n"
"  popq %rax\n"
"  popq %rcx\n"
"  retq\n");
