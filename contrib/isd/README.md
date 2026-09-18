# ISD packaging notes (kernel-first — no upstream patches)

When IR0 needs a missing **upstream** desktop client, add an ISD package under
`packages/<name>/` (fetch URL + sha256 + `build-xorg-autotools.sh` or ISD
recipe). **Do not** patch BusyBox, xload, xterm, etc. for ABI — fix the kernel.

This wave adds **`xload`** (Xorg app 1.2.1, unmodified sources) to
`profiles/desktop/packages.txt` via standard ISD metadata only.

Legacy note: if `contrib/isd/*.patch` exist on another machine, they are ISD-side
packaging/metadata — not IR0 kernel changes.
