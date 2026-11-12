#!/usr/bin/env bash
# run_tests.sh (worker "tests", 2026-11-09; docs/testing.md "CTest"): Linux / macOS (and Git Bash) twin of
# run_tests.ps1: builds hotd + hotd_unit, runs the CTest suite, prints a summary table. Exit 0 when nothing failed.
#
#   tools/run_tests.sh [--build <dir>] [--config Release] [--no-build] [--layer all|unit|module|translation]
#                      [--only <ctest -R regex>] [--jobs 4] [--root <folder with private/>]
#
# The game files: HOTD_TEST_ARCADE / HOTD_TEST_PC / HOTD_TEST_ES / HOTD_TEST_PC_HARNESS, else <root>/private's
# hotd.zip, pcharness/game, game_copies/pc_spanish/extracted/installed (module tests without them are skipped).
set -u
here="$(cd "$(dirname "$0")/.." && pwd)"
build="$here/build"; config=Release; nobuild=0; layer=all; only=""; jobs=4; root="$here"
while [ $# -gt 0 ]; do
    case "$1" in
        --build) build="$2"; shift ;;
        --config) config="$2"; shift ;;
        --no-build) nobuild=1 ;;
        --layer) layer="$2"; shift ;;
        --only) only="$2"; shift ;;
        --jobs) jobs="$2"; shift ;;
        --root) root="$2"; shift ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
    shift
done
pick() { [ -n "${!1:-}" ] && return; [ -e "$2" ] && export "$1=$2"; }
pick HOTD_TEST_ARCADE "$root/private/hotd.zip"
pick HOTD_TEST_PC "$root/private/pcharness/game"
pick HOTD_TEST_ES "$root/private/game_copies/pc_spanish/extracted/installed"
pick HOTD_TEST_PC_HARNESS "$root/private/pcharness/game"
for v in HOTD_TEST_ARCADE HOTD_TEST_PC HOTD_TEST_ES HOTD_TEST_PC_HARNESS; do
    printf '%-22s %s\n' "$v" "${!v:-(not set: its module tests are skipped)}"
done
t0=$(date +%s)
if [ $nobuild = 0 ]; then
    targets="hotd hotd_unit"
    [ "$layer" = unit ] || [ "$layer" = translation ] && targets="hotd_unit"
    cmake --build "$build" --config "$config" --parallel --target $targets || { echo "BUILD FAILED"; exit 1; }
fi
args=(--test-dir "$build" -C "$config" -j "$jobs" --output-on-failure)
if [ "$layer" != all ]; then args+=(-L "^$layer$"); else args+=(-LE "^translation$"); fi   # (all: unit + module)
[ -n "$only" ] && args+=(-R "$only")
log="$build/ctest_last.txt"
ctest "${args[@]}" 2>&1 | tee "$log"
code=${PIPESTATUS[0]}
echo
printf '%-34s %-8s %s\n' TEST RESULT SECONDS
grep -E '^ *[0-9]+/[0-9]+ Test +#[0-9]+: ' "$log" | sed -E 's/^ *[0-9]+\/[0-9]+ Test +#[0-9]+: ([^ ]+) [.* ]+(\*\*\*)?([A-Za-z]+)[^0-9]*([0-9.]+) sec.*/\1 \3 \4/' |
    sort | while read -r n r s; do printf '%-34s %-8s %s\n' "$n" "$r" "$s"; done
for l in unit module translation; do
    p=$(grep -cE "Test +#[0-9]+: $l\..* Passed" "$log"); f=$(grep -cE "Test +#[0-9]+: $l\..*(Failed|Timeout|Exception)" "$log")
    s=$(grep -cE "Test +#[0-9]+: $l\..*Skipped" "$log")
    [ $((p + f + s)) -gt 0 ] && printf '%-12s %3d passed, %3d failed, %3d skipped\n' "$l" "$p" "$f" "$s"
done
unit="$build/$config/hotd_unit"; [ -x "$unit" ] || unit="$build/$config/hotd_unit.exe"; [ -x "$unit" ] || unit="$build/hotd_unit"
if [ -x "$unit" ] && { [ "$layer" = all ] || [ "$layer" = unit ]; }; then
    echo "unit checks: $("$unit" util launcher lang mods files vr net rollback typing weapons survival evdev | tail -1)"
fi
echo "total: $(( $(date +%s) - t0 )) s"
exit $code
