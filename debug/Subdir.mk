
# variables
TARGETS-debug :=
ifeq ($(FOUND_X11),yes)
TARGETS-debug += \
	debug/xvideo
endif

debug/xvideo:  debug/xvideo.o

debug/xvideo  : LDLIBS  += $(ATHENA_LIBS)

# global targets
all:: $(TARGETS-debug)

distclean::
	rm -f $(TARGETS-debug)

