cmd_coreutils/test_ptr_hack.o := /usr/bin/x86_64-linux-musl-gcc -Wp,-MD,coreutils/.test_ptr_hack.o.d  -std=gnu99 -Iinclude -Ilibbb  -include include/autoconf.h -D_GNU_SOURCE -DNDEBUG  -DBB_VER='"1.36.1"' -fno-pie    -DKBUILD_BASENAME='"test_ptr_hack"'  -DKBUILD_MODNAME='"test_ptr_hack"' -c -o coreutils/test_ptr_hack.o coreutils/test_ptr_hack.c

deps_coreutils/test_ptr_hack.o := \
  coreutils/test_ptr_hack.c \

coreutils/test_ptr_hack.o: $(deps_coreutils/test_ptr_hack.o)

$(deps_coreutils/test_ptr_hack.o):
