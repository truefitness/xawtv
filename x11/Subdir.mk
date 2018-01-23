
# targets to build
TARGETS-x11 := \
	x11/v4lctl

ifeq ($(FOUND_X11),yes)
TARGETS-x11 += \
	x11/propwatch \
	x11/xawtv-remote \
	x11/rootv \
	x11/xawtv \
	x11/pia
endif
ifeq ($(FOUND_MOTIF),yes)
TARGETS-x11 += \
	x11/motv
endif
ifeq ($(FOUND_OS)$(FOUND_MOTIF)$(FOUND_ZVBI),linuxyesyes)
TARGETS-x11 += \
	x11/mtt
endif

# objects for targets
x11/xawtv: \
	x11/xawtv.o \
	x11/wmhooks.o \
	x11/atoms.o \
	x11/x11.o \
	x11/blit.o \
	x11/xt.o \
	x11/xv.o \
	x11/toolbox.o \
	x11/conf.o \
	x11/complete-xaw.o \
	x11/vbi-x11.o \
	jwz/remote.o \
	common/channel.o \
	common/vbi-data.o \
	$(OBJS-common-alsa) \
	$(OBJS-common-input) \
	$(OBJS-common-capture)

x11/motv: \
	x11/motv.o \
	x11/man.o \
	x11/icons.o \
	x11/wmhooks.o \
	x11/atoms.o \
	x11/x11.o \
	x11/blit.o \
	x11/xt.o \
	x11/xv.o \
	x11/complete-motif.o \
	x11/vbi-x11.o \
	jwz/remote.o \
	common/channel-no-x11.o \
	common/vbi-data.o \
	$(OBJS-common-alsa) \
	$(OBJS-common-input) \
	$(OBJS-common-capture)

x11/mtt: \
	x11/mtt.o \
	x11/icons.o \
	x11/atoms.o \
	x11/vbi-x11.o \
	x11/vbi-gui.o \
	console/vbi-tty.o \
	console/fbtools.o \
	common/vbi-data.o \
	common/channel-no-x11.o \
	$(OBJS-common-capture)

x11/v4lctl: \
	x11/v4lctl.o \
	common/channel-no-x11.o \
	$(OBJS-common-capture)

ifeq ($(FOUND_X11),yes)
x11/v4lctl: \
	x11/atoms.o \
	x11/xv.o
endif

x11/rootv: \
	x11/rootv.o \
	x11/atoms.o \
	common/parseconfig.o

x11/pia: \
	x11/pia.o \
	x11/blit.o \
	libng/libng.a

x11/xawtv-remote: x11/xawtv-remote.o
x11/propwatch:    x11/propwatch.o

# libraries to link
x11/xawtv        : LDLIBS  += \
	$(THREAD_LIBS) $(CURSES_LIBS) $(LIRC_LIBS) $(ALSA_LIBS) \
	$(ATHENA_LIBS) $(VBI_LIBS) $(GL_LIBS) -ljpeg -lm -ldl -lfontconfig
x11/motv         : LDLIBS  += \
	$(THREAD_LIBS) $(CURSES_LIBS) $(LIRC_LIBS) $(ALSA_LIBS) \
	$(MOTIF_LIBS) $(VBI_LIBS) $(GL_LIBS) -ljpeg -lm -ldl
x11/mtt          : LDLIBS  += $(THREAD_LIBS) $(MOTIF_LIBS) $(VBI_LIBS) -ljpeg -ldl
x11/v4lctl       : LDLIBS  += $(THREAD_LIBS) $(ATHENA_LIBS) -ljpeg -lm -ldl
x11/pia          : LDLIBS  += $(ATHENA_LIBS) $(GL_LIBS) -ljpeg -lm -ldl
x11/rootv        : LDLIBS  += $(ATHENA_LIBS)
x11/xawtv-remote : LDLIBS  += $(ATHENA_LIBS)
x11/propwatch    : LDLIBS  += $(ATHENA_LIBS)

# linker flags
x11/xawtv        : LDFLAGS := $(DLFLAGS)
x11/motv         : LDFLAGS := $(DLFLAGS)
x11/mtt          : LDFLAGS := $(DLFLAGS)
x11/v4lctl       : LDFLAGS := $(DLFLAGS)
x11/pia          : LDFLAGS := $(DLFLAGS)

# compile flags
x11/complete-xaw.o   : CFLAGS += -DATHENA=1
x11/complete-motif.o : CFLAGS += -DMOTIF=1


# i18n
LANGUAGES := de_DE.UTF-8 fr_FR.UTF-8 it_IT.UTF-8
MOTV-app  := $(patsubst %,x11/MoTV.%.ad,$(LANGUAGES))


# local targets
x11/complete-xaw.o: x11/complete.c
	@$(echo_compile_c)
	@$(compile_c)
	@$(fixup_deps)

x11/complete-motif.o: x11/complete.c
	@$(echo_compile_c)
	@$(compile_c)
	@$(fixup_deps)


# global targets
all:: $(TARGETS-x11)
ifeq ($(FOUND_MOTIF),yes)
all:: $(MOTV-app)
endif

ifeq ($(FOUND_X11),yes)
install::
	$(INSTALL_PROGRAM) $(STRIP_FLAG) $(TARGETS-x11) $(bindir)
	$(INSTALL_DIR) $(resdir)/app-defaults
	$(INSTALL_DATA) $(srcdir)/x11/Xawtv.ad $(resdir)/app-defaults/Xawtv
endif
ifeq ($(FOUND_MOTIF),yes)
install:: $(patsubst %,install-motv-%,$(LANGUAGES))
	$(INSTALL_DATA) $(srcdir)/x11/mtt.ad $(resdir)/app-defaults/mtt
	$(INSTALL_DATA) x11/MoTV.ad $(resdir)/app-defaults/MoTV
endif

distclean::
	rm -f $(TARGETS-x11)
	rm -f $(MOTV-app) x11/MoTV.ad x11/MoTV.h x11/Xawtv.h x11/mtt.h

# special dependences / rules
x11/xawtv.o: x11/Xawtv.h
x11/motv.o: x11/MoTV.h
x11/mtt.o: x11/mtt.h

x11/MoTV.ad: $(srcdir)/x11/MoTV-default $(srcdir)/x11/MoTV-fixed
	cat $(srcdir)/x11/MoTV-default $(srcdir)/x11/MoTV-fixed > x11/MoTV.ad

x11/MoTV.%.ad: x11/MoTV-%
	cat $< $(srcdir)/x11/MoTV-fixed > $@

install-motv-%:
	$(INSTALL_DIR) $(resdir)/$*/app-defaults
	$(INSTALL_DATA) x11/MoTV.$*.ad $(resdir)/$*/app-defaults/MoTV
