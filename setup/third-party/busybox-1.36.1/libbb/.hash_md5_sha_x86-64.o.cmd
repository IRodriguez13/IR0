cmd_libbb/hash_md5_sha_x86-64.o := /usr/bin/x86_64-linux-musl-gcc -Wp,-MD,libbb/.hash_md5_sha_x86-64.o.d  -std=gnu99 -Iinclude -Ilibbb  -include include/autoconf.h -D_GNU_SOURCE -DNDEBUG  -DBB_VER='"1.36.1"' -fno-pie       -c -o libbb/hash_md5_sha_x86-64.o libbb/hash_md5_sha_x86-64.S

deps_libbb/hash_md5_sha_x86-64.o := \
  libbb/hash_md5_sha_x86-64.S \
    $(wildcard include/config/sha1/small.h) \

libbb/hash_md5_sha_x86-64.o: $(deps_libbb/hash_md5_sha_x86-64.o)

$(deps_libbb/hash_md5_sha_x86-64.o):
