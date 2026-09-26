#pragma once

#include "system/angband.h"
#include <vector>

/*
 * RVIP: the command menu on Enter (inkey_from_menu()).  One flat list,
 * grouped as lib/help/commdesc.txt groups the commands; cmd 0 starts a group.
 * cmd is the underlying command (keyset 0); the key shown in the menu is
 * looked up in the current keyset.
 */
struct menu_content {
    concptr name;
    byte cmd;
};

extern const std::vector<menu_content> menu_info;
