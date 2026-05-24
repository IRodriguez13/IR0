# IR0 FASE52 — TinyCC search paths for /bin/tcc + /lib/tcc + /lib (musl crt).
EXTRA-DEFS += -DCONFIG_SYSROOT=\"/\"
EXTRA-DEFS += -DCONFIG_TCC_CRTPREFIX=\"/lib\"
EXTRA-DEFS += -DCONFIG_TCC_LIBPATHS=\"/lib:{B}\"
EXTRA-DEFS += -DCONFIG_TCC_SYSINCLUDEPATHS=\"{B}/include\"
