# The CTest suite (worker "tests", 2026-11-09; docs/testing.md "CTest"): `ctest -C Release` from the build folder.
#   label "unit":   hotd_unit, one CTest test per suite; no game data (CI runs these on Windows, Linux and macOS)
#   label "module": the game's headless runs with their pass rules (tests/module_test.cmake); they need the game files
#                   from the environment (HOTD_TEST_ARCADE: the arcade zip, HOTD_TEST_PC: the PC game's folder,
#                   HOTD_TEST_ES: a Spanish PC copy) and are skipped (CTest "Skipped") when those are not set.
# `ctest -C Release -L unit` runs the unit layer alone; tools/run_tests.ps1 / .sh build and run both with a summary.

set(HOTD_UNIT_SOURCES
    unit/main.cpp
    unit/test_util.cpp
    unit/test_launcher.cpp
    unit/test_lang.cpp
    unit/test_mods.cpp
    unit/test_files_vr_net.cpp
    unit/test_rollback.cpp
    unit/test_modes.cpp
    unit/test_charsel.cpp
    unit/test_natpunch.cpp)
add_executable(hotd_unit ${HOTD_UNIT_SOURCES})
# the game's objects (everything but main.cpp: nothing in them needs it), the same ones hotd links
target_link_libraries(hotd_unit PRIVATE hotd_game_obj)
target_include_directories(hotd_unit PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_compile_definitions(hotd_unit PRIVATE HOTD_UNIT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_target_properties(hotd_unit PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if(MSVC)
    target_compile_options(hotd_unit PRIVATE /W4 /utf-8 /wd4127 /fp:precise)
    target_link_options(hotd_unit PRIVATE /STACK:8388608)
    if(HOTD_OPENXR)
        target_link_options(hotd_unit PRIVATE "/DELAYLOAD:openxr_loader.dll")
    endif()
else()
    target_compile_options(hotd_unit PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
                           -ffp-contract=off -fno-fast-math)
    set_target_properties(hotd_unit PROPERTIES BUILD_RPATH "${_hotd_rpath}")
endif()
if(WIN32)
    add_custom_command(TARGET hotd_unit POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SDL3::SDL3-shared>" "$<TARGET_FILE_DIR:hotd_unit>"
        VERBATIM)
endif()

# one CTest test per suite (a new suite: add its name here). "langdone" (the translations' to-do: keys of en.txt not
# translated yet) is labelled "translation", not "unit": a gap for the translators, not a fault (CI leaves it out)
set(HOTD_UNIT_SUITES util launcher lang mods files vr net rollback typing weapons survival evdev charsel)
set(HOTD_UNIT_TRANSLATION_SUITES langdone)
foreach(s ${HOTD_UNIT_SUITES} ${HOTD_UNIT_TRANSLATION_SUITES})
    set(_label unit)
    if(s IN_LIST HOTD_UNIT_TRANSLATION_SUITES)
        set(_label translation)
    endif()
    add_test(NAME ${_label}.${s} COMMAND hotd_unit ${s})
    set_tests_properties(${_label}.${s} PROPERTIES LABELS ${_label} TIMEOUT 120
        ENVIRONMENT "HOTD_UNIT_SOURCE=${CMAKE_SOURCE_DIR}")
endforeach()

# ---- the module layer: the game's headless runs (they need the game files: HOTD_TEST_ARCADE, HOTD_TEST_PC, ...;
# skipped without them). Each: name, the files it needs, timeout (s), arguments, the lines that must appear. The
# numbers are the regression numbers of the chapter runs (the game's behaviour must not change: a different runtime
# fails the test; a deliberate change updates the number here with its reason)
set(HOTD_MODULE_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/module_test.cmake)
set(HOTD_DATA_ARGS --arcade @ARCADE@ --pc @PC@)
set(HOTD_C1 --frames 40000 --autofire --mute --lives-kept 1)
set(HOTD_CH --strict --autofire --lives-kept 1 --mute --frames 60000 --default-route)
# hotd_module(<name> NEED <arcade pc es harness> TIMEOUT <s> ARGS <..> PASS <regex ..> [ARGS2 <..> PASS2 <..>]
#             [ENVS <K=V ..>] [RESULT <file> RESULT_PASS <regex>] [LABELS <extra labels>])
function(hotd_module name)
    cmake_parse_arguments(M "" "TIMEOUT;RESULT;RESULT_PASS" "NEED;ARGS;PASS;ARGS2;PASS2;ENVS;LABELS" ${ARGN})
    if(NOT M_TIMEOUT)
        set(M_TIMEOUT 600)
    endif()
    set(extra "")
    if(M_ARGS2)
        list(JOIN M_ARGS2 "#|#" a2)
        list(JOIN M_PASS2 "#|#" p2)
        list(APPEND extra -DPAIR=1 "-DARGS2=${a2}" "-DPASS2=${p2}")
    endif()
    if(M_RESULT)
        list(APPEND extra "-DRESULT=${M_RESULT}" "-DRESULT_PASS=${M_RESULT_PASS}")
    endif()
    list(JOIN M_ARGS "#|#" a)
    list(JOIN M_PASS "#|#" p)
    list(JOIN M_NEED "#|#" n)
    list(JOIN M_ENVS "#|#" e)
    add_test(NAME module.${name}
        COMMAND ${CMAKE_COMMAND} "-DEXE=$<TARGET_FILE:hotd>" -DNAME=${name} "-DWORK=${CMAKE_BINARY_DIR}/module_tests/${name}"
                "-DARGS=${a}" "-DPASS=${p}" "-DNEED=${n}" "-DENVS=${e}" -DTIMEOUT=${M_TIMEOUT} ${extra} -P ${HOTD_MODULE_SCRIPT})
    set_tests_properties(module.${name} PROPERTIES LABELS "module;${M_LABELS}" SKIP_RETURN_CODE 77
        TIMEOUT ${M_TIMEOUT} RUN_SERIAL FALSE)
endfunction()

# the input test (docs/testing.md 1): every input path, its results file's last line "N checks, 0 failed"
hotd_module(input NEED arcade harness TIMEOUT 2400
    ARGS --arcade @ARCADE@ --pc @HARNESS@ --input-test @WORK@/input.txt
    RESULT @WORK@/input.txt RESULT_PASS "^[0-9]+ checks, 0 failed$")
# the arcade game, both model sets (the regression numbers)
foreach(m arcade pc)
    if(m STREQUAL "arcade")
        set(r1 16350)
    else()
        set(r1 16427)
    endif()
    hotd_module(chapter1_${m} NEED arcade pc TIMEOUT 600 ARGS ${HOTD_DATA_ARGS} ${HOTD_C1} --models ${m}
        PASS "[[]runtime ${r1}[]] chapter 1 ended")
    hotd_module(chapter2_${m} NEED arcade pc TIMEOUT 900 ARGS ${HOTD_DATA_ARGS} --chapter 2 ${HOTD_CH} --models ${m}
        PASS "[[]runtime 16550[]] chapter 2 ended" "[[]runtime 48114[]] chapter 3 ended")
    hotd_module(chapter3_${m} NEED arcade pc TIMEOUT 900 ARGS ${HOTD_DATA_ARGS} --chapter 3 ${HOTD_CH} --models ${m}
        PASS "[[]runtime 27279[]] chapter 3 ended")
    hotd_module(chapter4_${m} NEED arcade pc TIMEOUT 900 ARGS ${HOTD_DATA_ARGS} --chapter 4 ${HOTD_CH} --models ${m}
        PASS "[[]runtime 28083[]] chapter 4 ended" "[[]runtime 34967[]] the ending is over")
endforeach()
# Survival: 10 rounds, the shop, the weapons (the self-test's own verdict)
hotd_module(survival10 NEED arcade pc TIMEOUT 600 ARGS ${HOTD_DATA_ARGS} --survival-test 10
    PASS "SURVIVAL TEST: result=ok arena=courtyard round=10")
# The Typing of the Dead 0: each chapter's quick self-test (its PASS line, every thrown thing prompted in time)
foreach(c 1 2 3 4)
    hotd_module(totd_chapter${c} NEED arcade pc TIMEOUT 600 ARGS ${HOTD_DATA_ARGS} --totd-test --chapter ${c}
        PASS "TOTD TEST: chapter ${c} over" "TOTD TEST: PASS" "TOTD PROMPT CHECK: [0-9]+ thrown things on the screen, [0-9]+ prompted within 20 frames, [0-9]+ later, 0 not within"
             "TOTD FLICKER: 0 boxes gone and back within 60 frames [(]blinks[)], 0 quick keys")
endforeach()
# Boss Mode: each boss cleared
foreach(b chariot hangedman hermit magician)
    hotd_module(boss_${b} NEED arcade pc TIMEOUT 600 ARGS ${HOTD_DATA_ARGS} ${HOTD_C1} --boss-mode ${b}
        PASS "BOSS MODE: result=cleared")
endforeach()
# PC Mode, 2P, 4P, Sonic Mode (chapter 1)
hotd_module(pcmode_chapter1 NEED arcade pc ARGS ${HOTD_DATA_ARGS} ${HOTD_C1} --pc-char 0 PASS "[[]runtime 16933[]] chapter 1 ended")
hotd_module(players2_chapter1 NEED arcade pc ARGS ${HOTD_DATA_ARGS} ${HOTD_C1} --start-at 2:600 PASS "[[]runtime 17340[]] chapter 1 ended")
hotd_module(players4_chapter1 NEED arcade pc ARGS ${HOTD_DATA_ARGS} ${HOTD_C1} --players 4 PASS "[[]runtime 22283[]] chapter 1 ended")
hotd_module(sonic_frenzy_chapter1 NEED arcade pc ARGS ${HOTD_DATA_ARGS} ${HOTD_C1} --sonic-mode frenzy PASS "[[]runtime 16350[]] chapter 1 ended")
# Horde (the unreleased mode; only where its code is built)
# (modfix 2026-11-10: 23775 was the tests merge's, before hordepub's Horde (pickups, upgrades, standing creatures:
# 25627) and survhand's Magician's Hand (6 + 5 x 6 shots, 550 %, auto rank 6, kept empty until Ammo / Max Ammo: the
# Hand is the same weapon in Horde, whose Hand pickups and Max Ammo upgrades fill it): 24256, 2 researchers saved)
if(HOTD_UNRELEASED)
    hotd_module(horde_chapter1 NEED arcade pc ARGS ${HOTD_DATA_ARGS} ${HOTD_C1} --horde-mode normal PASS "[[]runtime 24256[]] chapter 1 ended")
endif()
# VR: the desktop preview (the aim's round trips through a rendered frame); the aim area (no game data)
hotd_module(vr_preview NEED arcade pc ARGS ${HOTD_DATA_ARGS} --vr-preview-test --frames 3000 --autofire --mute --lives-kept 1
    PASS "vr preview test: ok")
hotd_module(aim_area ARGS --aim-area-test PASS "aim area test: 0 failed" LABELS nodata)
# Spanish (the private Spanish copy): its voices and texture texts found, chapter 1 as the US game
hotd_module(spanish_chapter1 NEED arcade pc es ARGS ${HOTD_DATA_ARGS} --spanish-pc @ES@ --voice es --lang es ${HOTD_C1}
    PASS "the Spanish voices" "the Spanish texture text pages" "[[]runtime 16350[]] chapter 1 ended")
# online co-op over 127.0.0.1 (two instances, rollback): the story with 2 players and Survival with every weapon
# (the Magician's Hand included: every weapon from round 1, cycled; its explosions counted) switched through the
# input every 300 frames; both PCs must end alike with no desync (both run --survival-test: its test buyer and end
# round are in the online settings, but a joiner without it plays another test harness: desync at frame ~1635)
hotd_module(online_story_2p NEED arcade pc TIMEOUT 900
    ARGS ${HOTD_DATA_ARGS} ${HOTD_C1} --host 127.0.0.1:47910 --code UNIT-TEST-1 --players 2
    PASS "chapter 1 ended" "NET: result=ok"
    ARGS2 ${HOTD_DATA_ARGS} ${HOTD_C1} --join 127.0.0.1:47910 --code UNIT-TEST-1
    PASS2 "chapter 1 ended" "NET: result=ok")
# online without port forwarding (worker "natpunch" 2026-11-10; docs/netplay.md "Hole punching"): the host's mock STUN
# servers on 127.0.0.1 (no internet), each PC behind a simulated restricted NAT (--net-sim-nat: a datagram from an
# address it never sent to is dropped). The joiner reads the host's code from a file; its direct try is dropped, so
# it writes its reply code; the host "pastes" it (reads the file) and punches; both connect and play 3000 frames alike.
# _direct: no simulated NAT: the direct try connects, no reply code needed
set(HOTD_PUNCH_GAME ${HOTD_DATA_ARGS} --frames 3000 --autofire --mute --lives-kept 1)
hotd_module(online_punch NEED arcade pc TIMEOUT 300
    ARGS ${HOTD_PUNCH_GAME} --host 127.0.0.1:47914 --players 2 --stun-mock 47940,47941 --net-sim-nat
         --code-out @WORK@/code.txt --reply-from @WORK@/reply.txt
    PASS "STUN: got this PC's internet address [(]2 servers[)]" "hole punching: 1 reply code pasted" "NET: result=ok"
    ARGS2 ${HOTD_PUNCH_GAME} --join @@WORK@/code.txt --stun 127.0.0.1:47940,127.0.0.1:47941 --net-sim-nat --reply-out @WORK@/reply.txt
    PASS2 "STUN: got this PC's internet address" "NET: result=ok")
hotd_module(online_punch_direct NEED arcade pc TIMEOUT 300
    ARGS ${HOTD_PUNCH_GAME} --host 127.0.0.1:47916 --players 2 --stun-mock 47942,47943 --code-out @WORK@/code.txt
    PASS "STUN: got this PC's internet address" "NET: result=ok"
    ARGS2 ${HOTD_PUNCH_GAME} --join @@WORK@/code.txt --stun 127.0.0.1:47942,127.0.0.1:47943
    PASS2 "hole punching: trying the host directly" "NET: result=ok")
hotd_module(online_survival_hand NEED arcade pc TIMEOUT 900
    ARGS ${HOTD_DATA_ARGS} --autofire --lives-kept 1 --mute --survival-test 6 --survival-weapons-wave 1 --survival-cycle-weapons
         --host 127.0.0.1:47912 --code UNIT-TEST-2 --players 2
    PASS "SURVIVAL TEST: result=ok arena=courtyard round=6" "blasts=[1-9]" "NET: result=ok"
    ARGS2 ${HOTD_DATA_ARGS} --autofire --lives-kept 1 --mute --survival-test 6 --join 127.0.0.1:47912 --code UNIT-TEST-2
    PASS2 "SURVIVAL TEST: result=ok arena=courtyard round=6" "NET: result=ok"
    ENVS HOTD_NET_SURVIVAL_SWITCH=300)
