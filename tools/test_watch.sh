#!/usr/bin/env bash
# Copyright (c) 2026 Martial Systems LLC. All rights reserved.
#
# Rebuild the tree and run a ctest label tier whenever sources change (macOS / Linux).
# Usage: tools/test_watch.sh [--once] [--label fast|render|slow] [--preset NAME] [--filter REGEX] [--jobs N]
#   --once     run one build+test cycle and exit (no watching)
#   --label    ctest label to run after each build (default: fast)
#   --preset   CMake preset (default: linux-dev on Linux, mac-dev on macOS)
#   --filter   extra ctest -R regex
#   --jobs     cmake/ctest parallelism (default: 2)
# Env: JIDAI_CMAKE_ARGS = extra arguments for the first configure
# Prefers watchexec, then entr, then a 2 s poll (no extra package required).
# Run from anywhere; the script locates the repository root. CMakePresets live in jidai-rack/.
set -uo pipefail
cd "$(dirname "$0")/.."
ROOT=$(pwd)

ONCE=0; LABEL=fast; FILTER=""; JOBS=2
case "$(uname -s)" in Darwin) PRESET=mac-dev ;; *) PRESET=linux-dev ;; esac
while [ $# -gt 0 ]; do
  case "$1" in
    --once) ONCE=1 ;;
    --label) LABEL="$2"; shift ;;
    --preset) PRESET="$2"; shift ;;
    --filter) FILTER="$2"; shift ;;
    --jobs) JOBS="$2"; shift ;;
    -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
  shift
done
BUILD="$ROOT/build/$PRESET"

cycle() {
  if [ ! -f "$BUILD/build.ninja" ]; then
    # shellcheck disable=SC2086
    cmake -S "$ROOT/jidai-rack" --preset "$PRESET" ${JIDAI_CMAKE_ARGS:-} || return 1
  fi
  local t0 t1; t0=$(date +%s)
  local targets=(JidaiCommonTests JidaiDspTests JidaiHeaderHygiene JidaiCableEditTests JidaiRackTests JidaiPresetTests OrigamiTests)
  case "$LABEL" in
    render) targets+=(OrigamiPluginTests) ;;
    slow) targets+=(JidaiRackProbe OrigamiPluginTests) ;;
  esac
  cmake --build "$BUILD" --parallel "$JOBS" --target "${targets[@]}" || { echo "BUILD FAILED"; return 1; }
  t1=$(date +%s)
  local args=(--test-dir "$BUILD" -L "$LABEL" --parallel "$JOBS" --output-on-failure)
  [ -n "$FILTER" ] && args+=(-R "$FILTER")
  ctest "${args[@]}"; local rc=$?
  echo "== build $((t1 - t0)) s, tests $(( $(date +%s) - t1 )) s, ctest exit $rc ($(date +%H:%M:%S))"
  return $rc
}

if [ "${JIDAI_WATCH_CYCLE:-0}" = 1 ]; then cycle; exit $?; fi
if [ "$ONCE" = 1 ]; then cycle; exit $?; fi

export JIDAI_WATCH_CYCLE=1 JIDAI_CMAKE_ARGS
SELF="$ROOT/tools/test_watch.sh"
SELF_ARGS=(--label "$LABEL" --preset "$PRESET" --jobs "$JOBS")
[ -n "$FILTER" ] && SELF_ARGS+=(--filter "$FILTER")

WATCH_DIRS=(jidai-common jidai-rack cmake)
if command -v watchexec >/dev/null 2>&1; then
  echo "watching ${WATCH_DIRS[*]} with watchexec (Ctrl-C to stop)"
  cycle || true
  exec watchexec --watch jidai-common --watch jidai-rack --watch cmake \
    --exts h,hh,hpp,hxx,c,cc,cpp,cxx,cmake,txt,json,xml,svg --debounce 300ms --clear -n -p -- \
    "$SELF" "${SELF_ARGS[@]}"
elif command -v entr >/dev/null 2>&1; then
  echo "watching ${WATCH_DIRS[*]} with entr (Ctrl-C to stop; new files need a restart)"
  cycle || true
  while true; do
    find jidai-common jidai-rack cmake -type f \( -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' \
      -o -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.txt' -o -name '*.cmake' \
      -o -name '*.json' -o -name '*.xml' -o -name '*.svg' \) \
      | entr -d -c -s "$SELF ${SELF_ARGS[*]}" || true
  done
else
  echo "watchexec/entr not found; polling every 2 s (Ctrl-C to stop)."
  cycle || true
  stamp() {
    find jidai-common jidai-rack cmake -type f \( -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' \
      -o -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.txt' -o -name '*.cmake' \
      -o -name '*.json' -o -name '*.xml' -o -name '*.svg' \) -printf '%T@ %p\n' 2>/dev/null | sort
  }
  last=$(stamp)
  while true; do
    sleep 2
    now=$(stamp)
    if [ "$now" != "$last" ]; then
      last=$now
      cycle || true
    fi
  done
fi
