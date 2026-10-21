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
#include <vector>
#include <utility>
#include <cmath>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>

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
    bool  draw_lines   = false;
    bool  draw_boxes   = false;
    bool  draw_health  = false;
    bool  draw_names   = false;
    bool  draw_skeleton = false;
    bool  draw_origin  = false;
    float max_distance = 100.f;
};

struct AimbotConfig {
    bool  enabled     = false;
    bool  aim_lock    = false;
    bool  triggerbot  = false;
    float fov         = 30.f;     // half-angle radius, degrees
    float smoothness  = 5.f;      // 1 = instant
    bool  target_head = true;
};

struct CameraConfig {
    bool  freecam    = false;
    float fov        = 60.f;
    float timescale  = 1.f;
};

struct PlayerConfig {
    bool  noclip          = false;
    float speed_multiplier = 1.f;
    bool  infinite_jump   = false;
};

struct Config {
    VisualsConfig visuals;
    AimbotConfig  aimbot;
    CameraConfig  camera;
    PlayerConfig  player;
    bool          menu_open = true;
};

// ---------------------------------------------------------------------------
// Offsets resolved for this game build
// ---------------------------------------------------------------------------
struct Offsets {
    std::map<std::string, uintptr_t> values;
    std::map<std::string, double>    nums;     // floats (heights, strides...)

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

// ---------------------------------------------------------------------------
// Loader
// ---------------------------------------------------------------------------
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
    Offsets offs;
    std::string text;
    if (!read_text_file(assets_dir + "/resolved_offsets.json", text)) return offs;

    Json root = JsonParser(text.c_str()).parse();
    // Either { "offsets": { ... } } or a flat map.
    const Json* table = root.find("offsets");
    const Json& src = (table && table->type == Json::OBJ) ? *table : root;
    if (src.type != Json::OBJ) return offs;

    for (const auto& kv : src.obj) {
        const Json& v = kv.second;
        if (v.type == Json::NUM) {
            offs.nums[kv.first]   = v.num;
            offs.values[kv.first] = (uintptr_t)v.num;
        } else if (v.type == Json::STR) {
            uintptr_t parsed = (uintptr_t)strtoull(v.str.c_str(), nullptr, 0);
            offs.values[kv.first] = parsed;
            offs.nums[kv.first]   = (double)parsed;
        }
    }
    return offs;
}

inline void load_config(const std::string& assets_dir, Config& cfg) {
    std::string text;
    if (!read_text_file(assets_dir + "/mod_config.json", text)) return;

    Json root = JsonParser(text.c_str()).parse();

    if (const Json* v = root.find("visuals")) {
        cfg.visuals.draw_lines    = v->get_bool("lines", cfg.visuals.draw_lines);
        cfg.visuals.draw_boxes    = v->get_bool("boxes", cfg.visuals.draw_boxes);
        cfg.visuals.draw_health   = v->get_bool("health", cfg.visuals.draw_health);
        cfg.visuals.draw_names    = v->get_bool("names", cfg.visuals.draw_names);
        cfg.visuals.draw_skeleton = v->get_bool("skeleton", cfg.visuals.draw_skeleton);
        cfg.visuals.draw_origin   = v->get_bool("origin", cfg.visuals.draw_origin);
        cfg.visuals.max_distance  = v->get_float("max_distance", cfg.visuals.max_distance);
    }
    if (const Json* a = root.find("aimbot")) {
        cfg.aimbot.enabled     = a->get_bool("enabled", cfg.aimbot.enabled);
        cfg.aimbot.aim_lock    = a->get_bool("aim_lock", cfg.aimbot.aim_lock);
        cfg.aimbot.triggerbot  = a->get_bool("triggerbot", cfg.aimbot.triggerbot);
        cfg.aimbot.fov         = a->get_float("fov", cfg.aimbot.fov);
        cfg.aimbot.smoothness  = a->get_float("smoothness", cfg.aimbot.smoothness);
        cfg.aimbot.target_head = a->get_bool("target_head", cfg.aimbot.target_head);
    }
    if (const Json* c = root.find("camera")) {
        cfg.camera.freecam   = c->get_bool("freecam", cfg.camera.freecam);
        cfg.camera.fov       = c->get_float("fov", cfg.camera.fov);
        cfg.camera.timescale = c->get_float("timescale", cfg.camera.timescale);
    }
    if (const Json* p = root.find("player")) {
        cfg.player.noclip           = p->get_bool("noclip", cfg.player.noclip);
        cfg.player.speed_multiplier = p->get_float("speed_multiplier", cfg.player.speed_multiplier);
        cfg.player.infinite_jump    = p->get_bool("infinite_jump", cfg.player.infinite_jump);
    }
    cfg.menu_open = root.get_bool("menu_open", cfg.menu_open);
}

// ---------------------------------------------------------------------------
// Process memory access
// ---------------------------------------------------------------------------
template <typename T>
inline T read_mem(uintptr_t addr) {
    if (!addr) return T{};
    return *reinterpret_cast<T*>(addr);
}

// Writes through process_vm_writev, falling back to /proc/self/mem so we can
// touch read-only pages without permanently changing the mapping's protections.
inline bool write_mem(uintptr_t addr, const void* data, size_t len) {
    if (!addr || !len) return false;

    struct iovec local  { const_cast<void*>(data), len };
    struct iovec remote { reinterpret_cast<void*>(addr), len };
    if (process_vm_writev(getpid(), &local, 1, &remote, 1, 0) == (ssize_t)len) {
        return true;
    }

    int fd = open("/proc/self/mem", O_RDWR);
    if (fd >= 0) {
        ssize_t n = pwrite(fd, data, len, (off_t)addr);
        close(fd);
        if (n == (ssize_t)len) return true;
    }

    // Last resort: temporarily open the page.
    uintptr_t page = addr & ~(uintptr_t)(sysconf(_SC_PAGESIZE) - 1);
    if (mprotect((void*)page, len + (addr - page), PROT_READ | PROT_WRITE) != 0) return false;
    memcpy((void*)addr, data, len);
    return true;
}

template <typename T>
inline bool write_mem(uintptr_t addr, T value) {
    return write_mem(addr, &value, sizeof(T));
}

// ---------------------------------------------------------------------------
// Math
// ---------------------------------------------------------------------------
struct Vec2 { float x = 0, y = 0; };

struct Vec3 {
    float x = 0, y = 0, z = 0;

    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator*(float s)         const { return {x * s, y * s, z * s}; }

    float length() const { return sqrtf(x * x + y * y + z * z); }
    float distance(const Vec3& o) const { return (*this - o).length(); }
};

inline constexpr float kPi = 3.14159265358979f;
inline float rad2deg(float r) { return r * 180.0f / kPi; }
inline float deg2rad(float d) { return d * kPi / 180.0f; }

// Wraps to (-180, 180].
inline float normalize_angle(float deg) {
    while (deg >  180.0f) deg -= 360.0f;
    while (deg <= -180.0f) deg += 360.0f;
    return deg;
}

// Column-major 4x4, same layout a Unity camera matrix uses.
struct Mat4 { float m[16] = {}; };

inline Mat4 read_mat4(uintptr_t addr) {
    Mat4 out;
    if (!addr) return out;
    memcpy(out.m, reinterpret_cast<const void*>(addr), sizeof(out.m));
    return out;
}

// Projects a world position to screen pixels.
inline bool world_to_screen(const Vec3& p, const Mat4& vp, float width, float height, Vec2& out) {
    const float* m = vp.m;
    float cx = m[0] * p.x + m[4] * p.y + m[8]  * p.z + m[12];
    float cy = m[1] * p.x + m[5] * p.y + m[9]  * p.z + m[13];
    float cw = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];

    if (cw < 0.001f) return false;      // behind the camera

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

    int    entity_stride = 8;      // pointer array
    int    max_entities  = 64;

    uintptr_t off_health = 0;
    uintptr_t off_team   = 0;
    uintptr_t off_name   = 0;
    uintptr_t off_pos    = 0;
    uintptr_t off_head   = 0;

    float  head_height  = 1.65f;
    float  body_height  = 1.20f;

    // When true, camera_yaw/camera_pitch hold pointers to the float values
    // rather than the values themselves.
    bool   camera_indirect = false;

    bool   valid = false;

    // Small values are treated as offsets from the module base,
    // large ones as already-absolute addresses.
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

        // "GameAssembly" may be an absolute address or a base offset.
        base = value("GameAssembly", 0);
        if (base && base < 0x10000000ULL) {
            uintptr_t found = module_base("libil2cpp.so");
            if (!found) found = module_base("GameAssembly.so");
            base = found ? found + base : 0;
        }
        if (!base) {
            base = module_base("libil2cpp.so");
            if (!base) base = module_base("GameAssembly.so");
        }

        entity_list  = addr("EntityList");
        entity_count = addr("EntityCount");
        local_player = addr("LocalPlayer");
        view_matrix  = addr("ViewMatrix");
        camera_yaw   = addr("CameraYaw");
        camera_pitch = addr("CameraPitch");
        unlock_flags = addr("UnlockFlags");
        trigger_flag = addr("TriggerFlag");

        entity_stride = (int)value("EntityStride", 8);
        max_entities  = (int)value("MaxEntities", 64);

        off_health = value("Health", 0);
        off_team   = value("Team", 0);
        off_name   = value("Name", 0);
        off_pos    = value("Position", 0);
        off_head   = value("Head", 0);

        head_height = head_height_metres(o);
        body_height = 1.20f;
        camera_indirect = value("CameraYawPtr", 0) != 0;

        valid = base != 0 && (entity_list != 0 || local_player != 0);
    }

private:
    std::map<std::string, uintptr_t> offs_;

    uintptr_t value(const std::string& name, uintptr_t def) const {
        auto it = offs_.find(name);
        return it == offs_.end() ? def : it->second;
    }

    // Height is written either in metres (1.65) or centimetres (165).
    static float head_height_metres(const Offsets& o) {
        double h = o.num("HeadHeight", 165.0);
        if (h > 20.0) h *= 0.01;
        if (h <= 0.2 || h > 5.0) h = 1.65;
        return (float)h;
    }
};

// ---------------------------------------------------------------------------
// Safe reads: process_vm_readv first (validates mappings), then /proc/self/mem.
// Unmapped pages return false instead of crashing the host process.
// ---------------------------------------------------------------------------

inline int self_mem_fd() {
    static int fd = open("/proc/self/mem", O_RDONLY);
    return fd;
}

inline bool mem_read(uintptr_t addr, void* out, size_t len) {
    if (!addr || !len) return false;

    struct iovec local  { out, len };
    struct iovec remote { reinterpret_cast<void*>(addr), len };
    if (process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == (ssize_t)len) {
        return true;
    }

    int fd = self_mem_fd();
    if (fd >= 0 && pread(fd, out, len, (off_t)addr) == (ssize_t)len) {
        return true;
    }
    return false;
}

class RangeCache {
public:
    void refresh() { self_mem_fd(); }
    bool readable(uintptr_t addr, size_t len = 1) const {
        if (!addr) return false;
        uint8_t probe = 0;
        return mem_read(addr, &probe, len ? 1 : 0);
    }
};

// ---------------------------------------------------------------------------
// Entity snapshot
// ---------------------------------------------------------------------------
struct Entity {
    uintptr_t addr   = 0;
    Vec3      pos{};
    Vec3      head{};
    float     health = 0.f;
    int       team   = 0;
    bool      alive  = false;
    char      name[32] = {};
};

class EntityReader {
public:
    void bind(const Resolved& r) { res_ = r; }

    void refresh() { ranges_.refresh(); }

    bool world_matrix(Mat4& out) const {
        if (!ranges_.readable(res_.view_matrix, sizeof(float) * 16)) return false;
        out = read_mat4(res_.view_matrix);
        // A dead matrix projects nothing.
        return out.m[15] != 0.f;
    }

    int read_all(Entity* out, int capacity) const {
        if (!res_.valid || capacity <= 0) return 0;

        int count = entity_count();
        int written = 0;

        for (int i = 0; i < count && written < capacity; ++i) {
            uintptr_t slot = res_.entity_list + (uintptr_t)i * res_.entity_stride;
            uintptr_t ent  = res_.entity_stride == sizeof(uintptr_t)
                           ? safe_read<uintptr_t>(slot)
                           : slot;
            if (!ent) continue;

            Entity e;
            if (fill(ent, e)) out[written++] = e;
        }
        return written;
    }

    int entity_count() const {
        if (ranges_.readable(res_.entity_count, sizeof(int))) {
            int n = safe_read<int>(res_.entity_count);
            if (n > 0) return n > res_.max_entities ? res_.max_entities : n;
        }
        // Unity managed array: length sits at +0x18 on IL2CPP.
        if (ranges_.readable(res_.entity_list + 0x18, sizeof(int))) {
            int n = safe_read<int>(res_.entity_list + 0x18);
            if (n > 0) return n > res_.max_entities ? res_.max_entities : n;
        }
        return res_.max_entities;
    }

    uintptr_t local_player() const {
        if (!ranges_.readable(res_.local_player, sizeof(uintptr_t))) return 0;
        uintptr_t p = safe_read<uintptr_t>(res_.local_player);
        return ranges_.readable(p, 0x100) ? p : 0;
    }

    template <typename T>
    T safe_read(uintptr_t addr) const {
        T v{};
        mem_read(addr, &v, sizeof(T));
        return v;
    }

    const Resolved& resolved() const { return res_; }

private:
    Resolved   res_{};
    RangeCache ranges_{};

    bool fill(uintptr_t ent, Entity& e) const {
        if (!ranges_.readable(ent, 0x100)) return false;

        if (res_.off_pos) {
            Vec3 p = safe_read<Vec3>(ent + res_.off_pos);
            if (p.x == 0.f && p.y == 0.f && p.z == 0.f) return false;
            e.pos = p;
        } else {
            return false;
        }

        if (res_.off_head)
            e.head = safe_read<Vec3>(ent + res_.off_head);
        else
            e.head = {e.pos.x, e.pos.y + res_.head_height, e.pos.z};

        if (res_.off_health) e.health = safe_read<float>(ent + res_.off_health);
        if (res_.off_team)   e.team   = safe_read<int>(ent + res_.off_team);

        if (res_.off_name) {
            uintptr_t str = safe_read<uintptr_t>(ent + res_.off_name);
            if (str) {
                char raw[sizeof(e.name)] = {};
                if (mem_read(str, raw, sizeof(raw) - 1)) {
                    memcpy(e.name, raw, sizeof(e.name) - 1);
                    e.name[sizeof(e.name) - 1] = '\0';
                }
            }
        }

        e.addr  = ent;
        e.alive = e.health > 0.f;
        return true;
    }
};

}  // namespace ov
