ROOT_PATH=../
include $(ROOT_PATH)/build/shared.mk

# Verbose mode - use V=1 to see full commands
V ?= 0
ifeq ($(V),1)
    Q =
else
    Q = @
endif

# handy for debugging
print-%  : ; @echo $* = $($*)

# libshim.a - the shenango shim library
shim_src = $(wildcard *.c)
shim_obj = $(shim_src:.c=.o)

# must be first
all: libshim.a

libshim.a: $(shim_obj)
	@echo "  AR      $@"
	$(Q)$(AR) rcs $@ $^

# general build rules for all targets
src = $(shim_src)
obj = $(src:.c=.o)
dep = $(obj:.o=.d)

ifneq ($(MAKECMDGOALS),clean)
-include $(dep)   # include all dep files in the makefile
endif

# rule to generate a dep file by using the C preprocessor
# (see man cpp for details on the -MM and -MT options)
%.d: %.c
	$(Q)$(CC) $(CFLAGS) $< -MM -MT $(@:.d=.o) >$@

%.o: %.c
	@echo "  CC      $@"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	@echo "  CLEAN   shim"
	$(Q)rm -f $(obj) $(dep) libshim.a
