// menu.cpp - ImGui UI, ESP rendering and aimbot tick.

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
constexpr ImVec4 kPanel       = ImVec4(0.05f, 0.07f, 0.06f, 0.86f);
constexpr ImVec4 kPanelHeader = ImVec4(0.03f, 0.05f, 0.04f, 0.95f);

// ESP colours (packed for ImDrawList)
constexpr ImU32 kEspBox     = IM_COL32(0, 255, 140, 220);
constexpr ImU32 kEspLine    = IM_COL32(0, 255, 140, 120);
constexpr ImU32 kEspSkel    = IM_COL32(0, 220, 255, 200);
constexpr ImU32 kEspText    = IM_COL32(235, 255, 245, 255);
constexpr ImU32 kEspShadow  = IM_COL32(0, 0, 0, 160);

constexpr int kMaxEntities = 128;
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

    ImGui::SetNextWindowSize(ImVec2(440, 560), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60, 120), ImGuiCond_FirstUseEver);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar;

    if (!ImGui::Begin("##overlay_main", &cfg.menu_open, flags)) {
        ImGui::End();
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::TextUnformatted("UNITY OVERLAY");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextColored(kTextMuted, "| %zu offsets", offs.values.size());

    // --- branding block -----------------------------------------------------
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.03f, 0.05f, 0.04f, 0.65f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.00f, 0.78f, 0.27f, 0.30f));
    ImGui::BeginChild("##branding", ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::PushStyleColor(ImGuiCol_Text, kGold);
    ImGui::TextUnformatted("JAMES TOOL");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextColored(kGoldDim, "v1.0");
    ImGui::TextColored(kText, "Credits: James 254");
    ImGui::TextColored(kTextMuted, "Telegram: @JaMeS66699");
    ImGui::TextColored(kTextMuted, "Group: t.me/Harckdarkmod");
    ImGui::EndChild();
    ImGui::PopStyleColor(2);

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_None)) {

        // ------------------------------------------------------------------
        if (ImGui::BeginTabItem("ESP")) {
            ImGui::Spacing();
            ImGui::TextColored(kTextMuted, "Draw");
            ImGui::Spacing();

            ImGui::Checkbox("Line",     &cfg.visuals.draw_lines);
            ImGui::Checkbox("Box",      &cfg.visuals.draw_boxes);
            ImGui::Checkbox("Health",   &cfg.visuals.draw_health);
            ImGui::Checkbox("Name",     &cfg.visuals.draw_names);
            ImGui::Checkbox("Skeleton", &cfg.visuals.draw_skeleton);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextColored(kTextMuted, "Render distance");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##dist", &cfg.visuals.max_distance, 5.f, 500.f, "%.0f m");

            ImGui::EndTabItem();
        }

        // ------------------------------------------------------------------
        if (ImGui::BeginTabItem("Aimbot")) {
            ImGui::Spacing();
            ImGui::Checkbox("Aimbot",     &cfg.aimbot.enabled);
            ImGui::Checkbox("Aim Lock",   &cfg.aimbot.aim_lock);
            ImGui::Checkbox("Triggerbot", &cfg.aimbot.triggerbot);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextColored(kTextMuted, "FOV radius");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##fov", &cfg.aimbot.fov, 1.f, 180.f, "%.0f deg");

            ImGui::TextColored(kTextMuted, "Smoothness");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##smooth", &cfg.aimbot.smoothness, 1.f, 20.f, "%.1f");

            ImGui::Spacing();
            ImGui::TextColored(kTextMuted, "Target");
            if (ImGui::RadioButton("Head", cfg.aimbot.target_head)) cfg.aimbot.target_head = true;
            ImGui::SameLine();
            if (ImGui::RadioButton("Body", !cfg.aimbot.target_head)) cfg.aimbot.target_head = false;

            ImGui::EndTabItem();
        }

        // ------------------------------------------------------------------
        if (ImGui::BeginTabItem("Player")) {
            ImGui::Spacing();
            ImGui::Checkbox("No Clip",       &cfg.player.noclip);
            ImGui::Checkbox("Infinite Jump", &cfg.player.infinite_jump);

            ImGui::Spacing();
            ImGui::TextColored(kTextMuted, "Speed multiplier");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##speed", &cfg.player.speed_multiplier, 0.5f, 10.f, "%.1fx");

            ImGui::Spacing();
            ImGui::TextColored(kTextMuted, "Camera");
            ImGui::Checkbox("Freecam", &cfg.camera.freecam);

            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##fovc", &cfg.camera.fov, 30.f, 140.f, "FOV %.0f");

            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##ts", &cfg.camera.timescale, 0.f, 3.f, "Time %.2fx");

            ImGui::EndTabItem();
        }

        // ------------------------------------------------------------------
        if (ImGui::BeginTabItem("Unlockers")) {
            ImGui::Spacing();
            ImGui::TextColored(kTextMuted, "Writes unlock bitfield at UnlockFlags");
            ImGui::Spacing();

            if (ImGui::Button("Unlock All Weapons", ImVec2(-1, 32))) act.unlock_weapons = true;
            ImGui::Spacing();
            if (ImGui::Button("Unlock All Skins", ImVec2(-1, 32))) act.unlock_skins = true;

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Reload Config", ImVec2(-1, 0))) act.reload_config = true;
            if (ImGui::Button("Dump Scene Objects", ImVec2(-1, 0))) act.dump_scene = true;

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // --- footer -------------------------------------------------------------
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

ov::Vec2 lerp2(const ov::Vec2& a, const ov::Vec2& b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
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

    const ov::Resolved& r = reader.resolved();

    // Local player position, used for distance filtering.
    ov::Vec3 eye{-1e9f, -1e9f, -1e9f};
    bool have_eye = false;
    if (uintptr_t lp = reader.local_player(); lp && r.off_pos) {
        ov::Vec3 p = reader.safe_read<ov::Vec3>(lp + r.off_pos);
        if (p.x != 0.f || p.y != 0.f || p.z != 0.f) { eye = p; have_eye = true; }
    }

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float sw = (float)width;
    const float sh = (float)height;

    for (int i = 0; i < count; ++i) {
        const ov::Entity& e = ents[i];
        if (!e.alive && r.off_health) continue;

        ov::Vec2 feet, head;
        if (!ov::world_to_screen(e.pos, vp, sw, sh, feet)) continue;
        if (!ov::world_to_screen(e.head, vp, sw, sh, head)) continue;

        float box_h = feet.y - head.y;
        if (box_h < 4.0f || box_h > sh * 1.5f) continue;

        float dist = have_eye ? eye.distance(e.pos) : 0.f;
        if (have_eye && dist > cfg.visuals.max_distance) continue;

        const float box_w = box_h * 0.55f;
        const float left   = head.x - box_w * 0.5f;
        const float right  = head.x + box_w * 0.5f;
        const float top    = head.y;
        const float bottom = feet.y;

        if (cfg.visuals.draw_lines) {
            dl->AddLine(ImVec2(sw * 0.5f, sh), ImVec2(head.x, bottom),
                        kEspLine, 1.4f);
        }

        if (cfg.visuals.draw_boxes) {
            dl->AddRect(ImVec2(left - 1, top - 1), ImVec2(right + 1, bottom + 1),
                        IM_COL32(0, 0, 0, 150), 2.0f, 0, 3.0f);
            dl->AddRect(ImVec2(left, top), ImVec2(right, bottom),
                        kEspBox, 2.0f, 0, 1.5f);
        }

        if (cfg.visuals.draw_health && r.off_health) {
            float frac = e.health > 1.f ? e.health / 100.f : e.health;
            if (frac < 0.f) frac = 0.f;
            if (frac > 1.f) frac = 1.f;

            const float bar_w = 4.0f;
            const float bx = left - bar_w - 4.0f;
            dl->AddRectFilled(ImVec2(bx - 1, top - 1),
                              ImVec2(bx + bar_w + 1, bottom + 1),
                              IM_COL32(0, 0, 0, 150));
            dl->AddRectFilled(ImVec2(bx, bottom - (bottom - top) * frac),
                              ImVec2(bx + bar_w, bottom), health_color(frac));
        }

        if (cfg.visuals.draw_names) {
            const char* label = e.name[0] ? e.name : "object";
            char buf[64];
            snprintf(buf, sizeof(buf), "%s [%.0fm]", label, dist);

            ImVec2 ts = ImGui::CalcTextSize(buf);
            ImVec2 tp(head.x - ts.x * 0.5f, top - ts.y - 4.0f);
            dl->AddText(ImVec2(tp.x + 1, tp.y + 1), kEspShadow, buf);
            dl->AddText(tp, kEspText, buf);
        }

        if (cfg.visuals.draw_skeleton) {
            const ov::Vec2 neck  = lerp2(head, feet, 0.12f);
            const ov::Vec2 hip   = lerp2(head, feet, 0.55f);
            const ov::Vec2 sh_l  = {neck.x - box_w * 0.32f, neck.y + box_h * 0.04f};
            const ov::Vec2 sh_r  = {neck.x + box_w * 0.32f, neck.y + box_h * 0.04f};
            const ov::Vec2 el_l  = {left  - 2.0f, hip.y - box_h * 0.06f};
            const ov::Vec2 el_r  = {right + 2.0f, hip.y - box_h * 0.06f};
            const ov::Vec2 kn_l  = {hip.x - box_w * 0.22f, lerp2(head, feet, 0.80f).y};
            const ov::Vec2 kn_r  = {hip.x + box_w * 0.22f, lerp2(head, feet, 0.80f).y};

            const float th = 1.6f;
            dl->AddLine(ImVec2(head.x, head.y), ImVec2(neck.x, neck.y), kEspSkel, th);
            dl->AddLine(ImVec2(neck.x, neck.y), ImVec2(hip.x, hip.y),   kEspSkel, th);
            dl->AddLine(ImVec2(neck.x, neck.y), ImVec2(sh_l.x, sh_l.y), kEspSkel, th);
            dl->AddLine(ImVec2(neck.x, neck.y), ImVec2(sh_r.x, sh_r.y), kEspSkel, th);
            dl->AddLine(ImVec2(sh_l.x, sh_l.y), ImVec2(el_l.x, el_l.y), kEspSkel, th);
            dl->AddLine(ImVec2(sh_r.x, sh_r.y), ImVec2(el_r.x, el_r.y), kEspSkel, th);
            dl->AddLine(ImVec2(hip.x, hip.y), ImVec2(kn_l.x, kn_l.y),   kEspSkel, th);
            dl->AddLine(ImVec2(hip.x, hip.y), ImVec2(kn_r.x, kn_r.y),   kEspSkel, th);
            dl->AddLine(ImVec2(kn_l.x, kn_l.y), ImVec2(feet.x - box_w * 0.18f, feet.y), kEspSkel, th);
            dl->AddLine(ImVec2(kn_r.x, kn_r.y), ImVec2(feet.x + box_w * 0.18f, feet.y), kEspSkel, th);
        }
    }
}

void update_aimbot(const ov::Config& cfg, const ov::EntityReader& reader,
                   int width, int height) {
    if (!cfg.aimbot.enabled && !cfg.aimbot.triggerbot) return;
    (void)width;
    (void)height;

    const ov::Resolved& r = reader.resolved();

    uintptr_t yaw_addr   = r.camera_yaw;
    uintptr_t pitch_addr = r.camera_pitch;
    if (r.camera_indirect) {
        yaw_addr   = reader.safe_read<uintptr_t>(yaw_addr);
        pitch_addr = reader.safe_read<uintptr_t>(pitch_addr);
    }
    if (!yaw_addr || !pitch_addr) return;

    const float cur_yaw   = reader.safe_read<float>(yaw_addr);
    const float cur_pitch = reader.safe_read<float>(pitch_addr);

    const uintptr_t lp = reader.local_player();
    if (!lp || !r.off_pos) return;

    ov::Vec3 eye = reader.safe_read<ov::Vec3>(lp + r.off_pos);
    if (eye.x == 0.f && eye.y == 0.f && eye.z == 0.f) return;
    eye.y += r.head_height * 0.85f;      // roughly camera height

    static ov::Entity ents[kMaxEntities];
    const int count = reader.read_all(ents, kMaxEntities);
    if (count <= 0) {
        if (r.trigger_flag) ov::write_mem<int>(r.trigger_flag, 0);
        return;
    }

    const float max_angle = std::clamp(cfg.aimbot.fov, 0.5f, 180.0f);

    float best_angle = max_angle;
    float best_yaw = 0.f, best_pitch = 0.f;
    bool  found = false;

    for (int i = 0; i < count; ++i) {
        const ov::Entity& e = ents[i];
        if (r.off_health && !e.alive) continue;

        ov::Vec3 target = cfg.aimbot.target_head
                        ? e.head
                        : ov::Vec3{e.pos.x, e.pos.y + r.body_height, e.pos.z};

        ov::Vec3 d = target - eye;
        const float len = d.length();
        if (len < 0.5f) continue;

        const float yaw   = ov::rad2deg(atan2f(d.x, d.z));
        const float pitch = -ov::rad2deg(asinf(std::clamp(d.y / len, -1.f, 1.f)));

        const float dy = ov::normalize_angle(yaw   - cur_yaw);
        const float dp = ov::normalize_angle(pitch - cur_pitch);
        const float angle = sqrtf(dy * dy + dp * dp);

        if (angle < best_angle) {
            best_angle = angle;
            best_yaw   = dy;
            best_pitch = dp;
            found      = true;
        }
    }

    if (!found) {
        if (r.trigger_flag) ov::write_mem<int>(r.trigger_flag, 0);
        return;
    }

    if (cfg.aimbot.enabled) {
        float step = 1.0f / std::max(cfg.aimbot.smoothness, 1.0f);
        if (cfg.aimbot.aim_lock) step = std::max(step, 0.35f);

        const float new_yaw   = ov::normalize_angle(cur_yaw   + best_yaw   * step);
        const float new_pitch = std::clamp(cur_pitch + best_pitch * step, -89.0f, 89.0f);

        ov::write_mem<float>(yaw_addr, new_yaw);
        ov::write_mem<float>(pitch_addr, new_pitch);
    }

    if (cfg.aimbot.triggerbot && r.trigger_flag) {
        const bool on_target = best_angle < 1.5f;
        ov::write_mem<int>(r.trigger_flag, on_target ? 1 : 0);
    }
}

}  // namespace menu
