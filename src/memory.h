// memory.h - offsets + config loader, and process memory helpers.
//
// Reads two JSON files from the injected assets directory:
//   resolved_offsets.json  -> engine/field offsets for the target build
//   mod_config.json        -> persisted toggle state
//
// No external JSON dependency; small recursive-descent parser below.

#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <utility>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>

#ifdef __ANDROID__
#include <android/log.h>
#define OV_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "UnityOverlay", __VA_ARGS__)
#define OV_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "UnityOverlay", __VA_ARGS__)
#else
#define OV_LOGI(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define OV_LOGE(...) OV_LOGI(__VA_ARGS__)
#endif

namespace ov {

// ---------------------------------------------------------------------------
// Minimal JSON value
// ---------------------------------------------------------------------------
struct Json {
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type = NUL;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<Json> arr;
    std::map<std::string, Json> obj;

    const Json* find(const std::string& k) const {
        if (type != OBJ) return nullptr;
        auto it = obj.find(k);
        return it == obj.end() ? nullptr : &it->second;
    }
    bool  get_bool(const std::string& k, bool def = false) const {
        const Json* v = find(k); return (v && v->type == BOOL) ? v->b : def;
    }
    float get_float(const std::string& k, float def = 0.f) const {
        const Json* v = find(k); return (v && v->type == NUM) ? (float)v->num : def;
    }
    uintptr_t get_uptr(const std::string& k, uintptr_t def = 0) const {
        const Json* v = find(k);
        if (!v) return def;
        if (v->type == NUM) return (uintptr_t)v->num;
        if (v->type == STR) return (uintptr_t)strtoull(v->str.c_str(), nullptr, 0);
        return def;
    }
};

class JsonParser {
public:
    explicit JsonParser(const char* s) : p_(s) {}

    Json parse() { skip(); return value(); }

private:
    const char* p_;

    void skip() { while (*p_ && (unsigned char)*p_ <= ' ') ++p_; }

    void expect(char c) { skip(); if (*p_ == c) ++p_; }

    std::string string_lit() {
        expect('"');
        std::string out;
        while (*p_ && *p_ != '"') {
            if (*p_ == '\\') {
                ++p_;
                switch (*p_) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        // keep it simple: consume 4 hex digits, emit '?'
                        for (int i = 0; i < 4 && p_[1]; ++i) ++p_;
                        out += '?';
                        break;
                    }
                    default: out += *p_; break;
                }
            } else {
                out += *p_;
            }
            ++p_;
        }
        expect('"');
        return out;
    }

    Json value() {
        skip();
        Json v;
        if (*p_ == '{') {
            v.type = Json::OBJ;
            ++p_;
            skip();
            if (*p_ == '}') { ++p_; return v; }
            while (*p_) {
                skip();
                std::string key = string_lit();
                skip(); expect(':');
                v.obj[key] = value();
                skip();
                if (*p_ == ',') { ++p_; continue; }
                expect('}');
                break;
            }
        } else if (*p_ == '[') {
            v.type = Json::ARR;
            ++p_;
            skip();
            if (*p_ == ']') { ++p_; return v; }
            while (*p_) {
                v.arr.push_back(value());
                skip();
                if (*p_ == ',') { ++p_; continue; }
                expect(']');
                break;
            }
        } else if (*p_ == '"') {
            v.type = Json::STR;
            v.str = string_lit();
        } else if (!strncmp(p_, "true", 4)) {
            v.type = Json::BOOL; v.b = true; p_ += 4;
        } else if (!strncmp(p_, "false", 5)) {
            v.type = Json::BOOL; v.b = false; p_ += 5;
        } else if (!strncmp(p_, "null", 4)) {
            v.type = Json::NUL; p_ += 4;
        } else {
            v.type = Json::NUM;
            v.num = strtod(p_, const_cast<char**>(&p_));
        }
        return v;
    }
};

// ---------------------------------------------------------------------------
// Config state that the menu binds to
// ---------------------------------------------------------------------------
struct VisualsConfig {
    bool  draw_lines    = false;
    bool  draw_boxes    = true;
    bool  draw_health   = true;
    bool  draw_names    = true;
    bool  draw_skeleton = false;
    bool  draw_origin   = false;
    bool  draw_team     = false;
    bool  draw_distance = true;
    bool  enemy_color   = true;
    bool  chams         = false;
    bool  uav           = false;
    bool  radar          = false;
    float max_distance  = 250.f;
    float chams_color[3] = {1.f, 0.15f, 0.15f};
};

struct AimbotConfig {
    bool  enabled       = false;
    bool  aim_lock      = false;
    bool  triggerbot    = false;
    bool  magic_bullet  = false;
    bool  visible_only  = false;
    float fov           = 35.f;
    float smoothness    = 6.f;
    int   hitbox        = 0;      // 0 head, 1 chest, 2 pelvis
    bool  target_head   = true;
};

struct CameraConfig {
    bool  freecam    = false;
    float fov        = 60.f;
    float timescale  = 1.f;
};

struct PlayerConfig {
    bool  god_mode          = false;
    bool  noclip            = false;
    bool  infinite_jump     = false;
    bool  bunny_jump        = false;
    bool  speed_hack        = false;
    bool  jump_boost        = false;
    bool  slide_boost       = false;
    bool  free_climb        = false;
    bool  fast_swim         = false;
    bool  fast_parachute    = false;
    bool  ability_cooldown  = false;
    bool  infinite_ammo     = false;
    float speed_multiplier  = 1.6f;
    float jump_multiplier   = 1.8f;
    float slide_multiplier  = 2.0f;
    float swim_multiplier   = 2.0f;
    float parachute_multiplier = 3.0f;
};

struct WeaponConfig {
    bool  no_recoil        = false;
    bool  no_spread        = false;
    bool  fast_reload      = false;
    bool  fast_switch      = false;
    bool  fast_scope       = false;
    bool  rapid_fire       = false;
    bool  no_muzzle_flash  = false;
    bool  no_flash         = false;
    bool  no_camera_shake  = false;
    bool  small_crosshair  = false;
    bool  always_knife     = false;
    bool  infinite_ammo    = false;
    bool  magic_bullet     = false;
    float fire_rate_multiplier = 1.8f;
    float reload_multiplier    = 4.0f;
    float damage_multiplier    = 2.0f;
};

struct Config {
    VisualsConfig visuals;
    AimbotConfig  aimbot;
    CameraConfig  camera;
    PlayerConfig  player;
    WeaponConfig  weapon;
    bool          menu_open = true;
};

// ---------------------------------------------------------------------------
// Offsets resolved for this game build
// ---------------------------------------------------------------------------
struct Offsets {
    std::map<std::string, uintptr_t> values;
    std::map<std::string, double>    nums;

    uintptr_t get(const std::string& name) const {
        auto it = values.find(name);
        return it == values.end() ? 0 : it->second;
    }
    double num(const std::string& name, double def = 0.0) const {
        auto it = nums.find(name);
        return it == nums.end() ? def : it->second;
    }
    uintptr_t operator[](const std::string& name) const { return get(name); }
};

inline bool read_text_file(const std::string& path, std::string& out) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    struct stat st{};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) { close(fd); return false; }
    out.resize((size_t)st.st_size);
    size_t got = 0;
    while (got < out.size()) {
        ssize_t n = read(fd, &out[got], out.size() - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    out.resize(got);
    close(fd);
    return got > 0;
}

inline Offsets load_offsets(const std::string& assets_dir) {
    Offsets o;
    std::string text;
    if (!read_text_file(assets_dir + "/resolved_offsets.json", text)) return o;

    Json root = JsonParser(text.c_str()).parse();
    const Json* table = root.find("offsets");
    if (!table || table->type != Json::OBJ) table = &root;
    if (table->type != Json::OBJ) return o;

    for (const auto& kv : table->obj) {
        const Json& v = kv.second;
        if (v.type == Json::NUM) {
            o.nums[kv.first] = v.num;
            if (v.num >= 0 && v.num < 1e15) o.values[kv.first] = (uintptr_t)v.num;
        } else if (v.type == Json::STR) {
            uintptr_t p = (uintptr_t)strtoull(v.str.c_str(), nullptr, 0);
            o.values[kv.first] = p;
        }
    }
    return o;
}

inline void load_config(const std::string& assets_dir, Config& cfg) {
    std::string text;
    if (!read_text_file(assets_dir + "/mod_config.json", text)) return;
    Json root = JsonParser(text.c_str()).parse();
    if (root.type != Json::OBJ) return;

    if (const Json* v = root.find("visuals")) {
        cfg.visuals.draw_lines    = v->get_bool("lines", cfg.visuals.draw_lines);
        cfg.visuals.draw_boxes    = v->get_bool("boxes", cfg.visuals.draw_boxes);
        cfg.visuals.draw_health   = v->get_bool("health", cfg.visuals.draw_health);
        cfg.visuals.draw_names    = v->get_bool("names", cfg.visuals.draw_names);
        cfg.visuals.draw_skeleton = v->get_bool("skeleton", cfg.visuals.draw_skeleton);
        cfg.visuals.draw_team     = v->get_bool("team", cfg.visuals.draw_team);
        cfg.visuals.draw_distance = v->get_bool("distance", cfg.visuals.draw_distance);
        cfg.visuals.enemy_color   = v->get_bool("enemy_color", cfg.visuals.enemy_color);
        cfg.visuals.chams         = v->get_bool("chams", cfg.visuals.chams);
        cfg.visuals.uav           = v->get_bool("uav", cfg.visuals.uav);
        cfg.visuals.radar         = v->get_bool("radar", cfg.visuals.radar);
        cfg.visuals.max_distance  = v->get_float("max_distance", cfg.visuals.max_distance);
    }
    if (const Json* a = root.find("aimbot")) {
        cfg.aimbot.enabled      = a->get_bool("enabled", cfg.aimbot.enabled);
        cfg.aimbot.aim_lock     = a->get_bool("aim_lock", cfg.aimbot.aim_lock);
        cfg.aimbot.triggerbot   = a->get_bool("triggerbot", cfg.aimbot.triggerbot);
        cfg.aimbot.magic_bullet = a->get_bool("magic_bullet", cfg.aimbot.magic_bullet);
        cfg.aimbot.visible_only = a->get_bool("visible_only", cfg.aimbot.visible_only);
        cfg.aimbot.fov          = a->get_float("fov", cfg.aimbot.fov);
        cfg.aimbot.smoothness   = a->get_float("smoothness", cfg.aimbot.smoothness);
        cfg.aimbot.hitbox       = (int)a->get_float("hitbox", (float)cfg.aimbot.hitbox);
        cfg.aimbot.target_head  = a->get_bool("target_head", cfg.aimbot.target_head);
    }
    if (const Json* c = root.find("camera")) {
        cfg.camera.freecam   = c->get_bool("freecam", cfg.camera.freecam);
        cfg.camera.fov       = c->get_float("fov", cfg.camera.fov);
        cfg.camera.timescale = c->get_float("timescale", cfg.camera.timescale);
    }
    if (const Json* p = root.find("player")) {
        cfg.player.god_mode         = p->get_bool("god_mode", cfg.player.god_mode);
        cfg.player.noclip           = p->get_bool("noclip", cfg.player.noclip);
        cfg.player.infinite_jump    = p->get_bool("infinite_jump", cfg.player.infinite_jump);
        cfg.player.bunny_jump       = p->get_bool("bunny_jump", cfg.player.bunny_jump);
        cfg.player.speed_hack       = p->get_bool("speed_hack", cfg.player.speed_hack);
        cfg.player.jump_boost       = p->get_bool("jump_boost", cfg.player.jump_boost);
        cfg.player.slide_boost      = p->get_bool("slide_boost", cfg.player.slide_boost);
        cfg.player.free_climb       = p->get_bool("free_climb", cfg.player.free_climb);
        cfg.player.fast_swim        = p->get_bool("fast_swim", cfg.player.fast_swim);
        cfg.player.fast_parachute   = p->get_bool("fast_parachute", cfg.player.fast_parachute);
        cfg.player.ability_cooldown = p->get_bool("ability_cooldown", cfg.player.ability_cooldown);
        cfg.player.infinite_ammo    = p->get_bool("infinite_ammo", cfg.player.infinite_ammo);
        cfg.player.speed_multiplier = p->get_float("speed_multiplier", cfg.player.speed_multiplier);
        cfg.player.jump_multiplier  = p->get_float("jump_multiplier", cfg.player.jump_multiplier);
        cfg.player.slide_multiplier = p->get_float("slide_multiplier", cfg.player.slide_multiplier);
        cfg.player.swim_multiplier  = p->get_float("swim_multiplier", cfg.player.swim_multiplier);
        cfg.player.parachute_multiplier = p->get_float("parachute_multiplier", cfg.player.parachute_multiplier);
    }
    if (const Json* w = root.find("weapon")) {
        cfg.weapon.no_recoil       = w->get_bool("no_recoil", cfg.weapon.no_recoil);
        cfg.weapon.no_spread       = w->get_bool("no_spread", cfg.weapon.no_spread);
        cfg.weapon.fast_reload     = w->get_bool("fast_reload", cfg.weapon.fast_reload);
        cfg.weapon.fast_switch     = w->get_bool("fast_switch", cfg.weapon.fast_switch);
        cfg.weapon.fast_scope      = w->get_bool("fast_scope", cfg.weapon.fast_scope);
        cfg.weapon.rapid_fire      = w->get_bool("rapid_fire", cfg.weapon.rapid_fire);
        cfg.weapon.no_muzzle_flash = w->get_bool("no_muzzle_flash", cfg.weapon.no_muzzle_flash);
        cfg.weapon.no_flash        = w->get_bool("no_flash", cfg.weapon.no_flash);
        cfg.weapon.no_camera_shake = w->get_bool("no_camera_shake", cfg.weapon.no_camera_shake);
        cfg.weapon.small_crosshair = w->get_bool("small_crosshair", cfg.weapon.small_crosshair);
        cfg.weapon.always_knife    = w->get_bool("always_knife", cfg.weapon.always_knife);
        cfg.weapon.infinite_ammo   = w->get_bool("infinite_ammo", cfg.weapon.infinite_ammo);
        cfg.weapon.magic_bullet    = w->get_bool("magic_bullet", cfg.weapon.magic_bullet);
        cfg.weapon.fire_rate_multiplier = w->get_float("fire_rate_multiplier", cfg.weapon.fire_rate_multiplier);
        cfg.weapon.reload_multiplier    = w->get_float("reload_multiplier", cfg.weapon.reload_multiplier);
        cfg.weapon.damage_multiplier    = w->get_float("damage_multiplier", cfg.weapon.damage_multiplier);
    }
    cfg.menu_open = root.get_bool("menu_open", cfg.menu_open);
}

// ---------------------------------------------------------------------------
// Safe reads / writes
// ---------------------------------------------------------------------------
inline int self_mem_fd() {
    static int fd = open("/proc/self/mem", O_RDONLY);
    return fd;
}

// JamesTool ships modmenu_features.json as {groups:{<group>:{<key>:{value:..}}}}.
// Merge those dashboard toggles into the native feature set.
inline void load_feature_json(const std::string& assets_dir, Config& cfg) {
    std::string text;
    if (!read_text_file(assets_dir + "/mod_config.json", text)) return;
    Json root = JsonParser(text.c_str()).parse();
    if (root.type != Json::OBJ) return;
    const Json* groups = root.find("groups");
    if (!groups || groups->type != Json::OBJ) return;

    auto val = [](const Json* item) -> int {
        if (!item || item->type != Json::OBJ) return 0;
        const Json* v = item->find("value");
        if (v && v->type == Json::NUM) return (int)v->num;
        if (v && v->type == Json::BOOL) return v->b ? 1 : 0;
        return 0;
    };
    auto flag = [&val](const Json* item) -> bool { return val(item) != 0; };

    for (const auto& g : groups->obj) {
        if (g.second.type != Json::OBJ) continue;
        for (const auto& kv : g.second.obj) {
            const std::string& k = kv.first;
            const Json* item = &kv.second;
            if      (k == "esp.line")    cfg.visuals.draw_lines = flag(item);
            else if (k == "esp.box")     cfg.visuals.draw_boxes = flag(item);
            else if (k == "esp.health")  cfg.visuals.draw_health = flag(item);
            else if (k == "esp.distance")cfg.visuals.draw_distance = flag(item);
            else if (k == "esp.name")    cfg.visuals.draw_names = flag(item);
            else if (k == "esp.skeleton")cfg.visuals.draw_skeleton = flag(item);
            else if (k == "esp.radar")   cfg.visuals.radar = flag(item);
            else if (k == "esp.render_distance") cfg.visuals.max_distance = (float)val(item);
            else if (k == "aim.enabled") cfg.aimbot.enabled = flag(item);
            else if (k == "aim.lock")    cfg.aimbot.aim_lock = flag(item);
            else if (k == "aim.trigger") cfg.aimbot.triggerbot = flag(item);
            else if (k == "aim.fov")     cfg.aimbot.fov = (float)val(item);
            else if (k == "aim.smooth")  cfg.aimbot.smoothness = std::max(1.f, (float)val(item) / 10.f);
            else if (k == "aim.head")    cfg.aimbot.target_head = flag(item);
            else if (k == "aim.body")    { if (flag(item)) cfg.aimbot.hitbox = 2; }
            else if (k == "player.god")  cfg.player.god_mode = flag(item);
            else if (k == "player.speed")cfg.player.speed_hack = flag(item);
            else if (k == "player.jump") cfg.player.jump_boost = flag(item);
            else if (k == "player.ammo") cfg.player.infinite_ammo = flag(item);
            else if (k == "player.norecoil") cfg.weapon.no_recoil = flag(item);
            else if (k == "player.nospread") cfg.weapon.no_spread = flag(item);
        }
    }
}

inline bool mem_read(uintptr_t addr, void* out, size_t len) {
    if (!addr || !len) return false;
    struct iovec local  { out, len };
    struct iovec remote { reinterpret_cast<void*>(addr), len };
    if (process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == (ssize_t)len) return true;
    int fd = self_mem_fd();
    if (fd >= 0 && pread(fd, out, len, (off_t)addr) == (ssize_t)len) return true;
    return false;
}

inline bool write_mem(uintptr_t addr, const void* data, size_t len) {
    if (!addr || !len) return false;
    struct iovec local  { const_cast<void*>(data), len };
    struct iovec remote { reinterpret_cast<void*>(addr), len };
    if (process_vm_writev(getpid(), &local, 1, &remote, 1, 0) == (ssize_t)len) return true;
    int fd = open("/proc/self/mem", O_RDWR);
    if (fd >= 0) {
        ssize_t n = pwrite(fd, data, len, (off_t)addr);
        close(fd);
        if (n == (ssize_t)len) return true;
    }
    uintptr_t page = addr & ~(uintptr_t)(sysconf(_SC_PAGESIZE) - 1);
    if (mprotect((void*)page, len + (addr - page), PROT_READ | PROT_WRITE) != 0) return false;
    memcpy((void*)addr, data, len);
    return true;
}

template <typename T>
inline bool write_mem(uintptr_t addr, T value) {
    return write_mem(addr, &value, sizeof(T));
}

template <typename T>
inline T safe_read(uintptr_t addr) {
    T v{};
    mem_read(addr, &v, sizeof(T));
    return v;
}

// ---------------------------------------------------------------------------
// Math
// ---------------------------------------------------------------------------
struct Vec2 { float x = 0, y = 0; };

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float length() const { return sqrtf(x * x + y * y + z * z); }
    float distance(const Vec3& o) const { return (*this - o).length(); }
};

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};

inline constexpr float kPi = 3.14159265358979f;
inline float rad2deg(float r) { return r * 180.0f / kPi; }
inline float deg2rad(float d) { return d * kPi / 180.0f; }

inline float normalize_angle(float deg) {
    while (deg >  180.0f) deg -= 360.0f;
    while (deg <= -180.0f) deg += 360.0f;
    return deg;
}

inline Vec3 normalize_vec(const Vec3& v) {
    float l = v.length();
    return l > 1e-6f ? Vec3{v.x / l, v.y / l, v.z / l} : Vec3{0, 0, 1};
}

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Quat quat_from_yaw_pitch(float yaw_deg, float pitch_deg) {
    float hy = deg2rad(yaw_deg) * 0.5f;
    float hp = deg2rad(pitch_deg) * 0.5f;
    float cy = cosf(hy), sy = sinf(hy);
    float cp = cosf(hp), sp = sinf(hp);
    Quat q;
    q.w = cy * cp;
    q.x = cy * sp;
    q.y = sy * cp;
    q.z = -sy * sp;
    return q;
}

inline Quat quat_slerp(Quat a, Quat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0.f) { b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w; d = -d; }
    if (d > 0.9995f) {
        Quat r{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
               a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
        float l = sqrtf(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
        if (l > 1e-6f) { r.x /= l; r.y /= l; r.z /= l; r.w /= l; }
        return r;
    }
    float th = acosf(d);
    float s = sinf(th);
    float wa = sinf((1.f - t) * th) / s;
    float wb = sinf(t * th) / s;
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

// Column-major 4x4, Unity layout.
struct Mat4 { float m[16] = {}; };

inline Mat4 mat_identity() {
    Mat4 r;
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.f;
    return r;
}

inline Mat4 mat_mul(const Mat4& a, const Mat4& b) {
    Mat4 c;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.f;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[col * 4 + k];
            c.m[col * 4 + row] = s;
        }
    }
    return c;
}

inline void quat_basis(const Quat& q, Vec3& right, Vec3& up, Vec3& fwd) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    right = {1.f - 2.f * (y * y + z * z), 2.f * (x * y + w * z), 2.f * (x * z - w * y)};
    up    = {2.f * (x * y - w * z), 1.f - 2.f * (x * x + z * z), 2.f * (y * z + w * x)};
    fwd   = {2.f * (x * z + w * y), 2.f * (y * z - w * x), 1.f - 2.f * (x * x + y * y)};
}

inline Mat4 view_from_pos_rot(const Vec3& pos, const Quat& rot) {
    Vec3 r, u, f;
    quat_basis(rot, r, u, f);
    Mat4 v = mat_identity();
    v.m[0] = r.x; v.m[4] = r.y; v.m[8]  = r.z; v.m[12] = -dot(r, pos);
    v.m[1] = u.x; v.m[5] = u.y; v.m[9]  = u.z; v.m[13] = -dot(u, pos);
    v.m[2] = -f.x; v.m[6] = -f.y; v.m[10] = -f.z; v.m[14] = dot(f, pos);
    return v;
}

inline Mat4 perspective(float fov_deg, float aspect, float near_z, float far_z) {
    Mat4 p;
    float f = 1.f / tanf(deg2rad(fov_deg) * 0.5f);
    p.m[0] = f / (aspect > 0.01f ? aspect : 1.f);
    p.m[5] = f;
    p.m[10] = (far_z + near_z) / (near_z - far_z);
    p.m[11] = -1.f;
    p.m[14] = (2.f * far_z * near_z) / (near_z - far_z);
    return p;
}

inline bool world_to_screen(const Vec3& p, const Mat4& vp, float width, float height, Vec2& out) {
    const float* m = vp.m;
    float cx = m[0] * p.x + m[4] * p.y + m[8]  * p.z + m[12];
    float cy = m[1] * p.x + m[5] * p.y + m[9]  * p.z + m[13];
    float cw = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (cw < 0.001f) return false;
    float ndc_x = cx / cw;
    float ndc_y = cy / cw;
    out.x = (width  * 0.5f) * (1.0f + ndc_x);
    out.y = (height * 0.5f) * (1.0f - ndc_y);
    return out.x >= -width && out.x <= width * 2.0f &&
           out.y >= -height && out.y <= height * 2.0f;
}

// ---------------------------------------------------------------------------
// Module base lookup
// ---------------------------------------------------------------------------
inline uintptr_t module_base(const char* name) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, name)) {
            base = (uintptr_t)strtoull(line, nullptr, 16);
            break;
        }
    }
    fclose(f);
    return base;
}

// ---------------------------------------------------------------------------
// Offsets resolved into usable addresses
// ---------------------------------------------------------------------------
struct Resolved {
    uintptr_t base = 0;
    uintptr_t entity_list = 0;
    uintptr_t entity_count = 0;
    uintptr_t local_player = 0;
    uintptr_t view_matrix = 0;
    uintptr_t camera_yaw = 0;
    uintptr_t camera_pitch = 0;
    uintptr_t unlock_flags = 0;
    uintptr_t trigger_flag = 0;

    int    entity_stride = 8;
    int    max_entities  = 64;

    uintptr_t off_health = 0;
    uintptr_t off_team   = 0;
    uintptr_t off_name   = 0;
    uintptr_t off_pos    = 0;
    uintptr_t off_head   = 0;

    float  head_height = 1.65f;
    float  body_height = 1.20f;
    bool   camera_indirect = false;
    bool   valid = false;

    uintptr_t addr(const std::string& name) const {
        uintptr_t v = 0;
        auto it = offs_.find(name);
        if (it != offs_.end()) v = it->second;
        if (!v) return 0;
        if (v < 0x10000000ULL && base) return base + v;
        return v;
    }

    void resolve(const Offsets& o) {
        offs_ = o.values;
        base = module_base("libil2cpp.so");
        if (!base) base = module_base("libunity.so");
        if (!base) base = module_base("GameAssembly.so");

        entity_list  = addr("EntityList");
        entity_count = addr("EntityCount");
        local_player = addr("LocalPlayer");
        view_matrix  = addr("ViewMatrix");
        camera_yaw   = addr("CameraYaw");
        camera_pitch = addr("CameraPitch");
        unlock_flags = addr("UnlockFlags");
        trigger_flag = addr("TriggerFlag");

        entity_stride = (int)o.num("EntityStride", 8);
        max_entities  = (int)o.num("MaxEntities", 128);

        off_health = o.get("Health");
        off_team   = o.get("Team");
        off_name   = o.get("Name");
        off_pos    = o.get("Position");
        off_head   = o.get("Head");

        double h = o.num("HeadHeight", 165.0);
        if (h > 20.0) h *= 0.01;
        if (h <= 0.2 || h > 5.0) h = 1.65;
        head_height = (float)h;
        body_height = 1.20f;
        camera_indirect = o.get("CameraYawPtr") != 0;
        valid = base != 0;
    }

private:
    std::map<std::string, uintptr_t> offs_;
};

// ---------------------------------------------------------------------------
// IL2CPP runtime API
// ---------------------------------------------------------------------------
struct Il2CppApi {
    void* handle = nullptr;

    void* (*domain_get)() = nullptr;
    void* (*domain_get_assemblies)(void*, size_t*) = nullptr;
    void* (*assembly_get_image)(void*) = nullptr;
    void* (*class_from_name)(void*, const char*, const char*) = nullptr;
    void* (*class_get_parent)(void*) = nullptr;
    void* (*class_get_field_from_name)(void*, const char*) = nullptr;
    size_t (*field_get_offset)(void*) = nullptr;
    void* (*class_get_method_from_name)(void*, const char*, int) = nullptr;
    void* (*runtime_invoke)(void*, void*, void**, void**) = nullptr;
    void* (*class_get_type)(void*) = nullptr;
    void* (*type_get_object)(void*) = nullptr;
    void* (*object_get_class)(void*) = nullptr;
    int (*array_length)(void*) = nullptr;
    int (*array_element_size)(void*) = nullptr;
    int (*string_length)(void*) = nullptr;
    unsigned short* (*string_chars)(void*) = nullptr;
    void* (*thread_attach)(void*) = nullptr;
    void* (*thread_current)() = nullptr;
    const char* (*class_get_name)(void*) = nullptr;
    const char* (*class_get_namespace)(void*) = nullptr;
    const char* (*image_get_name)(void*) = nullptr;
    int (*class_instance_size)(void*) = nullptr;
    int (*class_value_size)(void*, uint32_t*) = nullptr;
    void* (*object_new)(void*) = nullptr;
    void* (*field_get_type)(void*) = nullptr;
    int (*field_get_flags)(void*) = nullptr;
    void* (*class_from_type)(void*) = nullptr;
    void* (*class_get_fields)(void*, void**) = nullptr;
    void* (*field_get_value_object)(void*, void*) = nullptr;
    void (*field_static_get_value)(void*, void*) = nullptr;
    bool (*class_is_assignable_from)(void*, void*) = nullptr;
    void (*field_set_value)(void*, void*, void*) = nullptr;
    void* (*string_new)(const char*) = nullptr;

    bool ok = false;

    template <typename T>
    static void bind_sym(void* h, const char* name, T& out) {
        void* p = h ? dlsym(h, name) : nullptr;
        if (!p) p = dlsym(RTLD_DEFAULT, name);
        out = reinterpret_cast<T>(p);
    }

    bool load() {
        if (ok) return true;
        const char* libs[] = {"libil2cpp.so", "libunity.so", "libmain.so", "libGame.so", nullptr};
        for (int i = 0; libs[i] && !handle; ++i) handle = dlopen(libs[i], RTLD_NOW | RTLD_LOCAL);

        bind_sym(handle, "il2cpp_domain_get", domain_get);
        bind_sym(handle, "il2cpp_domain_get_assemblies", domain_get_assemblies);
        bind_sym(handle, "il2cpp_assembly_get_image", assembly_get_image);
        bind_sym(handle, "il2cpp_class_from_name", class_from_name);
        bind_sym(handle, "il2cpp_class_get_parent", class_get_parent);
        bind_sym(handle, "il2cpp_class_get_field_from_name", class_get_field_from_name);
        bind_sym(handle, "il2cpp_field_get_offset", field_get_offset);
        bind_sym(handle, "il2cpp_class_get_method_from_name", class_get_method_from_name);
        bind_sym(handle, "il2cpp_runtime_invoke", runtime_invoke);
        bind_sym(handle, "il2cpp_class_get_type", class_get_type);
        bind_sym(handle, "il2cpp_type_get_object", type_get_object);
        bind_sym(handle, "il2cpp_object_get_class", object_get_class);
        bind_sym(handle, "il2cpp_array_length", array_length);
        bind_sym(handle, "il2cpp_array_element_size", array_element_size);
        bind_sym(handle, "il2cpp_string_length", string_length);
        bind_sym(handle, "il2cpp_string_chars", string_chars);
        bind_sym(handle, "il2cpp_thread_attach", thread_attach);
        bind_sym(handle, "il2cpp_thread_current", thread_current);
        bind_sym(handle, "il2cpp_class_get_name", class_get_name);
        bind_sym(handle, "il2cpp_class_get_namespace", class_get_namespace);
        bind_sym(handle, "il2cpp_image_get_name", image_get_name);
        bind_sym(handle, "il2cpp_class_instance_size", class_instance_size);
        bind_sym(handle, "il2cpp_class_value_size", class_value_size);
        bind_sym(handle, "il2cpp_object_new", object_new);
        bind_sym(handle, "il2cpp_field_get_type", field_get_type);
        bind_sym(handle, "il2cpp_field_get_flags", field_get_flags);
        bind_sym(handle, "il2cpp_class_from_type", class_from_type);
        bind_sym(handle, "il2cpp_class_get_fields", class_get_fields);
        bind_sym(handle, "il2cpp_field_get_value_object", field_get_value_object);
        bind_sym(handle, "il2cpp_field_set_value", field_set_value);
        bind_sym(handle, "il2cpp_field_static_get_value", field_static_get_value);
        bind_sym(handle, "il2cpp_class_is_assignable_from", class_is_assignable_from);
        bind_sym(handle, "il2cpp_string_new", string_new);

        ok = domain_get && domain_get_assemblies && assembly_get_image &&
             class_from_name && class_get_parent && class_get_field_from_name &&
             field_get_offset && class_get_method_from_name && runtime_invoke &&
             class_get_type && type_get_object && object_get_class &&
             array_length && string_length && string_chars && class_get_name;
        if (!ok) OV_LOGE("IL2CPP symbols missing (handle=%p)", handle);
        return ok;
    }
};

// ---------------------------------------------------------------------------
// Entity snapshot
// ---------------------------------------------------------------------------
struct Entity {
    uintptr_t addr = 0;
    uintptr_t weapon = 0;
    uint32_t  actor_id = 0;
    Vec3      pos{};
    Vec3      head{};
    Vec3      neck{};
    Vec3      spine{};
    Vec3      hips{};
    Vec3      l_ankle{};
    Vec3      r_ankle{};
    float     health = 0.f;
    float     max_health = 100.f;
    int       team = 0;
    bool      alive = false;
    bool      local = false;
    char      name[64] = {};
};

inline uint64_t now_ms() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

inline bool contains_ci(const char* s, const char* needle) {
    if (!s || !needle) return false;
    size_t n = strlen(needle);
    for (const char* p = s; *p; ++p) {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) ++i;
        if (i == n) return true;
    }
    return false;
}

class Game {
public:
    bool init() {
        if (ready_) return true;
        if (now_ms() - last_init_ < 1500) return false;
        last_init_ = now_ms();
        if (!api_.load()) return false;

        domain_ = api_.domain_get();
        if (!domain_) return false;
        if (api_.thread_attach) api_.thread_attach(domain_);

        size_t count = 0;
        void** assemblies = (void**)api_.domain_get_assemblies(domain_, &count);
        images_.clear();
        csharp_image_ = nullptr;
        for (size_t i = 0; assemblies && i < count; ++i) {
            void* image = api_.assembly_get_image(assemblies[i]);
            if (!image) continue;
            images_.push_back(image);
            const char* name = api_.image_get_name ? api_.image_get_name(image) : nullptr;
            if (name && strstr(name, "Assembly-CSharp")) csharp_image_ = image;
        }
        if (!csharp_image_ && !images_.empty()) csharp_image_ = images_[0];

        player_pawn_class_ = find_class("GameBase", "PlayerPawn");
        pawn_class_        = find_class("GameBase", "Pawn");
        unity_obj_class_   = find_class("UnityEngine", "Object");
        camera_class_      = find_class("UnityEngine", "Camera");
        transform_class_   = find_class("UnityEngine", "Transform");
        player_controller_class_ = find_class("GameEngine", "PlayerController");
        player_info_class_       = find_class("GameEngine", "PlayerInfo");
        renderer_class_          = find_class("UnityEngine", "Renderer");
        material_class_          = find_class("UnityEngine", "Material");

        ready_ = (player_pawn_class_ || pawn_class_) && unity_obj_class_;
        OV_LOGI("il2cpp ready=%d pawn=%p unityObj=%p camera=%p images=%zu",
                (int)ready_, player_pawn_class_, unity_obj_class_, camera_class_, images_.size());
        return ready_;
    }

    void bind(const Resolved& r) { res_ = r; }

    void tick(const Config& cfg, int width, int height) {
        last_w_ = width;
        last_h_ = height;
        if (!ready_ && !init()) return;
        // The render thread is not the thread that ran init(), so attach it.
        if (domain_ && api_.thread_attach) api_.thread_attach(domain_);

        uint64_t t = now_ms();
        if (t - last_scan_ >= 400) {
            last_scan_ = t;
            scan_pawns();
        }
        if (t - last_build_ >= 45) {
            last_build_ = t;
            build_entities();
        }
        if (t - last_camera_ >= 900) {
            last_camera_ = t;
            if (!camera_ || !camera_transform_) refresh_camera();
        }
        if (t - last_apply_ >= 80) {
            last_apply_ = t;
            apply_player_mods(cfg);
            apply_weapon_mods(cfg);
            apply_visuals(cfg);
        }
        if (cfg.aimbot.enabled || cfg.aimbot.triggerbot || cfg.aimbot.magic_bullet) {
            aim(cfg);
        } else if (fire_down_) {
            trigger(false);
        }
    }

    int copy_entities(Entity* out, int cap) const {
        if (!out || cap <= 0) return 0;
        int n = (int)entities_.size();
        if (n > cap) n = cap;
        for (int i = 0; i < n; ++i) out[i] = entities_[i];
        return n;
    }

    bool world_matrix(Mat4& out) const {
        if (!camera_ || !camera_transform_) return false;
        out = mat_identity();

        void* wm = method(camera_class_, "get_worldToCameraMatrix", 0);
        void* pm = method(camera_class_, "get_projectionMatrix", 0);
        Mat4 view, proj;
        bool ok_view = false, ok_proj = false;
        if (wm) {
            void* boxed = invoke(wm, camera_, nullptr);
            if (boxed) { mem_read((uintptr_t)boxed + 0x10, view.m, sizeof(view.m)); ok_view = true; }
        }
        if (pm) {
            void* boxed = invoke(pm, camera_, nullptr);
            if (boxed) { mem_read((uintptr_t)boxed + 0x10, proj.m, sizeof(proj.m)); ok_proj = true; }
        }

        if (!ok_view || !ok_proj) {
            Vec3 pos{};
            Quat rot{};
            void* tp = invoke(method(transform_class_, "get_position", 0), camera_transform_, nullptr);
            if (tp) mem_read((uintptr_t)tp + 0x10, &pos, sizeof(pos));
            void* tr = invoke(method(transform_class_, "get_rotation", 0), camera_transform_, nullptr);
            if (tr) mem_read((uintptr_t)tr + 0x10, &rot, sizeof(rot));
            if (!ok_view) view = view_from_pos_rot(pos, rot);
            if (!ok_proj) {
                float fov = 60.f;
                void* fm = method(camera_class_, "get_fieldOfView", 0);
                if (fm) { void* b = invoke(fm, camera_, nullptr); if (b) mem_read((uintptr_t)b + 0x10, &fov, 4); }
                float aspect = (last_h_ > 0) ? (float)last_w_ / (float)last_h_ : 1.f;
                proj = perspective(fov, aspect, 0.05f, 10000.f);
            }
        }
        out = mat_mul(proj, view);
        return out.m[15] != 0.f || out.m[11] != 0.f;
    }

    uintptr_t local_player() const { return (uintptr_t)local_; }

    bool local_entity(Entity& out) const {
        for (const Entity& e : entities_) {
            if (e.local) { out = e; return true; }
        }
        return false;
    }

    bool local_position(Vec3& p) const {
        if (!local_) return false;
        p = pawn_position(local_);
        return p.x != 0.f || p.y != 0.f || p.z != 0.f;
    }

    void unlock_all(bool skins, bool tutorial) {
        if (!ready_ && !init()) return;
        if (res_.unlock_flags) {
            uint32_t flags = safe_read<uint32_t>(res_.unlock_flags) | 0x3;
            write_mem<uint32_t>(res_.unlock_flags, flags);
        }
        unlock_loaded_objects(skins);
        if (tutorial) skip_tutorial();
    }

    void dump() const {
        OV_LOGI("entities=%zu local=%p camera=%p", entities_.size(), local_, camera_);
        for (size_t i = 0; i < entities_.size(); ++i) {
            const Entity& e = entities_[i];
            OV_LOGI(" [%zu] %p hp=%.0f/%.0f team=%d local=%d actor=%u pos=(%.1f %.1f %.1f) name=%s",
                    i, (void*)e.addr, e.health, e.max_health, e.team, (int)e.local,
                    e.actor_id, e.pos.x, e.pos.y, e.pos.z, e.name);
        }
    }

    const Resolved& resolved() const { return res_; }
    bool ready() const { return ready_; }

private:
    Il2CppApi api_;
    Resolved res_;
    bool ready_ = false;
    void* domain_ = nullptr;
    void* csharp_image_ = nullptr;
    std::vector<void*> images_;

    void* player_pawn_class_ = nullptr;
    void* pawn_class_ = nullptr;
    void* unity_obj_class_ = nullptr;
    void* camera_class_ = nullptr;
    void* transform_class_ = nullptr;
    void* player_controller_class_ = nullptr;
    void* damage_info_class_ = nullptr;
    void* player_info_class_ = nullptr;
    void* renderer_class_ = nullptr;
    void* material_class_ = nullptr;

    void* local_ = nullptr;
    int local_team_ = 0;
    void* camera_ = nullptr;
    void* camera_transform_ = nullptr;
    void* player_controller_ = nullptr;
    void* player_info_ = nullptr;

    uint64_t last_init_ = 0, last_scan_ = 0, last_build_ = 0, last_camera_ = 0, last_apply_ = 0;
    uint64_t last_magic_ = 0;
    uint64_t last_names_ = 0;
    uint64_t last_chams_ = 0;
    int last_w_ = 1080, last_h_ = 2400;

    std::vector<uintptr_t> pawns_;
    std::vector<Entity> entities_;
    std::map<uintptr_t, std::string> names_;
    mutable std::map<std::pair<void*, std::string>, size_t> field_cache_;
    mutable std::map<std::pair<void*, std::string>, void*> method_cache_;
    std::map<void*, void*> type_cache_;
    std::map<std::pair<uintptr_t, size_t>, float> base_f_;
    std::map<std::pair<uintptr_t, size_t>, int> base_i_;
    std::map<std::pair<uintptr_t, size_t>, uint8_t> base_b_;
    std::map<std::pair<uintptr_t, size_t>, Vec3> base_vec_;
    std::set<std::pair<uintptr_t, size_t>> active_;
    std::map<uintptr_t, uintptr_t> cham_mats_;
    float last_cham_color_[3] = {-1.f, -1.f, -1.f};
    bool fire_down_ = false;
    mutable bool exc_logged_ = false;

    void* find_class(const char* ns, const char* name) {
        if (!api_.class_from_name) return nullptr;
        if (csharp_image_) {
            void* k = api_.class_from_name(csharp_image_, ns, name);
            if (k) return k;
        }
        for (void* image : images_) {
            if (!image || image == csharp_image_) continue;
            void* k = api_.class_from_name(image, ns, name);
            if (k) return k;
        }
        return nullptr;
    }

    size_t field_off(void* klass, const char* name) const {
        if (!klass || !api_.class_get_field_from_name) return 0;
        auto key = std::make_pair(klass, std::string(name));
        auto it = field_cache_.find(key);
        if (it != field_cache_.end()) return it->second;
        for (void* k = klass; k; k = api_.class_get_parent(k)) {
            void* f = api_.class_get_field_from_name(k, name);
            if (f) {
                size_t o = api_.field_get_offset(f);
                field_cache_[key] = o;
                return o;
            }
        }
        field_cache_[key] = 0;
        return 0;
    }

    void* method(void* klass, const char* name, int argc) const {
        if (!klass || !api_.class_get_method_from_name) return nullptr;
        auto key = std::make_pair(klass, std::string(name) + "#" + std::to_string(argc));
        auto it = method_cache_.find(key);
        if (it != method_cache_.end()) return it->second;
        for (void* k = klass; k; k = api_.class_get_parent(k)) {
            void* m = api_.class_get_method_from_name(k, name, argc);
            if (m) { method_cache_[key] = m; return m; }
        }
        method_cache_[key] = nullptr;
        return nullptr;
    }

    void* type_object(void* klass) {
        auto it = type_cache_.find(klass);
        if (it != type_cache_.end()) return it->second;
        void* t = api_.class_get_type(klass);
        void* o = t ? api_.type_get_object(t) : nullptr;
        type_cache_[klass] = o;
        return o;
    }

    void* invoke(void* m, void* obj, void** params) const {
        if (!m) return nullptr;
        void* exc = nullptr;
        void* r = api_.runtime_invoke(m, obj, params, &exc);
        if (exc && !exc_logged_) {
            exc_logged_ = true;
            OV_LOGE("IL2CPP exception on invoke %p", m);
        }
        return r;
    }

    void* find_objects(void* klass) {
        if (!klass) return nullptr;
        void* type = type_object(klass);
        if (!type) return nullptr;
        void* args[1] = {type};
        void* m = method(unity_obj_class_, "FindObjectsOfType", 1);
        void* arr = invoke(m, nullptr, args);
        if (arr) return arr;

        void* resources = find_class("UnityEngine", "Resources");
        m = method(resources, "FindObjectsOfTypeAll", 1);
        return invoke(m, nullptr, args);
    }

    static void* array_at(void* arr, int i) {
        if (!arr) return nullptr;
        return safe_read<void*>((uintptr_t)arr + 0x20 + (uintptr_t)i * sizeof(void*));
    }

    bool is_local_pawn(void* p) {
        if (field_bool(p, pawn_class_, "m_IsControlledPawn")) return true;
        if (field_bool(p, player_pawn_class_, "<IsLocalObj>k__BackingField")) return true;
        if (field_bool(p, player_pawn_class_, "m_IsLocalObj")) return true;
        if (field_bool(p, player_pawn_class_, "IsLocalObj")) return true;
        return false;
    }

    // AllPawns lives on BaseGame, reachable through the static GamePlay.s_Game.
    // PlayerPawn is a plain managed class, so FindObjectsOfType never sees it.
    bool collect_pawns_from_game() {
        if (!api_.field_static_get_value) return false;
        void* gameplay_cls = find_class("GameEngine", "GamePlay");
        if (!gameplay_cls) return false;
        void* game_field = api_.class_get_field_from_name(gameplay_cls, "s_Game");
        if (!game_field) return false;
        void* base_game = nullptr;
        api_.field_static_get_value(game_field, &base_game);
        if (!base_game) return false;

        void* base_cls = find_class("GameBase", "BaseGame");
        size_t all_off = base_cls ? field_off(base_cls, "AllPawns") : 0;
        if (!all_off) all_off = 0x160;
        void* list = safe_read<void*>((uintptr_t)base_game + all_off);
        if (!list) return false;

        void* items = safe_read<void*>((uintptr_t)list + 0x10);
        int size = safe_read<int>((uintptr_t)list + 0x18);
        if (!items || size <= 0 || size > 4096) return false;

        size_t info_off = field_off(pawn_class_, "m_AttackableInfo");
        if (!info_off) return false;

        for (int i = 0; i < size; ++i) {
            void* p = array_at(items, i);
            if (!p) continue;
            if (!safe_read<void*>((uintptr_t)p + info_off)) continue;
            pawns_.push_back((uintptr_t)p);
        }
        return !pawns_.empty();
    }

    void scan_pawns() {
        pawns_.clear();
        local_ = nullptr;

        if (!collect_pawns_from_game()) {
            void* cls = player_pawn_class_ ? player_pawn_class_ : pawn_class_;
            void* arr = find_objects(cls);
            int n = arr ? api_.array_length(arr) : 0;
            if (n < 0) n = 0;
            if (n > 256) n = 256;
            for (int i = 0; i < n; ++i) {
                void* p = array_at(arr, i);
                if (p) pawns_.push_back((uintptr_t)p);
            }
        }

        for (uintptr_t p : pawns_) {
            if (is_local_pawn((void*)p)) { local_ = (void*)p; break; }
        }

        if (!camera_) refresh_camera();



        OV_LOGI("scan: pawns=%zu local=%p controller=%p", pawns_.size(), local_, player_controller_);
    }

    void refresh_camera() {
        if (!camera_class_) camera_class_ = find_class("UnityEngine", "Camera");
        if (!camera_class_) return;
        void* m = method(camera_class_, "get_main", 0);
        void* cam = invoke(m, nullptr, nullptr);
        if (!cam) return;
        camera_ = cam;
        camera_transform_ = invoke(method(camera_class_, "get_transform", 0), camera_, nullptr);
    }

    static Vec3 vec3_boxed(void* boxed) {
        Vec3 v{};
        if (boxed) mem_read((uintptr_t)boxed + 0x10, &v, sizeof(v));
        return v;
    }

    static Quat quat_boxed(void* boxed) {
        Quat q{};
        if (boxed) mem_read((uintptr_t)boxed + 0x10, &q, sizeof(q));
        return q;
    }

    Vec3 transform_pos(void* transform) const {
        if (!transform) return {};
        return vec3_boxed(invoke(method(transform_class_, "get_position", 0), transform, nullptr));
    }

    Vec3 bone_pos(void* pawn, const char* name, void* klass) const {
        size_t off = field_off(klass, name);
        if (!off) return {};
        void* tr = safe_read<void*>((uintptr_t)pawn + off);
        return transform_pos(tr);
    }

    Vec3 pawn_head(void* pawn) const {
        Vec3 h = vec3_boxed(invoke(method(player_pawn_class_, "get_HeadPosition", 0), pawn, nullptr));
        if (h.x == 0.f && h.y == 0.f && h.z == 0.f)
            h = vec3_boxed(invoke(method(player_pawn_class_, "GetRealPawnHeadPosition", 0), pawn, nullptr));
        if (h.x == 0.f && h.y == 0.f && h.z == 0.f)
            h = bone_pos(pawn, "m_HeadBone", pawn_class_);
        return h;
    }

    Vec3 pawn_position(void* pawn) const {
        void* root = invoke(method(player_pawn_class_, "GetRootTransform", 0), pawn, nullptr);
        Vec3 p = transform_pos(root);
        if (p.x == 0.f && p.y == 0.f && p.z == 0.f) {
            void* bip = invoke(method(player_pawn_class_, "GetBipTransform", 0), pawn, nullptr);
            p = transform_pos(bip);
            if (p.x != 0.f || p.y != 0.f || p.z != 0.f) p.y -= 0.85f;
        }
        if (p.x == 0.f && p.y == 0.f && p.z == 0.f) {
            Vec3 h = pawn_head(pawn);
            p = {h.x, h.y - 1.65f, h.z};
        }
        return p;
    }

    std::string read_string(void* s) {
        if (!s || !api_.string_length || !api_.string_chars) return {};
        int n = api_.string_length(s);
        if (n <= 0) return {};
        if (n > 63) n = 63;
        unsigned short* chars = api_.string_chars(s);
        std::string out;
        out.reserve((size_t)n);
        for (int i = 0; i < n; ++i) {
            unsigned short c = chars ? chars[i] : 0;
            if (c < 0x80) out.push_back((char)c);
            else if (c < 0x800) {
                out.push_back((char)(0xC0 | (c >> 6)));
                out.push_back((char)(0x80 | (c & 0x3F)));
            } else {
                out.push_back((char)(0xE0 | (c >> 12)));
                out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
                out.push_back((char)(0x80 | (c & 0x3F)));
            }
        }
        return out;
    }

    float field_f32(void* obj, void* klass, const char* name) {
        size_t o = field_off(klass, name);
        return o ? safe_read<float>((uintptr_t)obj + o) : 0.f;
    }
    int field_i32(void* obj, void* klass, const char* name) {
        size_t o = field_off(klass, name);
        return o ? safe_read<int>((uintptr_t)obj + o) : 0;
    }
    uint32_t field_u32(void* obj, void* klass, const char* name) {
        size_t o = field_off(klass, name);
        return o ? safe_read<uint32_t>((uintptr_t)obj + o) : 0;
    }
    bool field_bool(void* obj, void* klass, const char* name) {
        size_t o = field_off(klass, name);
        return o ? safe_read<uint8_t>((uintptr_t)obj + o) != 0 : false;
    }
    void set_f32(void* obj, void* klass, const char* name, float v) {
        size_t o = field_off(klass, name);
        if (o && obj) write_mem<float>((uintptr_t)obj + o, v);
    }
    void set_i32(void* obj, void* klass, const char* name, int v) {
        size_t o = field_off(klass, name);
        if (o && obj) write_mem<int>((uintptr_t)obj + o, v);
    }
    void set_bool(void* obj, void* klass, const char* name, bool v) {
        size_t o = field_off(klass, name);
        if (o && obj) write_mem<uint8_t>((uintptr_t)obj + o, v ? 1 : 0);
    }

    void build_entities() {
        entities_.clear();
        local_team_ = 0;
        if (pawns_.empty()) return;

        void* info_cls = find_class("GameEngine", "AttackableTargetInfo");
        size_t info_off = field_off(pawn_class_, "m_AttackableInfo");
        size_t hp_off = info_cls ? field_off(info_cls, "m_Health") : 0;
        size_t maxhp_off = info_cls ? field_off(info_cls, "m_MaxHealth") : 0;
        size_t camp_off = info_cls ? field_off(info_cls, "m_Camp") : 0;
        size_t actor_off = info_cls ? field_off(info_cls, "m_ActorID") : 0;

        uint64_t t = now_ms();
        bool refresh_names = (t - last_names_ > 2500);
        if (refresh_names) last_names_ = t;

        for (uintptr_t p : pawns_) {
            void* info = info_off ? safe_read<void*>(p + info_off) : nullptr;
            float hp = (info && hp_off) ? safe_read<float>((uintptr_t)info + hp_off) : 0.f;
            float maxhp = (info && maxhp_off) ? safe_read<float>((uintptr_t)info + maxhp_off) : 100.f;
            int team = (info && camp_off) ? safe_read<int>((uintptr_t)info + camp_off) : 0;
            uint32_t actor = (info && actor_off) ? safe_read<uint32_t>((uintptr_t)info + actor_off) : 0;

            if (!info) {
                void* hb = invoke(method(player_pawn_class_, "get_Health", 0), (void*)p, nullptr);
                if (hb) mem_read((uintptr_t)hb + 0x10, &hp, sizeof(hp));
                if (hp == 0.f) hp = field_f32((void*)p, player_pawn_class_, "m_Health");
                maxhp = 100.f;
                void* tb = invoke(method(player_pawn_class_, "get_TeamId", 0), (void*)p, nullptr);
                uint32_t tv = 0;
                if (tb) mem_read((uintptr_t)tb + 0x10, &tv, sizeof(tv));
                team = (int)tv;
            }
            if (maxhp <= 0.f) maxhp = 100.f;

            bool alive = field_bool((void*)p, pawn_class_, "m_IsAlive");
            if (!alive && hp <= 0.f) continue;

            Entity e;
            e.addr = p;
            e.actor_id = actor;
            e.health = hp;
            e.max_health = maxhp;
            e.team = team;
            e.alive = true;
            e.local = ((void*)p == local_);
            if (e.local) local_team_ = e.team;
            e.pos = pawn_position((void*)p);
            e.head = pawn_head((void*)p);
            if (e.head.x == 0.f && e.head.y == 0.f && e.head.z == 0.f)
                e.head = {e.pos.x, e.pos.y + 1.65f, e.pos.z};

            e.neck  = bone_pos((void*)p, "m_NeckBone", player_pawn_class_);
            e.spine = bone_pos((void*)p, "m_SpineBone", player_pawn_class_);
            e.hips  = bone_pos((void*)p, "m_HipsBone", player_pawn_class_);
            e.l_ankle = bone_pos((void*)p, "m_LeftAnkleBone", player_pawn_class_);
            e.r_ankle = bone_pos((void*)p, "m_RightAnkleBone", player_pawn_class_);

            auto it = names_.find(p);
            if (it != names_.end()) {
                snprintf(e.name, sizeof(e.name), "%s", it->second.c_str());
            } else if (refresh_names) {
                std::string n = read_string(invoke(method(player_pawn_class_, "get_PlayerName", 0), (void*)p, nullptr));
                if (n.empty()) n = "player";
                names_[p] = n;
                snprintf(e.name, sizeof(e.name), "%s", n.c_str());
            }
            entities_.push_back(e);
        }
    }

    void apply_player_mods(const Config& cfg) {
        if (!local_) return;
        const PlayerConfig& pc = cfg.player;

        size_t info_off = field_off(pawn_class_, "m_AttackableInfo");
        void* info = info_off ? safe_read<void*>((uintptr_t)local_ + info_off) : nullptr;
        void* info_cls = find_class("GameEngine", "AttackableTargetInfo");

        if (info && info_cls) {
            fixed_f(info, info_cls, "m_Health", 1000.f, pc.god_mode);
            fixed_f(info, info_cls, "m_MaxHealth", 1000.f, pc.god_mode);
        }
        fixed_b(local_, pawn_class_, "m_IsAlive", true, pc.god_mode);
        fixed_i(local_, pawn_class_, "GodBuffId", 1, pc.god_mode);
        fixed_f(local_, pawn_class_, "m_GunShieldHp", 1000.f, pc.god_mode);

        const bool fast = pc.speed_hack || pc.noclip;
        const float sm = pc.noclip ? 8.f : pc.speed_multiplier;
        scale_f(local_, pawn_class_, "m_MaxMoveSpeed", sm, fast);
        scale_f(local_, pawn_class_, "m_MaxSpeed", sm, fast);
        scale_f(local_, pawn_class_, "m_HorizontalMaxSpeed", sm, fast);
        scale_f(local_, pawn_class_, "m_WalkingSpeedAddPercent", sm, fast);
        scale_f(local_, pawn_class_, "m_FloatingMaxSpeed", sm, fast);

        const bool jump = pc.jump_boost || pc.bunny_jump;
        const float jm = pc.jump_multiplier;
        scale_f(local_, pawn_class_, "m_InitialVerticalSpeed", jm, jump);
        scale_f(local_, pawn_class_, "m_InitialMaxJumpHeight", jm, jump);
        scale_f(local_, pawn_class_, "m_MaxJumpHeightScale", jm, jump);
        scale_f(local_, pawn_class_, "m_JumpMagicNumber", jm, jump);
        scale_f(local_, pawn_class_, "m_JumpUpInitVelocity", jm, jump);
        scale_f(local_, pawn_class_, "m_JumpUpScaleMaxHeight", jm, jump);

        const bool inf_jump = pc.infinite_jump || pc.bunny_jump;
        fixed_b(local_, pawn_class_, "m_UseJumpLimit", false, inf_jump);
        fixed_f(local_, pawn_class_, "m_JumpLimitTime", 0.03f, inf_jump);
        fixed_f(local_, pawn_class_, "m_JumpUpLimitTime", 0.03f, inf_jump);
        fixed_f(local_, pawn_class_, "m_JumpLimitDecreaseRate", 0.f, inf_jump);

        fixed_f(local_, pawn_class_, "m_SlideCD", 0.f, pc.slide_boost);
        scale_f(local_, pawn_class_, "PhysWalkingOffset_MoveSlideSpeed", pc.slide_multiplier, pc.slide_boost);
        scale_f(local_, pawn_class_, "PhysWalkingOffset_MoveSlideDistance", pc.slide_multiplier, pc.slide_boost);
        scale_f(local_, pawn_class_, "m_AutoSlideSpeed", pc.slide_multiplier, pc.slide_boost);
        fixed_b(local_, pawn_class_, "m_UseSlideTackleSpeed", true, pc.slide_boost);
        fixed_b(local_, pawn_class_, "m_UseSpaceSlidingSpeed", true, pc.slide_boost);

        scale_f(local_, pawn_class_, "m_ClimbUpHeight", 2.f, pc.free_climb);
        scale_f(local_, pawn_class_, "m_MaxClimbClientForceMoveDistance", 2.f, pc.free_climb);
        scale_f(local_, pawn_class_, "m_MaxClimbFallingSpeed", 2.f, pc.free_climb);
        scale_f(local_, pawn_class_, "m_ClimbMantleWidth", 2.f, pc.free_climb);
        scale_f(local_, pawn_class_, "m_ClimbOverVelocityUp", 2.f, pc.free_climb);
        scale_f(local_, pawn_class_, "m_ClimbOverVelocityForward", 2.f, pc.free_climb);
        scale_f(local_, pawn_class_, "m_ClimbOverGravity", 0.35f, pc.free_climb);
        fixed_f(local_, pawn_class_, "m_MaxClimbOverTime", 0.15f, pc.free_climb);

        const float swim = pc.swim_multiplier;
        scale_f(local_, pawn_class_, "m_SwimmingMaxSpeed", swim, pc.fast_swim);
        scale_f(local_, pawn_class_, "m_SwimmingSprintMaxSpeed", swim, pc.fast_swim);
        scale_f(local_, pawn_class_, "m_UnderwaterSwimmingMaxSpeed", swim, pc.fast_swim);
        scale_f(local_, pawn_class_, "m_UnderwaterSwimmingSprintMaxSpeed", swim, pc.fast_swim);
        scale_f(local_, pawn_class_, "m_DivingMaxSpeed", swim, pc.fast_swim);
        fixed_f(local_, pawn_class_, "m_SwimPutWeaponTime", 0.01f, pc.fast_swim);
        fixed_f(local_, pawn_class_, "m_SwimGetWeaponTime", 0.01f, pc.fast_swim);

        scale_f(local_, player_pawn_class_, "m_PhysSkisMaxSpeedScale", pc.parachute_multiplier, pc.fast_parachute);
        scale_f(local_, player_pawn_class_, "m_SimulatePhysSkisSpeedUp", pc.parachute_multiplier, pc.fast_parachute);

        fixed_b(local_, pawn_class_, "<SkillLock>k__BackingField", false, pc.ability_cooldown);
        fixed_b(local_, pawn_class_, "m_QuickUseUltSkill", true, pc.ability_cooldown);
        fixed_b(local_, pawn_class_, "HasReleaseSkillButton", true, pc.ability_cooldown);
        fixed_b(local_, pawn_class_, "m_IsUsingWeaponSkill", false, pc.ability_cooldown);
        fixed_f(local_, player_pawn_class_, "m_SmallActionCD", 0.f, pc.ability_cooldown);
        fixed_f(local_, player_pawn_class_, "m_CurrentSmallActionCD", 0.f, pc.ability_cooldown);

        fixed_f(local_, pawn_class_, "m_LeftFlashEffectTime", 0.f, cfg.weapon.no_flash);
        fixed_f(local_, pawn_class_, "m_LeftFlashDroneTime", 0.f, cfg.weapon.no_flash);
        if (player_info_ && player_info_class_) {
            fixed_f(player_info_, player_info_class_, "m_FlashBangTimeScale", 0.f, cfg.weapon.no_flash);
        }

        const bool reveal = cfg.visuals.uav || cfg.visuals.radar;
        for (const Entity& e : entities_) {
            if (e.local) continue;
            void* p = (void*)e.addr;
            fixed_b(p, pawn_class_, "m_ShowOnRadarBySonar", true, reveal);
            fixed_b(p, pawn_class_, "m_ShowOnRadarByThreat", true, reveal);
            fixed_b(p, pawn_class_, "CachePawnRadarShowUAV", true, reveal);
            fixed_f(p, pawn_class_, "m_ShowBySonarDuration", 99999.f, reveal);
        }
    }

    void scale_f(void* obj, void* klass, const char* name, float mult, bool on) {
        if (!obj || !klass) return;
        size_t off = field_off(klass, name);
        if (!off) return;
        auto key = std::make_pair((uintptr_t)obj, off);
        auto it = base_f_.find(key);
        if (it == base_f_.end()) {
            float cur = safe_read<float>((uintptr_t)obj + off);
            it = base_f_.emplace(key, cur).first;
        }
        if (!on) {
            if (active_.erase(key)) write_mem<float>((uintptr_t)obj + off, it->second);
            return;
        }
        float v = it->second * mult;
        if (v > 1e9f) v = 1e9f;
        write_mem<float>((uintptr_t)obj + off, v);
        active_.insert(key);
    }

    void fixed_f(void* obj, void* klass, const char* name, float value, bool on) {
        if (!obj || !klass) return;
        size_t off = field_off(klass, name);
        if (!off) return;
        auto key = std::make_pair((uintptr_t)obj, off);
        auto it = base_f_.find(key);
        if (it == base_f_.end()) it = base_f_.emplace(key, safe_read<float>((uintptr_t)obj + off)).first;
        if (!on) {
            if (active_.erase(key)) write_mem<float>((uintptr_t)obj + off, it->second);
            return;
        }
        write_mem<float>((uintptr_t)obj + off, value);
        active_.insert(key);
    }

    void zero_vec3(void* obj, void* klass, const char* name, bool on = true) {
        if (!obj || !klass) return;
        size_t off = field_off(klass, name);
        if (!off) return;
        auto key = std::make_pair((uintptr_t)obj, off);
        auto it = base_vec_.find(key);
        if (it == base_vec_.end()) {
            Vec3 base;
            mem_read((uintptr_t)obj + off, &base, sizeof(base));
            it = base_vec_.emplace(key, base).first;
        }
        if (!on) {
            if (active_.erase(key)) write_mem((uintptr_t)obj + off, &it->second, sizeof(Vec3));
            return;
        }
        Vec3 v{0, 0, 0};
        write_mem((uintptr_t)obj + off, &v, sizeof(v));
        active_.insert(key);
    }

    void fixed_b(void* obj, void* klass, const char* name, bool value, bool on) {
        if (!obj || !klass) return;
        size_t off = field_off(klass, name);
        if (!off) return;
        auto key = std::make_pair((uintptr_t)obj, off);
        auto it = base_b_.find(key);
        if (it == base_b_.end()) it = base_b_.emplace(key, safe_read<uint8_t>((uintptr_t)obj + off)).first;
        if (!on) {
            if (active_.erase(key)) write_mem<uint8_t>((uintptr_t)obj + off, it->second);
            return;
        }
        write_mem<uint8_t>((uintptr_t)obj + off, value ? 1 : 0);
        active_.insert(key);
    }

    void fixed_i(void* obj, void* klass, const char* name, int value, bool on) {
        if (!obj || !klass) return;
        size_t off = field_off(klass, name);
        if (!off) return;
        auto key = std::make_pair((uintptr_t)obj, off);
        auto it = base_i_.find(key);
        if (it == base_i_.end()) it = base_i_.emplace(key, safe_read<int>((uintptr_t)obj + off)).first;
        if (!on) {
            if (active_.erase(key)) write_mem<int>((uintptr_t)obj + off, it->second);
            return;
        }
        write_mem<int>((uintptr_t)obj + off, value);
        active_.insert(key);
    }

    void apply_weapon_mods(const Config& cfg) {
        if (!local_) return;
        void* weapon = invoke(method(player_pawn_class_, "get_CurrentWeapon", 0), local_, nullptr);
        if (!weapon) return;
        void* weapon_cls = api_.object_get_class(weapon);
        if (!weapon_cls) return;

        void* comp = invoke(method(weapon_cls, "get_CurrentFireComponent", 0), weapon, nullptr);
        if (!comp) {
            size_t list_off = field_off(weapon_cls, "m_FireComponentList");
            void* arr = list_off ? safe_read<void*>((uintptr_t)weapon + list_off) : nullptr;
            if (arr && api_.array_length(arr) > 0) comp = array_at(arr, 0);
        }
        void* comp_cls = comp ? api_.object_get_class(comp) : nullptr;
        const WeaponConfig& wc = cfg.weapon;
        const bool magic = wc.magic_bullet || cfg.aimbot.magic_bullet;
        const bool inf_ammo = wc.infinite_ammo || cfg.player.infinite_ammo;
        const float fire_rate = std::max(0.1f, wc.fire_rate_multiplier);
        const float reload_rate = std::max(0.1f, wc.reload_multiplier);

        scale_f(weapon, weapon_cls, "m_fireSpeed", fire_rate, wc.rapid_fire);
        fixed_f(weapon, weapon_cls, "m_StartAimTime", 0.01f, wc.fast_scope);
        fixed_f(weapon, weapon_cls, "m_StartAssistAimTime", 0.01f, wc.fast_scope);
        fixed_f(weapon, weapon_cls, "m_SwitchTime", 0.03f, wc.fast_switch);
        scale_f(weapon, weapon_cls, "m_FastEquipTimeRate", 3.f, wc.fast_switch);
        scale_f(weapon, weapon_cls, "m_FastUnequipTimeRate", 3.f, wc.fast_switch);

        fixed_i(weapon, weapon_cls, "m_MuzzleFlashAssetID1P", 0, wc.no_muzzle_flash);
        fixed_i(weapon, weapon_cls, "m_MuzzleFlashAssetID3P", 0, wc.no_muzzle_flash);
        fixed_i(weapon, weapon_cls, "m_MuzzleFlashChargingAssetID1P", 0, wc.no_muzzle_flash);
        fixed_f(weapon, weapon_cls, "m_CameraShakeScale", 0.f, wc.no_camera_shake);
        fixed_f(weapon, weapon_cls, "m_CameraShakeScaleOnAiming", 0.f, wc.no_camera_shake);
        fixed_f(weapon, weapon_cls, "m_CameraSpringDampShakeScale", 0.f, wc.no_camera_shake);
        fixed_f(weapon, weapon_cls, "CrossHairOriginSize", 1.f, wc.small_crosshair);
        fixed_f(weapon, weapon_cls, "CrossHairOriginLength", 1.f, wc.small_crosshair);
        fixed_b(weapon, weapon_cls, "m_IsCameraShake", false, wc.no_camera_shake);
        fixed_b(weapon, weapon_cls, "m_WeaponShake_Enable", false, wc.no_camera_shake);
        fixed_b(weapon, weapon_cls, "m_WeaponShake_RecoilEnable", false, wc.no_camera_shake);
        fixed_b(weapon, weapon_cls, "m_WeaponShake_ShootEnable", false, wc.no_camera_shake);

        fixed_b(weapon, weapon_cls, "<ForcedKnifeMode>k__BackingField", true, wc.always_knife);
        fixed_b(weapon, weapon_cls, "m_UseGunMelee", true, wc.always_knife);
        fixed_b(weapon, weapon_cls, "m_IsMeleeDirectFire", true, wc.always_knife);
        fixed_b(local_, pawn_class_, "m_TriggerQuickKnife", true, wc.always_knife);

        if (magic) {
            fixed_f(weapon, weapon_cls, "m_WeaponRange", 5000.f, true);
            fixed_i(weapon, weapon_cls, "m_ThroughWallCount", 10, true);
        }

        if (!comp || !comp_cls) return;

        zero_f(comp, comp_cls, "m_RecoilUpBase", wc.no_recoil);
        zero_f(comp, comp_cls, "m_RecoilUpModifier", wc.no_recoil);
        zero_f(comp, comp_cls, "m_RecoilUpMax", wc.no_recoil);
        zero_f(comp, comp_cls, "m_RecoilLateralBase", wc.no_recoil);
        zero_f(comp, comp_cls, "m_RecoilLateralModifier", wc.no_recoil);
        zero_f(comp, comp_cls, "m_RecoilLateralMax", wc.no_recoil);
        zero_f(comp, comp_cls, "m_RecoilScale", wc.no_recoil);
        zero_f(comp, comp_cls, "m_RecoilScaleWeaponShake", wc.no_recoil);
        zero_f(comp, comp_cls, "m_GetHitPunchAngleX", wc.no_recoil);
        zero_vec3(comp, comp_cls, "m_PunchAngle", wc.no_recoil);
        zero_vec3(comp, comp_cls, "m_DeltaPunchAngle", wc.no_recoil);

        zero_f(comp, comp_cls, "m_ShotSpreadSize", wc.no_spread);
        zero_f(comp, comp_cls, "m_CurrentCrosshairInaccuracy", wc.no_spread);
        zero_f(comp, comp_cls, "m_MinInaccuracy", wc.no_spread);
        zero_f(comp, comp_cls, "m_MaxInaccuracy", wc.no_spread);
        zero_f(comp, comp_cls, "m_ShotSpreadAdjust", wc.no_spread);
        zero_f(comp, comp_cls, "m_DisperseRate", wc.no_spread);
        zero_vec3(comp, comp_cls, "m_ShotSpread", wc.no_spread);

        scale_f(comp, comp_cls, "m_FireInterval", 1.f / fire_rate, wc.rapid_fire);
        scale_f(comp, comp_cls, "m_MinFireInterval", 1.f / fire_rate, wc.rapid_fire);
        fixed_f(comp, comp_cls, "m_StartFireInterval", 0.01f, wc.rapid_fire);
        fixed_f(comp, comp_cls, "m_StopFireInterval", 0.01f, wc.rapid_fire);

        fixed_f(comp, comp_cls, "m_ChangeClipTime", 0.12f, wc.fast_reload);
        fixed_f(comp, comp_cls, "m_EmptyChangeClipTime", 0.12f, wc.fast_reload);
        fixed_f(comp, comp_cls, "m_AimedChangeClipTime", 0.12f, wc.fast_reload);
        fixed_f(comp, comp_cls, "m_QuickChangeClipTime", 0.04f, wc.fast_reload);
        scale_f(comp, comp_cls, "m_DoChangeClipTimeRate", reload_rate, wc.fast_reload);
        scale_f(comp, comp_cls, "m_DoEmptyChangeClipTimeRate", reload_rate, wc.fast_reload);
        scale_f(comp, comp_cls, "m_ChangeClipAnimRate", reload_rate, wc.fast_reload);
        fixed_b(comp, comp_cls, "m_AllowQuickChangeClip", true, wc.fast_reload);

        scale_f(comp, comp_cls, "m_FastEquipTimeRate", 3.f, wc.fast_switch);
        scale_f(comp, comp_cls, "m_FastUnequipTimeRate", 3.f, wc.fast_switch);
        fixed_f(comp, comp_cls, "m_EquipAnimClipTime", 0.03f, wc.fast_switch);
        fixed_f(comp, comp_cls, "m_UnequipAnimClipTime", 0.03f, wc.fast_switch);

        fixed_f(comp, comp_cls, "m_StartAimTime", 0.01f, wc.fast_scope);
        fixed_f(comp, comp_cls, "m_StartAssistAimTime", 0.01f, wc.fast_scope);
        scale_f(comp, comp_cls, "m_AimSpeedAcceleration", 4.f, wc.fast_scope);
        scale_f(comp, comp_cls, "m_MaxAimSpeedAcceleration", 4.f, wc.fast_scope);

        fixed_i(comp, comp_cls, "m_ClipAmmoCount", 999, inf_ammo);
        fixed_i(comp, comp_cls, "m_CarriedAmmoCount", 999, inf_ammo);
        fixed_i(comp, comp_cls, "m_AmmoCount", 999, inf_ammo);
        fixed_i(comp, comp_cls, "m_MaxAmmoCount", 999, inf_ammo);
        fixed_b(comp, comp_cls, "m_NoConsumeBullet", true, inf_ammo);

        zero_f(comp, comp_cls, "m_CoolDownCDTime", cfg.player.ability_cooldown);
        zero_f(comp, comp_cls, "m_OverHotCDTime", cfg.player.ability_cooldown);
        zero_f(comp, comp_cls, "m_CurCoolingDownValue", cfg.player.ability_cooldown);
        zero_f(comp, comp_cls, "CoolingDownTime", cfg.player.ability_cooldown);
        fixed_b(comp, comp_cls, "m_IsCoolingDown", false, cfg.player.ability_cooldown);

        if (magic) {
            fixed_f(comp, comp_cls, "m_WeaponRange", 5000.f, true);
            fixed_f(comp, comp_cls, "m_BulletInitSpeed", 5000.f, true);
            fixed_f(comp, comp_cls, "m_MinFireInterval", 0.01f, true);
        }
        scale_f(comp, comp_cls, "m_Damage", wc.damage_multiplier, wc.damage_multiplier > 1.001f);
    }

    void zero_f(void* obj, void* klass, const char* name, bool on = true) {
        if (!obj || !klass) return;
        size_t off = field_off(klass, name);
        if (!off) return;
        auto key = std::make_pair((uintptr_t)obj, off);
        auto it = base_f_.find(key);
        if (it == base_f_.end()) it = base_f_.emplace(key, safe_read<float>((uintptr_t)obj + off)).first;
        if (!on) {
            if (active_.erase(key)) write_mem<float>((uintptr_t)obj + off, it->second);
            return;
        }
        write_mem<float>((uintptr_t)obj + off, 0.f);
        active_.insert(key);
    }

    void aim(const Config& cfg) {
        if (!local_) return;
        if (!camera_ || !camera_transform_) refresh_camera();
        if (!camera_transform_) return;

        Quat cam_rot = quat_boxed(invoke(method(transform_class_, "get_rotation", 0), camera_transform_, nullptr));
        Vec3 eye = transform_pos(camera_transform_);
        if (eye.x == 0.f && eye.y == 0.f && eye.z == 0.f) eye = pawn_head(local_);

        Vec3 fwd = vec3_boxed(invoke(method(transform_class_, "get_forward", 0), camera_transform_, nullptr));
        fwd = normalize_vec(fwd);

        float best_angle = std::max(1.f, cfg.aimbot.fov);
        int best_idx = -1;
        Vec3 best_target{};

        for (size_t i = 0; i < entities_.size(); ++i) {
            const Entity& e = entities_[i];
            if (e.local || !e.alive) continue;
            if (local_team_ != 0 && e.team == local_team_) continue;
            if (cfg.aimbot.visible_only && e.health <= 0.f) continue;

            Vec3 target;
            if (cfg.aimbot.hitbox == 0 || cfg.aimbot.target_head) target = e.head;
            else if (cfg.aimbot.hitbox == 1) {
                Vec3 hips = e.hips;
                if (hips.x == 0.f && hips.y == 0.f && hips.z == 0.f) hips = {e.pos.x, e.pos.y + 0.95f, e.pos.z};
                target = {e.head.x * 0.45f + hips.x * 0.55f,
                          e.head.y * 0.45f + hips.y * 0.55f,
                          e.head.z * 0.45f + hips.z * 0.55f};
            } else {
                target = e.hips;
                if (target.x == 0.f && target.y == 0.f && target.z == 0.f)
                    target = {e.pos.x, e.pos.y + 0.9f, e.pos.z};
            }

            Vec3 d = normalize_vec(target - eye);
            float ang = rad2deg(acosf(std::clamp(dot(fwd, d), -1.f, 1.f)));
            if (ang < best_angle) {
                best_angle = ang;
                best_idx = (int)i;
                best_target = target;
            }
        }

        if (best_idx < 0) {
            trigger(false);
            return;
        }

        Vec3 d = best_target - eye;
        float len = d.length();
        if (len < 0.3f) { trigger(false); return; }

        float yaw = rad2deg(atan2f(d.x, d.z));
        float pitch = -rad2deg(asinf(std::clamp(d.y / len, -1.f, 1.f)));
        Quat target_q = quat_from_yaw_pitch(yaw, pitch);

        if (cfg.aimbot.enabled) {
            float step = 1.f / std::max(1.f, cfg.aimbot.smoothness);
            if (cfg.aimbot.aim_lock) step = std::max(step, 0.55f);
            Quat q = quat_slerp(cam_rot, target_q, std::clamp(step, 0.f, 1.f));

            void* set_rot = method(transform_class_, "set_rotation", 1);
            if (set_rot) {
                void* args[1] = {&q};
                invoke(set_rot, camera_transform_, args);
            }

            void* set_aim = method(player_pawn_class_, "SetAimRotation", 1);
            if (set_aim) {
                void* args[1] = {&target_q};
                invoke(set_aim, local_, args);
            }

            void* yaw_towards = method(player_pawn_class_, "SetAimYawTowards", 3);
            if (yaw_towards) {
                float speed = (cfg.aimbot.aim_lock ? 1440.f : 720.f / std::max(1.f, cfg.aimbot.smoothness));
                float dt = 0.016f;
                void* args[3] = {&yaw, &speed, &dt};
                invoke(yaw_towards, local_, args);
            }
        }

        if (cfg.aimbot.triggerbot && best_angle < 1.8f) {
            trigger(true);
        } else {
            trigger(false);
        }
        if (cfg.aimbot.magic_bullet && best_angle < 1.8f && now_ms() - last_magic_ > 90) {
            last_magic_ = now_ms();
            magic_damage(entities_[best_idx]);
        }
    }

    void trigger(bool down) {
        if (down == fire_down_) return;
        fire_down_ = down;

        // PlayerController is a plain managed object, so it is not reachable
        // through FindObjectsOfType. Pawn owns the same input entry points.
        if (down) {
            void* m = method(player_pawn_class_, "StartFire", 0);
            if (!m && pawn_class_) m = method(pawn_class_, "StartFire", 0);
            if (m) {
                invoke(m, local_, nullptr);
                return;
            }
            m = method(player_pawn_class_, "FireButtonDown", 0);
            if (m) { invoke(m, local_, nullptr); return; }
            if (player_controller_) {
                m = method(player_controller_class_, "StartFire", 0);
                if (m) invoke(m, player_controller_, nullptr);
            }
        } else {
            void* m = method(player_pawn_class_, "StopFire", 1);
            if (!m && pawn_class_) m = method(pawn_class_, "StopFire", 1);
            if (m) {
                bool immediate = true;
                void* args[1] = {&immediate};
                invoke(m, local_, args);
                return;
            }
            m = method(player_pawn_class_, "FireButtonUp", 0);
            if (m) { invoke(m, local_, nullptr); return; }
            if (player_controller_) {
                m = method(player_controller_class_, "StopFire", 1);
                if (m) {
                    bool immediate = true;
                    void* args[1] = {&immediate};
                    invoke(m, player_controller_, args);
                }
            }
        }
    }

    void magic_damage(const Entity& target) {
        if (!damage_info_class_) damage_info_class_ = find_class("GameEngine", "DamageInfo");
        if (!damage_info_class_ || !api_.object_new) return;

        void* info = api_.object_new(damage_info_class_);
        if (!info) return;
        void* ctor = method(damage_info_class_, ".ctor", 0);
        if (ctor) invoke(ctor, info, nullptr);

        auto setf = [&](const char* n, float v) {
            size_t o = field_off(damage_info_class_, n);
            if (o) write_mem<float>((uintptr_t)info + o, v);
        };
        auto seti = [&](const char* n, int v) {
            size_t o = field_off(damage_info_class_, n);
            if (o) write_mem<int>((uintptr_t)info + o, v);
        };
        auto setu = [&](const char* n, uint32_t v) {
            size_t o = field_off(damage_info_class_, n);
            if (o) write_mem<uint32_t>((uintptr_t)info + o, v);
        };
        auto setb = [&](const char* n, bool v) {
            size_t o = field_off(damage_info_class_, n);
            if (o) write_mem<uint8_t>((uintptr_t)info + o, v ? 1 : 0);
        };

        setf("Damage", 5000.f);
        setf("DisplayDamage", 5000.f);
        setf("Penetration", 10.f);
        setu("FleshDamage", 5000);
        seti("WeaponID", 0);
        seti("HitGroup", 0);
        seti("ThroughWallCount", 10);
        seti("ThroughPlayerCount", 10);
        seti("ShotSpreadX", 0);
        seti("ShotSpreadY", 0);
        setu("DamageType", 0);
        setb("Critical", true);
        setb("IsMeleeWeapon", false);

        if (local_ && target.addr) {
            size_t info_off = field_off(pawn_class_, "m_AttackableInfo");
            void* li = info_off ? safe_read<void*>((uintptr_t)local_ + info_off) : nullptr;
            void* ti = info_off ? safe_read<void*>(target.addr + info_off) : nullptr;
            uint32_t local_actor = li ? safe_read<uint32_t>((uintptr_t)li + 0x30) : 0;
            uint32_t target_actor = ti ? safe_read<uint32_t>((uintptr_t)ti + 0x30) : 0;
            setu("InstigatorPlayerID", local_actor);
            setu("AttackActorID", target_actor);
            setu("TargetActorID", target_actor);
            setu("ShooterID", local_actor);
            setu("WeaponActorId", local_actor);
            setu("DamageCauserID", local_actor);
        }

        void* m = method(player_pawn_class_, "TakeDamage", 1);
        if (!m && pawn_class_) m = method(pawn_class_, "TakeDamage", 1);
        if (!m) return;
        void* args[1] = {info};
        invoke(m, (void*)target.addr, args);
    }

    void apply_visuals(const Config& cfg) {
        for (const Entity& e : entities_) {
            if (e.local) continue;
            fixed_b((void*)e.addr, pawn_class_, "m_MaterialEffectEnable", true, cfg.visuals.chams);
        }
        if (cfg.visuals.chams && now_ms() - last_chams_ > 220) {
            last_chams_ = now_ms();
            apply_chams(cfg);
        }
    }

    void apply_chams(const Config& cfg) {
        if (!renderer_class_ || !material_class_ || !api_.string_new) return;
        void* type = type_object(renderer_class_);
        if (!type) return;
        void* get_children = method(transform_class_, "GetComponentsInChildren", 1);
        if (!get_children) return;
        void* set_color = method(material_class_, "SetColor", 2);
        if (!set_color) return;

        struct Color { float r, g, b, a; };
        if (last_cham_color_[0] != cfg.visuals.chams_color[0] ||
            last_cham_color_[1] != cfg.visuals.chams_color[1] ||
            last_cham_color_[2] != cfg.visuals.chams_color[2]) {
            last_cham_color_[0] = cfg.visuals.chams_color[0];
            last_cham_color_[1] = cfg.visuals.chams_color[1];
            last_cham_color_[2] = cfg.visuals.chams_color[2];
            cham_mats_.clear();
        }
        Color col{cfg.visuals.chams_color[0], cfg.visuals.chams_color[1],
                  cfg.visuals.chams_color[2], 0.92f};

        for (const Entity& e : entities_) {
            if (e.local) continue;
            void* root = invoke(method(player_pawn_class_, "GetRootTransform", 0), (void*)e.addr, nullptr);
            if (!root) continue;
            void* args[1] = {type};
            void* arr = invoke(get_children, root, args);
            int n = arr ? api_.array_length(arr) : 0;
            if (n > 24) n = 24;
            for (int i = 0; i < n; ++i) {
                void* rend = array_at(arr, i);
                if (!rend) continue;
                uintptr_t key = (uintptr_t)rend;
                auto it = cham_mats_.find(key);
                if (it != cham_mats_.end()) continue;
                void* mat = invoke(method(renderer_class_, "get_material", 0), rend, nullptr);
                if (!mat) continue;
                void* name = api_.string_new("_Color");
                if (!name) continue;
                void* cargs[2] = {name, &col};
                invoke(set_color, mat, cargs);
                cham_mats_[key] = (uintptr_t)mat;
            }
        }
    }

    void unlock_loaded_objects(bool skins) {
        const char* names[] = {
            "GameUI.CharacterItemData", "GameUI.RoleItemData", "GameUI.MeleeInfo",
            "GameUI.EquipmentInfo", "GameUI.AttachSkinData", "GameUI.BlueprintWeaponInfo",
            "GameUI.CamoMissionCamoData", "GameUI.CharacterRoleData",
            "GameUI.RandomCharacterData", "GameUI.ScoreStreakInfo", "GameUI.MpSkillInfo",
            "GameUI.MpSkillSkinInfo", "GameUI.PerkInfo", "GameUI.WildCardInfo",
            "GameUI.GrenadeInfo", "GameUI.BRThrowInfo", "GameUI.BRProfessionChipInfo",
            "GameUI.BRVehicleInfo", "GameUI.BrThrowSkinInfo",
            "GameUI.Loadout.DMZCamoData", "GameUI.Loadout.LoadoutWeaponCamoItemData",
            "GameUI.Loadout.LoadoutDecorationItemData",
            "GameUI.Loadout.LoadoutCamosMythicArmorData",
            "GameUI.Loadout.LoadoutCamosMythicSightData",
            "GameBase.Loadout", "GameBase.CommonLoadout", "GameBase.BRLoadout",
            "GameBase.LoadoutDataStore",
            nullptr
        };
        for (int i = 0; names[i]; ++i) {
            std::string full(names[i]);
            size_t dot = full.rfind('.');
            if (dot == std::string::npos) continue;
            void* cls = find_class(full.substr(0, dot).c_str(), full.substr(dot + 1).c_str());
            if (!cls) continue;
            void* arr = find_objects(cls);
            int n = arr ? api_.array_length(arr) : 0;
            for (int j = 0; j < n; ++j) {
                void* obj = array_at(arr, j);
                if (!obj) continue;
                // Fast path: the known unlock booleans.
                set_bool(obj, cls, "m_IsUnlock", true);
                set_bool(obj, cls, "<IsUnlock>k__BackingField", true);
                set_bool(obj, cls, "m_IsUnlocked", true);
                set_bool(obj, cls, "<IsUnlocked>k__BackingField", true);
                set_bool(obj, cls, "IsUnlock", true);
                set_bool(obj, cls, "Unlocked", true);
                set_bool(obj, cls, "IsUnlocked", true);
                set_bool(obj, cls, "owned", true);
                set_bool(obj, cls, "IsOwn", true);
                set_bool(obj, cls, "IsOwned", true);
                set_bool(obj, cls, "IsPurchased", true);
                if (skins) {
                    set_bool(obj, cls, "m_IsOwn", true);
                    set_bool(obj, cls, "m_Owned", true);
                }
            }
        }
        OV_LOGI("unlock_all skins=%d done", (int)skins);
    }

    void skip_tutorial() {
        void* prefs = find_class("UnityEngine", "PlayerPrefs");
        if (!prefs || !api_.string_new) return;
        void* set_int = method(prefs, "SetInt", 2);
        if (!set_int) return;
        const char* keys[] = {
            "TutorialFinished", "TutorialComplete", "NewbieGuideFinished",
            "NewbieTutorial", "FirstLaunch", "GuideFinished", "BRTutorialFinished",
            "BRNewTutorialStep", "BRNewTutorialFinished", "SkipTutorial", nullptr
        };
        for (int i = 0; keys[i]; ++i) {
            void* s = api_.string_new(keys[i]);
            int v = 1;
            void* args[2] = {s, &v};
            invoke(set_int, nullptr, args);
        }
        void* save = method(prefs, "Save", 0);
        invoke(save, nullptr, nullptr);
        OV_LOGI("skip_tutorial written");
    }
};

// ---------------------------------------------------------------------------
// Public reader facade
// ---------------------------------------------------------------------------
class EntityReader {
public:
    void bind(const Resolved& r) {
        res_ = r;
        game_.bind(r);
        game_.init();
    }

    void refresh() { game_.init(); }

    void tick(const Config& cfg, int width, int height) {
        game_.tick(cfg, width, height);
    }

    bool world_matrix(Mat4& out) const { return game_.world_matrix(out); }

    int read_all(Entity* out, int capacity) const { return game_.copy_entities(out, capacity); }

    uintptr_t local_player() const { return game_.local_player(); }

    bool local_entity(Entity& e) const { return game_.local_entity(e); }

    bool local_position(Vec3& p) const { return game_.local_position(p); }

    void unlock_all(bool skins, bool tutorial) { game_.unlock_all(skins, tutorial); }

    void dump() const { game_.dump(); }

    template <typename T>
    T safe_read(uintptr_t addr) const { return ov::safe_read<T>(addr); }

    const Resolved& resolved() const { return res_; }
    const Game& game() const { return game_; }

private:
    Resolved res_{};
    Game game_;
};

}  // namespace ov
