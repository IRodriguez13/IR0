# IR0 Kernel - C++ Support

This directory contains C++ runtime and compatibility headers for kernel development.

## Structure

- `include/` - C++ compatibility headers
- `runtime/` - C++ runtime implementation (new/delete, etc.)

## Files

- `include/compat.h` - C++/Rust interoperability header
- `runtime/compat.cpp` - C++ runtime for kernel

## Usage

Include in C++ kernel components:

```cpp
#include <cpp/include/compat.h>
```

## Building

C++ components are compiled with:

```bash
make unibuild -cpp kernel/component.cpp
```

See `CONTRIBUTING.md` for full development guide.
