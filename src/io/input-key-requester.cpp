#include "io/input-key-requester.h"
#include "cmd-io/cmd-menu-content-table.h"
#include "cmd-io/macro-util.h"
#include "core/asking-player.h" //!< @todo 相互依存している、後で何とかする.
#include "core/player-processor.h"
#include "core/stuff-handler.h"
#include "core/window-redrawer.h"
#include "dungeon/quest.h"
#include "game-option/game-play-options.h"
#include "game-option/input-options.h"
#include "game-option/map-screen-options.h"
#include "inventory/inventory-slot-types.h"
#include "io/cursor.h"
#include "io/input-key-acceptor.h"
#include "io/macro-configurations-store.h"
#include "main/sound-of-music.h"
#include "player-base/player-class.h"
#include "save/save.h"
#include "system/floor/floor-info.h"
#include "system/item/item-entity.h"
#include "system/player-type-definition.h"
#include "term/screen-processor.h" //!< @todo 相互依存している、後で何とかする.
#include "term/term-color-types.h"
#include <algorithm>
#include "util/int-char-converter.h"
#include "util/string-processor.h"
#include "view/display-messages.h"
#include "window/display-sub-windows.h"
#include "window/main-window-util.h"
#include "world/world.h"

bool use_menu;

int16_t command_cmd; /* Current "Angband Command" */
COMMAND_ARG command_arg; /*!< 各種コマンドの汎用的な引数として扱う / Gives argument of current command */
short command_rep; /*!< 各種コマンドの汎用的なリピート数として扱う / Gives repetition of current command */
Direction command_dir = Direction::none(); /*!< 各種コマンドの汎用的な方向値処理として扱う/ Gives direction of current command */
int16_t command_see; /* アイテム使用時等にリストを表示させるかどうか (ゲームオプションの他、様々なタイミングでONになったりOFFになったりする模様……) */
int16_t command_wrk; /* アイテムの使用許可状況 (ex. 装備品のみ、床上もOK等) */
TERM_LEN command_gap = 999; /* アイテムの表示に使う (詳細未調査) */
int16_t command_new; /* Command chaining from inven/equip view */

static char request_command_buffer[256]{};
static bool command_raw = false; /*!< RVIP: command_new came from a menu: no keymap */ /*!< Special buffer to hold the action of the current keymap */

InputKeyRequestor::InputKeyRequestor(PlayerType *player_ptr, bool shopping)
    : player_ptr(player_ptr)
    , shopping(shopping)
    , mode(rogue_like_commands ? KeymapMode::ROGUE : KeymapMode::ORIGINAL)
{
}

/*
 * @brief プレイヤーからのコマンド入力を受け付ける
 */
void InputKeyRequestor::request_command()
{
    command_cmd = 0;
    command_arg = 0;
    command_dir = Direction::none();
    use_menu = false;
    this->process_input_command();
    if (always_repeat && (command_arg <= 0)) {
        if (angband_strchr("TBDoc+", (char)command_cmd)) {
            command_arg = 99;
        }
    }

    this->change_shopping_command();
    this->sweep_confirmation_equipments();
    prt("", 0, 0);
}

void InputKeyRequestor::process_input_command()
{
    while (true) {
        if (fresh_once && macro_running()) {
            stop_term_fresh();
        }

        auto cmd = this->get_command();
        prt("", 0, 0);
        if (this->process_repeat_num(cmd)) {
            continue;
        }

        this->process_command_command(cmd);
        this->process_control_command(cmd);
        const auto &act = keymap_actions_map.at(this->mode).at(static_cast<uint8_t>(cmd));
        if (act && !inkey_next) {
            angband_strcpy(request_command_buffer, *act, sizeof(request_command_buffer));
            inkey_next = request_command_buffer;
            continue;
        }

        if (cmd == 0) {
            continue;
        }

        command_cmd = (byte)cmd;
        break;
    }
}

short InputKeyRequestor::get_command()
{
    if (command_new) {
        msg_erase();
        auto cmd_back = command_new;
        command_new = 0;
        if (command_raw && (inkey_next == nullptr)) {
            inkey_next = ""; /* RVIP: a command from a menu skips the keymaps */
        }

        command_raw = false;
        return cmd_back;
    }

    msg_flag = false;
    num_more = 0;
    inkey_flag = true;
    term_fresh();
    short cmd = inkey(true);
    if (!this->shopping && command_menu && ((cmd == '\r') || (cmd == '\n') || (cmd == 'x')) && !keymap_actions_map[this->mode][(byte)(cmd)]) {
        cmd = this->inkey_from_menu();
    }

    return cmd;
}

/*** RVIP: boxed menus and the command menu on Enter (Zangband's cmd_menu()) ***/

void queue_raw_command(char cmd)
{
    command_new = cmd;
    command_raw = true;
}

/* The key that gives underlying command cmd in the current keyset, or 0 */
char command_key(char cmd)
{
    const auto &keymap = keymap_actions_map.at(rogue_like_commands ? KeymapMode::ROGUE : KeymapMode::ORIGINAL);
    if (!keymap.at(static_cast<uint8_t>(cmd))) {
        return cmd;
    }

    for (auto k = 1; k < 256; k++) {
        const auto &act = keymap.at(static_cast<uint8_t>(k));
        if (act && (act->size() == 1) && ((*act)[0] == cmd)) {
            return static_cast<char>(k);
        }
    }

    return 0;
}

/* The same as text: "q", "^D", or "" */
std::string command_key_str(char cmd)
{
    const auto k = static_cast<uint8_t>(command_key(cmd));
    if (k == 0) {
        return "";
    }

    return (k < 32) ? std::string{ '^', static_cast<char>(k + 64) } : std::string(1, static_cast<char>(k));
}

/*
 * Draw a boxed menu at (x, y), exactly as big as its content (moved left/up
 * to fit the screen).  x becomes the column right of the box.  Returns the
 * row of the first entry.
 */
int box_draw(int &x, int &y, std::string_view title, const std::vector<std::string> &text, int cur)
{
    const auto &[wid, hgt] = term_get_size();
    auto w = title.empty() ? 0 : static_cast<int>(title.size()) + 1;
    for (const auto &t : text) {
        w = std::max(w, static_cast<int>(t.size()) + 3);
    }

    w = std::min(w, wid - 2);
    const auto h = static_cast<int>(text.size()) + (title.empty() ? 0 : 1);

    /* ponytail: no scrolling, every menu here is shorter than the screen */
    x = std::max(std::min(x, wid - w - 2), 0);
    y = std::max(std::min(y, hgt - h - 2), 0);
    const auto top = y + 1 + (title.empty() ? 0 : 1);
    const auto edge = "+" + std::string(w, '-') + "+";
    c_put_str(TERM_WHITE, edge, y, x);
    c_put_str(TERM_WHITE, edge, y + h + 1, x);
    for (auto i = 1; i <= h; i++) {
        c_put_str(TERM_WHITE, "|" + std::string(w, ' ') + "|", y + i, x);
    }

    if (!title.empty()) {
        c_put_str(TERM_YELLOW, title.substr(0, w), y + 1, x + 1);
    }

    for (auto i = 0; i < static_cast<int>(text.size()); i++) {
        const auto line = ((i == cur) ? "> " : "  ") + text[i];
        c_put_str((i == cur) ? TERM_L_BLUE : TERM_WHITE, line.substr(0, w), top + i, x + 1);
    }

    x += w + 2;
    return top;
}

/*
 * A boxed menu (see box_draw()).  keys[i] (0 = none) chooses entry i
 * directly.  2/8 (arrows) move, Enter/Space/5/6 choose, Escape/0/4 go back.
 * Returns the chosen entry or -1.
 */
int box_menu(int x, int y, std::string_view title, const std::vector<std::string> &text, const std::string &keys, int cur)
{
    const auto n = static_cast<int>(text.size());
    if (n <= 0) {
        return -1;
    }

    if ((cur < 0) || (cur >= n)) {
        cur = 0;
    }

    screen_save();
    while (true) {
        auto bx = x;
        auto by = y;
        (void)box_draw(bx, by, title, text, cur);
        const auto k = inkey();
        const auto pos = (k != 0) ? keys.find(k) : std::string::npos;
        if ((pos != std::string::npos) && (static_cast<int>(pos) < n)) {
            cur = static_cast<int>(pos);
            break;
        }

        if ((k == ESCAPE) || (k == '0') || (k == '4')) {
            cur = -1;
            break;
        }

        if ((k == '\r') || (k == '\n') || (k == ' ') || (k == '5') || (k == '6')) {
            break;
        }

        if (k == '8') {
            cur = (cur + n - 1) % n;
        }

        if (k == '2') {
            cur = (cur + 1) % n;
        }
    }

    screen_load();
    return cur;
}

/*
 * The command menu: groups, then the group's commands with their keys in
 * the current keyset.  Returns an underlying command or ESCAPE.
 */
char InputKeyRequestor::inkey_from_menu()
{
    static int group = 0;
    std::vector<std::string> gtext;
    std::string gkeys;
    std::vector<size_t> gstart;
    for (size_t i = 0; i < menu_info.size(); i++) {
        if (menu_info[i].cmd) {
            continue;
        }

        const auto label = static_cast<char>(I2A(gstart.size()));
        gstart.push_back(i + 1);
        gtext.push_back(std::string{ label, ')', ' ' } + menu_info[i].name);
        gkeys += label;
    }

    prt("", 0, 0);
    while (true) {
        group = box_menu(1, 1, _("コマンド", "Commands"), gtext, gkeys, group);
        if (group < 0) {
            group = 0;
            return ESCAPE;
        }

        size_t nw = 0;
        auto end = gstart[group];
        for (; (end < menu_info.size()) && menu_info[end].cmd; end++) {
            nw = std::max(nw, std::string_view(menu_info[end].name).size());
        }

        std::vector<std::string> ctext;
        std::string ckeys, cmds;
        for (auto i = gstart[group]; i < end; i++) {
            const auto cmd = static_cast<char>(menu_info[i].cmd);
            auto name = std::string(menu_info[i].name);
            name.resize(nw, ' ');
            cmds += cmd;
            ckeys += command_key(cmd);
            ctext.push_back(name + "  " + command_key_str(cmd));
        }

        /* The group box stays under the command box */
        screen_save();
        auto bx = 1;
        auto by = 1;
        (void)box_draw(bx, by, _("コマンド", "Commands"), gtext, group);
        const auto chosen = box_menu(bx, by + 1 + group, menu_info[gstart[group] - 1].name, ctext, ckeys, 0);
        screen_load();
        if (chosen >= 0) {
            use_menu = true;
            if (inkey_next == nullptr) {
                inkey_next = ""; /* underlying command: no keymap */
            }

            return cmds[chosen];
        }
    }
}

char InputKeyRequestor::input_repeat_num()
{
    while (true) {
        auto cmd = inkey();
        if ((cmd == 0x7F) || (cmd == KTRL('H'))) {
            command_arg = command_arg / 10;
            prt(format(_("回数: %d", "Count: %d"), command_arg), 0, 0);
            continue;
        }

        if (is_numeric(cmd)) {
            if (command_arg >= 1000) {
                bell();
                command_arg = 9999;
            } else {
                command_arg = command_arg * 10 + D2I(cmd);
            }

            prt(format(_("回数: %d", "Count: %d"), command_arg), 0, 0);
            continue;
        }

        return cmd;
    }
}

bool InputKeyRequestor::process_repeat_num(short &cmd)
{
    if (cmd != '0') {
        return false;
    }

    auto old_arg = command_arg;
    command_arg = 0;
    prt(_("回数: ", "Count: "), 0, 0);
    cmd = this->input_repeat_num();
    if (command_arg == 0) {
        command_arg = 99;
        prt(format(_("回数: %d", "Count: %d"), command_arg), 0, 0);
    }

    if (old_arg != 0) {
        command_arg = old_arg;
        prt(format(_("回数: %d", "Count: %d"), command_arg), 0, 0);
    }

    if ((cmd != ' ') && (cmd != '\n') && (cmd != '\r')) {
        return false;
    }

    const auto ret_cmd = input_command(_("コマンド: ", "Command: "));
    cmd = ret_cmd.value_or(ESCAPE);
    command_arg = 0;
    return true;
}

/*
 * @brief コマンドの入力を求めるコマンドの処理
 * @param cmd 入力コマンド
 * @details 全く意味がないような気もするが元のコードにあった機能は保持しておく
 */
void InputKeyRequestor::process_command_command(short &cmd)
{
    if (cmd != '\\') {
        return;
    }

    const auto new_command = input_command(_("コマンド: ", "Command: "));
    cmd = new_command.value_or(ESCAPE);
    if (inkey_next == nullptr) {
        inkey_next = "";
    }
}

void InputKeyRequestor::process_control_command(short &cmd)
{
    if (cmd != '^') {
        return;
    }

    const auto new_command = input_command(_("CTRL: ", "Control: "));
    const auto is_input = new_command.has_value();
    cmd = new_command.value_or(ESCAPE);
    if (is_input) {
        cmd = KTRL(cmd);
    }
}

void InputKeyRequestor::change_shopping_command() const
{
    if (!this->shopping) {
        return;
    }

    switch (command_cmd) {
    case 'p':
        command_cmd = 'g';
        return;
    case 'm':
        command_cmd = 'g';
        return;
    case 's':
        command_cmd = 'd';
        return;
    }
}

int InputKeyRequestor::get_caret_command() const
{
#ifdef JP
    auto caret_command = 0;
    for (auto i = 0; i < 256; i++) {
        const auto &action_opt = keymap_actions_map.at(this->mode).at(static_cast<uint8_t>(i));
        if (!action_opt) {
            continue;
        }

        const auto &action = *action_opt;
        if ((action[0] == command_cmd) && (action[1] == '\0')) {
            caret_command = i;
            break;
        }
    }

    if (caret_command == 0) {
        caret_command = command_cmd;
    }

    return caret_command;
#else
    return 0;
#endif
}

void InputKeyRequestor::sweep_confirmation_equipments()
{
    auto caret_command = this->get_caret_command();
    for (const auto i_idx : INVEN_WIELDING_SLOTS) {
        auto &item = *this->player_ptr->inventory[i_idx];
        if (!item.is_valid() || !item.is_inscribed()) {
            continue;
        }

        this->confirm_command(item.inscription, caret_command);
    }
}

void InputKeyRequestor::confirm_command(const tl::optional<std::string> &inscription, const int caret_command)
{
    if (!inscription) {
        return;
    }

    auto s = inscription->data();
    s = angband_strchr(s, '^');
    while (s != nullptr) {
#ifdef JP
        auto sure = s[1] == caret_command;
#else
        auto sure = s[1] == command_cmd;
        (void)caret_command;
#endif
        if (sure) {
            if (!input_check(_("本当ですか? ", "Are you sure? "))) {
                command_cmd = ' ';
            }
        }

        s = angband_strchr(s + 1, '^');
    }
}
