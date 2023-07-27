#pragma once

#include "df/enabler.h"
#include "modules/DFSDL.h"
#include <SDL.h>

#include <cstdint>
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

  // should be triggered on every game texpos reset
  void ResetTexpos() {
    handle_to_texpos.clear();
  }

  // should not be triggered in normal scenario
  void ResetSurface() {
    handle_to_surface.clear();
  }

  size_t SizeTexpos() const {
    return handle_to_texpos.size();
  }

  size_t SizeSurface() const {
    return handle_to_surface.size();
  }

  TexposHandle getNewHandle(SDL_Surface* surface) {
    if (!surface) return 0;
    auto new_surface = CopySurface(surface);
    auto handle = reinterpret_cast<uintptr_t>(surface);
    handle_to_surface.emplace(handle, new_surface);
    auto texpos = AddTexture(surface);
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
      auto new_surface = CopySurface(it->second);
      auto texpos = AddTexture(new_surface);
      handle_to_texpos.emplace(handle, texpos);
      return texpos;
    }
    // no data for this handle, get a new one
    return std::nullopt;
  }

private:
  TexposManager() {}
  TexposManager(const TexposManager&) = delete;
  TexposManager(TexposManager&&) = delete;

  ~TexposManager() {
    ResetTexpos();
    ResetSurface();
    delete this;
  };

  Texpos AddTexture(SDL_Surface* surface) {
    auto texpos = df::global::enabler->textures.raws.size();
    df::global::enabler->textures.raws.push_back(surface);
    return texpos;
  }

  SDL_Surface* CopySurface(SDL_Surface* surface) {
    SDL_Surface* new_surface =
      DFHack::DFSDL::DFSDL_CreateRGBSurface(surface->flags, surface->w, surface->h, surface->format->BitsPerPixel, surface->format->Rmask,
                                            surface->format->Gmask, surface->format->Bmask, surface->format->Amask);
    DFHack::DFSDL::DFSDL_UpperBlit(surface, NULL, new_surface, NULL);
    return new_surface;
  }

  std::unordered_map<TexposHandle, Texpos> handle_to_texpos;
  std::unordered_map<TexposHandle, SDL_Surface*> handle_to_surface;
};