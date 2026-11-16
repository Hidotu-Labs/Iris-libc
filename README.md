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
`ctype.h` supports the `C`/`POSIX` locales and the built-in
`en_US.ISO-8859-1` locale for byte classification and case conversion. Locale
category selection is available, but collation and numeric conventions remain
bytewise/C-style. UTF-8 decoding and wide-character functions are not
implemented. Calendar conversion and formatting currently use UTC only.

## Build, test, and benchmark

```sh
make
make test
make benchmark
make hello
make examples
make file-copy SOURCE=input.bin DESTINATION=output.bin
make kilo KILO_SOURCE=/path/to/kilo.c
```

The build produces `libc.a`. Stdio includes `printf`, `vprintf`, `fprintf`,
`vfprintf`, `sprintf`, `vsprintf`, `snprintf`, `vsnprintf`, and buffered `FILE`
streams with `fopen`, `freopen`, `tmpfile`, `tmpnam`, `fclose`, `fflush`,
`setvbuf`, `fseek`, `ftell`, `rewind`, `fread`, `fwrite`, `fgetc`, `getc`,
`ungetc`, `fputc`, `fgets`, `fputs`, `puts`, `remove`, and `rename`. Formatting
supports integer, character, string, pointer, and common `%f`, `%e`, and `%g`
conversions.
File streams currently use a fixed-size internal pool and do not support
caller-supplied `setvbuf` buffers. `localtime` currently uses UTC, and
`system` reports that command execution is unavailable. Tests run against the host compiler and do not yet
exercise AvoryOS integration. `make hello` builds and runs a host executable
linked against Iris's archive; host startup code is still used. `make examples`
builds the hello and file-copy examples. `make file-copy SOURCE=... DESTINATION=...`
copies a file using Iris's buffered streams. `make benchmark`
compares the implemented memory, string, unistd, `snprintf`, `fprintf`, and
`vfprintf` functions against the host libc in one process, and measures buffered
`fputc` against one-byte writes. A separate stdio benchmark compares a selected
set of `stdio.h` functions; `fopen` timing includes the paired
`fclose` needed to release each stream.
`make kilo KILO_SOURCE=...` builds a freestanding Kilo editor binary using
Iris's startup code and library archive.
`make benchmark-musl` runs the same
benchmark against musl, using `musl-gcc` by default; set `MUSL_CC` to use a
different musl compiler wrapper. An Iris speedup above `1.00x` means Iris was
faster for that case. Results are host-specific and do not represent AvoryOS
performance.

On x86-64, memory and string routines use runtime-checked SSE2, AVX2, and
AVX-512 paths when supported by both the CPU and operating system. Other targets
use the portable C implementations.
