# REQUIRES: x86
## A large section reached from small code through a 32-bit field, or one that
## itself has such a field or reaches the GOT through one, is demoted to its
## small counterpart.

# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=x86_64 a.s -o a.o
# RUN: llvm-mc -filetype=obj -triple=x86_64 b.s -o b.o
# RUN: ld.lld -shared b.o -o b.so

# RUN: ld.lld a.o b.so -o a.no-demote
# RUN: llvm-objdump -t a.no-demote | FileCheck %s --check-prefix=NO-DEMOTE
# NO-DEMOTE: .lbss {{[0-9a-f]+}} pc32{{$}}

# RUN: ld.lld --x86-64-demote-large-sections-by-relocs a.o b.so -o a
# RUN: llvm-objdump -t a | FileCheck %s
# RUN: llvm-readelf -S a | FileCheck %s --check-prefix=SEC \
# RUN:   --implicit-check-not=.lrodata --implicit-check-not=.lbss

## PC32, 32S and 32 from small code demote the target.
# CHECK-DAG: .bss {{[0-9a-f]+}} pc32{{$}}
# CHECK-DAG: .data {{[0-9a-f]+}} abs32s{{$}}
# CHECK-DAG: .rodata {{[0-9a-f]+}} abs32{{$}}
## A 32-bit relocation in a large section demotes both endpoints.
# CHECK-DAG: .data {{[0-9a-f]+}} chained{{$}}
# CHECK-DAG: .data {{[0-9a-f]+}} chained2{{$}}
## Calls, 64-bit references and non-SHF_ALLOC sections do not demote.
# CHECK-DAG: .ltext {{[0-9a-f]+}} plt{{$}}
# CHECK-DAG: .ldata {{[0-9a-f]+}} abs64{{$}}
# CHECK-DAG: .ldata {{[0-9a-f]+}} notchained{{$}}
# CHECK-DAG: .ldata {{[0-9a-f]+}} nonalloc{{$}}
## Initial-Exec demotes its own section unless it is relaxed to Local-Exec.
# CHECK-DAG: .text {{[0-9a-f]+}} ie_ext{{$}}
# CHECK-DAG: .ltext {{[0-9a-f]+}} ie_local{{$}}
## The personality is referenced from .eh_frame through a udata4 field.
# CHECK-DAG: .text {{[0-9a-f]+}} pers{{$}}
## .text.dead is live without --gc-sections.
# CHECK-DAG: .data {{[0-9a-f]+}} gc{{$}}
## A reference to an ifunc lands on its IPLT entry, so the resolver stays large.
# CHECK-DAG: .iplt {{[0-9a-f]+}} ifn{{$}}
# CHECK-DAG: .ltext {{[0-9a-f]+}} resolver{{$}}

## Demoted sections lose SHF_X86_64_LARGE.
# SEC:      .ltext PROGBITS {{.*}} AXl {{.*}}
# SEC:      .rodata PROGBITS {{.*}} A {{.*}}
# SEC:      .text PROGBITS {{.*}} AX {{.*}}
# SEC:      .data PROGBITS {{.*}} WA {{.*}}
# SEC:      .bss NOBITS {{.*}} WA {{.*}}
# SEC:      .ldata PROGBITS {{.*}} WAl {{.*}}

## A reference from a section discarded by --gc-sections does not demote.
# RUN: ld.lld --x86-64-demote-large-sections-by-relocs --gc-sections \
# RUN:   a.o b.so -o a.gc
# RUN: llvm-objdump -t a.gc | FileCheck %s --check-prefix=GC
# GC: .ldata {{[0-9a-f]+}} gc{{$}}

## -r leaves large sections alone.
# RUN: ld.lld --x86-64-demote-large-sections-by-relocs -r a.o -o a.ro
# RUN: llvm-readelf -S a.ro | FileCheck %s --check-prefix=RELOC
# RELOC: .lbss.pc32 NOBITS {{.*}} WAl {{.*}}

## In an executable, TLSLD and GD/TLSDESC for a non-preemptible symbol are
## relaxed to Local-Exec; for a preemptible symbol they become Initial-Exec.
# RUN: llvm-mc -filetype=obj -triple=x86_64 tls.s -o tls.o
# RUN: ld.lld --x86-64-demote-large-sections-by-relocs tls.o b.so -o tls
# RUN: llvm-objdump -t tls | FileCheck %s --check-prefix=TLS-EXE
# TLS-EXE-DAG: .ltext {{[0-9a-f]+}} ld{{$}}
# TLS-EXE-DAG: .ltext {{[0-9a-f]+}} gd{{$}}
# TLS-EXE-DAG: .text {{[0-9a-f]+}} gd_ext{{$}}
# TLS-EXE-DAG: .ltext {{[0-9a-f]+}} desc{{$}}
# TLS-EXE-DAG: .text {{[0-9a-f]+}} desc_ext{{$}}
# TLS-EXE-DAG: .text {{[0-9a-f]+}} ie4_ext{{$}}
# TLS-EXE-DAG: .text {{[0-9a-f]+}} ie6_ext{{$}}
# TLS-EXE-DAG: .text {{[0-9a-f]+}} desc4_ext{{$}}
# TLS-EXE-DAG: .ltext {{[0-9a-f]+}} tmb{{$}}

## A shared object keeps every GOT-based access.
# RUN: ld.lld --x86-64-demote-large-sections-by-relocs -shared \
# RUN:   tls.o b.so -o tls.so
# RUN: llvm-objdump -t tls.so | FileCheck %s --check-prefix=TLS-SO
# TLS-SO-DAG: .text {{[0-9a-f]+}} ld{{$}}
# TLS-SO-DAG: .text {{[0-9a-f]+}} gd{{$}}
# TLS-SO-DAG: .text {{[0-9a-f]+}} desc{{$}}
# TLS-SO-DAG: .text {{[0-9a-f]+}} ie{{$}}
# TLS-SO-DAG: .text {{[0-9a-f]+}} tmb{{$}}

## A demoted SHF_MERGE section is merged with the small one; .ldata.rel.ro.*
## becomes RELRO; a name without a known prefix (.ldatax is not .ldata) is kept.
# RUN: llvm-mc -filetype=obj -triple=x86_64 misc.s -o misc.o
# RUN: ld.lld --x86-64-demote-large-sections-by-relocs misc.o -o misc
# RUN: llvm-objdump -t misc | FileCheck %s --check-prefix=MISC
# RUN: llvm-readelf -S misc | FileCheck %s --check-prefix=MISC-SEC
# MISC-DAG: .rodata {{[0-9a-f]+}} str{{$}}
# MISC-DAG: .data.rel.ro {{[0-9a-f]+}} relro{{$}}
# MISC-DAG: .ldatax {{[0-9a-f]+}} unk{{$}}
# MISC-SEC-DAG: .rodata PROGBITS {{[0-9a-f]+}} {{[0-9a-f]+}} 000008 01 AMS {{.*}}
# MISC-SEC-DAG: .ldatax PROGBITS {{.*}} WA {{.*}}

## An FDE reaches its function through a 32-bit pc_begin under the small code
## model and through a 64-bit one under the large code model.
# RUN: llvm-mc -filetype=obj -triple=x86_64 cfi.s -o cfi4.o
# RUN: llvm-mc -filetype=obj -triple=x86_64 --large-code-model cfi.s -o cfi8.o
# RUN: ld.lld --x86-64-demote-large-sections-by-relocs cfi4.o -o cfi4
# RUN: llvm-objdump -t cfi4 | FileCheck %s --check-prefix=FDE4
# RUN: ld.lld --x86-64-demote-large-sections-by-relocs cfi8.o -o cfi8
# RUN: llvm-objdump -t cfi8 | FileCheck %s --check-prefix=FDE8
# FDE4: .text {{[0-9a-f]+}} cfi{{$}}
# FDE8: .ltext {{[0-9a-f]+}} cfi{{$}}
## The FDE of a function discarded by --gc-sections does not demote it, so its
## references do not demote their targets either.
# RUN: ld.lld --x86-64-demote-large-sections-by-relocs --gc-sections \
# RUN:   cfi4.o -o cfi4.gc
# RUN: llvm-objdump -t cfi4.gc | FileCheck %s --check-prefix=FDE-GC
# FDE-GC: .ldata {{[0-9a-f]+}} live{{$}}

## A large section with a 32-bit field other than a call, or a GOT access
## through one, is demoted itself; PLT32 in data counts, as no thunk serves it.
# RUN: llvm-mc -filetype=obj -triple=x86_64 self.s -o self.o
# RUN: ld.lld --x86-64-demote-large-sections-by-relocs self.o -o self
# RUN: llvm-objdump -t self | FileCheck %s --check-prefix=SELF
# SELF-DAG: .text {{[0-9a-f]+}} pc32_src{{$}}
# SELF-DAG: .text {{[0-9a-f]+}} got_src{{$}}
# SELF-DAG: .text {{[0-9a-f]+}} multi_src{{$}}
# SELF-DAG: .data {{[0-9a-f]+}} multi1{{$}}
# SELF-DAG: .data {{[0-9a-f]+}} multi2{{$}}
# SELF-DAG: .data {{[0-9a-f]+}} abs_src{{$}}
# SELF-DAG: .data {{[0-9a-f]+}} abs_dst{{$}}
# SELF-DAG: .rodata {{[0-9a-f]+}} vt{{$}}
# SELF-DAG: .text {{[0-9a-f]+}} vfn{{$}}
# SELF-DAG: .text {{[0-9a-f]+}} lfn{{$}}

## __start_X and __stop_X reached through a 32-bit field demote the sections
## named X; a 64-bit reference does not.
# RUN: llvm-mc -filetype=obj -triple=x86_64 startstop.s -o startstop.o
# RUN: ld.lld --x86-64-demote-large-sections-by-relocs startstop.o -o startstop
# RUN: llvm-readelf -S startstop | FileCheck %s --check-prefix=STARTSTOP
# RUN: llvm-objdump -t startstop | FileCheck %s --check-prefix=STARTSTOP-SYM
# STARTSTOP-DAG: lsec PROGBITS {{.*}} WA {{.*}}
# STARTSTOP-DAG: lsec64 PROGBITS {{.*}} WAl {{.*}}
# STARTSTOP-DAG: lsec_large PROGBITS {{.*}} WA {{.*}}
# STARTSTOP-SYM: .text {{[0-9a-f]+}} large_startstop{{$}}

## A lazy symbol becomes undefined, and preemptible, after the version script
## has made it local, so its Initial-Exec access keeps the GOT.
# RUN: llvm-mc -filetype=obj -triple=x86_64 tlslazy.s -o tlslazy.o
# RUN: llvm-mc -filetype=obj -triple=x86_64 tlslazy-def.s -o tlslazy-def.o
# RUN: rm -f lz.a && llvm-ar rc lz.a tlslazy-def.o
# RUN: ld.lld --x86-64-demote-large-sections-by-relocs tlslazy.o b.so lz.a \
# RUN:   --version-script=local.ver -o tlslazy
# RUN: llvm-objdump -t tlslazy | FileCheck %s --check-prefix=TLS-LAZY
# TLS-LAZY: .text {{[0-9a-f]+}} lazy{{$}}

## The large copy of a COMDAT group wins when it comes first, and the small
## copy's references bind to it, so it is demoted like any other target.
# RUN: llvm-mc -filetype=obj -triple=x86_64 comdat-large.s -o comdat-large.o
# RUN: llvm-mc -filetype=obj -triple=x86_64 comdat-small.s -o comdat-small.o
# RUN: ld.lld --x86-64-demote-large-sections-by-relocs \
# RUN:   comdat-large.o comdat-small.o -o comdat
# RUN: llvm-objdump -t comdat | FileCheck %s --check-prefix=COMDAT
# COMDAT: .data {{[0-9a-f]+}} cv{{$}}

#--- a.s
.globl _start
.text
_start:
  .cfi_startproc
  .cfi_personality 3, pers
  movl pc32(%rip), %eax
  movl abs32s, %eax
  movl $abs32, %eax
  call plt
  movabsq $abs64, %rax
  movabsq $gc, %rax
  call ie_ext
  call ie_local
  leaq ifn(%rip), %rax
  ret
  .cfi_endproc

.section .text.dead,"ax",@progbits
  movl $gc, %eax

.section .lbss.pc32,"awl",@nobits
pc32:
  .zero 4

.section .ldata.abs32s,"awl",@progbits
abs32s:
  .long 0

.section .lrodata.abs32,"al",@progbits
abs32:
  .long chained

.section .ldata.chained,"awl",@progbits
chained:
  .long chained2

.section .ldata.chained2,"awl",@progbits
chained2:
  .long 0

.section .ldata.abs64,"awl",@progbits
abs64:
  .quad notchained

.section .ldata.notchained,"awl",@progbits
notchained:
  .long 0

.section .ldata.nonalloc,"awl",@progbits
nonalloc:
  .long 0

.section .debug_foo,"",@progbits
  .long nonalloc

.section .ldata.gc,"awl",@progbits
gc:
  .long 0

.section .ltext.plt,"axl",@progbits
plt:
  ret

.section .ltext.ie_ext,"axl",@progbits
ie_ext:
  movq ext_tls@GOTTPOFF(%rip), %rax
  ret

.section .ltext.ie_local,"axl",@progbits
ie_local:
  movq loc_tls@GOTTPOFF(%rip), %rax
  ret

.section .ltext.pers,"axl",@progbits
pers:
  ret

.section .tbss,"awT",@nobits
loc_tls:
  .zero 4

.section .ltext.resolver,"axl",@progbits
.type resolver, @function
resolver:
  ret
.globl ifn
.type ifn, @gnu_indirect_function
.set ifn, resolver

#--- b.s
.section .tbss,"awT",@nobits
.globl ext_tls, y
ext_tls:
  .zero 4
y:
  .zero 4

#--- tls.s
.globl _start
_start:
  ret

.section .ltext.ld,"axl",@progbits
ld:
  leaq x@tlsld(%rip), %rdi
  movabsq $__tls_get_addr@PLTOFF, %rax
  addq %r15, %rax
  callq *%rax

.section .ltext.gd,"axl",@progbits
gd:
  leaq x@tlsgd(%rip), %rdi
  movabsq $__tls_get_addr@PLTOFF, %rax
  addq %rbx, %rax
  callq *%rax

.section .ltext.gd_ext,"axl",@progbits
gd_ext:
  leaq y@tlsgd(%rip), %rdi
  movabsq $__tls_get_addr@PLTOFF, %rax
  addq %rbx, %rax
  callq *%rax

.section .ltext.desc,"axl",@progbits
desc:
  leaq x@tlsdesc(%rip), %rax
  call *x@tlscall(%rax)

.section .ltext.desc_ext,"axl",@progbits
desc_ext:
  leaq y@tlsdesc(%rip), %rax
  call *y@tlscall(%rax)

.section .ltext.ie,"axl",@progbits
ie:
  movq x@gottpoff(%rip), %rax

.section .ltext.ie4_ext,"axl",@progbits
ie4_ext:
  movq y@gottpoff(%rip), %r16

.section .ltext.ie6_ext,"axl",@progbits
ie6_ext:
  addq %r8, y@gottpoff(%rip), %r16

.section .ltext.desc4_ext,"axl",@progbits
desc4_ext:
  leaq y@tlsdesc(%rip), %r16
  movq %r16, %rax
  call *y@tlscall(%rax)

.section .ltext.tmb,"axl",@progbits
tmb:
  leaq _TLS_MODULE_BASE_@tlsdesc(%rip), %rax
  call *_TLS_MODULE_BASE_@tlscall(%rax)

.section .tbss,"awT",@nobits
.globl x
.hidden x
x:
  .zero 4

#--- misc.s
.globl _start
_start:
  leaq str(%rip), %rax
  movl relro(%rip), %eax
  movl unk(%rip), %eax

.section .rodata.str1.1,"aMS",@progbits,1
  .asciz "demoted"

.section .lrodata.str1.1,"aMSl",@progbits,1
str:
  .asciz "demoted"

.section .ldata.rel.ro.relro,"awl",@progbits
relro:
  .quad 0

.section .ldatax,"awl",@progbits
unk:
  .long 0

#--- cfi.s
.globl _start
_start:
  movabsq $live, %rax
  ret

.section .ltext.cfi,"axl",@progbits
.globl cfi
cfi:
  .cfi_startproc
  ret
  .cfi_endproc

.section .ltext.dead,"axl",@progbits
dead:
  .cfi_startproc
  movl $live, %eax
  ret
  .cfi_endproc

.section .ldata.live,"awl",@progbits
live:
  .long 0

#--- comdat-large.s
.section .ldata.cv,"awlG",@progbits,cv,comdat
.weak cv
cv:
  .quad 1

#--- comdat-small.s
.globl _start
_start:
  movq cv(%rip), %rax
  ret

.section .data.cv,"awG",@progbits,cv,comdat
.weak cv
cv:
  .quad 1

#--- self.s
.globl _start
_start:
  ret

.section .ltext.pc32_src,"axl",@progbits
pc32_src:
  movl small(%rip), %eax
  ret

.section .ltext.got_src,"axl",@progbits
got_src:
  movq small@GOTPCREL(%rip), %rax
  ret

.section .ltext.multi_src,"axl",@progbits
multi_src:
  movq small@GOTPCREL(%rip), %rax
  movl multi1(%rip), %eax
  movl multi2(%rip), %eax
  ret

.section .ldata.multi1,"awl",@progbits
multi1:
  .long 0

.section .ldata.multi2,"awl",@progbits
multi2:
  .long 0

.section .ldata.abs_src,"awl",@progbits
abs_src:
  .long abs_dst

.section .ldata.abs_dst,"awl",@progbits
abs_dst:
  .long 0

.section .lrodata.vt,"al",@progbits
vt:
  .long vfn@PLT - vt

.section .ltext.vfn,"axl",@progbits
vfn:
  ret

.section .rodata.rel,"a",@progbits
rel:
  .long lfn@PLT - rel

.section .ltext.lfn,"axl",@progbits
lfn:
  ret

.data
small:
  .long 0

#--- startstop.s
.globl _start
_start:
  movl $__start_lsec, %eax
  leaq __stop_lsec(%rip), %rax
  movabsq $__start_lsec64, %rax
  ret

.section .ltext.large_startstop,"axl",@progbits
large_startstop:
  movq small@GOTPCREL(%rip), %rax
  movl $__start_lsec_large, %eax
  ret

.data
small:
  .long 0

.section lsec,"awl",@progbits
  .quad 0

.section lsec64,"awl",@progbits
  .quad 0

.section lsec_large,"awl",@progbits
  .quad 0

#--- tlslazy.s
.globl _start
_start:
  ret

.section .ltext.lazy,"axl",@progbits
lazy:
  movq lz@gottpoff(%rip), %rax
  ret
.weak lz

#--- tlslazy-def.s
.section .tbss,"awT",@nobits
.globl lz
lz:
  .zero 4

#--- local.ver
{ local: *; };
