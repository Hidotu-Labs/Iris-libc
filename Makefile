CC ?= cc
AR ?= ar
HOST_CC ?= cc
MUSL_CC ?= musl-gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector
BENCH_CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin

LIBRARY = libc.a
CRT0 = build/crt0.o
OBJECTS = build/memory.o build/string.o build/string_x86_64.o build/string_extra.o build/unistd.o build/termios.o build/mman.o build/stdlib.o build/stdio.o build/stdio_extra.o build/ctype.o build/locale.o
TEST = build/test_string
BENCH = build/bench_string
MUSL_BENCH = build/bench_string_musl
STDIO_BENCH = build/bench_stdio
STDIO_MUSL_BENCH = build/bench_stdio_musl
UNISTD_BENCH = build/bench_unistd
UNISTD_MUSL_BENCH = build/bench_unistd_musl
STDLIB_BENCH = build/bench_stdlib
STDLIB_MUSL_BENCH = build/bench_stdlib_musl
HELLO = build/hello
FILE_COPY = build/file_copy
LINE_SORT = build/line_sort
KILO_SOURCE ?= examples/kilo.c
KILO = build/kilo_iris
BENCH_OBJECTS = build/iris_memory_bench.o build/iris_string_bench.o build/iris_string_x86_64_bench.o build/iris_string_extra_bench.o build/iris_unistd_bench.o build/iris_mman_bench.o build/iris_stdlib_bench.o build/iris_stdio_bench.o
MUSL_BENCH_OBJECTS = build/iris_memory_musl_bench.o build/iris_string_musl_bench.o build/iris_string_x86_64_musl_bench.o build/iris_string_extra_musl_bench.o build/iris_unistd_musl_bench.o build/iris_mman_musl_bench.o build/iris_stdlib_musl_bench.o build/iris_stdio_musl_bench.o

.PHONY: all test benchmark benchmark-musl hello examples file-copy kilo clean

all: $(LIBRARY) $(CRT0)

$(LIBRARY): $(OBJECTS)
	$(AR) rcs $@ $^

$(CRT0): src/crt0.c include/stdio.h include/unistd.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -Isrc -c $< -o $@

build/memory.o: src/memory.c include/string.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -Isrc -c $< -o $@

build/string.o: src/string.c include/string.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -Isrc -c $< -o $@

build/string_x86_64.o: src/string_x86_64.c include/string.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -Isrc -c $< -o $@

build/string_extra.o: src/string_extra.c include/string.h include/errno.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -Isrc -c $< -o $@

build/unistd.o: src/unistd.c include/unistd.h include/fcntl.h include/errno.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -Isrc -c $< -o $@

build/termios.o: src/termios.c include/termios.h include/sys/ioctl.h include/unistd.h include/errno.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -Isrc -c $< -o $@

build/mman.o: src/mman.c include/sys/mman.h include/errno.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -Isrc -c $< -o $@

build/stdlib.o: src/stdlib.c include/stdlib.h include/stdio.h include/string.h include/sys/mman.h include/unistd.h include/errno.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -Isrc -c $< -o $@

build/stdio.o: src/stdio.c include/stdio.h include/unistd.h include/fcntl.h include/string.h include/errno.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -Isrc -c $< -o $@

build/stdio_extra.o: src/stdio_extra.c include/stdio.h include/stdlib.h include/string.h include/ctype.h include/errno.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -Isrc -c $< -o $@

build/ctype.o: src/ctype.c include/ctype.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -Isrc -c $< -o $@

build/locale.o: src/locale.c include/locale.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -Isrc -c $< -o $@

$(TEST): tests/test_string.c src/memory.c src/string.c src/string_x86_64.c src/string_extra.c src/unistd.c src/mman.c src/stdlib.c src/stdio.c src/stdio_extra.c include/string.h include/unistd.h include/stdio.h include/fcntl.h include/sys/mman.h include/stdlib.h include/errno.h
	mkdir -p build
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -Iinclude -Isrc tests/test_string.c src/memory.c src/string.c src/string_x86_64.c src/string_extra.c src/unistd.c src/mman.c src/stdlib.c src/stdio.c src/stdio_extra.c -o $@

build/test_stdlib: tests/test_stdlib.c $(CRT0) $(LIBRARY) include/stdlib.h include/stdio.h include/string.h include/ctype.h include/locale.h include/errno.h
	mkdir -p build
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -nostdlib -static -fno-builtin -Iinclude $< $(CRT0) $(LIBRARY) -o $@

test: $(TEST) build/test_stdlib
	./$(TEST)
	./build/test_stdlib

$(HELLO): examples/hello.c $(LIBRARY) include/stdio.h
	mkdir -p build
	$(HOST_CC) $(BENCH_CFLAGS) -Iinclude $< $(LIBRARY) -o $@

$(FILE_COPY): examples/file_copy.c $(LIBRARY) include/stdio.h
	mkdir -p build
	$(HOST_CC) $(BENCH_CFLAGS) -Iinclude $< $(LIBRARY) -o $@

$(LINE_SORT): examples/line_sort.c $(CRT0) $(LIBRARY) include/stdio.h include/stdlib.h include/string.h include/unistd.h include/fcntl.h include/sys/mman.h
	mkdir -p build
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -nostdlib -static -fno-builtin -Iinclude $< $(CRT0) $(LIBRARY) -o $@

examples: $(HELLO) $(FILE_COPY) $(LINE_SORT)

$(KILO): $(KILO_SOURCE) $(CRT0) $(LIBRARY) include/termios.h include/stdlib.h include/stdio.h include/errno.h include/string.h include/ctype.h include/time.h include/sys/types.h include/sys/ioctl.h include/sys/time.h include/unistd.h include/signal.h include/fcntl.h
	mkdir -p build
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Wno-unterminated-string-initialization -nostdlib -static -fno-builtin -Iinclude $< $(CRT0) $(LIBRARY) -o $@

kilo: $(KILO)

hello: $(HELLO)
	./$(HELLO)

file-copy: $(FILE_COPY)
	@test -n "$(SOURCE)" -a -n "$(DESTINATION)" || { printf 'Usage: make file-copy SOURCE=path DESTINATION=path\n'; exit 2; }
	./$(FILE_COPY) "$(SOURCE)" "$(DESTINATION)"

build/iris_memory_bench.o: src/memory.c include/string.h
	mkdir -p build
	$(HOST_CC) $(BENCH_CFLAGS) -Iinclude \
		-Isrc -Dmemcpy=iris_memcpy -Dmemmove=iris_memmove \
		-Dmemset=iris_memset -Dmemcmp=iris_memcmp -c $< -o $@

build/iris_string_bench.o: src/string.c include/string.h
	mkdir -p build
	$(HOST_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dstrlen=iris_strlen -Dstrcmp=iris_strcmp -Dstrncmp=iris_strncmp \
		-c $< -o $@

build/iris_string_extra_bench.o: src/string_extra.c include/string.h include/errno.h
	mkdir -p build
	$(HOST_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dstrchr=iris_strchr -Dstrstr=iris_strstr -Dstrerror=iris_strerror \
		-c $< -o $@

build/iris_string_extra_musl_bench.o: src/string_extra.c include/string.h include/errno.h
	mkdir -p build
	$(MUSL_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dstrchr=iris_strchr -Dstrstr=iris_strstr -Dstrerror=iris_strerror \
		-c $< -o $@

build/iris_string_x86_64_bench.o: src/string_x86_64.c include/string.h
	mkdir -p build
	$(HOST_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dmemcpy=iris_memcpy -Dmemmove=iris_memmove \
		-Dmemset=iris_memset -Dmemcmp=iris_memcmp \
		-Dstrlen=iris_strlen -Dstrcmp=iris_strcmp -Dstrncmp=iris_strncmp \
		-c $< -o $@

build/iris_unistd_bench.o: src/unistd.c include/unistd.h include/fcntl.h include/errno.h
	mkdir -p build
	$(HOST_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dread=iris_read -Dwrite=iris_write -Dclose=iris_close \
		-Dopen=iris_open -Dlseek=iris_lseek -Dftruncate=iris_ftruncate \
		-Dioctl=iris_ioctl -Dtime=iris_time -Dsignal=iris_signal \
		-Dgetpid=iris_getpid \
		-D_exit=iris__exit \
		-c $< -o $@

build/iris_termios_bench.o: src/termios.c include/termios.h include/sys/ioctl.h include/unistd.h include/errno.h
	mkdir -p build
	$(HOST_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dtcgetattr=iris_tcgetattr -Dtcsetattr=iris_tcsetattr \
		-Disatty=iris_isatty -Dioctl=iris_ioctl -c $< -o $@

build/iris_mman_bench.o: src/mman.c include/sys/mman.h include/errno.h
	mkdir -p build
	$(HOST_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dmmap=iris_mmap -Dmunmap=iris_munmap -Dmprotect=iris_mprotect \
		-c $< -o $@

build/iris_stdlib_bench.o: src/stdlib.c include/stdlib.h include/stdio.h include/string.h include/sys/mman.h include/unistd.h include/errno.h
	mkdir -p build
	$(HOST_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dmalloc=iris_malloc -Dcalloc=iris_calloc -Drealloc=iris_realloc \
		-Dfree=iris_free -Datexit=iris_atexit -Dexit=iris_exit -D_Exit=iris__Exit -Dabort=iris_abort \
		-Dabs=iris_abs -Dlabs=iris_labs -Datoi=iris_atoi \
		-Dstrtol=iris_strtol -Dstrtoul=iris_strtoul -Dqsort=iris_qsort \
		-Dmmap=iris_mmap -Dmunmap=iris_munmap -D_exit=iris__exit \
		-Dfflush=iris_fflush -Dmemset=iris_memset -Dmemcpy=iris_memcpy \
		-c $< -o $@

build/iris_ctype_bench.o: src/ctype.c include/ctype.h
	mkdir -p build
	$(HOST_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Disalnum=iris_isalnum -Disalpha=iris_isalpha -Disblank=iris_isblank \
		-Discntrl=iris_iscntrl -Disdigit=iris_isdigit -Disgraph=iris_isgraph \
		-Dislower=iris_islower -Disprint=iris_isprint -Dispunct=iris_ispunct \
		-Disspace=iris_isspace -Disupper=iris_isupper -Disxdigit=iris_isxdigit \
		-Dtolower=iris_tolower -Dtoupper=iris_toupper -c $< -o $@

build/iris_ctype_musl_bench.o: src/ctype.c include/ctype.h
	mkdir -p build
	$(MUSL_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Disalnum=iris_isalnum -Disalpha=iris_isalpha -Disblank=iris_isblank \
		-Discntrl=iris_iscntrl -Disdigit=iris_isdigit -Disgraph=iris_isgraph \
		-Dislower=iris_islower -Disprint=iris_isprint -Dispunct=iris_ispunct \
		-Disspace=iris_isspace -Disupper=iris_isupper -Disxdigit=iris_isxdigit \
		-Dtolower=iris_tolower -Dtoupper=iris_toupper -c $< -o $@

build/iris_locale_bench.o: src/locale.c include/locale.h
	mkdir -p build
	$(HOST_CC) $(BENCH_CFLAGS) -Iinclude -Isrc -Dsetlocale=iris_setlocale \
		-c $< -o $@

build/iris_locale_musl_bench.o: src/locale.c include/locale.h
	mkdir -p build
	$(MUSL_CC) $(BENCH_CFLAGS) -Iinclude -Isrc -Dsetlocale=iris_setlocale \
		-c $< -o $@

build/iris_stdio_bench.o: src/stdio.c include/stdio.h include/unistd.h include/fcntl.h include/string.h include/errno.h
	mkdir -p build
	$(HOST_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dprintf=iris_printf -Dvprintf=iris_vprintf \
		-Dfprintf=iris_fprintf -Dvfprintf=iris_vfprintf \
		-Dsnprintf=iris_snprintf -Dvsnprintf=iris_vsnprintf \
		-Dfopen=iris_fopen -Dfclose=iris_fclose -Dfflush=iris_fflush \
		-Dfseek=iris_fseek -Dftell=iris_ftell -Drewind=iris_rewind \
		-Dfgets=iris_fgets -Dfputs=iris_fputs -Dputs=iris_puts \
		-Dfread=iris_fread -Dfwrite=iris_fwrite -Dfgetc=iris_fgetc \
		-Dfputc=iris_fputc -Dfeof=iris_feof -Dferror=iris_ferror \
		-Dclearerr=iris_clearerr -Dstdin=iris_stdin -Dstdout=iris_stdout \
		-Dstderr=iris_stderr -Dopen=iris_open -Dclose=iris_close \
		-Dread=iris_read -Dwrite=iris_write -Dlseek=iris_lseek \
		-c $< -o $@

build/iris_stdio_extra_bench.o: src/stdio_extra.c include/stdio.h include/stdlib.h include/string.h include/ctype.h include/errno.h
	mkdir -p build
	$(HOST_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dgetline=iris_getline -Dvsscanf=iris_vsscanf \
		-Dsscanf=iris_sscanf -Dperror=iris_perror \
		-Disspace=iris_isspace -Dstrtol=iris_strtol -Dstrerror=iris_strerror \
		-Dmalloc=iris_malloc -Drealloc=iris_realloc \
		-Dfgetc=iris_fgetc -Dfeof=iris_feof -Dferror=iris_ferror \
		-Dfprintf=iris_fprintf -Dstderr=iris_stderr -c $< -o $@

build/iris_stdio_extra_musl_bench.o: src/stdio_extra.c include/stdio.h include/stdlib.h include/string.h include/ctype.h include/errno.h
	mkdir -p build
	$(MUSL_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dgetline=iris_getline -Dvsscanf=iris_vsscanf \
		-Dsscanf=iris_sscanf -Dperror=iris_perror \
		-Disspace=iris_isspace -Dstrtol=iris_strtol -Dstrerror=iris_strerror \
		-Dmalloc=iris_malloc -Drealloc=iris_realloc \
		-Dfgetc=iris_fgetc -Dfeof=iris_feof -Dferror=iris_ferror \
		-Dfprintf=iris_fprintf -Dstderr=iris_stderr -c $< -o $@

build/iris_memory_musl_bench.o: src/memory.c include/string.h
	mkdir -p build
	$(MUSL_CC) $(BENCH_CFLAGS) -Iinclude \
		-Isrc -Dmemcpy=iris_memcpy -Dmemmove=iris_memmove \
		-Dmemset=iris_memset -Dmemcmp=iris_memcmp -c $< -o $@

build/iris_string_musl_bench.o: src/string.c include/string.h
	mkdir -p build
	$(MUSL_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dstrlen=iris_strlen -Dstrcmp=iris_strcmp -Dstrncmp=iris_strncmp \
		-c $< -o $@

build/iris_string_x86_64_musl_bench.o: src/string_x86_64.c include/string.h
	mkdir -p build
	$(MUSL_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dmemcpy=iris_memcpy -Dmemmove=iris_memmove \
		-Dmemset=iris_memset -Dmemcmp=iris_memcmp \
		-Dstrlen=iris_strlen -Dstrcmp=iris_strcmp -Dstrncmp=iris_strncmp \
		-c $< -o $@

build/iris_unistd_musl_bench.o: src/unistd.c include/unistd.h include/fcntl.h include/errno.h
	mkdir -p build
	$(MUSL_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dread=iris_read -Dwrite=iris_write -Dclose=iris_close \
		-Dopen=iris_open -Dlseek=iris_lseek -Dftruncate=iris_ftruncate \
		-Dioctl=iris_ioctl -Dtime=iris_time -Dsignal=iris_signal \
		-Dgetpid=iris_getpid \
		-D_exit=iris__exit \
		-c $< -o $@

build/iris_termios_musl_bench.o: src/termios.c include/termios.h include/sys/ioctl.h include/unistd.h include/errno.h
	mkdir -p build
	$(MUSL_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dtcgetattr=iris_tcgetattr -Dtcsetattr=iris_tcsetattr \
		-Disatty=iris_isatty -Dioctl=iris_ioctl -c $< -o $@

build/iris_mman_musl_bench.o: src/mman.c include/sys/mman.h include/errno.h
	mkdir -p build
	$(MUSL_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dmmap=iris_mmap -Dmunmap=iris_munmap -Dmprotect=iris_mprotect \
		-c $< -o $@

build/iris_stdlib_musl_bench.o: src/stdlib.c include/stdlib.h include/stdio.h include/string.h include/sys/mman.h include/unistd.h include/errno.h
	mkdir -p build
	$(MUSL_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dmalloc=iris_malloc -Dcalloc=iris_calloc -Drealloc=iris_realloc \
		-Dfree=iris_free -Datexit=iris_atexit -Dexit=iris_exit -D_Exit=iris__Exit -Dabort=iris_abort \
		-Dabs=iris_abs -Dlabs=iris_labs -Datoi=iris_atoi \
		-Dstrtol=iris_strtol -Dstrtoul=iris_strtoul -Dqsort=iris_qsort \
		-Dmmap=iris_mmap -Dmunmap=iris_munmap -D_exit=iris__exit \
		-Dfflush=iris_fflush -Dmemset=iris_memset -Dmemcpy=iris_memcpy \
		-c $< -o $@

build/iris_stdio_musl_bench.o: src/stdio.c include/stdio.h include/unistd.h include/fcntl.h include/string.h include/errno.h
	mkdir -p build
	$(MUSL_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dprintf=iris_printf -Dvprintf=iris_vprintf \
		-Dfprintf=iris_fprintf -Dvfprintf=iris_vfprintf \
		-Dsnprintf=iris_snprintf -Dvsnprintf=iris_vsnprintf \
		-Dfopen=iris_fopen -Dfclose=iris_fclose -Dfflush=iris_fflush \
		-Dfseek=iris_fseek -Dftell=iris_ftell -Drewind=iris_rewind \
		-Dfgets=iris_fgets -Dfputs=iris_fputs -Dputs=iris_puts \
		-Dfread=iris_fread -Dfwrite=iris_fwrite -Dfgetc=iris_fgetc \
		-Dfputc=iris_fputc -Dfeof=iris_feof -Dferror=iris_ferror \
		-Dclearerr=iris_clearerr -Dstdin=iris_stdin -Dstdout=iris_stdout \
		-Dstderr=iris_stderr -Dopen=iris_open -Dclose=iris_close \
		-Dread=iris_read -Dwrite=iris_write -Dlseek=iris_lseek \
		-c $< -o $@

$(BENCH): benchmarks/bench_string.c $(BENCH_OBJECTS)
	$(HOST_CC) $(BENCH_CFLAGS) $^ -o $@

$(STDIO_BENCH): benchmarks/bench_stdio.c build/iris_stdio_bench.o build/iris_unistd_bench.o
	$(HOST_CC) $(BENCH_CFLAGS) $^ -o $@

$(UNISTD_BENCH): benchmarks/bench_unistd.c benchmarks/bench_common.h build/iris_unistd_bench.o build/iris_mman_bench.o build/iris_termios_bench.o
	$(HOST_CC) $(BENCH_CFLAGS) $< build/iris_unistd_bench.o build/iris_mman_bench.o build/iris_termios_bench.o -o $@

$(STDLIB_BENCH): benchmarks/bench_stdlib.c benchmarks/bench_common.h build/iris_stdlib_bench.o build/iris_stdio_extra_bench.o build/iris_string_extra_bench.o build/iris_ctype_bench.o build/iris_locale_bench.o build/iris_mman_bench.o build/iris_unistd_bench.o build/iris_stdio_bench.o build/iris_memory_bench.o build/iris_string_x86_64_bench.o
	$(HOST_CC) $(BENCH_CFLAGS) $< build/iris_stdlib_bench.o build/iris_stdio_extra_bench.o build/iris_string_extra_bench.o build/iris_ctype_bench.o build/iris_locale_bench.o build/iris_mman_bench.o build/iris_unistd_bench.o build/iris_stdio_bench.o build/iris_memory_bench.o build/iris_string_x86_64_bench.o -o $@

$(MUSL_BENCH): benchmarks/bench_string.c $(MUSL_BENCH_OBJECTS)
	$(MUSL_CC) $(BENCH_CFLAGS) -DBENCH_LIBC_NAME='"musl"' $^ -o $@

$(STDIO_MUSL_BENCH): benchmarks/bench_stdio.c build/iris_stdio_musl_bench.o build/iris_unistd_musl_bench.o
	$(MUSL_CC) $(BENCH_CFLAGS) -DBENCH_LIBC_NAME='"musl"' $^ -o $@

$(UNISTD_MUSL_BENCH): benchmarks/bench_unistd.c benchmarks/bench_common.h build/iris_unistd_musl_bench.o build/iris_mman_musl_bench.o build/iris_termios_musl_bench.o
	$(MUSL_CC) $(BENCH_CFLAGS) -DBENCH_LIBC_NAME='"musl"' $< build/iris_unistd_musl_bench.o build/iris_mman_musl_bench.o build/iris_termios_musl_bench.o -o $@

$(STDLIB_MUSL_BENCH): benchmarks/bench_stdlib.c benchmarks/bench_common.h build/iris_stdlib_musl_bench.o build/iris_stdio_extra_musl_bench.o build/iris_string_extra_musl_bench.o build/iris_ctype_musl_bench.o build/iris_locale_musl_bench.o build/iris_mman_musl_bench.o build/iris_unistd_musl_bench.o build/iris_stdio_musl_bench.o build/iris_memory_musl_bench.o build/iris_string_x86_64_musl_bench.o
	$(MUSL_CC) $(BENCH_CFLAGS) -DBENCH_LIBC_NAME='"musl"' $< build/iris_stdlib_musl_bench.o build/iris_stdio_extra_musl_bench.o build/iris_string_extra_musl_bench.o build/iris_ctype_musl_bench.o build/iris_locale_musl_bench.o build/iris_mman_musl_bench.o build/iris_unistd_musl_bench.o build/iris_stdio_musl_bench.o build/iris_memory_musl_bench.o build/iris_string_x86_64_musl_bench.o -o $@

benchmark: $(BENCH) $(STDIO_BENCH) $(UNISTD_BENCH) $(STDLIB_BENCH)
	./$(BENCH)
	./$(STDIO_BENCH)
	./$(UNISTD_BENCH)
	./$(STDLIB_BENCH)

benchmark-musl: $(MUSL_BENCH) $(STDIO_MUSL_BENCH) $(UNISTD_MUSL_BENCH) $(STDLIB_MUSL_BENCH)
	./$(MUSL_BENCH)
	./$(STDIO_MUSL_BENCH)
	./$(UNISTD_MUSL_BENCH)
	./$(STDLIB_MUSL_BENCH)

clean:
	rm -rf build $(LIBRARY)