# Makefile for hello_world application
ROOT_PATH=../..
include $(ROOT_PATH)/build/shared-apps.mk

# Define target categories
caladan_targets = hello_world threads network int
native_targets = parse_lame parse_bundle matmul
all_targets = $(caladan_targets) $(native_targets) threads_native

# Collect all source files
caladan_src = $(addsuffix .c, $(caladan_targets))
native_src = $(addsuffix .c, $(native_targets))
all_src = $(caladan_src) $(native_src)

# Object files and dependencies
obj = $(all_src:.c=.o)
dep = $(obj:.o=.d)

# must be first
all: $(all_targets)

lib_shim = -Wl,--whole-archive $(ROOT_PATH)/shim/libshim.a -ldl -Wl,--no-whole-archive

# Pattern rule for Caladan targets
$(caladan_targets): %: %.o $(RUNTIME_DEPS)
	$(LD) $(LDFLAGS) -o $@ $< \
	-Wl,--wrap=main $(lib_shim) $(RUNTIME_LIBS)
	@$(ROOT_PATH)/scripts/verify_shim.sh $@

# Pattern rule for native targets
$(native_targets): %: %.o
	$(CC) $(CFLAGS) -o $@ $<

# Native glibc version of threads (no Caladan runtime)
threads_native: threads.o
	$(CC) $(CFLAGS) -pthread -o $@ $<

ifneq ($(MAKECMDGOALS),clean)
-include $(dep)   # include all dep files in the makefile
endif

# rule to generate a dep file by using the C preprocessor
# (see man cpp for details on the -MM and -MT options)
%.d: %.c
	@$(CC) $(CFLAGS) $< -MM -MT $(@:.d=.o) >$@
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(obj) $(dep) $(all_targets) 