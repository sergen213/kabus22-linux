# level_sweep CTest driver: cmake -DK22=kabus22 -DGAME=GAME_ROOT -DSAVES=WINDOWS_GAME_DIR -DOUT=DIR [-DONLY=id;id] [-DARGS=--renderer;opengl;...] -P sweep.cmake
# For every row of levels.tsv (level N, entered from level M): load the row's original save through YukleMenu, then make two
# transitions the way an LY_Kapi door does (LevelYonetim.wdl LY_Tarama sets LY_HedefLevel=my.LevelID; LY_YuklemeTakip ->
# LevelYukle runs Acikmi(old)/Cikis(old)/level_load/Giris(old)): first into M, claiming M's own entry level as the origin
# (LY_AktifLevelID), then from M into N through the original path, so N's Acikmi/Giris see M's real state (harita_*).
# Then 600 frames of play (Up held for the first 120), checking that the player stays above the level geometry.
# Headless by default; ARGS replaces --headless (for example --renderer;opengl;--window-mode;windowed;--resolution;1280x720) and
# then each run writes OUT/<id>/out/level-<id>.ppm. Writes OUT/summary.tsv. Engine/script errors end the process (non-zero exit).
# -DCHARACTER=demir|ebru|inzar: after the load, the native console sets that character lock (docs/CHARACTER_LOCK.md), so both
# transitions place the locked character's player action, and the run asserts it is the one live player action.
if(NOT ARGS)
  set(ARGS --headless)
endif()
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/levels.tsv" rows REGEX "^[0-9]")
foreach(row IN LISTS rows)
  string(REPLACE "\t" ";" f "${row}")
  list(GET f 0 id)
  list(GET f 4 from_${id})
  list(GET f 5 hold_${id})
endforeach()
file(REMOVE_RECURSE "${OUT}")
set(base "${OUT}/base")
file(MAKE_DIRECTORY "${base}/data" "${base}/config" "${base}/state")
set(ENV{XDG_DATA_HOME} "${base}/data")
set(ENV{XDG_CONFIG_HOME} "${base}/config")
set(ENV{XDG_STATE_HOME} "${base}/state")
execute_process(COMMAND "${K22}" --import-saves "${SAVES}" --data "${GAME}" RESULT_VARIABLE code OUTPUT_QUIET)
if(NOT code EQUAL 0)
  message(FATAL_ERROR "save import failed")
endif()
set(summary "id\twmb\tcharacter\tsave\tfrom\tresult\tframes\tcontrol\tdetail\n")
set(failed "")
foreach(row IN LISTS rows)
  string(REPLACE "\t" ";" f "${row}")
  list(GET f 0 id)
  list(GET f 1 wmb)
  list(GET f 2 who)
  list(GET f 3 save)
  if(ONLY AND NOT id IN_LIST ONLY)
    continue()
  endif()
  set(from ${from_${id}})
  set(via ${from_${from}})
  set(root "${OUT}/${id}")
  file(MAKE_DIRECTORY "${root}/runtime")
  file(COPY "${base}/data" "${base}/config" "${base}/state" DESTINATION "${root}")
  file(CHMOD "${root}/runtime" DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
  set(who_id 0)
  if(who STREQUAL "Ebru")
    set(who_id 1)
  elseif(who STREQUAL "Inzar")
    set(who_id 2)
  endif()
  set(lock "")
  set(lockcheck "")
  if(CHARACTER)
    set(body Oyuncu_Demir)
    if(CHARACTER STREQUAL "ebru")
      set(body Oyuncu_Ebru)
    elseif(CHARACTER STREQUAL "inzar")
      set(body oyuncu_koruyucu)
    endif()
    set(lock "press F10\ntype character ${CHARACTER}\npress Return\npress F10\nframes 4\n")
    set(lockcheck "assert $count(${body}) == 1\nassert $count(Oyuncu_Demir) + $count(Oyuncu_Ebru) + $count(oyuncu_koruyucu) == 1\n")
  endif()
  set(checks "")
  foreach(i RANGE 1 6)
    if(i EQUAL 1)
      string(APPEND checks "down Up\nframes 100\n")
    elseif(i EQUAL 2)
      string(APPEND checks "frames 20\nup Up\nframes 80\n")
    else()
      string(APPEND checks "frames 100\n")
    endif()
    string(APPEND checks "assert player.z > $levelminz - 100\n")
  endforeach()
  # The settle waits keep a request from being lost (LY_YuklemeTakip clears LY_HedefLevel one second after LevelYukle starts)
  # and from racing the previous load's fade-out loop; a level whose entry keeps control (no_control) leaves the fade up.
  file(WRITE "${root}/level-${id}.k22s" "# level ${id} ${wmb} (${who}, kayit${save}.sav, via ${from} entered from ${via})
realtime off
load-save ${save}
wait until LY_HedefLevel == 0 && level_yuklendi_ok == 1 && LYP_Fade.visible == 0 timeout 6000
assert Aktif_Player == ${who_id}
${lock}set LY_AktifLevelID ${via}
set LY_HedefLevel ${from}
wait until LY_AktifLevelID == ${from} timeout 3000
wait until LY_HedefLevel == 0 && level_yuklendi_ok == 1 && LYP_Fade.visible == 0 || LY_HedefLevel == 0 && ${hold_${from}} timeout 1200
set LY_HedefLevel ${id}
wait until LY_AktifLevelID == ${id} timeout 3000
assert $level == \"${wmb}\"
wait until oyuncu_pasif == 0 && koruyucu_pasif == 0 && LYP_Fade.visible == 0 || ${hold_${id}} timeout 1200
${lockcheck}store x0 player.x
store y0 player.y
print player.z
print $levelminz
${checks}print player.x - @x0
print player.y - @y0
print oyuncu_pasif
print LY_AktifLevelID
capture level-${id}
")
  set(ENV{XDG_DATA_HOME} "${root}/data")
  set(ENV{XDG_CONFIG_HOME} "${root}/config")
  set(ENV{XDG_STATE_HOME} "${root}/state")
  set(ENV{XDG_RUNTIME_DIR} "${root}/runtime")
  execute_process(COMMAND "${K22}" --data "${GAME}" ${ARGS} --scenario "${root}/level-${id}.k22s" --scenario-out "${root}/out"
    RESULT_VARIABLE code OUTPUT_FILE "${root}/log.txt" ERROR_FILE "${root}/stderr.txt" TIMEOUT 600)
  file(STRINGS "${root}/log.txt" last REGEX "^scenario: [^ ]+\\.k22s ")
  string(REGEX MATCH "frames=([0-9]+)" _ "${last}")
  set(frames "${CMAKE_MATCH_1}")
  file(STRINGS "${root}/log.txt" pasif REGEX "print oyuncu_pasif -- ")
  string(REGEX REPLACE ".*-- " "" pasif "${pasif}")
  set(control yes)
  if(NOT pasif STREQUAL "0")
    set(control "no (scripted)")
  endif()
  set(detail "")
  if(code EQUAL 0)
    set(result PASS)
  else()
    set(result FAIL)
    list(APPEND failed ${id})
    file(STRINGS "${root}/log.txt" bad REGEX " FAIL ")
    file(STRINGS "${root}/stderr.txt" fatal REGEX "^kabus22: ")
    string(REPLACE "\t" " " detail "${fatal} ${bad}")
  endif()
  message(STATUS "level ${id} ${wmb} ${who}: ${result} frames=${frames} control=${control} ${detail}")
  string(APPEND summary "${id}\t${wmb}\t${who}\t${save}\t${from}\t${result}\t${frames}\t${control}\t${detail}\n")
endforeach()
file(WRITE "${OUT}/summary.tsv" "${summary}")
if(failed)
  message(FATAL_ERROR "level sweep failed: ${failed} (logs under ${OUT})")
endif()
