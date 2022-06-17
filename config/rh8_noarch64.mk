BUILDDIR:=rh8/noarch64

include config/base.mk
include config/with-debug.mk
include config/with-brutality.mk
include config/with-optimization.mk
include config/with-threads.mk

CPPFLAGS+=-DFD_HAS_INT128=0 -DFD_HAS_DOUBLE=1
