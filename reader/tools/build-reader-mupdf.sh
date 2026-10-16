#!/usr/bin/env bash
# Build cached MuPDF with EPUB in a fresh private tree; no dependency downloads.
# Target mode runs inside the existing verified Ferrari SDK environment.
set -euo pipefail
source_dir=${1:?Cached MuPDF 1.28.3 source directory}
output_dir=${2:?Fresh absolute task-private directory}
kind=${3:?host or target}
[[ $output_dir == /* && ! -e $output_dir ]]
[[ -f $source_dir/include/mupdf/fitz/version.h ]]
grep -q '"1.28.3"' "$source_dir/include/mupdf/fitz/version.h"
[[ -f $source_dir/thirdparty/harfbuzz/src/hb.h && -f $source_dir/thirdparty/gumbo-parser/src/gumbo.h ]]
extra='-DFZ_ENABLE_CBZ=0 -DFZ_ENABLE_IMG=0 -DFZ_ENABLE_PNM=0 -DFZ_ENABLE_PAM=0 -DFZ_ENABLE_OCR_OUTPUT=0 -DFZ_ENABLE_ODT_OUTPUT=0 -ffunction-sections -fdata-sections'
case $kind in
 target)
  [[ ${INKBOUND_FERRARI_SDK_VERIFIED:-} == 1 ]]
  [[ $($CC -dumpmachine) == aarch64-remarkable-linux ]]
  ;;
 host) CC=cc; CXX=c++; AR=ar; RANLIB=ranlib ;;
 *) exit 2 ;;
esac
mkdir -p "$output_dir"
cp -a "$source_dir" "$output_dir/source"
make -C "$output_dir/source" -j4 libs OUT="$output_dir/lib" build=release \
 CC="$CC" CXX="$CXX" AR="$AR" RANLIB="$RANLIB" \
 USE_SYSTEM_LIBS=no HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no \
 mujs=no html=yes xps=no svg=no extract=no brotli=no \
 tesseract=no barcode=no archive=no threading=no \
 tofu=yes tofu_cjk=yes XCFLAGS="$extra"
