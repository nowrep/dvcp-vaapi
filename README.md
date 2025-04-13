# dvcp-vaapi
DaVinci Resolve VAAPI encoder plugin. Studio is required, free version doesn't support plugins.

## Install

Extract the release into directory *IOPlugins* in the Resolve install directory (default `/opt/resolve/IOPlugins`).

## Build

```sh
meson setup build
meson compile -C build
```
