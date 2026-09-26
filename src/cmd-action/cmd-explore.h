#pragma once

/* RVIP: auto-explore ('X') and walking to stairs ('<' / '>') */
class PlayerType;
extern bool auto_explore;
extern int explore_stairs;
void explore_new_level();
void explore_step(PlayerType *player_ptr);
void explore_to_stairs(PlayerType *player_ptr, bool up);
void explore_stairs_arrive(PlayerType *player_ptr);
bool hostile_monster_in_view(PlayerType *player_ptr);
