// menu.cpp - ImGui UI and ESP rendering.

#include "menu.h"

#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace menu {

namespace {
constexpr ImVec4 kAccent      = ImVec4(0.00f, 1.00f, 0.55f, 1.00f);
constexpr ImVec4 kAccentDim   = ImVec4(0.00f, 1.00f, 0.55f, 0.35f);
constexpr ImVec4 kText        = ImVec4(0.88f, 0.92f, 0.90f, 1.00f);
constexpr ImVec4 kTextMuted   = ImVec4(0.55f, 0.60f, 0.58f, 1.00f);
constexpr ImVec4 kGold        = ImVec4(1.00f, 0.78f, 0.27f, 1.00f);
constexpr ImVec4 kGoldDim     = ImVec4(0.72f, 0.56f, 0.20f, 1.00f);
constexpr ImVec4 kPanel       = ImVec4(0.05f, 0.07f, 0.06f, 0.90f);
constexpr ImVec4 kPanelHeader = ImVec4(0.03f, 0.05f, 0.04f, 0.95f);

constexpr ImU32 kEspEnemy   = IM_COL32(255, 55, 55, 235);
constexpr ImU32 kEspTeam    = IM_COL32(60, 170, 255, 235);
constexpr ImU32 kEspLine    = IM_COL32(255, 55, 55, 120);
constexpr ImU32 kEspSkel    = IM_COL32(0, 220, 255, 220);
constexpr ImU32 kEspText    = IM_COL32(235, 255, 245, 255);
constexpr ImU32 kEspShadow  = IM_COL32(0, 0, 0, 180);

constexpr int kMaxEntities = 128;

void help(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kTextMuted);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}
}  // namespace

void apply_style() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding    = 10.0f;
    s.ChildRounding     = 8.0f;
    s.FrameRounding     = 5.0f;
    s.PopupRounding     = 6.0f;
    s.GrabRounding      = 5.0f;
    s.ScrollbarRounding = 8.0f;
    s.TabRounding       = 5.0f;

    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize  = 0.0f;
    s.WindowPadding    = ImVec2(12, 10);
    s.FramePadding     = ImVec2(8, 5);
    s.ItemSpacing      = ImVec2(9, 7);
    s.ScrollbarSize    = 8.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]        = kPanel;
    c[ImGuiCol_ChildBg]         = ImVec4(0.04f, 0.06f, 0.05f, 0.55f);
    c[ImGuiCol_PopupBg]         = ImVec4(0.05f, 0.07f, 0.06f, 0.96f);
    c[ImGuiCol_Border]          = ImVec4(0.00f, 0.85f, 0.45f, 0.28f);
    c[ImGuiCol_BorderShadow]    = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_Text]            = kText;
    c[ImGuiCol_TextDisabled]    = kTextMuted;

    c[ImGuiCol_FrameBg]         = ImVec4(0.10f, 0.13f, 0.11f, 0.90f);
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.14f, 0.20f, 0.17f, 0.95f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.16f, 0.26f, 0.21f, 0.95f);

    c[ImGuiCol_CheckMark]       = kAccent;
    c[ImGuiCol_SliderGrab]      = kAccent;
    c[ImGuiCol_SliderGrabActive]= ImVec4(0.55f, 1.00f, 0.80f, 1.00f);

    c[ImGuiCol_Button]          = ImVec4(0.08f, 0.13f, 0.10f, 0.95f);
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.00f, 0.55f, 0.31f, 0.85f);
    c[ImGuiCol_ButtonActive]    = ImVec4(0.00f, 0.80f, 0.44f, 0.95f);

    c[ImGuiCol_Header]          = kAccentDim;
    c[ImGuiCol_HeaderHovered]   = ImVec4(0.00f, 1.00f, 0.55f, 0.25f);
    c[ImGuiCol_HeaderActive]    = ImVec4(0.00f, 1.00f, 0.55f, 0.40f);

    c[ImGuiCol_Separator]       = ImVec4(0.00f, 0.85f, 0.45f, 0.20f);
    c[ImGuiCol_SeparatorHovered]= kAccentDim;
    c[ImGuiCol_SeparatorActive] = kAccent;

    c[ImGuiCol_Tab]             = ImVec4(0.06f, 0.10f, 0.08f, 0.95f);
    c[ImGuiCol_TabHovered]      = ImVec4(0.00f, 0.60f, 0.34f, 0.85f);
    c[ImGuiCol_TabSelected]     = ImVec4(0.00f, 0.82f, 0.45f, 0.90f);

    c[ImGuiCol_TitleBg]         = kPanelHeader;
    c[ImGuiCol_TitleBgActive]   = kPanelHeader;
    c[ImGuiCol_TitleBgCollapsed]= kPanelHeader;

    c[ImGuiCol_ScrollbarBg]     = ImVec4(0.03f, 0.05f, 0.04f, 0.60f);
    c[ImGuiCol_ScrollbarGrab]   = ImVec4(0.00f, 0.55f, 0.31f, 0.70f);
    c[ImGuiCol_ScrollbarGrabHovered] = kAccentDim;
    c[ImGuiCol_ScrollbarGrabActive]  = kAccent;

    c[ImGuiCol_ResizeGrip]      = kAccentDim;
    c[ImGuiCol_ResizeGripHovered] = kAccentDim;
    c[ImGuiCol_ResizeGripActive]  = kAccent;
}

void draw(ov::Config& cfg, const ov::Offsets& offs, Actions& act) {
    if (!cfg.menu_open) return;

    ImGui::SetNextWindowSize(ImVec2(460, 610), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(50, 90), ImGuiCond_FirstUseEver);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;
    if (!ImGui::Begin("##overlay_main", &cfg.menu_open, flags)) {
        ImGui::End();
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::TextUnformatted("JAMES TOOL");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (offs.values.empty())
        ImGui::TextColored(kTextMuted, "| CODM IL2CPP | dynamic");
    else
        ImGui::TextColored(kTextMuted, "| CODM IL2CPP | %zu offsets", offs.values.size());

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.03f, 0.05f, 0.04f, 0.65f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.00f, 0.78f, 0.27f, 0.30f));
    ImGui::BeginChild("##branding", ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::PushStyleColor(ImGuiCol_Text, kGold);
    ImGui::TextUnformatted("JAMES TOOL V1.0");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextColored(kGoldDim, "| James 254");
    ImGui::TextColored(kTextMuted, "t.me/Harckdarkmod");
    ImGui::EndChild();
    ImGui::PopStyleColor(2);

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_None)) {

        if (ImGui::BeginTabItem("ESP")) {
            ImGui::Checkbox("Box",      &cfg.visuals.draw_boxes);
            ImGui::SameLine();
            ImGui::Checkbox("Line",     &cfg.visuals.draw_lines);
            ImGui::Checkbox("Health",   &cfg.visuals.draw_health);
            ImGui::SameLine();
            ImGui::Checkbox("Name",     &cfg.visuals.draw_names);
            ImGui::Checkbox("Skeleton", &cfg.visuals.draw_skeleton);
            ImGui::SameLine();
            ImGui::Checkbox("Distance", &cfg.visuals.draw_distance);
            ImGui::Checkbox("Team",     &cfg.visuals.draw_team);
            ImGui::SameLine();
            ImGui::Checkbox("Enemy color", &cfg.visuals.enemy_color);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Checkbox("Chams",    &cfg.visuals.chams);
            ImGui::SameLine();
            ImGui::Checkbox("UAV",      &cfg.visuals.uav);
            ImGui::SameLine();
            ImGui::Checkbox("Radar",    &cfg.visuals.radar);
            ImGui::SetNextItemWidth(-1);
            ImGui::ColorEdit3("##chamscolor", cfg.visuals.chams_color,
                              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            ImGui::SameLine();
            ImGui::TextColored(kTextMuted, "Chams color");
            help("Forces every pawn into the radar/sonar reveal path.");

            ImGui::Spacing();
            ImGui::TextColored(kTextMuted, "Render distance");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##dist", &cfg.visuals.max_distance, 10.f, 1000.f, "%.0f m");

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Aimbot")) {
            ImGui::Checkbox("Aimbot",       &cfg.aimbot.enabled);
            ImGui::Checkbox("Aim lock",     &cfg.aimbot.aim_lock);
            ImGui::Checkbox("Triggerbot",   &cfg.aimbot.triggerbot);
            ImGui::Checkbox("Magic bullet", &cfg.aimbot.magic_bullet);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(kTextMuted, "FOV radius");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##fov", &cfg.aimbot.fov, 1.f, 180.f, "%.0f deg");

            ImGui::TextColored(kTextMuted, "Smoothness");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##smooth", &cfg.aimbot.smoothness, 1.f, 20.f, "%.1f");

            ImGui::TextColored(kTextMuted, "Hitbox");
            const char* hitboxes[] = {"Head", "Chest", "Pelvis"};
            ImGui::SetNextItemWidth(-1);
            ImGui::Combo("##hitbox", &cfg.aimbot.hitbox, hitboxes, 3);
            cfg.aimbot.target_head = (cfg.aimbot.hitbox == 0);

            ImGui::Checkbox("Visible only", &cfg.aimbot.visible_only);

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Player")) {
            ImGui::Checkbox("God mode",         &cfg.player.god_mode);
            ImGui::Checkbox("Speed hack",       &cfg.player.speed_hack);
            ImGui::Checkbox("Infinite jump",    &cfg.player.infinite_jump);
            ImGui::Checkbox("Bunny jump",       &cfg.player.bunny_jump);
            ImGui::Checkbox("Jump boost",       &cfg.player.jump_boost);
            ImGui::Checkbox("Slide boost",      &cfg.player.slide_boost);
            ImGui::Checkbox("Free climb",       &cfg.player.free_climb);
            ImGui::Checkbox("Fast swim",        &cfg.player.fast_swim);
            ImGui::Checkbox("Fast parachute",   &cfg.player.fast_parachute);
            ImGui::Checkbox("Ability cooldown", &cfg.player.ability_cooldown);
            ImGui::Checkbox("Infinite ammo",    &cfg.player.infinite_ammo);

            ImGui::Spacing();
            ImGui::TextColored(kTextMuted, "Speed multiplier");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##speed", &cfg.player.speed_multiplier, 0.5f, 10.f, "%.2fx");

            ImGui::TextColored(kTextMuted, "Jump multiplier");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##jump", &cfg.player.jump_multiplier, 1.f, 8.f, "%.2fx");

            ImGui::TextColored(kTextMuted, "Slide multiplier");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##slide", &cfg.player.slide_multiplier, 1.f, 8.f, "%.2fx");

            ImGui::TextColored(kTextMuted, "Swim multiplier");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##swim", &cfg.player.swim_multiplier, 1.f, 8.f, "%.2fx");

            ImGui::TextColored(kTextMuted, "Parachute multiplier");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##chute", &cfg.player.parachute_multiplier, 1.f, 10.f, "%.2fx");

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Weapon")) {
            ImGui::Checkbox("No recoil",       &cfg.weapon.no_recoil);
            ImGui::Checkbox("No spread",       &cfg.weapon.no_spread);
            ImGui::Checkbox("Fast reload",     &cfg.weapon.fast_reload);
            ImGui::Checkbox("Fast switch",     &cfg.weapon.fast_switch);
            ImGui::Checkbox("Fast scope",      &cfg.weapon.fast_scope);
            ImGui::Checkbox("Rapid fire",      &cfg.weapon.rapid_fire);
            ImGui::Checkbox("No muzzle flash", &cfg.weapon.no_muzzle_flash);
            ImGui::Checkbox("No flash",        &cfg.weapon.no_flash);
            ImGui::Checkbox("No camera shake", &cfg.weapon.no_camera_shake);
            ImGui::Checkbox("Small crosshair", &cfg.weapon.small_crosshair);
            ImGui::Checkbox("Always knife",    &cfg.weapon.always_knife);
            ImGui::Checkbox("Infinite ammo",   &cfg.weapon.infinite_ammo);
            ImGui::Checkbox("Magic bullet",    &cfg.weapon.magic_bullet);

            ImGui::Spacing();
            ImGui::TextColored(kTextMuted, "Fire rate");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##fire", &cfg.weapon.fire_rate_multiplier, 1.f, 10.f, "%.2fx");

            ImGui::TextColored(kTextMuted, "Reload speed");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##reload", &cfg.weapon.reload_multiplier, 1.f, 10.f, "%.2fx");

            ImGui::TextColored(kTextMuted, "Damage");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##dmg", &cfg.weapon.damage_multiplier, 1.f, 20.f, "%.2fx");

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Unlockers")) {
            ImGui::TextColored(kTextMuted, "Local inventory / UI unlock pass");
            ImGui::Spacing();
            if (ImGui::Button("Unlock All Weapons", ImVec2(-1, 34))) {
                act.unlock_all = true;
                act.unlock_weapons = true;
            }
            ImGui::Spacing();
            if (ImGui::Button("Ultra Unlocker (Skins + Camos)", ImVec2(-1, 34))) {
                act.ultra_unlock = true;
                act.unlock_skins = true;
            }
            ImGui::Spacing();
            if (ImGui::Button("Skip Tutorial", ImVec2(-1, 34))) act.skip_tutorial = true;

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            if (ImGui::Button("Reload Config", ImVec2(-1, 0))) act.reload_config = true;
            if (ImGui::Button("Dump Scene Objects", ImVec2(-1, 0))) act.dump_scene = true;

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::Spacing();
    ImGui::Separator();
    const char* footer = "JAMES TOOL V1.0 | Built by Kanha";
    const float footer_w = ImGui::CalcTextSize(footer).x;
    const float avail_w  = ImGui::GetContentRegionAvail().x;
    if (avail_w > footer_w)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - footer_w) * 0.5f);
    ImGui::TextColored(ImVec4(0.45f, 0.50f, 0.48f, 1.00f), "%s", footer);

    ImGui::End();
}

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
