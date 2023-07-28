#pragma once

#include "Core.h"
#include "SDL.h"
#include "VTableInterpose.h"

#include "df/enabler.h"
#include "df/viewscreen_adopt_regionst.h"
#include "df/viewscreen_loadgamest.h"
#include "df/viewscreen_new_regionst.h"
#include "modules/DFSDL.h"

// #include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>

class TexposManager {
public:
  typedef typename uintptr_t TexposHandle;
  typedef typename long Texpos;

  [[nodiscard]] static TexposManager& instance() {
    static TexposManager singleton;
    return singleton;
  }

  size_t sizeTexpos() const {
    return handle_to_texpos.size();
  }

  size_t sizeSurface() const {
    return handle_to_surface.size();
  }

  TexposHandle getNewHandle(SDL_Surface* surface) {
    if (!surface) return 0;
    auto new_surface = copy_surface(surface);
    auto handle = reinterpret_cast<uintptr_t>(new_surface);
    handle_to_surface.emplace(handle, new_surface);
    auto texpos = add_texture(surface);
    handle_to_texpos.emplace(handle, texpos);
    return handle;
  }

  std::optional<Texpos> getTexposByHandle(TexposHandle handle) {
    if (!handle) return std::nullopt;
    // search for existing texpos
    if (auto it = handle_to_texpos.find(handle); it != handle_to_texpos.end()) {
      return it->second;
    }
    // if not, search for cached texture en register new one
    if (auto it = handle_to_surface.find(handle); it != handle_to_surface.end()) {
      auto new_surface = copy_surface(it->second);
      auto texpos = add_texture(new_surface);
      handle_to_texpos.emplace(handle, texpos);
      return texpos;
    }
    // no data for this handle, get a new one
    return std::nullopt;
  }

private:
  struct tracking_stage_new_region : df::viewscreen_new_regionst {
    typedef df::viewscreen_new_regionst interpose_base;

    DEFINE_VMETHOD_INTERPOSE(void, logic, ()) {
      if (this->m_raw_load_stage != this->raw_load_stage) {
        this->m_raw_load_stage = this->raw_load_stage;
        if (this->m_raw_load_stage == 1) TexposManager::instance().reset_texpos();
      }
      INTERPOSE_NEXT(logic)();
    }

  private:
    int m_raw_load_stage = -2;
  };

  // reseting on Starting new game in existing world
  struct tracking_stage_adopt_region : df::viewscreen_adopt_regionst {
    typedef df::viewscreen_adopt_regionst interpose_base;

    DEFINE_VMETHOD_INTERPOSE(void, logic, ()) {
      if (this->m_progress != this->progress) {
        this->m_progress = this->progress;
        if (this->m_progress == 1) TexposManager::instance().reset_texpos();
      }
      INTERPOSE_NEXT(logic)();
    }

  private:
    int m_progress = -2;
  };

  // reseting on Load game
  struct tracking_stage_load_region : df::viewscreen_loadgamest {
    typedef df::viewscreen_loadgamest interpose_base;

    DEFINE_VMETHOD_INTERPOSE(void, logic, ()) {
      if (this->m_progress != this->progress) {
        this->m_progress = this->progress;
        if (this->m_progress == 1) TexposManager::instance().reset_texpos();
      }
      INTERPOSE_NEXT(logic)();
    }

  private:
    int m_progress = -2;
  };

  SDL_Surface* copy_surface(SDL_Surface* surface) {
    // should handle alpha?
    SDL_Surface* new_surface =
      DFHack::DFSDL::DFSDL_CreateRGBSurface(surface->flags, surface->w, surface->h, surface->format->BitsPerPixel, surface->format->Rmask,
                                            surface->format->Gmask, surface->format->Bmask, surface->format->Amask);
    // DFHack::DFSDL::DFSDL_UpperBlit(surface, NULL, new_surface, NULL);
    size_t size_in_bytes = surface->h * surface->w * surface->format->BytesPerPixel;
    memcpy(new_surface->pixels, surface->pixels, size_in_bytes);
    return new_surface;
  }

  // Add textuter and get texpos
  Texpos add_texture(SDL_Surface* surface) {
    std::lock_guard<std::mutex> lg_add_texture(add_mutex);
    auto texpos = df::global::enabler->textures.raws.size();
    df::global::enabler->textures.raws.push_back(surface);
    return texpos;
  }

  // should be triggered on every game texpos reset
  void reset_texpos() {
    handle_to_texpos.clear();
  }

  // should not be triggered in normal scenario
  void reset_surface() {
    handle_to_surface.clear();
  }

  void install_reset_point() {
    INTERPOSE_HOOK(tracking_stage_new_region, logic).apply();
    INTERPOSE_HOOK(tracking_stage_adopt_region, logic).apply();
    INTERPOSE_HOOK(tracking_stage_load_region, logic).apply();
  }

  void uninstall_reset_point() {
    INTERPOSE_HOOK(tracking_stage_new_region, logic).remove();
    INTERPOSE_HOOK(tracking_stage_adopt_region, logic).remove();
    INTERPOSE_HOOK(tracking_stage_load_region, logic).remove();
  }

  TexposManager() {
    install_reset_point();
  }
  TexposManager(const TexposManager&) = delete;
  TexposManager(TexposManager&&) = delete;

  ~TexposManager() {
    reset_texpos();
    reset_surface();
    uninstall_reset_point();
    delete this;
  };

  std::unordered_map<TexposHandle, Texpos> handle_to_texpos;
  std::unordered_map<TexposHandle, SDL_Surface*> handle_to_surface;

  std::mutex add_mutex;
};

IMPLEMENT_VMETHOD_INTERPOSE(TexposManager::tracking_stage_new_region, logic);
IMPLEMENT_VMETHOD_INTERPOSE(TexposManager::tracking_stage_adopt_region, logic);
IMPLEMENT_VMETHOD_INTERPOSE(TexposManager::tracking_stage_load_region, logic);