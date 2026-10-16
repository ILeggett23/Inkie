#!/usr/bin/env bash
# Local build only. Run target mode inside configure-ferrari-sdk.sh.
set -euo pipefail
source_dir=${1:?MuPDF source directory}
output_dir=${2:?absolute output directory}
kind=${3:?target or host}
[[ $output_dir == /* && -f $source_dir/include/mupdf/fitz/version.h ]]
grep -q '"1.28.3"' "$source_dir/include/mupdf/fitz/version.h"
extra='-DFZ_ENABLE_CBZ=0 -DFZ_ENABLE_IMG=0 -DFZ_ENABLE_PNM=0 -DFZ_ENABLE_PAM=0 -DFZ_ENABLE_OCR_OUTPUT=0 -DFZ_ENABLE_ODT_OUTPUT=0 -ffunction-sections -fdata-sections'
case $kind in
 target)
  [[ ${INKBOUND_FERRARI_SDK_VERIFIED:-} == 1 ]]
  [[ $($CC -dumpmachine) == aarch64-remarkable-linux ]]
  ;;
 host) CC=cc; CXX=c++; AR=ar; RANLIB=ranlib ;;
 host-asan) CC=cc; CXX=c++; AR=ar; RANLIB=ranlib
   extra="$extra -fsanitize=address,undefined -fno-omit-frame-pointer -g" ;;
 *) exit 2 ;;
esac
make -B -C "$source_dir" -j4 libs OUT="$output_dir" build=release \
  CC="$CC" CXX="$CXX" AR="$AR" RANLIB="$RANLIB" \
  USE_SYSTEM_LIBS=no HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no \
  mujs=no html=no xps=no svg=no extract=no brotli=no \
  tesseract=no barcode=no archive=no threading=no \
  tofu=yes tofu_cjk=yes XCFLAGS="$extra"
