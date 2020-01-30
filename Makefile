CC=clang
SOURCES=elf-module.c elf-module-i386.c reld.c
CFLAGS+=-std=c99 -DDEBUGGING -D_GNU_SOURCE -ggdb3 -m32 -pedantic -Iinclude
CFLAGS_MODULE= -m32 -fno-common -fno-builtin -Wno-implicit-function-declaration
TARGET=reld
MODULES=mod1.mod mod2.mod

all: $(TARGET) $(MODULES)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS)  -o $@ -Iinclude $(SOURCES)

clean:
	rm -f $(OBJS) $(TARGET) $(MODULES)

%.mod: %.c
	clang $(CFLAGS_MODULE) -c -o $@ $<

.PHONY: clean
