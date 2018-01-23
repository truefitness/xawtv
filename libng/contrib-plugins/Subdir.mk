
# targets to build
TARGETS-contrib-plugins := \
	libng/contrib-plugins/flt-smooth.so \
	libng/contrib-plugins/bilinear.so \
	libng/contrib-plugins/cubic.so \
	libng/contrib-plugins/linear-blend.so \
	libng/contrib-plugins/linedoubler.so

ifeq ($(FOUND_ALSA),yes)
#TARGETS-contrib-plugins += \
#	libng/contrib-plugins/snd-alsa.so
endif

# alsa is c++ and thus we should call g++ for linking ...
libng/contrib-plugins/snd-alsa.so : CC     := $(CXX)
libng/contrib-plugins/snd-alsa.so : LDLIBS := $(ALSA_LIBS)

# linear-blend has mmx support ...
ifeq ($(USE_MMX),yes)
libng/contrib-plugins/linear-blend.so : CFLAGS += -DMMX=1
endif

# global targets
all:: $(TARGETS-contrib-plugins)

install::
	$(INSTALL_DIR) $(libdir)
	$(INSTALL_PROGRAM) $(STRIP_FLAG) $(TARGETS-contrib-plugins) $(libdir)

clean::
	rm -f $(TARGETS-contrib-plugins)

libng/contrib-plugins/flt-smooth.so:   libng/contrib-plugins/flt-smooth.o
libng/contrib-plugins/snd-alsa.so:     libng/contrib-plugins/snd-alsa.o
libng/contrib-plugins/bilinear.so:     libng/contrib-plugins/bilinear.o
libng/contrib-pluginsa/cubic.so:       libng/contrib-plugins/cubic.o
libng/contrib-plugins/linear-blend.so: libng/contrib-plugins/linear-blend.o
libng/contrib-plugins/linedoubler.so:  libng/contrib-plugins/linedoubler.o
