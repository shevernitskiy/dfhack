#include <mutex>
#include <unordered_map>

#include "Internal.h"

#include "modules/DFSDL.h"
#include "modules/Textures.h"

#include "Debug.h"
#include "PluginManager.h"
#include "VTableInterpose.h"

#include "df/enabler.h"
#include "df/viewscreen_adopt_regionst.h"
#include "df/viewscreen_loadgamest.h"
#include "df/viewscreen_new_arenast.h"
#include "df/viewscreen_new_regionst.h"

using df::global::enabler;
using namespace DFHack;
using namespace DFHack::DFSDL;

namespace DFHack {
DBG_DECLARE(core, textures, DebugCategory::LINFO);
}

static std::unordered_map<TexposHandle, long> g_handle_to_texpos;
static std::unordered_map<TexposHandle, SDL_Surface*> g_handle_to_surface;
static std::mutex g_adding_mutex;

const uint32_t TILE_WIDTH_PX = 8;
const uint32_t TILE_HEIGHT_PX = 12;

static std::vector<TexposHandle> empty{};

static std::unordered_map<std::string, std::vector<TexposHandle>> g_static_assets{
    {"hack/data/art/dfhack.png", empty},       {"hack/data/art/green-pin.png", empty},
    {"hack/data/art/red-pin.png", empty},      {"hack/data/art/icons.png", empty},
    {"hack/data/art/on-off.png", empty},       {"hack/data/art/pathable.png", empty},
    {"hack/data/art/unsuspend.png", empty},    {"hack/data/art/control-panel.png", empty},
    {"hack/data/art/border-thin.png", empty},  {"hack/data/art/border-medium.png", empty},
    {"hack/data/art/border-bold.png", empty},  {"hack/data/art/border-panel.png", empty},
    {"hack/data/art/border-window.png", empty}};

// Converts an arbitrary Surface to something like the display format
// (32-bit RGBA), and converts magenta to transparency if convert_magenta is set
// and the source surface didn't already have an alpha channel.
// It also deletes the source surface.
//
// It uses the same pixel format (RGBA, R at lowest address) regardless of
// hardware.
SDL_Surface* canonicalize_format(SDL_Surface* src) {
    SDL_PixelFormat fmt;
    fmt.palette = NULL;
    fmt.BitsPerPixel = 32;
    fmt.BytesPerPixel = 4;
    fmt.Rloss = fmt.Gloss = fmt.Bloss = fmt.Aloss = 0;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    fmt.Rshift = 24;
    fmt.Gshift = 16;
    fmt.Bshift = 8;
    fmt.Ashift = 0;
#else
    fmt.Rshift = 0;
    fmt.Gshift = 8;
    fmt.Bshift = 16;
    fmt.Ashift = 24;
#endif
    fmt.Rmask = 255 << fmt.Rshift;
    fmt.Gmask = 255 << fmt.Gshift;
    fmt.Bmask = 255 << fmt.Bshift;
    fmt.Amask = 255 << fmt.Ashift;

    SDL_Surface* tgt = DFSDL_ConvertSurface(src, &fmt, SDL_SWSURFACE);
    DFSDL_FreeSurface(src);
    for (int x = 0; x < tgt->w; ++x) {
        for (int y = 0; y < tgt->h; ++y) {
            Uint8* p = (Uint8*)tgt->pixels + y * tgt->pitch + x * 4;
            if (p[3] == 0) {
                for (int c = 0; c < 3; c++) {
                    p[c] = 0;
                }
            }
        }
    }
    return tgt;
}

// register surface in texture raws, get a texpos
static long add_texture(SDL_Surface* surface) {
    std::lock_guard<std::mutex> lg_add_texture(g_adding_mutex);
    auto texpos = enabler->textures.raws.size();
    enabler->textures.raws.push_back(surface);
    return texpos;
}

TexposHandle Textures::loadTexture(SDL_Surface* surface) {
    if (!surface)
        return 0; // should be some error, i guess

    auto handle = reinterpret_cast<TexposHandle>(surface); // not the best way? but cheap
    g_handle_to_surface.emplace(handle, surface);
    surface->refcount++; // prevent destruct on next FreeSurface by game
    auto texpos = add_texture(surface);
    g_handle_to_texpos.emplace(handle, texpos);
    return handle;
}

std::vector<TexposHandle> Textures::loadTileset(const std::string& file,
                                                int tile_px_w = TILE_WIDTH_PX,
                                                int tile_px_h = TILE_HEIGHT_PX) {
    std::vector<TexposHandle> handles{};

    SDL_Surface* surface = DFIMG_Load(file.c_str());
    if (!surface) {
        ERR(textures).printerr("unable to load textures from '%s'\n", file.c_str());
        return handles;
    }

    surface = canonicalize_format(surface);
    int dimx = surface->w / tile_px_w;
    int dimy = surface->h / tile_px_h;
    for (int y = 0; y < dimy; y++) {
        for (int x = 0; x < dimx; x++) {
            SDL_Surface* tile = DFSDL_CreateRGBSurface(
                0, tile_px_w, tile_px_h, 32, surface->format->Rmask, surface->format->Gmask,
                surface->format->Bmask, surface->format->Amask);
            SDL_Rect vp{tile_px_w * x, tile_px_h * y, tile_px_w, tile_px_h};
            DFSDL_UpperBlit(surface, &vp, tile, NULL);
            auto handle = Textures::loadTexture(tile);
            handles.push_back(handle);
        }
    }

    DFSDL_FreeSurface(surface);
    DEBUG(textures).print("loaded %ld textures from '%s'\n", handles.size(), file.c_str());

    return handles;
}

long Textures::getTexposByHandle(TexposHandle handle) {
    if (!handle)
        return -1;

    if (g_handle_to_texpos.contains(handle))
        return g_handle_to_texpos[handle];

    if (g_handle_to_surface.contains(handle)) {
        g_handle_to_surface[handle]->refcount++; // prevent destruct on next FreeSurface by game
        auto texpos = add_texture(g_handle_to_surface[handle]);
        g_handle_to_texpos.emplace(handle, texpos);
        return texpos;
    }

    return -1;
}

static void reset_texpos() {
    g_handle_to_texpos.clear();
}

static void reset_surface() {
    for (auto& entry : g_handle_to_surface) {
        DFSDL_FreeSurface(entry.second);
    }
    g_handle_to_surface.clear();
}

// reset point on New Game
struct tracking_stage_new_region : df::viewscreen_new_regionst {
    typedef df::viewscreen_new_regionst interpose_base;

    DEFINE_VMETHOD_INTERPOSE(void, logic, ()) {
        if (this->m_raw_load_stage != this->raw_load_stage) {
            this->m_raw_load_stage = this->raw_load_stage;
            if (this->m_raw_load_stage == 1)
                reset_texpos();
        }
        INTERPOSE_NEXT(logic)();
    }

  private:
    inline static int m_raw_load_stage = -2; // not valid state at the start
};
IMPLEMENT_VMETHOD_INTERPOSE(tracking_stage_new_region, logic);

// reset point on New Game in Existing World
struct tracking_stage_adopt_region : df::viewscreen_adopt_regionst {
    typedef df::viewscreen_adopt_regionst interpose_base;

    DEFINE_VMETHOD_INTERPOSE(void, logic, ()) {
        if (this->m_cur_step != this->cur_step) {
            this->m_cur_step = this->cur_step;
            if (this->m_cur_step == df::viewscreen_adopt_regionst::T_cur_step::ProcessingRawData)
                reset_texpos();
        }
        INTERPOSE_NEXT(logic)();
    }

  private:
    inline static int m_cur_step = -2; // not valid state at the start
};
IMPLEMENT_VMETHOD_INTERPOSE(tracking_stage_adopt_region, logic);

// reset point on Load Game
struct tracking_stage_load_region : df::viewscreen_loadgamest {
    typedef df::viewscreen_loadgamest interpose_base;

    DEFINE_VMETHOD_INTERPOSE(void, logic, ()) {
        if (this->m_cur_step != this->cur_step) {
            this->m_cur_step = this->cur_step;
            if (this->m_cur_step == df::viewscreen_loadgamest::T_cur_step::ProcessingRawData)
                reset_texpos();
        }
        INTERPOSE_NEXT(logic)();
    }

  private:
    inline static int m_cur_step = -2; // not valid state at the start
};
IMPLEMENT_VMETHOD_INTERPOSE(tracking_stage_load_region, logic);

// reset point on New Arena
struct tracking_stage_new_arena : df::viewscreen_new_arenast {
    typedef df::viewscreen_new_arenast interpose_base;

    DEFINE_VMETHOD_INTERPOSE(void, logic, ()) {
        if (this->m_cur_step != this->cur_step) {
            this->m_cur_step = this->cur_step;
            if (this->m_cur_step == 0)
                reset_texpos();
        }
        INTERPOSE_NEXT(logic)();
    }

  private:
    inline static int m_cur_step = -2; // not valid state at the start
};
IMPLEMENT_VMETHOD_INTERPOSE(tracking_stage_new_arena, logic);

static void install_reset_point() {
    INTERPOSE_HOOK(tracking_stage_new_region, logic).apply();
    INTERPOSE_HOOK(tracking_stage_adopt_region, logic).apply();
    INTERPOSE_HOOK(tracking_stage_load_region, logic).apply();
    INTERPOSE_HOOK(tracking_stage_new_arena, logic).apply();
}

static void uninstall_reset_point() {
    INTERPOSE_HOOK(tracking_stage_new_region, logic).remove();
    INTERPOSE_HOOK(tracking_stage_adopt_region, logic).remove();
    INTERPOSE_HOOK(tracking_stage_load_region, logic).remove();
    INTERPOSE_HOOK(tracking_stage_new_arena, logic).remove();
}

void Textures::init(color_ostream& out) {
    install_reset_point();
    DEBUG(textures, out).print("dynamic texture loading ready");

    for (auto& [key, value] : g_static_assets) {
        g_static_assets[key] = Textures::loadTileset(key);
    }

    DEBUG(textures, out).print("static assets loaded");
}

void Textures::cleanup() {
    reset_texpos();
    reset_surface();
    uninstall_reset_point();
}

long Textures::getAsset(const std::string asset, size_t index) {
    if (!g_static_assets.contains(asset))
        return -1;
    if (g_static_assets[asset].size() <= index)
        return -1;
    return Textures::getTexposByHandle(g_static_assets[asset][index]);
}
