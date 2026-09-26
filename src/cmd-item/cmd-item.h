#pragma once

class PlayerType;
void do_cmd_inven(PlayerType *player_ptr);
void do_cmd_drop(PlayerType *player_ptr);
void do_cmd_observe(PlayerType *player_ptr);
void do_cmd_uninscribe(PlayerType *player_ptr);
void do_cmd_inscribe(PlayerType *player_ptr);
void do_cmd_use(PlayerType *player_ptr);
void do_cmd_activate(PlayerType *player_ptr);

/* RVIP: i/e lists with a cursor and item menus */
extern char gear_reopen;
void gear_ui(PlayerType *player_ptr, bool equip);
