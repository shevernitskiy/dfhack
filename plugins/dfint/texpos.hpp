#pragma once

#include "Core.h"
#include "SDL.h"
#include "VTableInterpose.h"

#include "df/enabler.h"
#include "df/viewscreen_adopt_regionst.h"
#include "df/viewscreen_loadgamest.h"
#include "df/viewscreen_new_regionst.h"
// #include "modules/DFSDL.h"

// #include <cstdint>
#include <execution>
#include <functional>
// #include <optional>
// #include <unordered_map>
// #include <unordered_set>

class TexposManager {
public:
  // typedef typename uintptr_t TexposHandle;
  typedef typename long Texpos;

  [[nodiscard]] static TexposManager& instance() {
    static TexposManager singleton;
    return singleton;
  }

  static Texpos AddTexture(SDL_Surface* surface, std::function<void()> callback) {
    return TexposManager::instance().m_AddTexture(surface, callback);
  }

  static Texpos AddTexture(SDL_Surface* surface) {
    return TexposManager::instance().m_AddTexture(surface);
  }

  static void RegisterResetCallback(std::function<void()> callback) {
    return TexposManager::instance().m_RegisterResetCallback(callback);
  }

  // // should not be triggered in normal scenario
  // void ResetSurface() {
  //   handle_to_surface.clear();
  // }

  // size_t SizeTexpos() const {
  //   return handle_to_texpos.size();
  // }

  // size_t SizeSurface() const {
  //   return handle_to_surface.size();
  // }

  // TexposHandle getNewHandle(SDL_Surface* surface) {
  //   if (!surface) return 0;
  //   auto new_surface = CopySurface(surface);
  //   auto handle = reinterpret_cast<uintptr_t>(surface);
  //   handle_to_surface.emplace(handle, new_surface);
  //   auto texpos = AddTexture(surface);
  //   handle_to_texpos.emplace(handle, texpos);
  //   return handle;
  // }

  // std::optional<Texpos> getTexposByHandle(TexposHandle handle) {
  //   if (!handle) return std::nullopt;
  //   // search for existing texpos
  //   if (auto it = handle_to_texpos.find(handle); it != handle_to_texpos.end()) {
  //     return it->second;
  //   }
  //   // if not, search for cached texture en register new one
  //   if (auto it = handle_to_surface.find(handle); it != handle_to_surface.end()) {
  //     auto new_surface = CopySurface(it->second);
  //     auto texpos = AddTexture(new_surface);
  //     handle_to_texpos.emplace(handle, texpos);
  //     return texpos;
  //   }
  //   // no data for this handle, get a new one
  //   return std::nullopt;
  // }

private:
  // SDL_Surface* CopySurface(SDL_Surface* surface) {
  //   SDL_Surface* new_surface =
  //     DFHack::DFSDL::DFSDL_CreateRGBSurface(surface->flags, surface->w, surface->h, surface->format->BitsPerPixel,
  //     surface->format->Rmask,
  //                                           surface->format->Gmask, surface->format->Bmask, surface->format->Amask);
  //   DFHack::DFSDL::DFSDL_UpperBlit(surface, NULL, new_surface, NULL);
  //   return new_surface;
  // }

  struct tracking_stage_new_region : df::viewscreen_new_regionst {
    typedef df::viewscreen_new_regionst interpose_base;

    DEFINE_VMETHOD_INTERPOSE(void, logic, ()) {
      if (this->m_raw_load_stage != this->raw_load_stage) {
        this->m_raw_load_stage = this->raw_load_stage;
        if (this->m_raw_load_stage == 1) TexposManager::instance().ResetTextures();
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
        if (this->m_progress == 1) TexposManager::instance().ResetTextures();
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
        if (this->m_progress == 1) TexposManager::instance().ResetTextures();
      }
      INTERPOSE_NEXT(logic)();
    }

  private:
    int m_progress = -2;
  };

  // Use this one if you have only one texture
  Texpos m_AddTexture(SDL_Surface* surface, std::function<void()> callback) {
    m_RegisterResetCallback(callback);
    auto texpos = df::global::enabler->textures.raws.size();
    df::global::enabler->textures.raws.push_back(surface);
    return texpos;
  }

  // Add textuter and get texpos
  Texpos m_AddTexture(SDL_Surface* surface) {
    auto texpos = df::global::enabler->textures.raws.size();
    df::global::enabler->textures.raws.push_back(surface);
    return texpos;
  }

  void m_RegisterResetCallback(std::function<void()> callback) {
    callbacks.push_back(callback);
  }

  // should be triggered on every game texpos reset
  void ResetTextures() {
    // handle_to_texpos.clear();
    std::for_each(std::execution::par, callbacks.begin(), callbacks.end(), [](auto& fn) { fn(); });
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
    callbacks.clear();
    // handle_to_texpos.clear();
    // handle_to_surface.clear();
    uninstall_reset_point();
    delete this;
  };

  // std::unordered_map<TexposHandle, Texpos> handle_to_texpos;
  // std::unordered_map<TexposHandle, SDL_Surface*> handle_to_surface;

  std::vector<std::function<void()>> callbacks;
};

IMPLEMENT_VMETHOD_INTERPOSE(TexposManager::tracking_stage_new_region, logic);
IMPLEMENT_VMETHOD_INTERPOSE(TexposManager::tracking_stage_adopt_region, logic);
IMPLEMENT_VMETHOD_INTERPOSE(TexposManager::tracking_stage_load_region, logic);