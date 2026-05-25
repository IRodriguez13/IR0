cmd_libbb/perror_nomsg_and_die.o := /usr/bin/x86_64-linux-musl-gcc -Wp,-MD,libbb/.perror_nomsg_and_die.o.d  -std=gnu99 -Iinclude -Ilibbb  -include include/autoconf.h -D_GNU_SOURCE -DNDEBUG  -DBB_VER='"1.36.1"' -fno-pie    -DKBUILD_BASENAME='"perror_nomsg_and_die"'  -DKBUILD_MODNAME='"perror_nomsg_and_die"' -c -o libbb/perror_nomsg_and_die.o libbb/perror_nomsg_and_die.c

deps_libbb/perror_nomsg_and_die.o := \
  libbb/perror_nomsg_and_die.c \
  include/platform.h \
    $(wildcard include/config/werror.h) \
    $(wildcard include/config/big/endian.h) \
    $(wildcard include/config/little/endian.h) \
    $(wildcard include/config/nommu.h) \
  /usr/include/x86_64-linux-musl/limits.h \
  /usr/include/x86_64-linux-musl/features.h \
  /usr/include/x86_64-linux-musl/bits/alltypes.h \
  /usr/include/x86_64-linux-musl/bits/limits.h \
  /usr/include/x86_64-linux-musl/byteswap.h \
  /usr/include/x86_64-linux-musl/stdint.h \
  /usr/include/x86_64-linux-musl/bits/stdint.h \
  /usr/include/x86_64-linux-musl/endian.h \
  /usr/include/x86_64-linux-musl/stdbool.h \
  /usr/include/x86_64-linux-musl/unistd.h \
  /usr/include/x86_64-linux-musl/bits/posix.h \

libbb/perror_nomsg_and_die.o: $(deps_libbb/perror_nomsg_and_die.o)

$(deps_libbb/perror_nomsg_and_die.o):
