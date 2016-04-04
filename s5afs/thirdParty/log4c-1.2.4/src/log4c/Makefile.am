INCLUDES = \
	-I$(top_srcdir)/src \
	-DLOG4C_RCPATH="\"$(sysconfdir)\""

lib_LTLIBRARIES  = liblog4c.la

DEF_FILE = liblog4c.def
if WIN32
def_ldflags = -Wl,--output-def,$(DEF_FILE)
endif

liblog4c_la_SOURCES = \
	rc.c \
	init.c \
	appender_type_stream.c \
	appender_type_stream2.c \
	appender_type_syslog.c \
	layout_type_basic.c \
	layout_type_dated.c \
	layout_type_dated_local.c \
	layout_type_basic_r.c \
	layout_type_dated_r.c \
	layout_type_dated_local_r.c \
	version.c \
	logging_event.c \
	priority.c \
	appender.c \
	layout.c \
	category.c

if WITH_ROLLINGFILE
liblog4c_la_SOURCES += appender_type_rollingfile.c \
                          rollingpolicy.c rollingpolicy_type_sizewin.c
endif

if WITH_MMAP
liblog4c_la_SOURCES += appender_type_mmap.c
endif

liblog4c_la_LDFLAGS = -version-info @LT_VERSION@ $(LDFLAGS_NOUNDEFINED) \
	$(def_ldflags)
liblog4c_la_LIBADD  = ../sd/liblog4c_sd.la

pkginclude_HEADERS = \
	config-win32.h \
	buffer.h \
	rc.h \
	init.h \
	defs.h \
	version.h \
	location_info.h \
	logging_event.h \
	priority.h \
	layout_type_basic.h \
	layout_type_dated.h \
	layout_type_dated_local.h \
	layout_type_basic_r.h \
	layout_type_dated_r.h \
	layout_type_dated_local_r.h \
	layout.h \
	appender_type_stream.h \
	appender_type_stream2.h \
	appender_type_syslog.h \
	appender_type_mmap.h \
	appender.h \
	category.h \
  appender_type_rollingfile.h \
  rollingpolicy.h \
  rollingpolicy_type_sizewin.h

if WIN32
install-exec-local:
	-$(MKDIR_P) $(DESTDIR)$(libdir)
	$(INSTALL) $(DEF_FILE) $(DESTDIR)$(libdir)

uninstall-local:
	rm -f $(DEF_FILE)
endif
