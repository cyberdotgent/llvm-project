# OS400MI LLVM Target

OS400MI is a placeholder target for emitting OS/400 symbolic Machine Interface
source. The intended final artifact flow is:

```text
LLVM IR -> symbolic MI source -> CRTMIPGM/QPRCRTPG on OS/400 -> *PGM
```

The target deliberately starts with target-info registration only. Do not add
invented registers, instructions, or object emission paths until the V4R4 MI
source format, ABI, and program creation path are verified.
