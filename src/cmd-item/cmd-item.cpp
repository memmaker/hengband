/*!
 *  @brief プレイヤーのアイテムに関するコマンドの実装1 / Inventory and equipment commands
 *  @date 2014/01/02
 *  @author
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 *
 * This software may be copied and distributed for educational, research,
 * and not for profit purposes provided that this copyright and statement
 * are included in all such copies.  Other copyrights may also apply.
 */

#include "cmd-item/cmd-item.h"
#include "action/action-limited.h"
#include "action/activation-execution.h"
#include "action/weapon-shield.h"
#include "cmd-action/cmd-open-close.h"
#include "cmd-action/cmd-pet.h"
#include "cmd-item/cmd-eat.h"
#include "cmd-item/cmd-quaff.h"
#include "cmd-item/cmd-read.h"
#include "cmd-item/cmd-zaprod.h"
#include "cmd-item/cmd-zapwand.h"
#include "combat/shoot.h"
#include "core/asking-player.h"
#include "core/window-redrawer.h"
#include "flavor/flavor-describer.h"
#include "flavor/object-flavor-types.h"
#include "floor/floor-object.h"
#include "game-option/input-options.h"
#include "inventory/inventory-object.h"
#include "inventory/inventory-slot-types.h"
#include "io/input-key-acceptor.h"
#include "io/input-key-requester.h"
#include "locale/japanese.h"
#include "mind/snipe-types.h"
#include "object-activation/activation-switcher.h"
#include "object-hook/hook-magic.h"
#include "object-use/quaff/quaff-execution.h"
#include "object-use/read/read-execution.h"
#include "object-use/use-execution.h"
#include "object-use/zaprod-execution.h"
#include "object-use/zapwand-execution.h"
#include "object/item-tester-hooker.h"
#include "object/item-use-flags.h"
#include "inventory/floor-item-getter.h"
#include "inventory/inventory-util.h"
#include "object-hook/hook-armor.h"
#include "object-hook/hook-expendable.h"
#include "object/object-info.h"
#include "sv-definition/sv-lite-types.h"
#include "term/term-color-types.h"
#include "util/enum-converter.h"
#include "util/finalizer.h"
#include <algorithm>
#include "perception/identification.h"
#include "perception/object-perception.h"
#include "player-base/player-class.h"
#include "player-info/class-info.h"
#include "player-info/race-types.h"
#include "player-info/samurai-data-type.h"
#include "player-info/self-info.h"
#include "player-status/player-energy.h"
#include "player/attack-defense-types.h"
#include "player/player-personality-types.h"
#include "player/player-status.h"
#include "player/special-defense-types.h"
#include "racial/racial-android.h"
#include "realm/realm-hex-numbers.h"
#include "realm/realm-types.h"
#include "status/action-setter.h"
#include "system/item/item-entity.h"
#include "system/player-type-definition.h"
#include "system/redrawing-flags-updater.h"
#include "term/screen-processor.h"
#include "term/z-form.h"
#include "util/bit-flags-calculator.h"
#include "util/int-char-converter.h"
#include "util/string-processor.h"
#include "view/display-inventory.h"
#include "view/display-messages.h"
#include "world/world.h"

/************************************************************************
 * RVIP: item menus in the inventory/equipment list (i/e)
 * Letter = main action, Shift+letter = drop, Ctrl+letter = examine,
 * 2/8 move the cursor, 4/6 switch list, Enter/Space/5 = menu of every action
 * that fits the cursor's item, + - * = main/drop/examine of that item,
 * Esc/0/. close, any other key is a normal command.  An action runs the
 * game's own command (queued past the keymaps) with the item preselected
 * for its first item prompt (item_preselect in get_item_floor()); the list
 * reopens afterwards (player-processor.cpp) unless a hostile is in view.
 ***********************************************************************/
char gear_reopen = 0; /* 'i' or 'e': reopen that list after the action */

namespace {
bool can_refuel(PlayerType *player_ptr, const ItemEntity &item)
{
    const auto &lite = *player_ptr->inventory[INVEN_LITE];
    if (lite.bi_key.tval() != ItemKindType::LITE) {
        return false;
    }

    if (lite.bi_key.sval() == SV_LITE_LANTERN) {
        return item.can_refill_lantern();
    }

    return (lite.bi_key.sval() == SV_LITE_TORCH) && item.can_refill_torch();
}

bool is_book(PlayerType *player_ptr, const ItemEntity &item)
{
    return item.is_spell_book() && check_book_realm(player_ptr, item.bi_key);
}

bool is_tval(const ItemEntity &item, ItemKindType tval)
{
    return item.bi_key.tval() == tval;
}

struct GearAction {
    char key; /* underlying command (keyset 0) */
    const char *name;
    BIT_FLAGS where; /* USE_INVEN / USE_EQUIP, as the command's own choose_item() */
    bool (*test)(PlayerType *, const ItemEntity &); /* the command's own item tester */
    bool main; /* candidate for the letter's main action (first fit wins) */
};

/* Main actions first, in the order the main action is picked */
const GearAction gear_actions[] = {
    { 'q', "Quaff", USE_INVEN, [](auto p, auto &o) { return item_tester_hook_quaff(p, &o); }, true },
    { 'r', "Read", USE_INVEN, [](auto, auto &o) { return o.is_readable(); }, true },
    { 'u', "Use", USE_INVEN, [](auto, auto &o) { return is_tval(o, ItemKindType::STAFF); }, true },
    { 'a', "Aim", USE_INVEN, [](auto, auto &o) { return is_tval(o, ItemKindType::WAND); }, true },
    { 'z', "Zap", USE_INVEN, [](auto, auto &o) { return is_tval(o, ItemKindType::ROD); }, true },
    { 'E', "Eat", USE_INVEN, [](auto p, auto &o) { return item_tester_hook_eatable(p, &o); }, true }, /* after the devices: some races eat staffs and wands */
    { 'm', "Cast a spell", USE_INVEN, [](auto p, auto &o) { return is_book(p, o); }, true },
    { 'w', "Wear/wield", USE_INVEN, [](auto p, auto &o) { return item_tester_hook_wear(p, &o); }, true },
    { 't', "Take off", USE_EQUIP, [](auto, auto &) { return true; }, true },
    { 'F', "Refuel", USE_INVEN, [](auto p, auto &o) { return can_refuel(p, o); }, true },
    { 'b', "Browse", USE_INVEN, [](auto p, auto &o) { return is_book(p, o); }, false },
    { 'A', "Activate", USE_EQUIP, [](auto, auto &o) { return o.is_activatable(); }, false },
    { 'f', "Fire", USE_INVEN, [](auto p, auto &o) { return (p->tval_ammo != ItemKindType::NONE) && is_tval(o, p->tval_ammo); }, false },
    { 'v', "Throw", USE_INVEN | USE_EQUIP, [](auto, auto &) { return true; }, false },
    { 'd', "Drop", USE_INVEN | USE_EQUIP, [](auto, auto &) { return true; }, false },
    { 'k', "Destroy", USE_INVEN, [](auto, auto &) { return true; }, false },
    { '{', "Inscribe", USE_INVEN | USE_EQUIP, [](auto, auto &) { return true; }, false },
    { '}', "Uninscribe", USE_INVEN | USE_EQUIP, [](auto, auto &o) { return o.is_inscribed(); }, false },
    { 'I', "Examine", USE_INVEN | USE_EQUIP, [](auto, auto &) { return true; }, false },
};

bool gear_action_ok(PlayerType *player_ptr, const GearAction &act, short i_idx)
{
    const auto &item = *player_ptr->inventory[i_idx];
    const auto where = (i_idx >= INVEN_MAIN_HAND) ? USE_EQUIP : USE_INVEN;
    return item.is_valid() && (act.where & where) && act.test(player_ptr, item);
}

char gear_main(PlayerType *player_ptr, short i_idx)
{
    for (const auto &act : gear_actions) {
        if (act.main && gear_action_ok(player_ptr, act, i_idx)) {
            return act.key;
        }
    }

    return 'I';
}

/* The action menu: a box left of the list, at the item's row; returns a command or 0 */
char gear_menu(PlayerType *player_ptr, short i_idx, int row)
{
    std::vector<std::string> text;
    std::string keys, cmds;
    for (const auto &act : gear_actions) {
        if (!gear_action_ok(player_ptr, act, i_idx)) {
            continue;
        }

        auto name = std::string(act.name);
        name.resize(12, ' ');
        cmds += act.key;
        keys += command_key(act.key);
        text.push_back(name + command_key_str(act.key));
    }

    auto title = describe_flavor(player_ptr, *player_ptr->inventory[i_idx], 0);
    if (title.size() > 40) {
        title.resize(40);
    }

    const auto chosen = box_menu(1, row, title, text, keys, 0);
    return (chosen < 0) ? 0 : cmds[chosen];
}
}

/*!
 * @brief The inventory (equip = false) or equipment list with a cursor and item menus
 */
void gear_ui(PlayerType *player_ptr, bool equip)
{
    static int cursor = 0;
    while (true) {
        command_wrk = equip ? true : false;
        if (easy_floor) {
            command_wrk = equip ? USE_EQUIP : USE_INVEN;
        }

        /* Rows: every equipment slot, or the (packed) inventory */
        auto rows = 0;
        const auto first = equip ? static_cast<short>(INVEN_MAIN_HAND) : static_cast<short>(0);
        if (equip) {
            rows = INVEN_TOTAL - INVEN_MAIN_HAND;
        } else {
            for (const auto i_idx : INVEN_PACK_SLOTS) {
                if (player_ptr->inventory[i_idx]->is_valid()) {
                    rows = enum2i(i_idx) + 1;
                }
            }
        }

        cursor = (rows > 0) ? std::clamp(cursor, 0, rows - 1) : 0;
        screen_save();
        const auto restore = util::make_finalizer([] { screen_load(); }); /* the list stays under the action menu */
        if (equip) {
            (void)show_equipment(player_ptr, 0, USE_FULL, AllMatchItemTester());
        } else {
            (void)show_inventory(player_ptr, 0, USE_FULL, AllMatchItemTester());
        }

        if ((rows > 0) && (command_gap >= 1)) {
            c_put_str(TERM_L_BLUE, ">", cursor + 1, command_gap - 1);
        }

        const auto weight = calc_inventory_weight(player_ptr);
        const auto weight_lim = calc_weight_limit(player_ptr);
        const auto percentage = weight * 100 / weight_lim;
#ifdef JP
        const auto mes = format("%s： 合計 %3d.%1d kg (限界の%d%%) 文字:使う Shift:落とす Ctrl:調べる Enter:メニュー", equip ? "装備" : "持ち物", lb_to_kg_integer(weight), lb_to_kg_fraction(weight), percentage);
#else
        const auto mes = format("%s (%d.%d lb, %d%%): letter use, Shift drop, Ctrl examine, Enter menu",
            equip ? "Equipment" : "Inventory", weight / 10, weight % 10, percentage);
#endif
        prt(mes, 0, 0);
        const auto key = inkey();

        const auto labels = prepare_label_string(player_ptr, equip ? USE_EQUIP : USE_INVEN, AllMatchItemTester());
        auto label_slot = [&](char c) -> short {
            const auto pos = labels.find(c);
            return ((pos == std::string::npos) || (static_cast<int>(pos) >= rows)) ? -1 : static_cast<short>(first + pos);
        };

        short i_idx = -1;
        char cmd = 0;
        const auto ukey = static_cast<unsigned char>(key);
        if ((key == ESCAPE) || (key == '0') || (key == '.')) {
            if (key == ESCAPE) {
                const auto &[wid, hgt] = term_get_size();
                command_gap = wid - 30;
            }

            return;
        } else if ((key == '2') || (key == '8')) {
            if (rows > 0) {
                cursor = (cursor + ((key == '2') ? 1 : rows - 1)) % rows;
            }

            continue;
        } else if ((key == '4') || (key == '6')) {
            equip = !equip;
            cursor = 0;
            continue;
        } else if ((key == '\r') || (key == '\n') || (key == ' ') || (key == '5') || (key == '+') || (key == '-') || (key == '*')) {
            if (rows == 0) {
                continue;
            }

            i_idx = first + cursor;
            if (!player_ptr->inventory[i_idx]->is_valid()) {
                continue;
            }

            if (key == '+') {
                cmd = gear_main(player_ptr, i_idx);
            } else if (key == '-') {
                cmd = 'd';
            } else if (key == '*') {
                cmd = 'I';
            } else {
                cmd = gear_menu(player_ptr, i_idx, cursor + 1);
                if (!cmd) {
                    continue;
                }
            }
        } else if ((i_idx = label_slot(key)) >= 0) {
            cmd = gear_main(player_ptr, i_idx);
        } else if ((ukey < 128) && isupper(ukey) && ((i_idx = label_slot(static_cast<char>(tolower(ukey)))) >= 0)) {
            cmd = 'd';
        } else if ((ukey >= 1) && (ukey <= 26) && ((i_idx = label_slot(static_cast<char>(ukey + 'a' - 1))) >= 0)) {
            cmd = 'I';
        } else {
            /* Any other key is a normal command, as before */
            command_new = key;
            command_see = true;
            return;
        }

        if ((i_idx < 0) || !player_ptr->inventory[i_idx]->is_valid()) {
            continue;
        }

        cursor = i_idx - first;
        item_preselect = i_idx;
        queue_raw_command(cmd);
        gear_reopen = equip ? 'e' : 'i';
        return;
    }
}

/*!
 * @brief 持ち物一覧を表示するコマンドのメインルーチン / Display inventory_list
 */
void do_cmd_inven(PlayerType *player_ptr)
{
    gear_ui(player_ptr, false);
}

/*!
 * @brief アイテムを落とすコマンドのメインルーチン / Drop an item
 */
void do_cmd_drop(PlayerType *player_ptr)
{
    int amt = 1;
    PlayerClass(player_ptr).break_samurai_stance({ SamuraiStanceType::MUSOU });

    constexpr auto q = _("どのアイテムを落としますか? ", "Drop which item? ");
    constexpr auto s = _("落とせるアイテムを持っていない。", "You have nothing to drop.");
    const auto &[item, i_idx] = choose_item(player_ptr, q, s, (USE_EQUIP | USE_INVEN | IGNORE_BOTHHAND_SLOT));
    if (!item) {
        return;
    }

    if ((i_idx >= INVEN_MAIN_HAND) && item->is_cursed()) {
        msg_print(_("ふーむ、どうやら呪われているようだ。", "Hmmm, it seems to be cursed."));
        return;
    }

    if (item->number > 1) {
        amt = input_quantity(item->number);
        if (amt <= 0) {
            return;
        }
    }

    PlayerEnergy(player_ptr).set_player_turn_energy(50);
    drop_from_inventory(player_ptr, i_idx, amt);
    if (i_idx >= INVEN_MAIN_HAND) {
        verify_equip_slot(player_ptr, i_idx);
        calc_android_exp(player_ptr);
    }

    RedrawingFlagsUpdater::get_instance().set_flag(MainWindowRedrawingFlag::EQUIPPY);
}

/*!
 * @brief アイテムを調査するコマンドのメインルーチン / Observe an item which has been *identify*-ed
 */
void do_cmd_observe(PlayerType *player_ptr)
{
    constexpr auto q = _("どのアイテムを調べますか? ", "Examine which item? ");
    constexpr auto s = _("調べられるアイテムがない。", "You have nothing to examine.");
    const auto &[item, i_idx] = choose_item(player_ptr, q, s, (USE_EQUIP | USE_INVEN | USE_FLOOR | IGNORE_BOTHHAND_SLOT));
    if (!item) {
        return;
    }

    if (!item->is_fully_known()) {
        msg_print(_("このアイテムについて特に知っていることはない。", "You have no special knowledge about that item."));
        return;
    }

    const auto item_name = describe_flavor(player_ptr, *item, 0);
    msg_format(_("%sを調べている...", "Examining %s..."), item_name.data());
    if (!screen_object(player_ptr, *item, SCROBJ_FORCE_DETAIL)) {
        msg_print(_("特に変わったところはないようだ。", "You see nothing special."));
    }
}

/*!
 * @brief アイテムの銘を消すコマンドのメインルーチン
 * Remove the inscription from an object XXX Mention item (when done)?
 */
void do_cmd_uninscribe(PlayerType *player_ptr)
{
    constexpr auto q = _("どのアイテムの銘を消しますか? ", "Un-inscribe which item? ");
    constexpr auto s = _("銘を消せるアイテムがない。", "You have nothing to un-inscribe.");
    const auto &[item, i_idx] = choose_item(player_ptr, q, s, (USE_EQUIP | USE_INVEN | USE_FLOOR | IGNORE_BOTHHAND_SLOT));
    if (!item) {
        return;
    }

    if (!item->is_inscribed()) {
        msg_print(_("このアイテムには消すべき銘がない。", "That item had no inscription to remove."));
        return;
    }

    msg_print(_("銘を消した。", "Inscription removed."));
    item->inscription.reset();
    auto &rfu = RedrawingFlagsUpdater::get_instance();
    static constexpr auto flags_srf = {
        StatusRecalculatingFlag::COMBINATION,
        StatusRecalculatingFlag::BONUS,
    };
    rfu.set_flags(flags_srf);
    static constexpr auto flags_swrf = {
        SubWindowRedrawingFlag::INVENTORY,
        SubWindowRedrawingFlag::EQUIPMENT,
        SubWindowRedrawingFlag::FLOOR_ITEMS,
        SubWindowRedrawingFlag::FOUND_ITEMS,
    };
    rfu.set_flags(flags_swrf);
}

/*!
 * @brief アイテムの銘を刻むコマンドのメインルーチン
 * Inscribe an object with a comment
 */
void do_cmd_inscribe(PlayerType *player_ptr)
{
    constexpr auto q = _("どのアイテムに銘を刻みますか? ", "Inscribe which item? ");
    constexpr auto s = _("銘を刻めるアイテムがない。", "You have nothing to inscribe.");
    const auto &[item, i_idx] = choose_item(player_ptr, q, s, (USE_EQUIP | USE_INVEN | USE_FLOOR | IGNORE_BOTHHAND_SLOT));
    if (!item) {
        return;
    }

    const auto item_name = describe_flavor(player_ptr, *item, OD_OMIT_INSCRIPTION);
    msg_format(_("%sに銘を刻む。", "Inscribing %s."), item_name.data());
    msg_erase();
    const auto initial_inscription = item->is_inscribed() ? *item->inscription : "";
    const auto input_inscription = input_string(_("銘: ", "Inscription: "), MAX_INSCRIPTION, initial_inscription);
    if (!input_inscription) {
        return;
    }

    item->inscription.emplace(*input_inscription);
    auto &rfu = RedrawingFlagsUpdater::get_instance();
    static constexpr auto flags_srf = {
        StatusRecalculatingFlag::COMBINATION,
        StatusRecalculatingFlag::BONUS,
    };
    rfu.set_flags(flags_srf);
    static constexpr auto flags_swrf = {
        SubWindowRedrawingFlag::INVENTORY,
        SubWindowRedrawingFlag::EQUIPMENT,
        SubWindowRedrawingFlag::FLOOR_ITEMS,
        SubWindowRedrawingFlag::FOUND_ITEMS,
    };
    rfu.set_flags(flags_swrf);
}

/*!
 * @brief アイテムを汎用的に「使う」コマンドのメインルーチン /
 * Use an item
 * @details
 * XXX - Add actions for other item types
 */
void do_cmd_use(PlayerType *player_ptr)
{
    if (AngbandWorld::get_instance().is_wild_mode() || cmd_limit_arena(player_ptr)) {
        return;
    }

    PlayerClass(player_ptr).break_samurai_stance({ SamuraiStanceType::MUSOU, SamuraiStanceType::KOUKIJIN });
    constexpr auto q = _("どれを使いますか？", "Use which item? ");
    constexpr auto s = _("使えるものがありません。", "You have nothing to use.");
    const auto options = USE_INVEN | USE_EQUIP | USE_FLOOR | IGNORE_BOTHHAND_SLOT;
    const auto &[item, i_idx] = choose_item(player_ptr, q, s, options, FuncItemTester(item_tester_hook_use, player_ptr));
    if (!item) {
        return;
    }

    switch (item->bi_key.tval()) {
    case ItemKindType::SPIKE:
        do_cmd_spike(player_ptr);
        break;
    case ItemKindType::FOOD:
        exe_eat_food(player_ptr, i_idx);
        break;
    case ItemKindType::WAND:
        ObjectZapWandEntity(player_ptr).execute(i_idx);
        break;
    case ItemKindType::STAFF:
        ObjectUseEntity(player_ptr, i_idx).execute();
        break;
    case ItemKindType::ROD:
        ObjectZapRodEntity(player_ptr).execute(i_idx);
        break;
    case ItemKindType::POTION:
        ObjectQuaffEntity(player_ptr).execute(i_idx);
        break;
    case ItemKindType::SCROLL:
        if (cmd_limit_blind(player_ptr) || cmd_limit_confused(player_ptr)) {
            return;
        }

        ObjectReadEntity(player_ptr, i_idx).execute(true);
        break;
    case ItemKindType::SHOT:
    case ItemKindType::ARROW:
    case ItemKindType::BOLT:
        exe_fire(player_ptr, i_idx, player_ptr->inventory[INVEN_BOW].get(), SP_NONE);
        break;
    default:
        exe_activate(player_ptr, i_idx);
        break;
    }
}

/*!
 * @brief 装備を発動するコマンドのメインルーチン /
 * @param player_ptr プレイヤーへの参照ポインタ
 */
void do_cmd_activate(PlayerType *player_ptr)
{
    if (AngbandWorld::get_instance().is_wild_mode() || cmd_limit_arena(player_ptr)) {
        return;
    }

    PlayerClass(player_ptr).break_samurai_stance({ SamuraiStanceType::MUSOU, SamuraiStanceType::KOUKIJIN });
    constexpr auto q = _("どのアイテムを始動させますか? ", "Activate which item? ");
    constexpr auto s = _("始動できるアイテムを装備していない。", "You have nothing to activate.");
    const auto &[item, i_idx] = choose_item(player_ptr, q, s, (USE_EQUIP | IGNORE_BOTHHAND_SLOT), FuncItemTester(&ItemEntity::is_activatable));
    if (!item) {
        return;
    }

    exe_activate(player_ptr, i_idx);
}
