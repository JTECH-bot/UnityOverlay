// main.cpp - EGL swap hook, ImGui lifecycle, JNI entry points.
//
// Flow:
//   JNI_OnLoad -> spawn installer thread
//   installer  -> wait for libEGL.so, PLT-hook eglSwapBuffers, refresh
//   hook       -> first call boots ImGui/GLES3, every call renders the UI,
//                 then forwards to the real eglSwapBuffers

#include <jni.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>

#include <atomic>
#include <cstring>
#include <string>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "xhook.h"

#include "menu.h"
#include "memory.h"

#define TAG "UnityOverlay"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace {

using EglSwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);

EglSwapBuffersFn g_orig_swap = nullptr;

std::atomic<bool> g_imgui_ready{false};
std::atomic<bool> g_menu_visible{true};

ov::Config   g_config;
ov::Offsets  g_offsets;
ov::Resolved g_resolved;
ov::EntityReader g_reader;
menu::Actions g_actions;

std::string g_assets_dir = "/data/local/tmp/overlay_assets";

// --- touch state (single finger drives the ImGui mouse) ---------------------
std::atomic<bool>  g_touch_down{false};
std::atomic<float> g_touch_x{0.f};
std::atomic<float> g_touch_y{0.f};

// ---------------------------------------------------------------------------
void init_imgui(int width, int height) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // don't write imgui.ini into the game dir
    io.LogFilename = nullptr;

    // Default font is unreadable on a phone without scaling.
    float density = (width > height ? height : width) / 720.0f;
    if (density < 1.0f) density = 1.0f;
    io.FontGlobalScale = density * 1.4f;

    menu::apply_style();

    ImGui_ImplOpenGL3_Init("#version 300 es");

    g_config  = ov::Config{};
    ov::load_config(g_assets_dir, g_config);
    g_offsets = ov::load_offsets(g_assets_dir);
    g_resolved.resolve(g_offsets);
    g_reader.bind(g_resolved);
    g_reader.refresh();

    g_imgui_ready.store(true);
    LOGI("ImGui ready (%dx%d, %zu offsets, base=0x%llX)", width, height,
         g_offsets.values.size(), (unsigned long long)g_resolved.base);
}

// --- minimal GL state guard -------------------------------------------------
struct GlState {
    GLint viewport[4];
    GLfloat clear_color[4];
    GLboolean blend, depth, cull, scissor;

    void save() {
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_color);
        blend   = glIsEnabled(GL_BLEND);
        depth   = glIsEnabled(GL_DEPTH_TEST);
        cull    = glIsEnabled(GL_CULL_FACE);
        scissor = glIsEnabled(GL_SCISSOR_TEST);
    }
    void restore() const {
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
        blend   ? glEnable(GL_BLEND)      : glDisable(GL_BLEND);
        depth   ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
        cull    ? glEnable(GL_CULL_FACE)  : glDisable(GL_CULL_FACE);
        scissor ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
    }
};

// ---------------------------------------------------------------------------
void render_frame(int width, int height) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DisplayFramebufferScale = ImVec2(1.f, 1.f);

    // Feed the latest touch sample as mouse input.
    if (g_touch_x.load() >= 0.f) {
        io.AddMousePosEvent(g_touch_x.load(), g_touch_y.load());
    }
    static bool was_down = false;
    bool down = g_touch_down.load();
    if (down != was_down) {
        io.AddMouseButtonEvent(0, down);
        was_down = down;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    // Refresh the readable-range cache a few times a second, not every frame.
    static int rng_tick = 0;
    if (++rng_tick >= 8) { rng_tick = 0; g_reader.refresh(); }

    menu::update_aimbot(g_config, g_reader, width, height);

    if (g_menu_visible.load()) {
        menu::draw(g_config, g_offsets, g_actions);
    }

    menu::draw_overlay(g_config, g_reader, width, height);

    ImGui::Render();

    GlState state;
    state.save();

    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    state.restore();
}

// ---------------------------------------------------------------------------
EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    if (!g_orig_swap) return EGL_FALSE;

    EGLint w = 0, h = 0;
    if (eglQuerySurface(dpy, surface, EGL_WIDTH,  &w) != EGL_TRUE) w = 1080;
    if (eglQuerySurface(dpy, surface, EGL_HEIGHT, &h) != EGL_TRUE) h = 2400;

    if (!g_imgui_ready.load()) {
        EGLContext ctx = eglGetCurrentContext();
        if (ctx != EGL_NO_CONTEXT && w > 0 && h > 0) {
            init_imgui(w, h);
        }
    }

    if (g_imgui_ready.load()) {
        render_frame(w, h);

        if (g_actions.reload_config) {
            g_actions.reload_config = false;
            g_offsets = ov::load_offsets(g_assets_dir);
            ov::load_config(g_assets_dir, g_config);
            g_resolved.resolve(g_offsets);
            g_reader.bind(g_resolved);
            g_reader.refresh();
            LOGI("config reloaded (%zu offsets)", g_offsets.values.size());
        }
        if (g_actions.save_config) {
            g_actions.save_config = false;
            LOGI("save_config requested (persist from the injecting tool)");
        }
        if (g_actions.dump_scene) {
            g_actions.dump_scene = false;
            ov::Entity ents[64];
            int n = g_reader.read_all(ents, 64);
            LOGI("scene dump: %d entities", n);
            for (int i = 0; i < n; ++i) {
                LOGI("  [%d] %p pos(%.2f %.2f %.2f) hp=%.1f name=%s", i,
                     (void*)ents[i].addr, ents[i].pos.x, ents[i].pos.y, ents[i].pos.z,
                     ents[i].health, ents[i].name);
            }
        }
        if (g_actions.unlock_weapons || g_actions.unlock_skins) {
            if (g_resolved.unlock_flags) {
                uint32_t flags = ov::read_mem<uint32_t>(g_resolved.unlock_flags);
                if (g_actions.unlock_weapons) flags |= 0x1;
                if (g_actions.unlock_skins)   flags |= 0x2;
                ov::write_mem<uint32_t>(g_resolved.unlock_flags, flags);
                LOGI("unlock flags -> 0x%X", flags);
            } else {
                LOGE("UnlockFlags offset is not set");
            }
            g_actions.unlock_weapons = false;
            g_actions.unlock_skins = false;
        }
    }

    return g_orig_swap(dpy, surface);
}

// ---------------------------------------------------------------------------
bool library_loaded(const char* needle) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return false;
    char line[512];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, needle)) { found = true; break; }
    }
    fclose(f);
    return found;
}

void* installer_thread(void*) {
    LOGI("installer waiting for libEGL");
    for (int i = 0; i < 600 && !library_loaded("libEGL.so"); ++i) usleep(100 * 1000);

    if (!library_loaded("libEGL.so")) {
        LOGE("libEGL never showed up");
        return nullptr;
    }

    xhook_enable_debug(0);
    int rc = xhook_register("libEGL.so", "eglSwapBuffers",
                            reinterpret_cast<void*>(hook_eglSwapBuffers),
                            reinterpret_cast<void**>(&g_orig_swap));
    if (rc != 0 || !g_orig_swap) {
        LOGE("xhook_register failed (%d)", rc);
        return nullptr;
    }

    // Keep refreshing so late dlopens still get patched.
    for (int i = 0; i < 20; ++i) {
        xhook_refresh(1);
        usleep(250 * 1000);
    }

    LOGI("eglSwapBuffers hooked");
    return nullptr;
}

void start_installer() {
    pthread_t t;
    if (pthread_create(&t, nullptr, installer_thread, nullptr) == 0) {
        pthread_detach(t);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// JNI
// ---------------------------------------------------------------------------
extern "C" {

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }

    // Register the controller the Java injector talks to.
    static const JNINativeMethod methods[] = {
        { "nativeTouch",       "(IFF)V", reinterpret_cast<void*>(+[](JNIEnv*, jclass, jint action, jfloat x, jfloat y) {
            if (action == 0) {          // down
                g_touch_x.store(x); g_touch_y.store(y); g_touch_down.store(true);
            } else if (action == 1) {   // move
                g_touch_x.store(x); g_touch_y.store(y);
            } else {                    // up
                g_touch_down.store(false);
            }
        }) },
        { "nativeToggleMenu",  "()V", reinterpret_cast<void*>(+[](JNIEnv*, jclass) {
            g_menu_visible.store(!g_menu_visible.load());
        }) },
        { "nativeSetAssetsPath", "(Ljava/lang/String;)V", reinterpret_cast<void*>(+[](JNIEnv* e, jclass, jstring path) {
            if (!path) return;
            const char* c = e->GetStringUTFChars(path, nullptr);
            if (c) { g_assets_dir = c; e->ReleaseStringUTFChars(path, c); }
        }) },
    };

    jclass cls = env->FindClass("com/unityoverlay/Overlay");
    if (cls) {
        env->RegisterNatives(cls, methods, sizeof(methods) / sizeof(methods[0]));
        env->DeleteLocalRef(cls);
    } else {
        env->ExceptionClear();
        LOGE("com/unityoverlay/Overlay not found - JNI names won't bind");
    }

    start_installer();
    LOGI("JNI_OnLoad done");
    return JNI_VERSION_1_6;
}

}  // extern "C"
