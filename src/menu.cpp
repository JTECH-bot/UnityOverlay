// menu.cpp - ImGui UI and ESP rendering.
//
// Layout: header banner + vertical sidebar nav on the left,
// scrollable category content on the right. Theme: charcoal + crimson.

#include "menu.h"

#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace menu {

namespace {

// --- palette ---------------------------------------------------------------
constexpr ImVec4 kAccent      = ImVec4(0.80f, 0.10f, 0.10f, 1.00f);
constexpr ImVec4 kAccentHot   = ImVec4(0.95f, 0.18f, 0.18f, 1.00f);
constexpr ImVec4 kAccentDim   = ImVec4(0.80f, 0.10f, 0.10f, 0.30f);
constexpr ImVec4 kText        = ImVec4(0.94f, 0.94f, 0.95f, 1.00f);
constexpr ImVec4 kTextMuted   = ImVec4(0.55f, 0.55f, 0.58f, 1.00f);
constexpr ImVec4 kPanel       = ImVec4(0.06f, 0.06f, 0.07f, 0.94f);
constexpr ImVec4 kPanelDeep   = ImVec4(0.04f, 0.04f, 0.05f, 0.92f);
constexpr ImVec4 kPanelHeader = ImVec4(0.03f, 0.03f, 0.04f, 0.98f);

constexpr ImU32  kColAccent = IM_COL32(204, 26, 26, 255);
constexpr float  kPi        = 3.14159265358979323846f;

// --- ESP -------------------------------------------------------------------
constexpr ImU32 kEspEnemy   = IM_COL32(255, 45, 45, 235);
constexpr ImU32 kEspTeam    = IM_COL32(60, 170, 255, 235);
constexpr ImU32 kEspLine    = IM_COL32(255, 45, 45, 120);
constexpr ImU32 kEspSkel    = IM_COL32(255, 60, 60, 220);
constexpr ImU32 kEspText    = IM_COL32(240, 240, 245, 255);
constexpr ImU32 kEspShadow  = IM_COL32(0, 0, 0, 180);

constexpr int kMaxEntities = 128;

// --- menu state ------------------------------------------------------------
int  g_category = 0;          // 0..5 sidebar selection
int  g_skin_tab = 0;          // 0..3 skinhacks sub tab

const char* kCategories[]   = {"Visuals", "Aimbot", "Player", "Weapon",
                               "Skinhacks", "Settings"};
const char* kCategoryIcons[] = {"[V]", "[A]", "[P]", "[W]", "[S]", "[C]"};
constexpr int kCategoryCount = 6;

enum Rarity { RAR_COMMON, RAR_RARE, RAR_EPIC, RAR_LEGENDARY, RAR_MYTHIC };

struct RarityStyle {
    const char* name;
    ImVec4      color;
    ImU32       u32;
    int         stars;
};

RarityStyle rarity_of(int r) {
    switch (r) {
        case RAR_MYTHIC:    return {"Mythic",    ImVec4(0.95f, 0.16f, 0.16f, 1.f), IM_COL32(242, 41, 41, 255), 5};
        case RAR_LEGENDARY: return {"Legendary", ImVec4(1.00f, 0.55f, 0.10f, 1.f), IM_COL32(255, 140, 26, 255), 4};
        case RAR_EPIC:      return {"Epic",      ImVec4(0.65f, 0.28f, 0.95f, 1.f), IM_COL32(166, 71, 242, 255), 3};
        case RAR_RARE:      return {"Rare",      ImVec4(0.25f, 0.60f, 1.00f, 1.f), IM_COL32(64, 153, 255, 255), 2};
        default:            return {"Common",    ImVec4(0.70f, 0.72f, 0.75f, 1.f), IM_COL32(178, 184, 191, 255), 1};
    }
}

struct SkinItem {
    const char* name;
    int         rarity;
};

const SkinItem kSkinWeapons[] = {
    {"AK117 - Mythic Guard",   RAR_MYTHIC},
    {"DL Q33 - Void Stalker",  RAR_LEGENDARY},
    {"QQ9 - Neon Nightmare",   RAR_LEGENDARY},
    {"M4 - Ashfall",           RAR_EPIC},
    {"KN-44 - Jungle Fury",    RAR_EPIC},
    {"PDW-57 - Dust Devil",    RAR_RARE},
    {"BK57 - Iron Sight",      RAR_RARE},
    {"M16 - Field Issue",      RAR_COMMON},
};

const SkinItem kSkinCharacters[] = {
    {"Ghost - Lunar Wolf",     RAR_MYTHIC},
    {"Mara - Awakened",        RAR_LEGENDARY},
    {"Price - Classic",        RAR_EPIC},
    {"Nikto - Steel",          RAR_EPIC},
    {"Tank Dempsey",           RAR_RARE},
    {"Spec Ops 1",             RAR_COMMON},
};

const SkinItem kSkinWatches[] = {
    {"Cosmic Timepiece",       RAR_MYTHIC},
    {"Crimson Chrono",         RAR_LEGENDARY},
    {"Digital Camo",           RAR_EPIC},
    {"Tactical Field",         RAR_RARE},
    {"Standard Issue",         RAR_COMMON},
};

const SkinItem kSkinBoxes[] = {
    {"Mythic Crate",           RAR_MYTHIC},
    {"Legendary Crate",        RAR_LEGENDARY},
    {"Epic Crate",             RAR_EPIC},
    {"Rare Crate",             RAR_RARE},
    {"Standard Crate",         RAR_COMMON},
};

// Per-item unlock feedback. Session-local, purely cosmetic.
bool g_unlocked[4][16] = {};

struct SkinTab {
    const char*           label;
    const SkinItem*       items;
    int                   count;
};

SkinTab skin_tab(int i) {
    switch (i) {
        case 0: return {"Weapons",    kSkinWeapons,    IM_ARRAYSIZE(kSkinWeapons)};
        case 1: return {"Characters", kSkinCharacters, IM_ARRAYSIZE(kSkinCharacters)};
        case 2: return {"Watches",    kSkinWatches,    IM_ARRAYSIZE(kSkinWatches)};
        default:return {"Lootboxes",  kSkinBoxes,      IM_ARRAYSIZE(kSkinBoxes)};
    }
}

// --- small drawing helpers -------------------------------------------------
void draw_star(ImDrawList* dl, ImVec2 center, float radius, ImU32 col) {
    ImVec2 pts[10];
    for (int i = 0; i < 10; ++i) {
        const float angle = -kPi * 0.5f + (float)i * kPi / 5.0f;
        const float r     = (i & 1) ? radius * 0.44f : radius;
        pts[i] = ImVec2(center.x + std::cos(angle) * r, center.y + std::sin(angle) * r);
    }
    for (int i = 0; i < 10; ++i) {
        const ImVec2& a = pts[i];
        const ImVec2& b = pts[(i + 1) % 10];
        dl->AddTriangleFilled(center, a, b, col);
    }
}

void draw_stars_inline(int count, float radius, ImU32 col) {
    const float step = radius * 2.6f;
    const ImVec2 size(step * (float)count, radius * 2.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < count; ++i) {
        draw_star(dl, ImVec2(p.x + radius + step * (float)i, p.y + radius), radius, col);
    }
}

void help(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kTextMuted);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void section_header(const char* title) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    dl->AddRectFilled(ImVec2(p.x, p.y + 2.f), ImVec2(p.x + 3.f, p.y + h - 1.f), kColAccent, 2.f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.f);
    ImGui::TextColored(kAccentHot, "%s", title);
    ImGui::Spacing();
}

void slider_float(const char* label, const char* id, float* v, float lo, float hi,
                  const char* fmt) {
    ImGui::TextColored(kTextMuted, "%s", label);
    ImGui::SetNextItemWidth(-1.f);
    ImGui::SliderFloat(id, v, lo, hi, fmt);
}

bool nav_button(const char* icon, const char* label, bool selected) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_Button,
                          selected ? kAccent : ImVec4(0.10f, 0.10f, 0.12f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          selected ? kAccentHot : ImVec4(0.22f, 0.10f, 0.10f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentHot);

    char buf[64];
    snprintf(buf, sizeof(buf), "%s  %s", icon, label);
    const bool pressed = ImGui::Button(buf, ImVec2(-1.f, 38.f));

    if (selected) {
        // left accent bar
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x, a.y + 6.f),
                                                  ImVec2(a.x + 3.f, b.y - 6.f),
                                                  IM_COL32(255, 255, 255, 210), 2.f);
    }

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    return pressed;
}

bool pill_tab(const char* label, bool selected, float width) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14.0f);
    ImGui::PushStyleColor(ImGuiCol_Button,
                          selected ? kAccent : ImVec4(0.10f, 0.10f, 0.12f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          selected ? kAccentHot : ImVec4(0.24f, 0.11f, 0.11f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentHot);
    const bool pressed = ImGui::Button(label, ImVec2(width, 30.f));
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    return pressed;
}

// One skinhacks row: stars + name | rarity | unlock button.
void skin_row(const SkinItem& item, bool& unlocked, bool& unlock_clicked) {
    const RarityStyle rs = rarity_of(item.rarity);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    draw_stars_inline(rs.stars, 5.0f, rs.u32);
    ImGui::SameLine();
    ImGui::TextColored(unlocked ? ImVec4(0.70f, 0.74f, 0.76f, 1.f) : kText,
                       "%s", item.name);

    ImGui::TableSetColumnIndex(1);
    ImGui::TextColored(rs.color, "%s", rs.name);

    ImGui::TableSetColumnIndex(2);
    if (unlocked) {
        ImGui::TextColored(ImVec4(0.30f, 0.80f, 0.40f, 1.f), "Unlocked");
    } else {
        const char* id = "Unlock";
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.06f, 0.06f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccent);
        if (ImGui::Button(id, ImVec2(-1.f, 26.f))) {
            unlocked = true;
            unlock_clicked = true;
        }
        ImGui::PopStyleColor(2);
    }
}

void skin_list(int tab_idx, Actions& act) {
    const SkinTab t = skin_tab(tab_idx);
    bool clicked = false;

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                                  ImGuiTableFlags_NoBordersInBody;
    if (ImGui::BeginTable("##skin_items", 3, flags)) {
        ImGui::TableSetupColumn("Item",   ImGuiTableColumnFlags_WidthStretch, 0.58f);
        ImGui::TableSetupColumn("Rarity", ImGuiTableColumnFlags_WidthStretch, 0.22f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 0.20f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < t.count && i < 16; ++i) {
            ImGui::PushID(i);
            skin_row(t.items[i], g_unlocked[tab_idx][i], clicked);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (clicked) act.unlock_skins = true;
}

}  // namespace

// ---------------------------------------------------------------------------
void apply_style() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding    = 12.0f;
    s.ChildRounding     = 8.0f;
    s.FrameRounding     = 6.0f;
    s.PopupRounding     = 8.0f;
    s.GrabRounding      = 6.0f;
    s.ScrollbarRounding  = 10.0f;
    s.TabRounding       = 6.0f;

    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize  = 0.0f;
    s.WindowPadding    = ImVec2(10, 10);
    s.FramePadding     = ImVec2(9, 6);
    s.ItemSpacing      = ImVec2(9, 7);
    s.ScrollbarSize    = 8.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]        = kPanel;
    c[ImGuiCol_ChildBg]         = ImVec4(0.05f, 0.05f, 0.06f, 0.55f);
    c[ImGuiCol_PopupBg]         = ImVec4(0.05f, 0.05f, 0.06f, 0.97f);
    c[ImGuiCol_Border]          = ImVec4(0.80f, 0.10f, 0.10f, 0.35f);
    c[ImGuiCol_BorderShadow]    = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_Text]            = kText;
    c[ImGuiCol_TextDisabled]    = kTextMuted;

    c[ImGuiCol_FrameBg]         = ImVec4(0.12f, 0.12f, 0.14f, 0.90f);
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.20f, 0.11f, 0.12f, 0.95f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.30f, 0.10f, 0.11f, 0.95f);

    c[ImGuiCol_CheckMark]       = kAccentHot;
    c[ImGuiCol_SliderGrab]      = kAccent;
    c[ImGuiCol_SliderGrabActive]= kAccentHot;

    c[ImGuiCol_Button]          = ImVec4(0.12f, 0.12f, 0.14f, 0.95f);
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.45f, 0.08f, 0.08f, 0.95f);
    c[ImGuiCol_ButtonActive]    = kAccent;

    c[ImGuiCol_Header]          = kAccentDim;
    c[ImGuiCol_HeaderHovered]   = ImVec4(0.80f, 0.10f, 0.10f, 0.30f);
    c[ImGuiCol_HeaderActive]    = ImVec4(0.80f, 0.10f, 0.10f, 0.55f);

    c[ImGuiCol_Separator]       = ImVec4(0.80f, 0.10f, 0.10f, 0.28f);
    c[ImGuiCol_SeparatorHovered]= kAccentDim;
    c[ImGuiCol_SeparatorActive] = kAccent;

    c[ImGuiCol_Tab]             = ImVec4(0.10f, 0.10f, 0.12f, 0.95f);
    c[ImGuiCol_TabHovered]      = ImVec4(0.45f, 0.08f, 0.08f, 0.90f);
    c[ImGuiCol_TabSelected]     = kAccent;

    c[ImGuiCol_TableHeaderBg]   = ImVec4(0.10f, 0.10f, 0.12f, 0.95f);
    c[ImGuiCol_TableRowBg]      = ImVec4(0.07f, 0.07f, 0.08f, 0.45f);
    c[ImGuiCol_TableRowBgAlt]   = ImVec4(0.12f, 0.12f, 0.14f, 0.45f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.80f, 0.10f, 0.10f, 0.30f);
    c[ImGuiCol_TableBorderLight]  = ImVec4(0.80f, 0.10f, 0.10f, 0.12f);

    c[ImGuiCol_TitleBg]         = kPanelHeader;
    c[ImGuiCol_TitleBgActive]   = kPanelHeader;
    c[ImGuiCol_TitleBgCollapsed]= kPanelHeader;

    c[ImGuiCol_ScrollbarBg]     = ImVec4(0.03f, 0.03f, 0.04f, 0.60f);
    c[ImGuiCol_ScrollbarGrab]   = ImVec4(0.45f, 0.08f, 0.08f, 0.80f);
    c[ImGuiCol_ScrollbarGrabHovered] = kAccentDim;
    c[ImGuiCol_ScrollbarGrabActive]  = kAccent;

    c[ImGuiCol_ResizeGrip]        = kAccentDim;
    c[ImGuiCol_ResizeGripHovered] = kAccentDim;
    c[ImGuiCol_ResizeGripActive]  = kAccent;
}

// ---------------------------------------------------------------------------
void draw(ov::Config& cfg, const ov::Offsets& offs, Actions& act) {
    if (!cfg.menu_open) return;

    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(40, 60), ImGuiCond_FirstUseEver);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoTitleBar;

    if (!ImGui::Begin("##overlay_main", &cfg.menu_open, flags)) {
        ImGui::End();
        return;
    }

    const ImVec2 win_pos  = ImGui::GetWindowPos();
    const ImVec2 win_size = ImGui::GetWindowSize();
    ImDrawList*  dl       = ImGui::GetWindowDrawList();

    // --- header banner -----------------------------------------------------
    const float header_h = 74.0f;
    dl->AddRectFilled(win_pos, ImVec2(win_pos.x + win_size.x, win_pos.y + header_h),
                      IM_COL32(16, 16, 19, 245), 12.0f);
    dl->AddRectFilled(win_pos, ImVec2(win_pos.x + win_size.x, win_pos.y + 3.0f),
                      kColAccent, 12.0f);
    dl->AddRectFilledMultiColor(
        ImVec2(win_pos.x, win_pos.y),
        ImVec2(win_pos.x + win_size.x * 0.55f, win_pos.y + header_h),
        IM_COL32(150, 18, 18, 90), IM_COL32(150, 18, 18, 0),
        IM_COL32(150, 18, 18, 0), IM_COL32(150, 18, 18, 90));

    ImGui::SetCursorScreenPos(ImVec2(win_pos.x + 18.f, win_pos.y + 14.f));
    ImGui::BeginGroup();
    ImGui::TextColored(kAccentHot, "JAMES TOOL V1.0 | Credits: James 254");
    ImGui::TextColored(ImVec4(0.78f, 0.78f, 0.80f, 1.f),
                       "Telegram: @JaMeS66699 | Group: https://t.me/Harckdarkmod");
    ImGui::EndGroup();

    // close button (top right)
    ImGui::SetCursorScreenPos(ImVec2(win_pos.x + win_size.x - 44.f, win_pos.y + 18.f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.20f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccent);
    if (ImGui::Button("X##close", ImVec2(28.f, 26.f))) cfg.menu_open = false;
    ImGui::PopStyleColor(2);

    // --- body: sidebar + content ------------------------------------------
    ImGui::SetCursorScreenPos(ImVec2(win_pos.x + 10.f, win_pos.y + header_h + 8.f));
    const float body_h  = win_size.y - header_h - 18.f;
    const float side_w  = 168.f;
    const float content_w = win_size.x - side_w - 30.f;

    // sidebar
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanelDeep);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.80f, 0.10f, 0.10f, 0.22f));
    ImGui::BeginChild("##sidebar", ImVec2(side_w, body_h), ImGuiChildFlags_Borders);
    ImGui::Spacing();
    for (int i = 0; i < kCategoryCount; ++i) {
        if (nav_button(kCategoryIcons[i], kCategories[i], g_category == i))
            g_category = i;
        ImGui::Spacing();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);

    ImGui::SameLine(0.f, 10.f);

    // content
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.06f, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.80f, 0.10f, 0.10f, 0.20f));
    ImGui::BeginChild("##content", ImVec2(content_w, body_h), ImGuiChildFlags_Borders);
    ImGui::Spacing();

    switch (g_category) {
    case 0: {  // Visuals
        section_header("PLAYER ESP");
        ImGui::Checkbox("Box",      &cfg.visuals.draw_boxes);
        ImGui::SameLine(210.f);
        ImGui::Checkbox("Line",     &cfg.visuals.draw_lines);
        ImGui::Checkbox("Health",   &cfg.visuals.draw_health);
        ImGui::SameLine(210.f);
        ImGui::Checkbox("Name",     &cfg.visuals.draw_names);
        ImGui::Checkbox("Skeleton", &cfg.visuals.draw_skeleton);
        ImGui::SameLine(210.f);
        ImGui::Checkbox("Distance", &cfg.visuals.draw_distance);
        ImGui::Checkbox("Team",       &cfg.visuals.draw_team);
        ImGui::SameLine(210.f);
        ImGui::Checkbox("Enemy color", &cfg.visuals.enemy_color);

        ImGui::Spacing();
        section_header("WORLD");
        ImGui::Checkbox("Chams", &cfg.visuals.chams);
        ImGui::SameLine(210.f);
        ImGui::Checkbox("UAV",   &cfg.visuals.uav);
        ImGui::SameLine(330.f);
        ImGui::Checkbox("Radar", &cfg.visuals.radar);

        ImGui::Spacing();
        ImGui::SetNextItemWidth(160.f);
        ImGui::ColorEdit3("##chamscolor", cfg.visuals.chams_color,
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        ImGui::SameLine();
        ImGui::TextColored(kTextMuted, "Chams color");
        help("Forces every pawn into the radar/sonar reveal path.");

        ImGui::Spacing();
        slider_float("Render distance", "##dist", &cfg.visuals.max_distance,
                     10.f, 1000.f, "%.0f m");
        break;
    }

    case 1: {  // Aimbot
        section_header("AIM");
        ImGui::Checkbox("Aimbot",       &cfg.aimbot.enabled);
        ImGui::SameLine(210.f);
        ImGui::Checkbox("Aim lock",     &cfg.aimbot.aim_lock);
        ImGui::Checkbox("Triggerbot",   &cfg.aimbot.triggerbot);
        ImGui::SameLine(210.f);
        ImGui::Checkbox("Magic bullet", &cfg.aimbot.magic_bullet);
        ImGui::Checkbox("Visible only", &cfg.aimbot.visible_only);

        ImGui::Spacing();
        section_header("TUNING");
        slider_float("FOV radius", "##fov", &cfg.aimbot.fov, 1.f, 180.f, "%.0f deg");
        slider_float("Smoothness", "##smooth", &cfg.aimbot.smoothness, 1.f, 20.f, "%.1f");

        ImGui::Spacing();
        ImGui::TextColored(kTextMuted, "Hitbox");
        const char* hitboxes[] = {"Head", "Chest", "Pelvis"};
        ImGui::SetNextItemWidth(-1.f);
        ImGui::Combo("##hitbox", &cfg.aimbot.hitbox, hitboxes, 3);
        cfg.aimbot.target_head = (cfg.aimbot.hitbox == 0);
        break;
    }

    case 2: {  // Player
        section_header("MOVEMENT");
        ImGui::Checkbox("God mode",      &cfg.player.god_mode);
        ImGui::SameLine(210.f);
        ImGui::Checkbox("Noclip",        &cfg.player.noclip);
        ImGui::Checkbox("Speed hack",    &cfg.player.speed_hack);
        ImGui::SameLine(210.f);
        ImGui::Checkbox("Infinite jump", &cfg.player.infinite_jump);
        ImGui::Checkbox("Bunny jump",    &cfg.player.bunny_jump);
        ImGui::SameLine(210.f);
        ImGui::Checkbox("Jump boost",    &cfg.player.jump_boost);
        ImGui::Checkbox("Slide boost",   &cfg.player.slide_boost);
        ImGui::SameLine(210.f);
        ImGui::Checkbox("Free climb",    &cfg.player.free_climb);

        ImGui::Spacing();
        section_header("UTILITY");
        ImGui::Checkbox("Fast swim",        &cfg.player.fast_swim);
        ImGui::SameLine(210.f);
        ImGui::Checkbox("Fast parachute",   &cfg.player.fast_parachute);
        ImGui::Checkbox("Ability cooldown", &cfg.player.ability_cooldown);
        ImGui::SameLine(210.f);
        ImGui::Checkbox("Infinite ammo",    &cfg.player.infinite_ammo);

        ImGui::Spacing();
        slider_float("Speed multiplier", "##speed", &cfg.player.speed_multiplier,
                     0.5f, 10.f, "%.2fx");
        slider_float("Jump multiplier", "##jump", &cfg.player.jump_multiplier,
                     1.f, 8.f, "%.2fx");
        slider_float("Slide multiplier", "##slide", &cfg.player.slide_multiplier,
                     1.f, 8.f, "%.2fx");
        break;
    }

    case 3: {  // Weapon
        section_header("RECOIL & HANDLING");
        ImGui::Checkbox("No recoil",       &cfg.weapon.no_recoil);
        ImGui::SameLine(210.f);
        ImGui::Checkbox("No spread",       &cfg.weapon.no_spread);
        ImGui::Checkbox("Fast reload",     &cfg.weapon.fast_reload);
        ImGui::SameLine(210.f);
        ImGui::Checkbox("Fast switch",     &cfg.weapon.fast_switch);
        ImGui::Checkbox("Fast scope",      &cfg.weapon.fast_scope);
        ImGui::SameLine(210.f);
        ImGui::Checkbox("Rapid fire",      &cfg.weapon.rapid_fire);

        ImGui::Spacing();
        section_header("VISUAL NOISE");
        ImGui::Checkbox("No muzzle flash", &cfg.weapon.no_muzzle_flash);
        ImGui::SameLine(210.f);
        ImGui::Checkbox("No flash",        &cfg.weapon.no_flash);
        ImGui::Checkbox("No camera shake", &cfg.weapon.no_camera_shake);
        ImGui::SameLine(210.f);
        ImGui::Checkbox("Small crosshair", &cfg.weapon.small_crosshair);

        ImGui::Spacing();
        ImGui::Checkbox("Always knife",  &cfg.weapon.always_knife);
        ImGui::SameLine(210.f);
        ImGui::Checkbox("Infinite ammo", &cfg.weapon.infinite_ammo);
        ImGui::SameLine(330.f);
        ImGui::Checkbox("Magic bullet",  &cfg.weapon.magic_bullet);

        ImGui::Spacing();
        section_header("BALLISTICS");
        slider_float("Fire rate", "##fire", &cfg.weapon.fire_rate_multiplier,
                     1.f, 10.f, "%.2fx");
        slider_float("Reload speed", "##reload", &cfg.weapon.reload_multiplier,
                     1.f, 10.f, "%.2fx");
        slider_float("Damage", "##dmg", &cfg.weapon.damage_multiplier,
                     1.f, 20.f, "%.2fx");
        break;
    }

    case 4: {  // Skinhacks
        section_header("UNLOCKER");
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42f, 0.06f, 0.06f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccent);
        if (ImGui::Button("Ultra Unlocker - All Skins + Camos", ImVec2(-1.f, 36.f))) {
            act.ultra_unlock = true;
            act.unlock_skins = true;
        }
        if (ImGui::Button("Unlock All Weapons", ImVec2(-1.f, 32.f))) {
            act.unlock_all     = true;
            act.unlock_weapons = true;
        }
        ImGui::PopStyleColor(2);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // sub tabs
        const char* tab_names[] = {"Weapons", "Characters", "Watches", "Lootboxes"};
        const float tab_w = (ImGui::GetContentRegionAvail().x - 24.f) / 4.f;
        for (int i = 0; i < 4; ++i) {
            if (i) ImGui::SameLine(0.f, 8.f);
            if (pill_tab(tab_names[i], g_skin_tab == i, tab_w)) g_skin_tab = i;
        }

        ImGui::Spacing();
        skin_list(g_skin_tab, act);
        ImGui::Spacing();
        help("Star count and color map to rarity: Common, Rare, Epic, Legendary, Mythic.");
        break;
    }

    default: {  // Settings
        section_header("RUNTIME");
        if (offs.values.empty())
            ImGui::TextColored(kTextMuted, "Offsets: not resolved (dynamic)");
        else
            ImGui::TextColored(kTextMuted, "Offsets: %zu resolved", offs.values.size());

        ImGui::Spacing();
        if (ImGui::Button("Reload Config", ImVec2(-1.f, 34.f))) act.reload_config = true;
        if (ImGui::Button("Dump Scene Objects", ImVec2(-1.f, 34.f))) act.dump_scene = true;
        if (ImGui::Button("Skip Tutorial", ImVec2(-1.f, 34.f))) act.skip_tutorial = true;

        ImGui::Spacing();
        section_header("ABOUT");
        ImGui::TextColored(kTextMuted, "JAMES TOOL V1.0 | Credits: James 254");
        ImGui::TextColored(kTextMuted, "Telegram: @JaMeS66699");
        ImGui::TextColored(kTextMuted, "Group: https://t.me/Harckdarkmod");
        break;
    }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(2);

    ImGui::End();
}

// ---------------------------------------------------------------------------
namespace {

ImU32 health_color(float frac) {
    frac = frac < 0.f ? 0.f : (frac > 1.f ? 1.f : frac);
    int r = (int)(255 * (1.0f - frac));
    int g = (int)(255 * frac);
    return IM_COL32(r, g, 100, 235);
}

bool screen(const ov::Vec3& p, const ov::Mat4& vp, float w, float h, ov::Vec2& out) {
    return ov::world_to_screen(p, vp, w, h, out);
}

}  // namespace

void draw_overlay(const ov::Config& cfg, const ov::EntityReader& reader,
                  int width, int height) {
    const bool any = cfg.visuals.draw_lines || cfg.visuals.draw_boxes ||
                     cfg.visuals.draw_health || cfg.visuals.draw_names ||
                     cfg.visuals.draw_skeleton;
    if (!any || width <= 0 || height <= 0) return;

    ov::Mat4 vp;
    if (!reader.world_matrix(vp)) return;

    static ov::Entity ents[kMaxEntities];
    const int count = reader.read_all(ents, kMaxEntities);
    if (count <= 0) return;

    ov::Entity local{};
    const bool have_local = reader.local_entity(local);

    ov::Vec3 eye{0, 0, 0};
    bool have_eye = reader.local_position(eye);
    if (!have_eye && have_local) { eye = local.pos; have_eye = true; }

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float sw = (float)width;
    const float sh = (float)height;

    for (int i = 0; i < count; ++i) {
        const ov::Entity& e = ents[i];
        if (e.local || !e.alive) continue;

        bool teammate = have_local && local.team != 0 && e.team == local.team;
        if (teammate && !cfg.visuals.draw_team) continue;

        ov::Vec3 feet = e.pos;
        ov::Vec3 head = e.head;
        if (head.x == 0.f && head.y == 0.f && head.z == 0.f)
            head = {feet.x, feet.y + 1.65f, feet.z};

        ov::Vec2 sfeet, shead;
        if (!screen(feet, vp, sw, sh, sfeet)) continue;
        if (!screen(head, vp, sw, sh, shead)) continue;

        float box_h = sfeet.y - shead.y;
        if (box_h < 4.0f || box_h > sh * 1.6f) continue;

        float dist = have_eye ? eye.distance(feet) : 0.f;
        if (have_eye && dist > cfg.visuals.max_distance) continue;

        const float box_w = box_h * 0.52f;
        const float left   = shead.x - box_w * 0.5f;
        const float right  = shead.x + box_w * 0.5f;
        const float top    = shead.y;
        const float bottom = sfeet.y;
        const ImU32 color  = teammate    ? kEspTeam :
                             cfg.visuals.enemy_color ? kEspEnemy :
                             IM_COL32(0, 255, 140, 235);

        if (cfg.visuals.draw_lines) {
            dl->AddLine(ImVec2(sw * 0.5f, sh), ImVec2(shead.x, bottom),
                        teammate ? kEspTeam : (cfg.visuals.enemy_color ? kEspLine
                                                                       : IM_COL32(0, 255, 140, 120)),
                        1.4f);
        }

        if (cfg.visuals.draw_boxes) {
            dl->AddRect(ImVec2(left - 1, top - 1), ImVec2(right + 1, bottom + 1),
                        IM_COL32(0, 0, 0, 160), 2.0f, 0, 3.2f);
            dl->AddRect(ImVec2(left, top), ImVec2(right, bottom), color, 2.0f, 0, 1.5f);
        }

        if (cfg.visuals.draw_health) {
            float frac = e.max_health > 0.f ? e.health / e.max_health : e.health / 100.f;
            if (frac < 0.f) frac = 0.f;
            if (frac > 1.f) frac = 1.f;
            const float bar_w = 4.0f;
            const float bx = left - bar_w - 4.0f;
            dl->AddRectFilled(ImVec2(bx - 1, top - 1), ImVec2(bx + bar_w + 1, bottom + 1),
                              IM_COL32(0, 0, 0, 160));
            dl->AddRectFilled(ImVec2(bx, bottom - (bottom - top) * frac),
                              ImVec2(bx + bar_w, bottom), health_color(frac));
        }

        if (cfg.visuals.draw_names) {
            const char* label = e.name[0] ? e.name : "player";
            char buf[96];
            if (cfg.visuals.draw_distance)
                snprintf(buf, sizeof(buf), "%s [%.0fm]", label, dist);
            else
                snprintf(buf, sizeof(buf), "%s", label);

            ImVec2 ts = ImGui::CalcTextSize(buf);
            ImVec2 tp(shead.x - ts.x * 0.5f, top - ts.y - 4.0f);
            dl->AddText(ImVec2(tp.x + 1, tp.y + 1), kEspShadow, buf);
            dl->AddText(tp, kEspText, buf);
        }

        if (cfg.visuals.draw_skeleton) {
            ov::Vec2 h, n, s, hip, la, ra;
            ov::Vec3 neck = e.neck, spine = e.spine, hips = e.hips;
            ov::Vec3 lank = e.l_ankle, rank = e.r_ankle;
            if (neck.x == 0.f && neck.y == 0.f && neck.z == 0.f)
                neck = {head.x, head.y - 0.12f, head.z};
            if (spine.x == 0.f && spine.y == 0.f && spine.z == 0.f)
                spine = {feet.x, feet.y + 1.05f, feet.z};
            if (hips.x == 0.f && hips.y == 0.f && hips.z == 0.f)
                hips = {feet.x, feet.y + 0.85f, feet.z};
            if (lank.x == 0.f && lank.y == 0.f && lank.z == 0.f)
                lank = {feet.x - 0.16f, feet.y + 0.08f, feet.z};
            if (rank.x == 0.f && rank.y == 0.f && rank.z == 0.f)
                rank = {feet.x + 0.16f, feet.y + 0.08f, feet.z};

            if (!screen(head, vp, sw, sh, h)) continue;
            if (!screen(neck, vp, sw, sh, n)) continue;
            if (!screen(spine, vp, sw, sh, s)) continue;
            if (!screen(hips, vp, sw, sh, hip)) continue;
            if (!screen(lank, vp, sw, sh, la)) continue;
            if (!screen(rank, vp, sw, sh, ra)) continue;

            const float th = 2.0f;
            dl->AddLine(ImVec2(h.x, h.y), ImVec2(n.x, n.y), kEspSkel, th);
            dl->AddLine(ImVec2(n.x, n.y), ImVec2(s.x, s.y), kEspSkel, th);
            dl->AddLine(ImVec2(s.x, s.y), ImVec2(hip.x, hip.y), kEspSkel, th);
            dl->AddLine(ImVec2(hip.x, hip.y), ImVec2(la.x, la.y), kEspSkel, th);
            dl->AddLine(ImVec2(hip.x, hip.y), ImVec2(ra.x, ra.y), kEspSkel, th);
            dl->AddCircleFilled(ImVec2(h.x, h.y), 3.0f, kEspSkel);
        }
    }
}

void update_aimbot(const ov::Config& cfg, const ov::EntityReader& reader,
                   int width, int height) {
    (void)cfg; (void)reader; (void)width; (void)height;
}

}  // namespace menu
