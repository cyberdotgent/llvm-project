# OS400MI LLVM Target

OS400MI is a placeholder target for emitting OS/400 symbolic Machine Interface
source. The canonical target triple is:

```text
os400mi-ibm-os400
```

The intended final artifact flow is:

```text
LLVM IR -> symbolic MI source -> CRTMIPGM/QPRCRTPG on OS/400 -> *PGM
```

The target deliberately starts without invented registers, instructions, or
native object emission. The first code generation path emits symbolic MI source
text directly for the verified V4R4 entry-wrapper program shape.

Initial scope is absolute bare freestanding C: enough to generate a valid MI
text listing for a program equivalent to `return 0;`. There is no libc, no ILE
module output, and no dynamic linking model in this first phase.
