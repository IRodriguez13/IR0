# ISD sync patch (this agent wave)

The Cursor cloud token cannot push to `IRodriguez13/ISD`. Apply on a checkout of ISD:

```bash
cd ../ISD
git am ../IR0/contrib/isd/*.patch
git push -u origin cursor/usmang-desktop-3bc2
```

Or cherry-pick the local commit from a machine that has write access:

```bash
# if this VM's /home/ubuntu/src/ISD is reachable
git -C ../ISD log -1 --oneline
```

Contents: ISD release stamp, `ir0-status` multi-command, package origins,
`USERLAND_BASE=busybox`, desktop wallpaper XBM, unmodified `xload` package,
DESKTOP_ABI.md.
