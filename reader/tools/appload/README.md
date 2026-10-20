# Inkie in AppLoad

The tablet's AppLoad entry lives in `/home/root/xovi/exthome/appload/inkbound/`: `external.manifest.json`, `icon.png` and
`launch.sh`. Only the first two change here. The folder name and `launch.sh` stay as they are: `launch.sh` starts the
staged app in `/home/root/inkbound-e2/E/app`.

- `external.manifest.json`: shows the app as **Inkie**.
- `icon.png`: 256x256 RGB. A fountain-pen nib standing on its tip, whose slit is the stem of a lowercase "i", with the
  wordmark's magenta drop of ink as its dot (docs/INKIE-DESIGN.md §5).

To regenerate the icon:

```
clang++ -std=c++20 -O2 -Isrc -Irecovery -isystem third_party src/ib/canvas.cpp src/core.cpp tools/appload_icon.cpp -lz -o appload-icon
./appload-icon tools/appload/icon.png
```

Installing these two files is a separate, approved step: back up the current manifest and icon, then replace them.
