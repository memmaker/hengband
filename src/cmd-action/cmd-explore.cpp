/*!
 * @brief RVIP: auto-explore ('X') and walking to stairs ('<' / '>')
 * @details
 * Port of FrogComposband's explore (end of its cmd2.c).  Walks one step per
 * turn towards the nearest known grid next to an unknown one (known =
 * CAVE_MARK or CAVE_KNOWN, which the game never forgets), or to a found
 * item not stood on yet.  Stops on disturb() (keys, monsters moving in
 * view, ...), any new message (msg_print()), a visible hostile, a step that
 * did not move, no light, or nothing left.  Never picks locks.  '<' / '>'
 * off the right stairs pick the nearest known staircase and let the game's
 * Travel walk there; process_player() takes them on arrival.
 */
#include "cmd-action/cmd-explore.h"
#include "action/movement-execution.h"
#include "action/open-close-execution.h"
#include "action/travel-execution.h"
#include "action/tunnel-execution.h"
#include "cmd-action/cmd-move.h"
#include "core/stuff-handler.h"
#include "floor/geometry.h"
#include "game-option/input-options.h"
#include "locale/language-switcher.h"
#include "monster/monster-describer.h"
#include "object/object-mark-types.h"
#include "grid/grid.h"
#include "player-status/player-energy.h"
#include "system/floor/floor-info.h"
#include "system/floor/wilderness-grid.h"
#include "system/grid-type-definition.h"
#include "system/item/item-entity.h"
#include "system/monster-entity.h"
#include "system/player-type-definition.h"
#include "system/terrain/terrain-definition.h"
#include "term/z-term.h"
#include "timed-effect/timed-effects.h"
#include "view/display-messages.h"
#include "world/world.h"
#include <set>

bool auto_explore = false;

/* Heading for stairs by travel: 1 up, -1 down, 0 none */
int explore_stairs = 0;

namespace {
/* Found items the player has stood on (this level) */
std::set<const ItemEntity *> explore_visited;

bool explore_in(const FloorType &floor, const Pos2D &pos)
{
    return floor.contains(pos, floor.is_underground() ? FloorBoundary::OUTER_WALL_INCLUSIVE : FloorBoundary::OUTER_WALL_EXCLUSIVE);
}

bool explore_known(const FloorType &floor, const Pos2D &pos)
{
    const auto &grid = floor.get_grid(pos);
    return grid.is_mark() || (grid.info & CAVE_KNOWN);
}

/* What the player believes is there */
const TerrainType &explore_terrain(const FloorType &floor, const Pos2D &pos)
{
    return floor.get_grid(pos).get_terrain(TerrainKind::MIMIC);
}

/* Door we won't open: locked or stuck (true terrain) */
bool explore_locked_door(const FloorType &floor, const Pos2D &pos)
{
    const auto &grid = floor.get_grid(pos);
    if (!grid.is_closed_door(true)) {
        return false;
    }

    const auto &terrain = grid.get_terrain();
    return terrain.flags.has_not(TerrainCharacteristics::OPEN) || terrain.door_power;
}

bool explore_rubble(const FloorType &floor, const Pos2D &pos)
{
    const auto &flags = explore_terrain(floor, pos).flags;
    return flags.has(TerrainCharacteristics::CAN_DIG) && !flags.has_any_of({ TerrainCharacteristics::WALL, TerrainCharacteristics::MOVE });
}

bool explore_passable(PlayerType *player_ptr, const Pos2D &pos)
{
    const auto &floor = *player_ptr->current_floor_ptr;
    const auto &grid = floor.get_grid(pos);
    const auto &flags = explore_terrain(floor, pos).flags;
    if (grid.has_monster() && floor.m_list[grid.m_idx].ml) {
        return false;
    }

    /* Known traps, shops and buildings, harmful terrain */
    if (flags.has_any_of({ TerrainCharacteristics::TRAP, TerrainCharacteristics::STORE, TerrainCharacteristics::BLDG, TerrainCharacteristics::LAVA, TerrainCharacteristics::QUEST_ENTER })) {
        return false;
    }

    if (flags.has_all_of({ TerrainCharacteristics::WATER, TerrainCharacteristics::DEEP })) {
        return false;
    }

    /* Doors we may open, rubble we may dig */
    if (grid.is_closed_door(true) || explore_rubble(floor, pos)) {
        return true;
    }

    return player_can_enter(player_ptr, grid.get_terrain_id(TerrainKind::MIMIC), 0);
}

/* Known grid next to an unknown one */
bool explore_frontier(const FloorType &floor, const Pos2D &pos)
{
    for (const auto &d : Direction::directions_8()) {
        const auto pos_neighbor = pos + d.vec();
        if (explore_in(floor, pos_neighbor) && !explore_known(floor, pos_neighbor)) {
            return true;
        }
    }

    return false;
}

/* A found item here the player has not stood on yet */
bool explore_item(const FloorType &floor, const Pos2D &pos)
{
    for (const auto o_idx : floor.get_grid(pos).o_idx_list) {
        const auto *item = floor.o_list[o_idx].get();
        if (item->marked.has(OmType::FOUND) && !explore_visited.contains(item)) {
            return true;
        }
    }

    return false;
}

bool explore_is_stairs(const FloorType &floor, const Pos2D &pos, int stairs)
{
    const auto &flags = explore_terrain(floor, pos).flags;
    if (!explore_known(floor, pos) || flags.has(TerrainCharacteristics::QUEST_ENTER)) {
        return false;
    }

    return flags.has((stairs > 0) ? TerrainCharacteristics::UP_STAIRS : TerrainCharacteristics::DOWN_STAIRS);
}

/* A visible hostile monster in view, or none */
const MonsterEntity *hostile_in_view(PlayerType *player_ptr)
{
    const auto &floor = *player_ptr->current_floor_ptr;
    for (short i = 1; i < floor.m_max; i++) {
        const auto &monster = floor.m_list[i];
        if (!monster.is_valid() || !monster.ml || monster.is_pet() || monster.is_friendly()) {
            continue;
        }

        if (floor.get_grid(monster.get_position()).is_view()) {
            return &monster;
        }
    }

    return nullptr;
}

/*
 * BFS from the player for the nearest target (stairs: 1 up, -1 down, 0 explore).
 * Returns the target and the first step; *blocked tells whether known traps or
 * locked doors were in the way.
 */
struct ExploreTarget {
    Pos2D pos;
    Direction first;
};

tl::optional<ExploreTarget> explore_find(PlayerType *player_ptr, int stairs, bool &blocked)
{
    static std::array<std::array<short, MAX_WID>, MAX_HGT> from;
    static std::vector<Pos2D> queue;
    const auto &floor = *player_ptr->current_floor_ptr;
    const auto p_pos = player_ptr->get_position();
    const auto dirs = Direction::directions_8();

    for (auto &row : from) {
        row.fill(-1);
    }

    queue.clear();
    from[p_pos.y][p_pos.x] = 8;
    queue.push_back(p_pos);
    for (size_t head = 0; head < queue.size(); head++) {
        const auto pos = queue[head];
        const auto here = (pos == p_pos);
        const auto found = !here && (stairs ? explore_is_stairs(floor, pos, stairs) : (!explore_locked_door(floor, pos) && (explore_frontier(floor, pos) || explore_item(floor, pos))));
        if (found) {
            auto step = pos;
            while (true) {
                const auto d = dirs[from[step.y][step.x]];
                if (step - d.vec() == p_pos) {
                    return ExploreTarget{ pos, d };
                }

                step = step - d.vec();
            }
        }

        /* Don't walk through doors we won't open */
        if (!here && explore_locked_door(floor, pos)) {
            continue;
        }

        for (short i = 0; i < 8; i++) {
            const auto pos_next = pos + dirs[i].vec();
            if (!explore_in(floor, pos_next) || from[pos_next.y][pos_next.x] != -1) {
                continue;
            }

            /* Like Travel: the stairs walk may cross unknown grids on the surface (town at night) */
            if (!explore_known(floor, pos_next) && (!stairs || floor.is_underground())) {
                continue;
            }

            if (!explore_passable(player_ptr, pos_next)) {
                if (explore_terrain(floor, pos_next).flags.has(TerrainCharacteristics::TRAP)) {
                    blocked = true;
                }

                continue;
            }

            if (explore_locked_door(floor, pos_next) && explore_frontier(floor, pos_next)) {
                blocked = true;
            }

            from[pos_next.y][pos_next.x] = i;
            queue.push_back(pos_next);
        }
    }

    return tl::nullopt;
}
}

/* RVIP: also used to decide whether the item list reopens after an item action */
bool hostile_monster_in_view(PlayerType *player_ptr)
{
    return hostile_in_view(player_ptr) != nullptr;
}

void explore_new_level()
{
    auto_explore = false;
    explore_stairs = 0;
    explore_visited.clear();
}

/* One explore step ('X', then each turn while auto_explore is set) */
void explore_step(PlayerType *player_ptr)
{
#ifdef USE_WEB
    /* RVIP: auto-explore moves visibly: paint the last step, then wait 40 ms */
    if (auto_explore) {
        handle_stuff(player_ptr);
        term_fresh();
        term_xtra(TERM_XTRA_DELAY, 40);
    }
#endif
    auto &floor = *player_ptr->current_floor_ptr;
    const auto p_pos = player_ptr->get_position();
    auto_explore = false;

    /* Stood on these items now */
    for (const auto o_idx : floor.get_grid(p_pos).o_idx_list) {
        explore_visited.insert(floor.o_list[o_idx].get());
    }

    if (AngbandWorld::get_instance().is_wild_mode()) {
        return;
    }

    const auto effects = player_ptr->effects();
    if (effects->confusion().is_confused() || effects->hallucination().is_hallucinated() || effects->blindness().is_blind()) {
        msg_print(_("今は探索できない。", "You cannot explore right now."));
        return;
    }

    /* Without a light of its own the explorer can't see past a lit room's exits */
    if (no_lite(player_ptr) || (floor.is_underground() && (player_ptr->cur_lite <= 0))) {
        msg_print(_("明かりがない。", "You have no light to explore by."));
        return;
    }

    if (!floor.is_underground() && !WildernessGrids::get_instance().get_player_grid().has_town()) {
        msg_print(_("ここには探索するものがない。", "There is nothing to explore here."));
        return;
    }

    if (const auto *monster = hostile_in_view(player_ptr)) {
        msg_format(_("%sが見えている。", "In view: %s."), monster_desc(player_ptr, *monster, 0).data());
        return;
    }

    auto blocked = false;
    const auto target = explore_find(player_ptr, 0, blocked);
    if (!target) {
        msg_print(blocked ? _("鍵のかかったドアか既知の罠しか残っていない。", "Only locked doors or known traps are in the way.")
                          : _("もう探索する場所がない。", "Nothing left to explore."));
        return;
    }

    const auto pos = p_pos + target->first.vec();

    /* Keep going next turn unless the step disturbs us or prints a message */
    auto_explore = true;
    if (floor.get_grid(pos).is_closed_door(true)) {
        (void)exe_open(player_ptr, pos.y, pos.x);
    } else if (explore_rubble(floor, pos)) {
        /* Its own "You dig in the rubble" messages don't stop us */
        auto_explore = exe_tunnel(player_ptr, pos.y, pos.x);
    } else {
        PlayerEnergy(player_ptr).set_player_turn_energy(100);
        exe_movement(player_ptr, target->first, always_pickup, false);

        /* Blocked (unseen monster, ...): stop instead of retrying forever */
        if (player_ptr->get_position() == p_pos) {
            auto_explore = false;
        }
    }
}

/* '<' / '>' off the right stairs: travel to the nearest known one */
void explore_to_stairs(PlayerType *player_ptr, bool up)
{
    explore_stairs = 0;
    if (AngbandWorld::get_instance().is_wild_mode()) {
        return;
    }

    auto blocked = false;
    const auto target = explore_find(player_ptr, up ? 1 : -1, blocked);
    if (!target) {
        msg_print(up ? _("上り階段を知らない。", "You know of no way up.") : _("下り階段を知らない。", "You know of no way down."));
        return;
    }

    auto &travel = Travel::get_instance();
    travel.set_goal(player_ptr, target->pos);
    explore_stairs = up ? 1 : -1;
}

/* Travel to stairs ended: take them if we are on them */
void explore_stairs_arrive(PlayerType *player_ptr)
{
    const auto stairs = explore_stairs;
    explore_stairs = 0;
    const auto &flags = player_ptr->current_floor_ptr->get_grid(player_ptr->get_position()).get_terrain().flags;
    if (flags.has_not((stairs > 0) ? TerrainCharacteristics::UP_STAIRS : TerrainCharacteristics::DOWN_STAIRS)) {
        return;
    }

    if (stairs > 0) {
        do_cmd_go_up(player_ptr);
    } else {
        do_cmd_go_down(player_ptr);
    }
}
