#!/usr/bin/env bash
# Live pad test: one rendered kabus22 scenario with vpad.py feeding a virtual Xbox 360 pad in parallel.
#   run-live.sh KABUS22 GAME_ROOT WINDOWS_SAVE_DIR OUT_DIR CASE [CONTROLLER_INI|-]
# CASE names live/CASE.k22s + live/CASE.vpad. "-" (default) = no controller.ini (original mode).
# Private XDG tree per run (saves imported fresh); logs: game.log (scenario), stderr.log, vpad.log, ff.log (rumble).
set -uo pipefail
k22=$1 game=$2 saves=$3 out=$4 case=$5 ini=${6:--}
here=$(cd "$(dirname "$0")" && pwd)
root=$out/$case-$( [ "$ini" = - ] && echo original || basename "$ini" .ini )
rm -rf "$root" && mkdir -p "$root"/{data,config/kabus22,state,runtime} && chmod 700 "$root/runtime"
export XDG_DATA_HOME=$root/data XDG_CONFIG_HOME=$root/config XDG_STATE_HOME=$root/state XDG_RUNTIME_DIR=$root/runtime
export SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=pipewire PIPEWIRE_RUNTIME_DIR=/run/user/1000 DISPLAY=${DISPLAY:-:0}
"$k22" --import-saves "$saves" --data "$game" >/dev/null || exit 1
# SDL drops pad events while its window is unfocused; on a desktop in use the new window may not get focus.
export SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=${SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS:-1}
[ "$ini" = - ] || cp "$ini" "$root/config/kabus22/controller.ini"
: >"$root/game.log"; : >"$root/ff.log"
python3 "$here/vpad.py" --sync "$root/game.log" --ff-log "$root/ff.log" "$here/live/$case.vpad" >"$root/vpad.log" 2>&1 &
vpad=$!
trap 'kill $vpad 2>/dev/null; wait $vpad 2>/dev/null' EXIT
sleep 1 # device node published before SDL enumerates
timeout -k 10 900 "$k22" --data "$game" --renderer opengl --window-mode windowed --resolution 1280x720 \
  --scenario "$here/live/$case.k22s" --scenario-out "$root" >"$root/game.log" 2>"$root/stderr.log"
code=$?
grep -E '^scenario: [^ ]+\.k22s ' "$root/game.log" || echo "scenario: $case no summary (exit $code)"
grep -E ' FAIL ' "$root/game.log"
echo "ff events: $(grep -c ' ff ' "$root/ff.log") ($root)"
exit $code
