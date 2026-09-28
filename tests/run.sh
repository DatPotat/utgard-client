#!/bin/sh
# Portable tests of the client's core: parsers, the AmneziaWG service config,
# the switch order, the profile format, the sing-box config, the PAC logic.
# Built for the host, with AddressSanitizer and UBSan when the compiler can
# link them (gcc from mingw-w64 cannot: then the tests run without them).
#
# Usage: sh tests/run.sh            (from the repository root or from tests/)
#        CC=clang sh tests/run.sh   (any host C compiler; default clang, then gcc)
#        SINGBOX=/path/to/sing-box sh tests/run.sh
#            also runs "sing-box check" on the generated config; needs a
#            sing-box 1.14.1 binary for the host.
set -eu
cd "$(dirname "$0")"
if [ -z "${CC:-}" ]; then
    if command -v clang >/dev/null 2>&1; then CC=clang; else CC=gcc; fi
fi
BASE="-std=c11 -g -O1 -Wall -Wextra"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
INC="-I../src -I../src/core -I../src/win -I../vendor/parson -I../vendor/puff -I."
LINK="../src/core/link.c ../vendor/parson/parson.c ../vendor/puff/puff.c"
PAC="../src/core/pacblob.c ../src/core/pacguard.c ../src/core/pacloop.c ../src/core/paclogic.c ../src/core/pacrecord.c ../src/core/pacudp.c"
mkdir -p out/list
fail=0

printf 'int main(void){return 0;}\n' > out/probe.c
if ! $CC $BASE $SAN -o out/probe out/probe.c >/dev/null 2>&1 || ! ./out/probe >/dev/null 2>&1; then
    SAN=""
    echo "note: $CC cannot link AddressSanitizer/UBSan here; running without them"
fi
FLAGS="$BASE $SAN"

run() {
    name=$1; shift
    if ! $CC $FLAGS $INC -o "out/$name" "$@"; then echo "$name: BUILD FAILED"; fail=1; return; fi
    if ! "./out/$name"; then fail=1; fi
}

run test_link      test_link.c $LINK
run test_awgconf   test_awgconf.c ../src/core/awgconf.c $LINK
run test_vpnswitch test_vpnswitch.c ../src/core/vpnswitch.c
run test_profiles  test_profiles.c ../src/core/profiles.c $LINK
run test_genconf   test_genconf.c ../src/core/genconf.c ../src/core/defconfig.c $LINK
run test_pac       test_pac.c $PAC
run test_lists     test_lists.c ../src/core/lists.c ../vendor/parson/parson.c
run test_update    test_update.c ../src/core/update.c
run fuzz_link      fuzz_link.c $LINK

if [ -n "${SINGBOX:-}" ] && [ -f out/genconf_awg.json ]; then
    printf '{"version":3,"rules":[{"domain_suffix":["invalid.placeholder.local"]}]}' > out/list/general.json
    (cd out && "$SINGBOX" rule-set compile --output list/general.srs list/general.json >/dev/null &&
        "$SINGBOX" check --disable-color -c genconf_awg.json &&
        "$SINGBOX" check --disable-color -c genconf_pac.json) && echo "sing-box check: ok" || { echo "sing-box check: FAILED"; fail=1; }
fi

[ $fail -eq 0 ] && echo "ALL TESTS PASSED" || { echo "SOME TESTS FAILED"; exit 1; }
