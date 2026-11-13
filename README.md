# Iris-libc

Iris-libc is a C library for AvoryOS, with the long-term goal of running
programs built for the glibc API.

glibc compatibility is a broad ABI goal, not something provided by the current
implementation. The project currently contains a small subset of memory and
string functions, plus Linux `open`, `read`, `write`, `close`, and `lseek` calls. Process
startup, dynamic linking, symbol versioning, and the rest of the C and POSIX
APIs are not implemented yet.

The source is split between portable implementations in `src/memory.c` and
`src/string.c`, and x86-64 SIMD implementations in `src/string_x86_64.c`.

## Build, test, and benchmark

```sh
make
make test
make benchmark
make hello
make examples
make file-copy SOURCE=input.bin DESTINATION=output.bin
```

The build produces `libc.a`. Stdio includes `printf`, `vprintf`, `fprintf`,
`vfprintf`, `snprintf`, `vsnprintf`, and buffered `FILE` streams with `fopen`, `fclose`, `fflush`,
`fread`, `fwrite`, `fgetc`, and `fputc`. Formatting supports integer,
character, string, and pointer conversions; floating-point formatting is not
implemented yet. File streams currently use a fixed-size internal pool. Tests run against the host compiler and do not yet
exercise AvoryOS integration. `make hello` builds and runs a host executable
linked against Iris's archive; host startup code is still used. `make examples`
builds the hello and file-copy examples. `make file-copy SOURCE=... DESTINATION=...`
copies a file using Iris's buffered streams. `make benchmark`
compares the implemented memory, string, unistd, `snprintf`, `fprintf`, and
`vfprintf` functions against the host libc in one process, and measures buffered
`fputc` against one-byte writes. A separate stdio benchmark compares every
function currently declared in `stdio.h`; `fopen` timing includes the paired
`fclose` needed to release each stream.
`make benchmark-musl` runs the same
benchmark against musl, using `musl-gcc` by default; set `MUSL_CC` to use a
different musl compiler wrapper. An Iris speedup above `1.00x` means Iris was
faster for that case. Results are host-specific and do not represent AvoryOS
performance.

On x86-64, memory and string routines use runtime-checked SSE2, AVX2, and
AVX-512 paths when supported by both the CPU and operating system. Other targets
use the portable C implementations.
