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
#include "game-option/special-options.h"
#include "io/input-key-acceptor.h"
#include "main/sound-definitions-table.h"
#include "system/angband.h"
#include "system/floor/floor-info.h"
#include "system/player-type-definition.h"
#include "term/gameterm.h"
#include "term/term-color-types.h"
#include "term/z-term.h"
#include "term/z-util.h"
#include "util/enum-converter.h"
#include "util/int-char-converter.h"
#include "world/world.h"
#include <emscripten.h>
#include <string_view>

constexpr auto WEB_TERMS = 6;

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
EM_JS(void, js_quit, (const char *msg), { Module.qb.quit(msg ? UTF8ToString(msg) : ""); });
EM_JS(void, js_plog, (const char *msg), { Module.qb.plog(UTF8ToString(msg)); });
EM_JS(void, js_sync, (void), { Module.qb.sync(); });

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
            term_key_push(k);
        }
        got = true;
    }

    /* Safe autosave: only while waiting for a command */
    if (web_want_save && web_at_prompt() && !p_ptr->is_dead && !got && (game_term->key_head == game_term->key_tail)) {
        web_want_save = false;
        term_key_push(KTRL('S'));
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

static errr term_curs_web(TERM_LEN x, TERM_LEN y)
{
    js_curs(web_idx(), x, y, 1);
    return 0;
}

static errr term_bigcurs_web(TERM_LEN x, TERM_LEN y)
{
    js_curs(web_idx(), x, y, 2);
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
    js_quit(s.data());
}

errr init_web(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    web_react();

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
