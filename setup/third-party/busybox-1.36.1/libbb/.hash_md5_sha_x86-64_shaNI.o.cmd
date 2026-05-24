cmd_libbb/hash_md5_sha_x86-64_shaNI.o := /usr/bin/x86_64-linux-musl-gcc -Wp,-MD,libbb/.hash_md5_sha_x86-64_shaNI.o.d  -std=gnu99 -Iinclude -Ilibbb  -include include/autoconf.h -D_GNU_SOURCE -DNDEBUG  -DBB_VER='"1.36.1"' -fno-pie -DIR0_INTERACTIVE_GUI      -c -o libbb/hash_md5_sha_x86-64_shaNI.o libbb/hash_md5_sha_x86-64_shaNI.S

deps_libbb/hash_md5_sha_x86-64_shaNI.o := \
  libbb/hash_md5_sha_x86-64_shaNI.S \
    $(wildcard include/config/sha1/hwaccel.h) \

libbb/hash_md5_sha_x86-64_shaNI.o: $(deps_libbb/hash_md5_sha_x86-64_shaNI.o)

$(deps_libbb/hash_md5_sha_x86-64_shaNI.o):
