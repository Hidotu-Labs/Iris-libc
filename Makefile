CC ?= cc
AR ?= ar
HOST_CC ?= cc
MUSL_CC ?= musl-gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin
BENCH_CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin

LIBRARY = libc.a
OBJECTS = build/memory.o build/string.o build/string_x86_64.o build/unistd.o
TEST = build/test_string
BENCH = build/bench_string
MUSL_BENCH = build/bench_string_musl
HELLO = build/hello
BENCH_OBJECTS = build/iris_memory_bench.o build/iris_string_bench.o build/iris_string_x86_64_bench.o build/iris_unistd_bench.o
MUSL_BENCH_OBJECTS = build/iris_memory_musl_bench.o build/iris_string_musl_bench.o build/iris_string_x86_64_musl_bench.o build/iris_unistd_musl_bench.o

.PHONY: all test benchmark benchmark-musl hello clean

all: $(LIBRARY)

$(LIBRARY): $(OBJECTS)
	$(AR) rcs $@ $^

build/memory.o: src/memory.c include/string.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -Isrc -c $< -o $@

build/string.o: src/string.c include/string.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -Isrc -c $< -o $@

build/string_x86_64.o: src/string_x86_64.c include/string.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -Isrc -c $< -o $@

build/unistd.o: src/unistd.c include/unistd.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -Isrc -c $< -o $@

$(TEST): tests/test_string.c src/memory.c src/string.c src/string_x86_64.c src/unistd.c include/string.h include/unistd.h
	mkdir -p build
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -Iinclude -Isrc tests/test_string.c src/memory.c src/string.c src/string_x86_64.c src/unistd.c -o $@

test: $(TEST)
	./$(TEST)

$(HELLO): examples/hello.c $(LIBRARY) include/unistd.h
	mkdir -p build
	$(HOST_CC) $(BENCH_CFLAGS) -Iinclude $< $(LIBRARY) -o $@

hello: $(HELLO)
	./$(HELLO)

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

build/iris_string_x86_64_bench.o: src/string_x86_64.c include/string.h
	mkdir -p build
	$(HOST_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dmemcpy=iris_memcpy -Dmemmove=iris_memmove \
		-Dmemset=iris_memset -Dmemcmp=iris_memcmp \
		-Dstrlen=iris_strlen -Dstrcmp=iris_strcmp -Dstrncmp=iris_strncmp \
		-c $< -o $@

build/iris_unistd_bench.o: src/unistd.c include/unistd.h
	mkdir -p build
	$(HOST_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dread=iris_read -Dwrite=iris_write -Dclose=iris_close \
		-c $< -o $@

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

build/iris_unistd_musl_bench.o: src/unistd.c include/unistd.h
	mkdir -p build
	$(MUSL_CC) $(BENCH_CFLAGS) -Iinclude -Isrc \
		-Dread=iris_read -Dwrite=iris_write -Dclose=iris_close \
		-c $< -o $@

$(BENCH): benchmarks/bench_string.c $(BENCH_OBJECTS)
	$(HOST_CC) $(BENCH_CFLAGS) $^ -o $@

$(MUSL_BENCH): benchmarks/bench_string.c $(MUSL_BENCH_OBJECTS)
	$(MUSL_CC) $(BENCH_CFLAGS) -DBENCH_LIBC_NAME='"musl"' $^ -o $@

benchmark: $(BENCH)
	./$(BENCH)

benchmark-musl: $(MUSL_BENCH)
	./$(MUSL_BENCH)

clean:
	rm -rf build $(LIBRARY)