# Makefile for hello_world application
ROOT_PATH=../..
include $(ROOT_PATH)/build/shared.mk

hello_world_src = hello_world.c
hello_world_obj = $(hello_world_src:.c=.o)

threads_src = threads.c
threads_obj = $(threads_src:.c=.o)

network_src = network.c
network_obj = $(network_src:.c=.o)

int_src = int.c
int_obj = $(int_src:.c=.o)

parse_lame_src = parse_lame.c
parse_lame_obj = $(parse_lame_src:.c=.o)

# must be first
all: hello_world threads network int parse_lame

lib_shim = $(ROOT_PATH)/shim/libshim.a -ldl

hello_world: $(hello_world_obj) $(RUNTIME_DEPS)
	$(LD) $(LDFLAGS) -o $@ $(hello_world_obj) \
	-Wl,--wrap=main $(lib_shim) $(RUNTIME_LIBS)

threads: $(threads_obj) $(RUNTIME_DEPS)
	$(LD) $(LDFLAGS) -o $@ $(threads_obj) \
	-Wl,--wrap=main $(lib_shim) $(RUNTIME_LIBS)

network: $(network_obj) $(RUNTIME_DEPS)
	$(LD) $(LDFLAGS) -o $@ $(network_obj) \
	-Wl,--wrap=main $(lib_shim) $(RUNTIME_LIBS)

int: $(int_obj) $(RUNTIME_DEPS)
	$(LD) $(LDFLAGS) -o $@ $(int_obj) \
	-Wl,--wrap=main $(lib_shim) $(RUNTIME_LIBS)

parse_lame: $(parse_lame_obj)
	$(CC) $(CFLAGS) -o $@ $(parse_lame_obj)

# general build rules for all targets
src = $(hello_world_src) $(threads_src) $(network_src) $(int_src) $(parse_lame_src)
obj = $(src:.c=.o)
dep = $(obj:.o=.d)

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
	rm -f $(obj) $(dep) hello_world threads network int parse_lame 