# Hengband (English) web port: handover

## RVIP progress

### Stage 1 (get + build): done 2026-09-26
- Folder `~/Games/hengband`, **case A** (z-term, `lib/edit`, `lib/pref`).
  Full upstream history, remote renamed `upstream`
  (https://github.com/hengband/hengband); our commits on local `master`.
  `lib/xtra` is a git submodule (https://github.com/hengband/hengband.xtra
  @ `4ed597b`: sound, music, `graf/8x8.bmp`); `git submodule update --init
  lib/xtra` after a fresh clone.
- Base: **upstream Hengband 3.0.2.4-Beta @ `bf1054199`** (`master`, tag
  `3.0.2.4-Beta`, 2026-08-02). Why: newest release; English is complete in
  the source (`_("日本語", "English")` macro, JSON data with `"en"` names,
  English help `lib/help/*.txt`), built without `-DJP` = English. Other
  choices: `develop` (newer, untagged work in progress), `v2.2.1r2` (2013 C
  code, far older). The todo's "Cryomaniac13 en" is
  `Cryomaniac13/hengband-touhou-katteban-en`, a translation of the Touhou
  Katteban fork, a different game: not used.
- Language: **C++20** (`configure.ac` requires it), ~870 `.cpp` files in
  folders under `src/` + `external-lib/fmt/format.cc`. Emscripten 6.0.10
  compiles it unchanged (`std::filesystem` works on MEMFS/IDBFS, no locale
  or thread problems in the English build). **`-fexceptions` is required**
  (compile and link): Emscripten drops `catch` by default, and the game
  catches `std::stoi` errors at every number prompt (`core/asking-player.cpp`:
  "-" at a Quantity prompt aborted the wasm without it).
- Web frontend: `src/main-web.cpp` (Frog/TinyAngband `main-web.c` ported to
  the 3.x z-term API: `term_type`, `term_init/term_activate/term_resize/
  term_key_push`, `game_term`, `angband_terms[]`, hooks take `TERM_LEN`,
  `quit_aux`/`plog_aux` take `std::string_view`, globals
  `AngbandWorld::get_instance().character_generated`, `inkey_flag`,
  `p_ptr->current_floor_ptr->dun_level`, sounds via `sound_names`).
  `main.cpp` is an `if (!done)` chain: `init_web()` called under `USE_WEB`,
  `ANGBAND_SYS = "x11"`. Page: `web/index.html`, `web/hengband.js` (from
  Frog's, `Module.qb`, IDBFS on `lib/save|user|apex|bone`, save =
  `0.PLAYER`, sound.cfg read from the preload by `loadSoundCfg()`), shared
  `rvip-wm.js` copied by the build. Text only; Tiles button hidden and the
  `tiles.webp` load disabled until stage 4 (`hengband.js` l.806).
- Build: `sh web/build.sh` → `web/dist` (first build ~4 min on 10 cores,
  objects cached in `web/obj`, gitignored; `rm -rf web/obj` after header
  edits). Source list = every `.cpp`/`.cc` in `src/Makefile.am` except
  `main-x11/gcu/cap.cpp`, plus `main-web.cpp`. Compile: `em++ -O2
  -std=c++20 -fexceptions -DUSE_WEB -DDISABLE_NET -Isrc -isystem src/external-lib/include
  -w`, parallel via xargs. Link (`em++ -O2 -fexceptions`): `-sASYNCIFY -sASYNCIFY_STACK_SIZE=131072
  -sSTACK_SIZE=2097152 -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=128MB
  -sEXPORTED_FUNCTIONS=_main,_web_request_save
  -sEXPORTED_RUNTIME_METHODS=FS,IDBFS,HEAPU8,addRunDependency,removeRunDependency
  -sFORCE_FILESYSTEM -lidbfs.js -sENVIRONMENT=web`, preload
  `lib/{edit,file,help,pref}` + `lib/xtra/sound/sound.cfg` + empty
  `data info save user apex bone script` as `/hengband/lib`. **Zero
  wasm-ld warnings.** wasm 8.2 MB (6.0 without exceptions), data 6.6 MB.
- Port edits (`USE_WEB`): `system/h-config.h` no `PRIVATE_USER_PATH`
  (`~/.angband`) and no `SAFE_SETUID`; `save/save.cpp` `web_sync_files()`
  after a successful `save_player()`; `main.cpp` web entry.
- Quirks: `-DDISABLE_NET` (else libcurl). `lib/xtra` is a submodule (empty
  after a plain clone). No quick start for the first character: birth is
  sex, race, class, personality (each "Are you sure? [Y/n]"), then the
  **autoroller** with every minimum at maximum (1 in 500000): press `n`
  on each stat (sets it to 3) before Accept. Birth screens flush input, so
  scripted keys need ~200 ms spacing there. `Ctrl-S` saves, `Ctrl-X`
  saves and quits. Natively `path_parse("~")` uses `getpwuid`, not `$HOME`
  (isolated-HOME tests still hit the real home: compile the test build
  without `PRIVATE_USER_PATH`).
- ASan: native curses build (`clang++ -fsanitize=address -DUSE_GCU
  -DUSE_NCURSES -DDISABLE_NET`, `-lncurses`; user dir `lib/user` via a
  patched copy of `h-config.h` for `main.cpp` + `angband-initializer.cpp`),
  pyte + pty driver (birth by screen text, then random keys without
  `Q`, ^A, ^C, ^Z, ^\, ^Y; Ctrl-X at the end), 8 seeds × (3000 keys from a
  new character, save, 2000 keys after restore), seeds 1-12. Three upstream
  bugs, each fixed in its own `port:` commit: `6c97a4f7d`
  `io/read-pref-file.cpp` `open_auto_dump()` tested `fpp` instead of
  `*fpp` (dump to an unopenable path at the File: prompt of the visuals
  menu → `fprintf(NULL)`); `08a97e41f` `util/angband-files.cpp`
  `path_parse()` threw an uncaught `runtime_error` for `~unknownuser`
  typed at a file prompt (help `?`; on the web `getpwnam()` never finds
  anyone) → now returns the path unchanged; `781b6fe62`
  `knowledge/knowledge-monsters.cpp` `r` (recall) on an empty monster group
  indexed an empty vector. Then clean. ASan build and objects deleted.
- Browser test (own tab, 127.0.0.1), twice (second on the final
  `-fexceptions` build): title → new character (Human Warrior) → town
  (Outpost) → 400-1000 random keys, `d a -` Enter at the Quantity prompt
  (caught exception, game goes on), no console errors, Ctrl-S "Saving
  game... done" → reload → restored in place (same turn, same spot). Test
  IDBFS databases (`/hengband/lib/*`) deleted.
- **Tiles decision (stage 4): Shockbolt** (case A fallback). Hengband.xtra
  ships only `graf/8x8.bmp` (old 8x8 set, `graf-xxx.prf`); the 16x16 /
  32x32 sheets for `graf-new.prf` / `graf-ne2.prf` are not in the repo.
  8x8 covers **1629/2247 = 72.5%** (`python3 web/tile-coverage.py`:
  monsters 930/1416, base items 599/642, terrains 100/189, by id from the
  `*Definitions.jsonc` files). Below 95%. Pref lines: `R:<monrace id>`,
  `K:<baseitem id>` (not tval:sval), `F:<terrain id>[:LIT]`.
- Open problems: sub-windows get the wrong content (inventory in
  Messages, messages in Inventory: window flags for the web terms, stage 5).
  Once after random keys the map was drawn a few rows too low until Ctrl-R
  (a centred full-screen menu's offset left over; not reproduced, watch in
  stage 5). Music (`music/new_town.ogg` copied from Quickband like Frog) is a
  placeholder; Hengband has its own `lib/xtra/music/*.mp3` (stage 6).

### Next: stage 2 (explore + stairs)
- Copy Frog's stage-2 code (end of `~/Games/frogcomposband/src/cmd2.c`:
  explore BFS, stairs walk, plus its hooks in `dungeon.c`
  `process_command()` / `process_player()`), ported to C++ and this API.
- Main loop: `core/player-processor.cpp` `process_player()` (l.100); travel
  already runs per turn there (l.291: `Travel::get_instance().is_ongoing()`
  → `travel.step(player_ptr)`). Commands: `io/input-key-processor.cpp`
  `process_command()` (switch on `command_cmd`; `` ` `` = `do_cmd_travel()`,
  disabled in wild mode). Travel API (`action/travel-execution.h`):
  `Travel::get_instance()`, `can_travel_to(floor, pos)`, `set_goal(p,
  pos)`, `reset_goal()`, `is_ongoing()`, `step()`: the stairs walk can
  reuse it.
- Map: `p_ptr->current_floor_ptr` (`FloorType`, `system/floor/floor-info.h`)
  → `get_grid(Pos2D)` → `Grid` (`is_mark()` = known, `m_idx`, `o_idx_list`,
  terrain via `get_terrain()`); terrain flags `TerrainCharacteristics::
  UP_STAIRS / DOWN_STAIRS / STAIRS / DOOR / TRAP`
  (`system/enums/terrain/terrain-characteristics.h`).
- Wilderness: town is part of the wilderness;
  `AngbandWorld::get_instance().is_wild_mode()` = world map (no travel
  there); `dun_level == 0` on the surface.
- Free keys (original keyset, from the `process_command()` switch): `X`
  (Frog's explore key) is free; also `h n x y H J K N O P W Y Z` are unused
  letters (check the roguelike keyset before choosing).
