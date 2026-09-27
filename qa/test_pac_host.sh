#!/bin/sh
set -eu
mkdir -p build/qa
clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Isrc/core qa/pac_host.c src/core/pacblob.c src/core/pacguard.c \
  src/core/paclogic.c src/core/pacrecord.c src/core/pacudp.c -o build/qa/pac-host
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 build/qa/pac-host
