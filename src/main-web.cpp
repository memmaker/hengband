/*
 * Browser (Emscripten/WASM) front end for Hengband.
 *
 * All drawing is done by JavaScript on one <canvas> per term (see
 * web/hengband.js).  Blocking input uses Asyncify: when the game waits
 * for a key we sleep in emscripten_sleep(), which yields to the browser.
 *
 * The module registers itself as "x11" so that the same pref files
 * (keymaps, window layout) as the X11 build are used; special keys are
 * sent in the X11 keysym macro format.
 */

#ifdef USE_WEB

#include "cmd-visual/cmd-draw.h"
#include "core/visuals-reseter.h"
#include "game-option/option-flags.h"
#include "game-option/runtime-arguments.h"
#include "game-option/special-options.h"
#include "io/input-key-acceptor.h"
#include "main/sound-definitions-table.h"
#include "player/player-status.h"
#include "system/inner-game-data.h"
#include "system/angband.h"
#include "system/floor/floor-info.h"
#include "system/player-type-definition.h"
#include "system/system-variables.h"
#include "term/gameterm.h"
#include "term/term-color-types.h"
#include "term/z-term.h"
#include "term/z-util.h"
#include "util/enum-converter.h"
#include "util/int-char-converter.h"
#include "window/main-window-util.h"
#include "world/world.h"
#include <emscripten.h>
#include <string_view>
#include <vector>

constexpr auto WEB_TERMS = 8; /* terms 1-7: see web_window_flags() */

static term_type web_term[WEB_TERMS];

/* Pending "save now" request from the page (tab hidden / closing) */
static bool web_want_save = false;

/* Last time we yielded to the browser */
static double web_last_yield = 0;

/* ---- JavaScript side (implemented in web/hengband.js) ---- */

EM_JS(void, js_text, (int t, int x, int y, int n, int a, const char *s), { Module.qb.text(t, x, y, n, a, s); });
EM_JS(void, js_wipe, (int t, int x, int y, int n), { Module.qb.wipe(t, x, y, n); });
EM_JS(void, js_clear, (int t), { Module.qb.clear(t); });
EM_JS(void, js_curs, (int t, int x, int y, int w), { Module.qb.curs(t, x, y, w); });
/* big = 1 in big-tile mode: the tile covers this cell and the next one */
EM_JS(void, js_pict, (int t, int x, int y, int n, const TERM_COLOR *ap, const char *cp, const TERM_COLOR *tap, const char *tcp, int big),
    { Module.qb.pict(t, x, y, n, ap, cp, tap, tcp, big); });
/* Tiles (1) or text (0) as the page's Tiles button says; switch: -1 = no change */
EM_JS(int, js_tiles_wanted, (void), { return Module.qb.tilesWanted(); });
EM_JS(int, js_tiles_switch, (void), { return Module.qb.tilesSwitch(); });
EM_JS(void, js_fresh, (int t), { Module.qb.fresh(t); });
EM_JS(void, js_bell, (void), { Module.qb.bell(); });
EM_JS(void, js_sound, (const char *name), { Module.qb.sound(UTF8ToString(name)); });
EM_JS(void, js_depth, (int depth), { Module.qb.depth(depth); });
EM_JS(void, js_color, (int i, int r, int g, int b), { Module.qb.color(i, r, g, b); });
EM_JS(int, js_term_cols, (int t), { return Module.qb.termCols(t); });
EM_JS(int, js_term_rows, (int t), { return Module.qb.termRows(t); });
EM_JS(int, js_layout_pending, (int t), { return Module.qb.layoutPending(t); });
EM_JS(int, js_pending_cols, (int t), { return Module.qb.pendingCols(t); });
EM_JS(int, js_pending_rows, (int t), { return Module.qb.pendingRows(t); });
EM_JS(void, js_apply_layout, (int t, int cols, int rows), { Module.qb.applyLayout(t, cols, rows); });
/* Next queued input: -1 none, else key */
EM_JS(int, js_next_event, (int at_cmd), { return Module.qb.nextEvent(at_cmd); });
/* dead = 1: the character died (tombstone shown), the page starts a new game */
EM_JS(void, js_quit, (const char *msg, int dead), { Module.qb.quit(msg ? UTF8ToString(msg) : "", dead); });
EM_JS(void, js_plog, (const char *msg), { Module.qb.plog(UTF8ToString(msg)); });
EM_JS(void, js_sync, (void), { Module.qb.sync(); });

/* Graveyard + leaderboard beacon (roguelikes-index/server/CONTRACT.md) */
EM_JS(void, js_beacon, (const char *ev, const char *name, const char *killer, int depth, int score, int turns, int lvl), {
    try {
        var p = [['g', 'hengband'], ['ev', UTF8ToString(ev)], ['name', UTF8ToString(name)], ['killer', UTF8ToString(killer)],
                 ['depth', depth], ['score', score], ['turns', turns], ['lvl', lvl]];
        var q = p.filter(function (a) { return a[1] !== ''; })
                 .map(function (a) { return a[0] + '=' + encodeURIComponent(a[1]); }).join('&');
        if (window.RvipWM && RvipWM.report) RvipWM.report(q); else fetch('/roguelikes/beacon?' + q, { keepalive: true, mode: 'no-cors' }).catch(function () {});
    } catch (e) {}
});

/*
 * Called from close_game() (core/game-closer.cpp) once the run is over,
 * before kingly()/tombstone. Suicide (Q) and signals die too ("Quitting",
 * "Interrupting", "Abortion"); a winner's retire/seppuku keeps total_winner.
 */
void web_run_end(PlayerType *player_ptr)
{
    const auto &world = AngbandWorld::get_instance();
    std::string k = player_ptr->died_from;
    const char *ev = "death";
    if (world.total_winner) {
        ev = "win", k.clear();
    } else if (k == "Quitting" || k == "Interrupting" || k == "Abortion") {
        ev = "quit", k.clear();
    }
    /* player-damage.cpp take_hit(): "{hallucinatingly distorted }{a monster}{ while paralyzed}" */
    for (std::string_view x : { " while paralyzed", " while being the statue" }) {
        if (k.ends_with(x)) {
            k.erase(k.size() - x.size());
        }
    }
    for (std::string_view x : { "hallucinatingly distorted ", "a ", "an ", "the ", "The " }) {
        if (k.starts_with(x)) {
            k.erase(0, x.size());
        }
    }
    js_beacon(ev, player_ptr->name, k.data(), player_ptr->current_floor_ptr->dun_level, (int)calc_score(player_ptr),
        InnerGameData::get_instance().get_real_turns(world.game_turn), player_ptr->lev);
}

/* Persist the save directories (called after every save) */
void web_sync_files()
{
    js_sync();
}

/* Called from JS when the page is about to be hidden or closed */
extern "C" EMSCRIPTEN_KEEPALIVE void web_request_save()
{
    web_want_save = true;
}

static bool web_at_prompt()
{
    return inkey_flag && AngbandWorld::get_instance().character_generated;
}

/*
 * Resize the terms to the layout the page computed after a browser resize.
 * The main window changes its size only at the command prompt; a
 * cell-size change alone applies at once.
 */
static void web_apply_layout()
{
    auto *old = game_term;
    const auto at_prompt = web_at_prompt();

    for (auto i = 0; i < WEB_TERMS; i++) {
        auto *t = &web_term[i];
        if (!js_layout_pending(i)) {
            continue;
        }

        auto cols = std::max(js_pending_cols(i), 1);
        auto rows = std::max(js_pending_rows(i), 1);
        if (i == 0) {
            cols = std::max(cols, MAIN_TERM_MIN_COLS);
            rows = std::max(rows, MAIN_TERM_MIN_ROWS);
            if (((cols != t->wid) || (rows != t->hgt)) && !at_prompt) {
                continue;
            }
        }

        /* New canvas size and cell size (the canvas starts blank) */
        js_apply_layout(i, cols, rows);
        term_activate(t);
        if ((cols == t->wid) && (rows == t->hgt)) {
            term_redraw();
        } else {
            term_resize(cols, rows);
        }
    }

    term_activate(old);
}

/* Move queued browser input into the main term's key queue */
/* Append a key to the queue (FIFO; term_key_push() puts keys back in front,
 * which reversed multi-key events such as the arrow-key macro triggers) */
static void web_keypress(int k)
{
    auto *t = game_term;
    if (!k) {
        return;
    }

    t->key_queue[t->key_head] = static_cast<char>(k);
    if (++t->key_head == t->key_size) {
        t->key_head = 0;
    }

    if (t->key_head == t->key_tail) { /* overflow: drop the key */
        t->key_head = (t->key_head == 0) ? t->key_size - 1 : t->key_head - 1;
    }
}

static void web_switch_graphics(bool on);

static bool web_pump()
{
    auto got = false;
    auto *old = game_term;

    web_apply_layout();
    term_activate(&web_term[0]);

    int k;
    while ((k = js_next_event(web_at_prompt())) >= 0) {
        /* No mouse support in this variant */
        if (k != 0x10000) {
            web_keypress(k);
        }
        got = true;
    }

    /* Tiles <-> text: only while waiting for a command */
    if (web_at_prompt() && !got) {
        const auto on = js_tiles_switch();
        if ((on >= 0) && ((on != 0) != use_graphics)) {
            web_switch_graphics(on != 0);
            got = true;
        }
    }

    /* Safe autosave: only while waiting for a command */
    if (web_want_save && web_at_prompt() && !p_ptr->is_dead && !got && (game_term->key_head == game_term->key_tail)) {
        web_want_save = false;
        web_keypress(KTRL('S'));
        got = true;
    }

    term_activate(old);
    return got;
}

static void web_yield(int ms)
{
    emscripten_sleep(ms);
    web_last_yield = emscripten_get_now();
}

static errr web_check_events(int wait)
{
    if (web_pump()) {
        return 0;
    }

    if (!wait) {
        /* Let the browser paint now and then during long actions */
        if (emscripten_get_now() - web_last_yield > 50) {
            web_yield(0);
        }
        return web_pump() ? 0 : 1;
    }

    while (true) {
        web_yield(10);
        if (web_pump()) {
            return 0;
        }
    }
}

static void web_react()
{
    for (auto i = 0; i < 16; i++) {
        js_color(i, angband_color_table[i][1], angband_color_table[i][2], angband_color_table[i][3]);
    }
}

static int web_idx()
{
    return static_cast<int>(game_term - web_term);
}

static errr term_xtra_web(int n, int v)
{
    switch (n) {
    case TERM_XTRA_NOISE:
        js_bell();
        return 0;
    case TERM_XTRA_SOUND:
        if ((v > 0) && (v < enum2i(SoundKind::MAX))) {
            const auto it = sound_names.find(i2enum<SoundKind>(v));
            if (it != sound_names.end()) {
                js_sound(it->second.data());
            }
        }
        return 0;
    case TERM_XTRA_FRESH: {
        js_fresh(web_idx());

        /* The page's Sound button is the only switch (off by default) */
        use_sound = true;

        /* The page plays town music at depth 0 */
        const auto generated = AngbandWorld::get_instance().character_generated;
        js_depth((generated && p_ptr->current_floor_ptr) ? p_ptr->current_floor_ptr->dun_level : -1);
        return 0;
    }
    case TERM_XTRA_BORED:
        return web_check_events(0);
    case TERM_XTRA_EVENT:
        return web_check_events(v);
    case TERM_XTRA_FLUSH:
        while (js_next_event(0) >= 0) {
        }
        return 0;
    case TERM_XTRA_CLEAR:
        js_clear(web_idx());
        return 0;
    case TERM_XTRA_DELAY:
        js_fresh(web_idx());
        if (v > 0) {
            web_yield(v);
        }
        return 0;
    case TERM_XTRA_REACT:
        web_react();
        return 0;
    }

    return 1;
}

/* RVIP: no cursor box on the hero's own map cell (the hero is marked already) */
static bool web_cursor_on_hero(TERM_LEN x, TERM_LEN y)
{
    if ((web_idx() != 0) || !AngbandWorld::get_instance().character_generated || !p_ptr || !p_ptr->current_floor_ptr) {
        return false;
    }
    if (AngbandWorld::get_instance().character_icky_depth > 0) {
        return false;
    }
    return (y == p_ptr->y - panel_row_prt) && (x == panel_col_of(p_ptr->x));
}

static errr term_curs_web(TERM_LEN x, TERM_LEN y)
{
    if (!web_cursor_on_hero(x, y)) {
        js_curs(web_idx(), x, y, 1);
    }
    return 0;
}

static errr term_bigcurs_web(TERM_LEN x, TERM_LEN y)
{
    if (!web_cursor_on_hero(x, y)) {
        js_curs(web_idx(), x, y, 2);
    }
    return 0;
}

static errr term_wipe_web(TERM_LEN x, TERM_LEN y, int n)
{
    js_wipe(web_idx(), x, y, n);
    return 0;
}

static errr term_text_web(TERM_LEN x, TERM_LEN y, int n, TERM_COLOR a, concptr s)
{
    js_text(web_idx(), x, y, n, a, s);
    return 0;
}

static errr term_pict_web(TERM_LEN x, TERM_LEN y, int n, const TERM_COLOR *ap, concptr cp, const TERM_COLOR *tap, concptr tcp)
{
    /* A map tile in big-tile mode: the next cell holds the pad (AF_BIGTILE2) */
    const auto &scr = game_term->scr;
    const auto big = use_bigtile && (x + 1 < game_term->wid) && (scr->a[y][x + 1] == 0xF0) && (static_cast<uint8_t>(scr->c[y][x + 1]) == 0xFF);
    js_pict(web_idx(), x, y, n, ap, cp, tap, tcp, big ? 1 : 0);
    return 0;
}

/* Adam Bolt 16x16 tiles (graf-new.prf + graf-ab.prf) in big-tile mode, or text */
static void web_graphics(bool on)
{
    use_graphics = on;
    arg_graphics = on ? 2 : 0; /* GRAPHICS_ADAM_BOLT : GRAPHICS_NONE (main.cpp) */
    ANGBAND_GRAF = on ? "new" : "ascii";
    arg_bigtile = on;
}

/* The page's Tiles button, applied at the command prompt */
static void web_switch_graphics(bool on)
{
    auto *old = game_term;
    web_graphics(on);
    term_activate(&web_term[0]);
    term_resize(game_term->wid, game_term->hgt); /* takes arg_bigtile */
    reset_visuals(p_ptr);
    do_cmd_redraw(p_ptr);
    term_activate(old);
}

static void hook_plog(std::string_view str)
{
    const std::string s(str);
    js_plog(s.data());
}

static void hook_quit(std::string_view str)
{
    for (auto i = WEB_TERMS - 1; i >= 0; i--) {
        term_nuke(&web_term[i]);
    }

    js_sync();
    const std::string s(str);
    js_quit(s.data(), p_ptr->is_dead);
}

/*
 * What each web term shows (the page's TERMS in web/hengband.js). Called
 * from init_other() in place of its X11 defaults, before birth and load;
 * a savefile brings its own flags.
 */
void web_window_flags()
{
    using F = SubWindowRedrawingFlag;
    static const std::vector<F> flags[WEB_TERMS] = {
        {},
        { F::INVENTORY }, /* 1 Inventory */
        { F::MESSAGE }, /* 2 Messages */
        { F::SIGHT_MONSTERS }, /* 3 Visible */
        { F::MONSTER_LORE, F::ITEM_KNOWLEDGE }, /* 4 Recall */
        { F::EQUIPMENT }, /* 5 Equipment */
        { F::FOUND_ITEMS }, /* 6 Objects */
        { F::PLAYER }, /* 7 Character */
    };
    for (auto i = 0; i < WEB_TERMS; i++) {
        g_window_flags[i].clear();
        g_window_flags[i].set(flags[i].begin(), flags[i].end());
    }
}

errr init_web(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    web_react();

    /* Tiles unless the page says text */
    web_graphics(js_tiles_wanted() != 0);
    use_bigtile = arg_bigtile;

    for (auto i = 0; i < WEB_TERMS; i++) {
        auto *t = &web_term[i];
        auto cols = js_term_cols(i);
        auto rows = js_term_rows(i);
        if (i == 0) {
            cols = std::max(cols, MAIN_TERM_MIN_COLS);
            rows = std::max(rows, MAIN_TERM_MIN_ROWS);
        }

        term_init(t, cols, rows, (i == 0) ? 1024 : 16);
        t->soft_cursor = true;
        t->attr_blank = TERM_WHITE;
        t->char_blank = ' ';
        t->xtra_hook = term_xtra_web;
        t->curs_hook = term_curs_web;
        t->bigcurs_hook = term_bigcurs_web;
        t->wipe_hook = term_wipe_web;
        t->text_hook = term_text_web;
        t->pict_hook = term_pict_web;
        t->higher_pict = true;

        term_activate(t);
        angband_terms[i] = t;
    }

    term_activate(&web_term[0]);
    web_last_yield = emscripten_get_now();
    quit_aux = hook_quit;
    plog_aux = hook_plog;
    return 0;
}

#endif /* USE_WEB */
