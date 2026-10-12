#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Synth-only host suite. Run after ./build.sh (build/melodee-synth.fwsc).
# SOUND=0 skips DSP; SOUND=only runs DSP alone. PARALLEL=n controls jobs.
# Single-track integration exercises real UI/MIDI/preset/persistence/editor/capture code against NOR/HAL models.
# UI lint renders every visible page and palette plus every FM6 algorithm. Retained sound and transport tests
# cover the synths, full FX, USB PCM16/24, drift, endpoint lifecycle, clock fallback and installation protocol.
# Sequencer/drum/project-runtime fixtures remaining in tests/ belong to the main branch and are not run here.
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
cd "$(dirname "$0")/.."
# Pillow's Raqm (text spacing, renders): macOS strips DYLD_* on the way into /bin/sh (build.sh)
[ "$(uname -s)" = Darwin ] && export DYLD_FALLBACK_LIBRARY_PATH="${DYLD_FALLBACK_LIBRARY_PATH:-/opt/homebrew/lib:/usr/local/lib:/usr/lib}"
OUT=build/host
LOG=$OUT/log
CC="${CC:-cc} -O1 -ffp-contract=off -Wall -Wno-unused-function"
FW="-w -Ibuild/gen -Ifirmware/src"
P5=build/prophet5/Prophet-5+10-Factory-Programs-ReadMe1.03
SOUND=${SOUND:-1}
PARALLEL=${PARALLEL:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}
case $SOUND in 0 | 1 | only) ;; *) echo "SOUND=0, 1 or only"; exit 2 ;; esac
[ "$PARALLEL" -ge 1 ] 2>/dev/null || { echo "PARALLEL=n, n >= 1"; exit 2; }
python3 tools/gen_scales.py --check || exit 1
[ -f build/melodee-synth.fwsc ] || { echo "run ./build.sh first"; exit 1; }
TABLES=0; [ -f build/gen/melodee_tables.h ] && TABLES=1
PIL=0; python3 -c "import PIL" 2>/dev/null && PIL=1
mkdir -p "$OUT" "$LOG"
rm -f "$LOG"/*
start=$(date +%s)

# ------------------------------------------------------------------- jobs ---
# job FN ARGS...: FN ARGS in the background once one of the PARALLEL slots is free (fd 3, a FIFO holding a line per
#   free slot), the output in $LOG/<n>.log; a failure is a line in $LOG/<n>.fail, $LOG/<n>.done marks the end.
# In a job: step TITLE CMD... (the title, then CMD; a failure is noted and the job goes on),
#   build OUT SRC FLAGS... (tests/SRC.c -> $OUT/OUT), fail WHAT.
# t TITLE OUT[:SRC] FLAGS... [-- CMD...]: the job building tests/SRC.c (SRC: OUT) and running CMD ($OUT/OUT).
n=0 shown=0 pids=
mkfifo "$LOG/slots" && exec 3<>"$LOG/slots" && rm -f "$LOG/slots" || exit 1
i=0; while [ $i -lt "$PARALLEL" ]; do echo; i=$((i + 1)); done >&3
# jobs ignore SIGINT (asynchronous lists): stop them and everything under them (cc -> clang -cc1, regress -> its renders)
tree() { echo "$1"; for c in $(pgrep -P "$1"); do tree "$c"; done; }
stop() { kill -TERM $(for p in $pids; do tree "$p"; done) 2>/dev/null; }
trap 'stop; exit 130' INT
trap 'stop; exit 143' TERM

fail() { echo "$1" >> "$LOG/$n.fail"; }
step() { echo "== $1"; _step=$1; shift; "$@" || fail "$_step"; }
build() {
    _out=$1 _src=$2; shift 2
    $CC "$@" -o "$OUT/$_out" "tests/$_src.c" -lm || { fail "compile tests/$_src.c ($_out)"; return 1; }
}
_t() {
    _title=$1 _out=${2%%:*} _src=${2#*:} _flags=; shift 2
    while [ $# -gt 0 ] && [ "$1" != -- ]; do _flags="$_flags $1"; shift; done
    [ $# -gt 0 ] && shift
    [ $# -gt 0 ] || set -- "$OUT/$_out"
    echo "== $_title"
    build "$_out" "$_src" $_flags || return
    "$@" || fail "$_title"
}
flush() {                                         # the logs of the jobs ended, in order
    while [ $shown -lt $n ] && [ -f "$LOG/$((shown + 1)).done" ]; do
        shown=$((shown + 1))
        cat "$LOG/$shown.log"
        [ -f "$LOG/$shown.fail" ] && cat "$LOG/$shown.fail" >> "$LOG/failed"
    done
}
job() {
    read -r slot <&3
    flush
    n=$((n + 1))
    ( "$@" 3>&- > "$LOG/$n.log" 2>&1; : > "$LOG/$n.done"; echo >&3 ) &
    pids="$pids $!"
}
t() { job _t "$@"; }
run() { job step "$@"; }
note() { job echo "$@"; }
core() { [ "$SOUND" != only ]; }
sound() { [ "$SOUND" != 0 ]; }

# Synth branch: single-track integration plus retained synth/effect/USB/loader gates.
# Sequencer, drum and project runtime fixtures from the main branch are not applicable.
synth_integration() {
    build synth_test synth_test -O1 $FW || return
    step "single synth: UI, MIDI, native persistence, master capture and editor" "$OUT/synth_test"
    if command -v node >/dev/null 2>&1; then
        step "synth web editor with the real INFO and state record" node web/test_synth.mjs
        step "archive validation and restore" node web/test_backup.mjs
    else
        fail "web checks require Node.js"
    fi
}
synth_ui() {
    build synth_ui_render synth_ui_render -O1 $FW || return
    step "all synth UI pages, palettes and FM6 algorithms" "$OUT/synth_ui_render" build/ui_synth
    if [ "$PIL" = 1 ]; then
        step "HOME knob cards match the established design" python3 tools/ui_mockcmp.py build/ui_synth build/ui_synth_home_mock \
            --screens stage_p5_knobs --crop 0 22 240 66 --min 60
        step "retained UI beside the approved mockups" python3 tools/ui_mockcmp.py build/ui_synth build/ui_synth_mock \
            --screens browser env lfo edit_osc fx dly settings sheet_sound picker_wave sections_p5 map_p5 map_fm6 \
            scl_list mod picker_mod arp slicer fmeg scales
    else
        fail "UI design comparisons require Pillow"
    fi
}
if core; then
    job synth_integration
    t "flash storage A/B and torn writes" storage_test
    t "input scanning" input_test
    t "LED PWM" led_pwm_test
    t "TRS MIDI parser" midi_uart_test
    t "audio worker deadlines" audio_worker_test
    t "cache RAM startup and fallback" cache_ram_test
    t "system clock rollback" clock_test
    t "settings layout compatibility" settings_test -w -Ibuild/gen
    t "USB stereo routing, drift and recovery" usb_audio_test
    t "USB endpoint lifecycle" usb_audio_driver_test
    run "USB descriptors" python3 tests/usb_audio_desc_test.py
    run "resident audio relocation" python3 tests/audio_ram_test.py
    run "target budget decoder" python3 tests/target_budget_test.py
    run "installer simulated device" python3 tests/install_test.py
    t "M-UPGRADE" ota_test -DOWN_PKG=1 -- "$OUT/ota_test" build/melodee-synth.fwsc
    mkdir -p build/ui_synth/ppm
    job synth_ui
fi
if sound; then
    t "native fp32 numerical contracts" synth_fpu_test -O2 $FW
    t "CZ-1 native controls" cz1_knob_test -O2 $FW
    t "SID oscillator/register semantics" sid_test -O2 $FW
    t "FM6 algorithms and sound" fm6_test -O2 $FW
    t "FM6 modulation reference" fm6_ams_test -O2 $FW
    t "Prophet native DSP and wire format" prophet_test -O2 $FW
    t "speaker bass" speaker_test -O2 $FW
    t "modulation matrix and MIDI" mod_test -DMELODEE_LEGACY_EXTRAS=1 -O2 $FW
    t "spread and matrix sources" mod_spread_test -O2 $FW
    t "SLICER signal/timing/cost" slicer_test -O2 $FW
    t "FX layer signal/timing/stereo/cost" perform_test -O2 $FW
    t "reverb signal/stability/cost" reverb_test -O2 $FW
    t "delay signal/stability/cost" delay_test -O2 $FW
    t "dual-core sample equality" dual_core_test -O2 $FW -pthread
    run "target DSP budgets" python3 tests/target_budget.py build/melodee.dis tests/target_budget.txt
fi
# every slot back: every job ended
i=0; while [ $i -lt "$PARALLEL" ]; do read -r slot <&3; flush; i=$((i + 1)); done
wait
flush
exec 3>&-
secs=$(($(date +%s) - start))
case $SOUND in
    1) what="ALL HOST TESTS" ;;
    0) what="HOST TESTS (sound tests skipped: SOUND=0)" ;;
    only) what="SOUND TESTS (SOUND=only)" ;;
esac
if [ -s "$LOG/failed" ]; then
    echo "$what FAILED in $secs s:"
    sed 's/^/  /' "$LOG/failed"
    exit 1
fi
echo "$what PASSED in $secs s"
