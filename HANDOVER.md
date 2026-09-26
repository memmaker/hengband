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

### Stage 2 (explore + stairs): done 2026-09-26
- **Explore key `X`** (original keyset). `X` was a keymap to `w0` (swap
  weapons by `@0` inscription) in both keysets and one of the keys that
  open the command menu: the keyset-0 keymap is commented out in
  `lib/pref/pref-key.prf`, `X` dropped from the menu test in
  `io/input-key-requester.cpp` `request_command()` (Enter / `x` still open
  it). Roguelike keyset: `X` is still `w0`, no explore key there.
- Code: `src/cmd-action/cmd-explore.cpp/.h` (port of Frog's end of
  `cmd2.c`; in `src/Makefile.am`, so `web/build.sh` picks it up):
  `auto_explore`, `explore_stairs`, `explore_new_level()`, `explore_find()`
  (BFS), `explore_step()`, `explore_to_stairs()`, `explore_stairs_arrive()`.
- Hooks: `io/input-key-processor.cpp` `process_command()` `case 'X'` (not
  in wild mode); `core/player-processor.cpp`: `auto_explore` in
  `continuous_action_running()` (key abort), `else if (auto_explore)
  explore_step()` before running, `else if (explore_stairs)
  explore_stairs_arrive()` after the travel branch;
  `dungeon/dungeon-processor.cpp` `process_dungeon()` calls
  `explore_new_level()` after `leaving = false`; `core/disturbance.cpp`
  `disturb()` and `view/display-messages.cpp` `msg_print()` (every message
  once `character_generated`) clear `auto_explore`;
  `cmd-action/cmd-move.cpp` `do_cmd_go_up/down()` call
  `explore_to_stairs()` instead of "I see no ... staircase here".
- **Known grid**: `grid.is_mark() || (grid.info & CAVE_KNOWN)`. CAVE_KNOWN
  ("directly viewed", set by `note_spot()`) is never forgotten, so no own
  seen array is needed (unlit floor loses CAVE_MARK, not CAVE_KNOWN).
  Terrain as the player sees it: `grid.get_terrain(TerrainKind::MIMIC)`.
- Targets: known grid next to an unknown one, or an item with
  `OmType::FOUND` not stood on yet (set of `ItemEntity*` per level;
  `OmType::TOUCHED` is game state, not used). Avoids TRAP, STORE, BLDG,
  QUEST_ENTER, LAVA, deep WATER, visible monsters, else
  `player_can_enter()`. Opens closed doors with `exe_open()` unless locked
  (true terrain `door_power` or no OPEN); digs rubble (CAN_DIG, not
  WALL/MOVE) with `exe_tunnel()`. Stops: disturb, new message, visible
  non-pet/non-friendly monster whose grid `is_view()` ("In view: the
  Frail yeek."), step that did not move, no light (`no_lite()`, and in the
  dungeon `cur_lite <= 0`: without its own light it walked back and forth
  at a lit room's exit forever), confused/blind/hallucinating, surface
  outside a town, nothing left ("Only locked doors or known traps are in
  the way." when that is why).
- Stairs: BFS for the nearest known UP/DOWN_STAIRS (not QUEST_ENTER) →
  `Travel::get_instance().set_goal()`; the per-turn travel branch walks,
  `explore_stairs_arrive()` takes the stairs once travel stopped on them
  (prompts like "Do you really get in this dungeon?" kept). On the surface
  the BFS may cross unknown grids (travel does too), so `>` in town finds
  the dungeon entrance at night. Travel itself refuses without light ("You
  cannot see!"). `<` on the surface keeps the world-map toggle; `>` in wild
  mode keeps leaving it; `X` does nothing on the world map.
- **auto_more (3d)**: Hengband's `auto_more` still stops at `-more-` when
  no term has the MESSAGE window flag / it overflowed
  (`is_msg_window_flowed()`); `skip_more` never stops. Defaults for new
  characters come from `lib/pref/pref-opt.prf` (read at start, before the
  savefile; the table defaults in `option-types-table.cpp` are overridden
  by it), so `web/build.sh` rewrites the staged copy: `Y:auto_more`,
  `Y:skip_more`, `Y:center_player`. Savefiles keep their own options (an
  old test save kept them off: delete test IDBFS databases first).
- Help: `lib/help/command.txt` (X row, `@0` note), `commdesc.txt`
  (Auto-explore, `<`/`>` walk).
- Tested in the browser (own tab, 127.0.0.1): new Human Warrior, town
  (day) `X` = "Nothing left", `>` walked to the nearest entrance (Yeek
  cave) and asked; dungeon: explore over ~60 presses (rooms, corridors,
  doors opened, rubble dug, items/gold picked up, monster stops with name,
  "Only locked doors or known traps" once, no light stop), `>` walked to
  a known `>` and descended, `<` walked to `<` and went up (twice, once
  back to town). Test IDBFS databases (`/hengband/lib/*`) deleted.
- ASan (native, own copy of stage 1's pty driver with `X` ×25 and `<`/`>`
  ×8 in the key pool): one upstream bug, fixed in `port:` commit
  `23e62fd4e` (help `%` Goto File with an unknown name threw an uncaught
  `runtime_error` from `FileDisplayer::display()` = abort, also on the
  web), then clean: seeds 1-11 (3000 keys new + 2000 after restore; some
  restores ended early when the random keys had killed the character).
- Open problems: explore stops on each "You see ..." when it walks over an
  item again, and on a revisited (saved) floor it re-visits every item
  (the stood-on set is per level visit); a visible monster it cannot
  reach (behind a wall, "Blinking dot") blocks explore until it moves or
  dies (Frog rule); the ending "You have removed the rubble" message stops
  explore once per rubble; no explore key in the roguelike keyset.

### Next: stage 3 (Enter menu + inventory)
- Template: Frog's Stage 3 in `~/Games/frogcomposband/HANDOVER.md`
  (rewritten `inkey_from_menu()` as Zangband's `cmd_menu()`, `gear_ui()`
  with an `obj_prompt()` preselect).
- Key dispatch: `io/input-key-requester.cpp`
  `InputKeyRequestor::request_command()` → `process_command()` in
  `io/input-key-processor.cpp` (switch on `command_cmd`). Hengband's own
  command menu: `InputKeyRequestor::inkey_from_menu()` (same file, opened
  by Enter / `x` when `command_menu` is on and no keymap), data in
  `cmd-io/cmd-menu-content-table.cpp` (`menu_info[][]`,
  `special_menu_info[]`); `autopick/autopick-menu-data-table.cpp` is the
  autopick editor's menu, not this one. Add `X` (explore) to the menu.
- Items: `i`/`e` = `do_cmd_inven()` / `do_cmd_equip()`
  (`cmd-item/cmd-item.cpp`); every item prompt goes through
  `choose_item()` (`floor/floor-object.cpp`) → `get_item_floor()`
  (`inventory/floor-item-getter.cpp`) with `USE_INVEN|USE_EQUIP|USE_FLOOR`
  flags (`object/item-use-flags.h`) and an `ItemTester`
  (`object/item-tester-hooker.h`, e.g. `FuncItemTester(item_tester_hook_quaff)`
  in `cmd-item/cmd-quaff.cpp`); helpers in `inventory/item-selection-util.cpp`.
- Test characters: `lib/pref/pref-opt.prf` has `Y:command_menu`; birth as
  above (Warrior carries torches unwielded: `w` one before the dungeon).
- **Tiles (stage 4): Adam Bolt 16x16, the user's explicit choice**
  (overrides the stage 1 Shockbolt fallback; the 95% rule is waived for
  this game, gaps get same-set stand-ins, never a second set). Upstream
  never had the sheet in git (`hengband` and `hengband.xtra` history only
  carry `graf/8x8.bmp`; `graf-new.prf` exists without its image): take
  `~/Games/frogcomposband/lib/xtra/graf/16x16.bmp` (same Adam Bolt sheet)
  with Frog's `lib/pref/graf-new.prf`, remapped to Hengband's JSON ids
  (`R:`/`K:`/`F:` by id, see stage 1).

### Stage 3 (Enter menu + inventory): done 2026-09-26
- **Enter menu**: Hengband's own `InputKeyRequestor::inkey_from_menu()`
  (`src/io/input-key-requester.cpp`) rewritten as Frog's/Zangband's
  `cmd_menu()`: the old fixed 2-column frame (`make_commands_frame()`,
  cursor helpers, `special_menu_info`, class/wild names) is gone. Same call
  site in `get_command()`: Enter / `x` when `command_menu` is on and no
  keymap uses the key (`pref-key.prf` `^J` → `\r` opens it too). Data:
  `src/cmd-io/cmd-menu-content-table.cpp` `menu_info` is now one flat
  `std::vector<menu_content>` (`{name, cmd}`, cmd 0 = group), 15 groups as
  `lib/help/commdesc.txt` groups them, incl. `X` explore, `<`/`>`, travel
  `` ` ``, repeat `n`, pets, ^I, screen dumps, ^V. Boxes: free functions
  `box_draw()` / `box_menu()` (sized to content, moved to fit, no
  scrolling); keys of the current keyset: `command_key()` /
  `command_key_str()` (reverse lookup in `keymap_actions_map`; roguelike
  shows `^D`, `T`, `,`; explore has no roguelike key, pick it by cursor).
  2/8/arrows move, Enter/Space/5/6 choose, group letter or command key
  chooses, Esc/0/4 back. The chosen underlying command skips keymaps
  (`inkey_next = ""`) and sets `use_menu` (the game's own cursor item
  prompts), runs through `process_command()`.
- **Item menus**: `i`/`e` = `gear_ui(player, equip)` in
  `src/cmd-item/cmd-item.cpp` (`do_cmd_inven()`, and `do_cmd_equip()` in
  `cmd-equipment.cpp`, just call it). Drawn by the game's
  `show_inventory()` / `show_equipment()` + a `>` at `command_gap - 1`.
  Letter = main action (`gear_main()`: first fitting of quaff, read, use
  staff, aim, zap, eat, cast (book), wear/wield, take off, refuel, else
  examine; eat comes after the devices because MANA-food races can eat
  staffs), Shift+letter drop, Ctrl+letter examine, 2/8 cursor, 4/6 switch
  list, Enter/Space/5 = `gear_menu()` (a `box_menu()` of every fitting
  action in `gear_actions[]`: eat, quaff, read, use, aim, zap, cast,
  wear/wield, take off, refuel, browse, activate, fire, throw, drop,
  destroy, inscribe, uninscribe, examine, each with the same
  `USE_INVEN/USE_EQUIP` places and item tester as the command's own
  `choose_item()` call), `+ - *` main/drop/examine of the cursor item,
  Esc/0/. close, any other key is a normal command (as before).
- **How item actions run (key queue + preselect)**: the list sets
  `item_preselect = i_idx` (`inventory/floor-item-getter.h`),
  `queue_raw_command(key)` (`command_new` + `command_raw` → no keymap in
  `get_command()`) and `gear_reopen = 'i'/'e'`, then closes. The command
  runs through `process_command()`; the first `get_item_floor()` takes the
  preselect if the item is in its places and passes its tester (then
  `repeat_push()`s it, so `n` repeats) and always clears it.
  `core/player-processor.cpp` before `request_command()`: queues
  `gear_reopen` unless `hostile_monster_in_view()` (new wrapper in
  `cmd-explore.cpp`, the explore stop test); after `process_command()`
  clears the preselect unless a command is queued.
- **Web fixes found on the way** (stage 1 bugs): `main-web.cpp` pushed
  JS keys with `term_key_push()`, which puts keys in *front*: every
  multi-key event (arrow/keypad/F-key macro triggers `^_..._FF54\r`) came
  in reversed and ran as junk keys (`_` opened the autopick editor). Now
  `web_keypress()` appends FIFO. `hengband.js` `fresh()`: cells of row 0
  the game never wrote were holes in `row0`, so the prompt line lost its
  spaces ("Savefiledoesnotexist"): holes are blanks now.
- Tested in the browser (own tab, 127.0.0.1): Enter menu by arrows
  (Resting box), by letters (`b` `X` → explore ran), Esc; items (Skeleton
  Weaponsmith; Potions of Water + Ration at the General Store, Phase Door
  at the Alchemist, reached by look `l` → `g`): wield torch (menu `w`),
  take off (letter in `e`, list reopened, cursor on the item), quaff
  (letter), drop (Shift, quantity prompt), examine (Ctrl), use staff
  (menu `u`), read (menu, cursor + Enter); roguelike (`"` 
  `Y:rogue_like_commands`): menu shows `T`, `^D`, `,`, `;`; `T` from the
  menu runs take off; take off by letter in `e`; explore from the menu by
  cursor. Test IDBFS databases (`/hengband/lib/*`) deleted.
- ASan (native gcu build, stage 1's pty driver with Enter x12, `i`/`e`
  x10, 2/4/6/8, arrows, `X`, `<`/`>` added to the key pool; seeds 21-27,
  3000 keys new + 2000 after restore): two upstream bugs, each fixed in
  its own `port:` commit: `230c6c467` `term/z-term.cpp` `term_erase()`
  stepped to column -1 when column 0 held attr 0xFF (colour menu `&` `3`
  lets the index wrap to 255, which looks like AF_BIGTILE2); `d894bd0b9`
  `cmd-action/cmd-racial.cpp` `U` `/` + letter redrew the list at page -1
  (`power_desc[-15]`). Then clean (seeds 22, 24-27). Build deleted.
- Open problems: the web prompt line (a DOM overlay, `rvip-wm.js`) wraps
  and covers the first rows of the `i`/`e` list (stage 5 layout); the
  reopened list hides the action's message (messages window / `^P`); the
  action menu box sits at the left over the side panel and can cover the
  list's labels on a narrow term; every item prompt does not get a cursor
  (only prompts of commands picked from the Enter menu, the game's own
  `use_menu` mode); Tab/^I, ^J, ^M in the list are Enter/examine keys,
  not commands; the sub-windows still show the wrong content (stage 1).

### Next: stage 4 (tiles)
- **Adam Bolt 16x16, the user's explicit choice** (the 95% rule is waived
  for this game; gaps get same-set stand-ins, never a second set). Source:
  `~/Games/frogcomposband/lib/xtra/graf/16x16.bmp` (the sheet Hengband's
  `graf-new.prf` was made for; never in Hengband's git) with Frog's
  `lib/pref/graf-new.prf`, remapped to Hengband's JSON ids (`R:<monrace
  id>`, `K:<baseitem id>`, `F:<terrain id>[:LIT]`; `web/tile-coverage.py`
  knows the id scheme and the `*Definitions.jsonc` files).
- Loader: the template's `js_pict` path in `src/main-web.cpp` (Frog's
  `main-web.c`), `hengband.js` `pict()` already draws from `tiles.webp`
  (load disabled, l.~806) and the Tiles button is hidden: enable both;
  nearest-neighbour at cell size; Tiles on/off button.

### Stage 4 (tiles): done 2026-09-26
- **Tile set: Adam Bolt 16x16** (the user's choice; one set, 95% rule
  waived). Sheet `web/tiles.webp` (512x1072, lossless, 165 kB, committed;
  credit in `web/tiles.txt`) = FrogComposband `lib/xtra/graf/16x16.bmp`
  with `mask.bmp` as alpha (mask 255 = transparent). `web/build.sh` copies
  it to `web/dist`. Never in Hengband's git (`lib/xtra` has only 8x8).
- **Prefs**: upstream `lib/pref/graf-new.prf` is already keyed by
  Hengband 3.x ids (`R:`/`K:`/`F:`; checked against the JSON `"en"`
  names: 1418 tiles by name, 17 later renames, same monster) and
  `xtra-new.prf` has the hero per race (column) + class (row), so Frog's
  prf was not needed. New `lib/pref/graf-ab.prf` (generated by `python3
  web/mkgraf-ab.py`, read by a `%:graf-ab.prf` line at the end of
  graf-new.prf, listed in `lib/pref/Makefile.am`) fills every id without
  a real tile: monster = same symbol + colour, else symbol; item = same
  tval + symbol, else tval; terrain = its mimic, else symbol; mimic
  monsters (`\ / ] 7` symbols) take an item/terrain of the symbol; by hand
  MUSEUM = BOOKSTORE building, `N` monsters = a `G`, UNDETECTED (unknown
  grid, upstream a text `x`) = an empty cell (black). Loaded via
  `ANGBAND_GRAF = "new"` (`graf.prf` `?:[EQU $GRAF new]`; also turns on the
  A.B. bolt colours in `gameterm.cpp`).
- **Coverage** (`python3 web/tile-coverage.py ab`): **2246/2247 = 100.0%**
  (monsters 1416/1416, base items 642/642, terrains 188/189; the miss is
  UNDETECTED = the empty tile on purpose). 1435 upstream tiles (1418 by
  name, 17 renames), 3 by hand, **808 family stand-ins** (611 monsters,
  61 items, 136 terrains).
- **C++ decides per cell** (`src/main-web.cpp`): `js_pict`,
  `js_tiles_wanted`, `js_tiles_switch`; `term_pict_web()` passes `big` =
  next cell is the `AF_BIGTILE2` pad (0xF0/0xFF); `web_graphics()` sets
  `use_graphics`, `arg_graphics` (2 = GRAPHICS_ADAM_BOLT), `ANGBAND_GRAF`,
  `arg_bigtile`; `init_web()` asks the page before the terms exist
  (`higher_pict`, `pict_hook`); `web_pump()` applies a Tiles switch at the
  command prompt only (`web_switch_graphics()`: `term_resize()` of term 0
  for `arg_bigtile`, `reset_visuals()`, `do_cmd_redraw()`). Hengband's
  own viewport handles big-tile mode (no Frog-style `UI_MAP_STEP` fix).
- **Scale**: big-tile mode (one grid = 2 half-width cells), tile = `L.tile`
  CSS px, nearest-neighbour (`imageSmoothingEnabled = false`);
  `hengband.js` `TILE = 16`, `TILE_STEPS = [16, 32, 48, 64]` (whole
  multiples). JS only blits (`pict()`: black, terrain tile, then the tile
  over it). **Tiles: on/off** button (`btn-tiles`, no longer hidden),
  saved in the layout as `L.text`. No `.prf` fetch (preload).
- Tested in the browser (own tab, 127.0.0.1, viewport 1600x1000): town
  day and night (shop entrances with signs, buildings, permanent walls,
  water, grass, trees, dungeon entrance), Yeek cave L1/L3 via `>` + `X`
  (granite, magma/quartz/treasure veins, open/closed doors, up/down
  stairs, yeek/insect swarm/soldier ant/merchant, scroll/weapons/gold
  items), Half-Troll Warrior and Skeleton Weaponsmith hero tiles, item
  tiles in lists. Canvas check (16x32 device px cells): town 6123 cells
  with only sheet colours, text cells only in the side panel (96);
  text mode: 0 tile cells. Tiles→text→tiles→text→tiles: identical counts
  each time, map intact; Tiles off survives a reload. Traps not seen (none
  detected on the levels walked; trap ids map to upstream trap tiles).
  Test IDBFS databases (`/hengband/lib/*`) deleted.
- Native ASan: skipped (native build is curses `USE_GCU`, no graphics;
  the change is web-only code in `main-web.cpp` + prefs).
- Open problems: 808 stand-ins (Hengband-only monsters share family
  tiles; mimic monsters look like items); unknown-but-mapped grids (`x` in
  text mode) are black in tiles; on a pane narrower than 80 grid cells ×
  tile the map canvas is scaled down by CSS (smooth, not nearest), e.g.
  800x600 at 16 px: stage 5 layout should size the main window so the
  canvas is not shrunk (or set `image-rendering: pixelated`).

### Next: stage 5 (web page)
- Sub-windows show the wrong content (inventory in Messages, messages in
  Inventory): add a window flags table in `init_web()` like Frog's
  `web_window_flags[]` (W4) for the 6 web terms.
- The page prompt line (`RvipWM.prompt`) wraps over the first rows of the
  `i`/`e` list; the layout file (sizes, Tiles on/off already in `L.text`).
- Game end: `hook_quit` → `js_quit` shows "Hengband has ended" + Play
  again; check the death path (tombstone, "Last words" prompt loops with
  "Are you sure?" until `y`).
- **Temp floor files prompt (W5)**: on reload after a save in the
  dungeon, "If the temporary files are garbage ... Do you delete the old
  temporary files? [y/n]" appears; `n` quits the game ("Aborted."). Answer
  it in the web build (as Frog did) or keep the floor files in IDBFS.
- `web/deploy.sh` from Frog's, target `ruzzoli.de/roguelikes/hengband/`,
  no deploy until stage 7.

### Stage 5 (web page): done 2026-09-26
- **Windows** (`rvip-wm.js`, shared copy; `web/index.html` `#t-<id>`,
  `TERMS` in `web/hengband.js`, 8 terms = `WEB_TERMS` in
  `src/main-web.cpp`, the z-term maximum): 0 Map, 1 Inventory `INVENTORY`,
  2 Messages `MESSAGE`, 3 Visible `SIGHT_MONSTERS`, 4 Recall
  `MONSTER_LORE|ITEM_KNOWLEDGE`, 5 Equipment `EQUIPMENT`, 6 Objects
  `FOUND_ITEMS`, 7 Character `PLAYER` (all `SubWindowRedrawingFlag`s exist
  upstream, no new flag). Default on: Map, Inventory, Visible, Messages.
- **Wrong-content bug**: `main/game-data-initializer.cpp` `init_other()`
  (runs *after* `init_web()`) hard-set term 1 = MESSAGE, term 2 =
  INVENTORY (X11 order). Under `USE_WEB` it now calls `web_window_flags()`
  (the one table, `main-web.cpp`). Birth only fills empty terms 1/2; a
  savefile brings its own flags.
- **Prompt line**: `#t-main .wm-topl` CSS in `index.html` (one row high,
  full width, opaque, no wrap) + `fitCanvas()` sets `--row-h`/`--row-font`
  from term 0's cell size: the box covers exactly row 0 (the game's own
  message row), never the `i`/`e` list below. Text still from C++ (row 0).
- **Layout file** `/hengband/lib/user/web-layout.json` (IDBFS: splits, wm
  tree incl. which windows are on, zoom, fonts, titles, Tiles).
- **Temp-files prompt fixed**: `floor/floor-save.cpp` `init_saved_floors()`
  forces `force = true` under `USE_WEB` (leftover `0.PLAYER.Fnn` deleted
  silently at start).
- **Game end**: one path, `play_game()` → `close_game()`
  (`core/game-closer.cpp`: Ctrl-X → save + "Press Return (or Escape)";
  death → tombstone + filename prompt (RET/ESC → character sheet) → scores)
  → `quit("")` → `quit_aux` = `hook_quit` → `js_quit(msg, p_ptr->is_dead)`.
  Dead: page syncs and reloads into a new birth (quick start offered).
  Ctrl-X: "Play again" overlay, reload restores. Only `exit()` calls are in
  `term/z-util.cpp` `quit()`, after `quit_aux`. **Last words**
  (`player/player-damage.cpp`): under `USE_WEB` one `input_string()`, Enter
  takes the text, Esc keeps the random default line, no "Are you sure?"
  loop. "Dump the screen? [y/n]" before it takes Esc.
- **Help**: `build.sh` writes a stub `help.html` (stage 6 replaces it).
- **`web/deploy.sh`**: Frog's, target `ruzzoli.de/roguelikes/hengband/`,
  guard first. Dry run: "commit + push first", exit 1. **Not deployed, no
  repo** (only remote `upstream`, no `@{u}`).
- Tested (own tab, 127.0.0.1, 1440x900 and 1000x650): new Skeleton
  Weaponsmith → Inventory, Messages, Equipment, Character sheet, Objects
  ("Found items"), White icky thing in Visible and (after `*`) Recall;
  all 8 windows on → Ctrl-S → reload → same layout, character restored;
  fake `0.PLAYER.F01` + reload → no prompt, file gone; Ctrl-X → Press
  Return → overlay → Play again → restored; suicide `Q y @` → tombstone →
  RET sheet → ESC → "Score not registered" → reload → quick start → new
  character; debug death (`"` `Y:allow_debug_opts`, `^A k` 1000000 dmg,
  type 10) → "Dump the screen?" Esc → Last words Esc → tombstone → new
  game; `i` prompt row exactly one 16 px row, list rows intact; Help
  opens/Esc closes; no console errors. IDBFS `/hengband/lib/*` deleted.
  Native ASan (gcu, seed 51, 2500 new + 1500 restored keys; the C++
  changes are all `USE_WEB`): clean.
- Open problems: a dead character's message history shows in the new
  character's Messages window when it starts via quick start; Character
  window shows only the first sheet page; Map window gets small with all 8
  windows on (the WM splits it; user can drag); the "Recall" window needs
  a look/target (`*`, `l` only on interesting grids).

### Next: stage 6 (docs + sound)
- Sound: `sound.cfg` is already in the preload (`lib/xtra/sound/sound.cfg`)
  and read by `loadSoundCfg()` in `hengband.js`; `js_sound` hook exists in
  `main-web.cpp` (`sound_names`). Wavs: Hengband's own `lib/xtra/sound`
  (submodule `hengband.xtra`) if licensed, else `web/sounds.py` from
  Frog's/Zangband's (Dubtrain wavs).
- Music: Hengband's own `lib/xtra/music/*.mp3` (stage 1 note) instead of
  the Quickband `new_town.ogg` placeholder `build.sh` copies now.
  Sound/Music buttons off by default.
- Help: `web/make-help.py` from Frog's with `PAGE='hengband.html'`
  (replace the `help.html` stub line in `build.sh`); Docs entry under
  `~/Desktop/Games/Roguelikes/Docs/` with both keysets (original + roguelike;
  explore `X` only in the original keyset); Adam Bolt tile credit
  (`web/tiles.txt`).
