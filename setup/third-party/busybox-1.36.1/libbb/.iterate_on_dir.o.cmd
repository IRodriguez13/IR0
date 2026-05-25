cmd_libbb/iterate_on_dir.o := /usr/bin/x86_64-linux-musl-gcc -Wp,-MD,libbb/.iterate_on_dir.o.d  -std=gnu99 -Iinclude -Ilibbb  -include include/autoconf.h -D_GNU_SOURCE -DNDEBUG  -DBB_VER='"1.36.1"' -fno-pie    -DKBUILD_BASENAME='"iterate_on_dir"'  -DKBUILD_MODNAME='"iterate_on_dir"' -c -o libbb/iterate_on_dir.o libbb/iterate_on_dir.c

deps_libbb/iterate_on_dir.o := \
  libbb/iterate_on_dir.c \
  include/libbb.h \
    $(wildcard include/config/feature/shadowpasswds.h) \
    $(wildcard include/config/use/bb/shadow.h) \
    $(wildcard include/config/selinux.h) \
    $(wildcard include/config/feature/utmp.h) \
    $(wildcard include/config/locale/support.h) \
    $(wildcard include/config/use/bb/pwd/grp.h) \
    $(wildcard include/config/lfs.h) \
    $(wildcard include/config/feature/buffers/go/on/stack.h) \
    $(wildcard include/config/feature/buffers/go/in/bss.h) \
    $(wildcard include/config/extra/cflags.h) \
    $(wildcard include/config/variable/arch/pagesize.h) \
    $(wildcard include/config/feature/verbose.h) \
    $(wildcard include/config/feature/etc/services.h) \
    $(wildcard include/config/feature/ipv6.h) \
    $(wildcard include/config/feature/seamless/xz.h) \
    $(wildcard include/config/feature/seamless/lzma.h) \
    $(wildcard include/config/feature/seamless/bz2.h) \
    $(wildcard include/config/feature/seamless/gz.h) \
    $(wildcard include/config/feature/seamless/z.h) \
    $(wildcard include/config/float/duration.h) \
    $(wildcard include/config/feature/check/names.h) \
    $(wildcard include/config/feature/prefer/applets.h) \
    $(wildcard include/config/long/opts.h) \
    $(wildcard include/config/feature/pidfile.h) \
    $(wildcard include/config/feature/syslog.h) \
    $(wildcard include/config/feature/syslog/info.h) \
    $(wildcard include/config/warn/simple/msg.h) \
    $(wildcard include/config/feature/individual.h) \
    $(wildcard include/config/shell/ash.h) \
    $(wildcard include/config/shell/hush.h) \
    $(wildcard include/config/echo.h) \
    $(wildcard include/config/sleep.h) \
    $(wildcard include/config/printf.h) \
    $(wildcard include/config/test.h) \
    $(wildcard include/config/test1.h) \
    $(wildcard include/config/test2.h) \
    $(wildcard include/config/kill.h) \
    $(wildcard include/config/killall.h) \
    $(wildcard include/config/killall5.h) \
    $(wildcard include/config/chown.h) \
    $(wildcard include/config/ls.h) \
    $(wildcard include/config/xxx.h) \
    $(wildcard include/config/route.h) \
    $(wildcard include/config/feature/hwib.h) \
    $(wildcard include/config/desktop.h) \
    $(wildcard include/config/feature/crond/d.h) \
    $(wildcard include/config/feature/setpriv/capabilities.h) \
    $(wildcard include/config/run/init.h) \
    $(wildcard include/config/feature/securetty.h) \
    $(wildcard include/config/pam.h) \
    $(wildcard include/config/use/bb/crypt.h) \
    $(wildcard include/config/feature/adduser/to/group.h) \
    $(wildcard include/config/feature/del/user/from/group.h) \
    $(wildcard include/config/ioctl/hex2str/error.h) \
    $(wildcard include/config/feature/editing.h) \
    $(wildcard include/config/feature/editing/history.h) \
    $(wildcard include/config/feature/tab/completion.h) \
    $(wildcard include/config/feature/username/completion.h) \
    $(wildcard include/config/feature/editing/fancy/prompt.h) \
    $(wildcard include/config/feature/editing/savehistory.h) \
    $(wildcard include/config/feature/editing/vi.h) \
    $(wildcard include/config/feature/editing/save/on/exit.h) \
    $(wildcard include/config/pmap.h) \
    $(wildcard include/config/feature/show/threads.h) \
    $(wildcard include/config/feature/ps/additional/columns.h) \
    $(wildcard include/config/feature/topmem.h) \
    $(wildcard include/config/feature/top/smp/process.h) \
    $(wildcard include/config/pgrep.h) \
    $(wildcard include/config/pkill.h) \
    $(wildcard include/config/pidof.h) \
    $(wildcard include/config/sestatus.h) \
    $(wildcard include/config/unicode/support.h) \
    $(wildcard include/config/feature/mtab/support.h) \
    $(wildcard include/config/feature/clean/up.h) \
    $(wildcard include/config/feature/devfs.h) \
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
  /usr/include/x86_64-linux-musl/ctype.h \
  /usr/include/x86_64-linux-musl/dirent.h \
  /usr/include/x86_64-linux-musl/bits/dirent.h \
  /usr/include/x86_64-linux-musl/errno.h \
  /usr/include/x86_64-linux-musl/bits/errno.h \
  /usr/include/x86_64-linux-musl/fcntl.h \
  /usr/include/x86_64-linux-musl/bits/fcntl.h \
  /usr/include/x86_64-linux-musl/inttypes.h \
  /usr/include/x86_64-linux-musl/netdb.h \
  /usr/include/x86_64-linux-musl/netinet/in.h \
  /usr/include/x86_64-linux-musl/sys/socket.h \
  /usr/include/x86_64-linux-musl/bits/socket.h \
  /usr/include/x86_64-linux-musl/setjmp.h \
  /usr/include/x86_64-linux-musl/bits/setjmp.h \
  /usr/include/x86_64-linux-musl/signal.h \
  /usr/include/x86_64-linux-musl/bits/signal.h \
  /usr/include/x86_64-linux-musl/paths.h \
  /usr/include/x86_64-linux-musl/stdio.h \
  /usr/include/x86_64-linux-musl/stdlib.h \
  /usr/include/x86_64-linux-musl/alloca.h \
  /usr/include/x86_64-linux-musl/stdarg.h \
  /usr/include/x86_64-linux-musl/stddef.h \
  /usr/include/x86_64-linux-musl/string.h \
  /usr/include/x86_64-linux-musl/strings.h \
  /usr/include/x86_64-linux-musl/libgen.h \
  /usr/include/x86_64-linux-musl/poll.h \
  /usr/include/x86_64-linux-musl/bits/poll.h \
  /usr/include/x86_64-linux-musl/sys/ioctl.h \
  /usr/include/x86_64-linux-musl/bits/ioctl.h \
  /usr/include/x86_64-linux-musl/bits/ioctl_fix.h \
  /usr/include/x86_64-linux-musl/sys/mman.h \
  /usr/include/x86_64-linux-musl/bits/mman.h \
  /usr/include/x86_64-linux-musl/sys/resource.h \
  /usr/include/x86_64-linux-musl/sys/time.h \
  /usr/include/x86_64-linux-musl/sys/select.h \
  /usr/include/x86_64-linux-musl/bits/resource.h \
  /usr/include/x86_64-linux-musl/sys/stat.h \
  /usr/include/x86_64-linux-musl/bits/stat.h \
  /usr/include/x86_64-linux-musl/sys/types.h \
  /usr/include/x86_64-linux-musl/sys/sysmacros.h \
  /usr/include/x86_64-linux-musl/sys/wait.h \
  /usr/include/x86_64-linux-musl/termios.h \
  /usr/include/x86_64-linux-musl/bits/termios.h \
  /usr/include/x86_64-linux-musl/time.h \
  /usr/include/x86_64-linux-musl/sys/param.h \
  /usr/include/x86_64-linux-musl/pwd.h \
  /usr/include/x86_64-linux-musl/grp.h \
  /usr/include/x86_64-linux-musl/mntent.h \
  /usr/include/x86_64-linux-musl/sys/statfs.h \
  /usr/include/x86_64-linux-musl/sys/statvfs.h \
  /usr/include/x86_64-linux-musl/bits/statfs.h \
  /usr/include/x86_64-linux-musl/arpa/inet.h \
  include/xatonum.h \

libbb/iterate_on_dir.o: $(deps_libbb/iterate_on_dir.o)

$(deps_libbb/iterate_on_dir.o):
