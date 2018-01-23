OBJS-libng := \
	libng/grab-ng.o \
	libng/devices.o \
	libng/writefile.o \
	libng/color_common.o \
	libng/color_packed.o \
	libng/color_lut.o \
	libng/color_yuv2rgb.o \
	libng/convert.o \
	common/get_media_devices.o

libng/libng.a: $(OBJS-libng)
	@$(echo_ar_lib)
	@$(ar_lib)

clean::
	rm -f libng.a
