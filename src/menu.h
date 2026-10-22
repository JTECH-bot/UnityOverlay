// menu.h - public surface of the overlay UI

#pragma once

#include "memory.h"

namespace menu {

struct Actions {
    bool reload_config   = false;
    bool save_config     = false;
    bool dump_scene      = false;
    bool unlock_all      = false;
    bool ultra_unlock    = false;
    bool skip_tutorial   = false;
    // kept for the older JamesTool Java bridge
    bool unlock_weapons  = false;
    bool unlock_skins    = false;
};

void apply_style();

// Draws the menu window. Sets flags in `act` for deferred work.
void draw(ov::Config& cfg, const ov::Offsets& offs, Actions& act);

// Draws ESP for the current frame (screen space, ImGui background list).
void draw_overlay(const ov::Config& cfg, const ov::EntityReader& reader,
                  int width, int height);

// Compatibility shim: the reader tick performs aim and feature writes.
void update_aimbot(const ov::Config& cfg, const ov::EntityReader& reader,
                   int width, int height);

}  // namespace menu
