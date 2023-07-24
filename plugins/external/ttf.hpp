#pragma once

#ifndef TEXTURE_CACHE_SIZE
#  define TEXTURE_CACHE_SIZE 500
#endif

#include "SDL_ttf.h"
#include "cache.hpp"
#include "modules/DFSDL.h"
#include <SDL.h>

#include <string>

class TTFManager {
public:
  // TODO: handle BOTH_HALF
  enum Viewbox {
    NORMAL,
    BOTH_HALF,
    UPPER_HALF,
    BOTTOM_HALF,
  };

  [[nodiscard]] static TTFManager* GetSingleton() {
    static TTFManager singleton;
    return &singleton;
  }

  void Init() {
    if (TTF_Init() == -1) exit(2);
  }

  void Quit() {
    if (this->font != nullptr) {
      TTF_CloseFont(this->font);
    }
    TTF_Quit();
  }

  void LoadFont(const std::string& file, int ptsize, int shift_from_top = 0) {
    Init();
    if (this->font != nullptr) {
      TTF_CloseFont(this->font);
    }
    auto font = TTF_OpenFont(file.c_str(), ptsize);
    this->font = font;
    this->shift_from_top = shift_from_top;
  }

  // void ClearCache() {
  //   this->cache.Clear();
  // }

  // size_t Size() const {
  //   return this->cache.Size();
  // }

  SDL_Surface* GetTextureWS(const std::wstring& str, const Viewbox& view = Viewbox::NORMAL, SDL_Color font_color = { 255, 255, 255 }) {
    if (this->font == nullptr) exit(2);

    auto wstr = str;
    auto shift = this->shift_from_top;

    // TODO: BOTH_HALF
    switch (view) {
      case Viewbox::UPPER_HALF:
        wstr += L"UPPER_HALF";
        shift -= (TTFManager::frame_height / 2) - 1; // 1px kinda evil hack
        break;
      case Viewbox::BOTTOM_HALF:
        wstr += L"BOTTOM_HALF";
        shift += (TTFManager::frame_height / 2) + 1;
        break;
    }

    // is it in cache? get and return if so
    // if (auto cached = this->cache.Get(wstr); cached) {
    //   return cached.value().get();
    // }

    // create texture
    auto texture = CreateCharTextureAnyway(str.c_str(), font_color);
    texture = ResizeSurface(texture, TTFManager::frame_width, TTFManager::frame_height, shift);
    // this->cache.Put(wstr, texture);
    return texture;
  }

private:
  TTFManager() {}
  TTFManager(const TTFManager&) = delete;
  TTFManager(TTFManager&&) = delete;

  ~TTFManager() {
    this->Quit();
    delete this;
  };

  SDL_Surface* ResizeSurface(SDL_Surface* surface, int width, int height, int shift_from_top) {
    // should check cause it may crashed
    if (!surface || !width || !height) {
      return 0;
    }
    SDL_Surface* sized_texture =
      DFHack::DFSDL::DFSDL_CreateRGBSurface(surface->flags, width, height, surface->format->BitsPerPixel, surface->format->Rmask,
                                            surface->format->Gmask, surface->format->Bmask, surface->format->Amask);

    for (int y = shift_from_top; y < surface->h; y++) {
      for (int x = 0; x < surface->w; x++) {
        if (x < width && y - shift_from_top < height) {
          DrawPixel(sized_texture, x, y - shift_from_top, ReadPixel(surface, x, y));
        }
      }
    }
    // clear source surface
    DFHack::DFSDL::DFSDL_FreeSurface(surface);
    return sized_texture;
  }

  SDL_Surface* ScaleSurface(SDL_Surface* Surface, Uint16 Width, Uint16 Height) {
    if (!Surface || !Width || !Height) return 0;

    SDL_Surface* _ret =
      DFHack::DFSDL::DFSDL_CreateRGBSurface(Surface->flags, Width, Height, Surface->format->BitsPerPixel, Surface->format->Rmask,
                                            Surface->format->Gmask, Surface->format->Bmask, Surface->format->Amask);

    double _stretch_factor_x = (static_cast<double>(Width) / static_cast<double>(Surface->w)),
           _stretch_factor_y = (static_cast<double>(Height) / static_cast<double>(Surface->h));

    for (Sint32 y = 0; y < Surface->h; y++)
      for (Sint32 x = 0; x < Surface->w; x++)
        for (Sint32 o_y = 0; o_y < _stretch_factor_y; ++o_y)
          for (Sint32 o_x = 0; o_x < _stretch_factor_x; ++o_x)
            DrawPixel(_ret, static_cast<Sint32>(_stretch_factor_x * x) + o_x, static_cast<Sint32>(_stretch_factor_y * y) + o_y,
                      ReadPixel(Surface, x, y));

    return _ret;
  }

  void DrawPixel(SDL_Surface* surface, int x, int y, Uint32 pixel) {
    int bpp = surface->format->BytesPerPixel;
    /* Here p is the address to the pixel we want to set */
    Uint8* p = (Uint8*)surface->pixels + y * surface->pitch + x * bpp;

    switch (bpp) {
      case 1:
        *p = pixel;
        break;
      case 2:
        *(Uint16*)p = pixel;
        break;
      case 3:
        if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
          p[0] = (pixel >> 16) & 0xff;
          p[1] = (pixel >> 8) & 0xff;
          p[2] = pixel & 0xff;
        } else {
          p[0] = pixel & 0xff;
          p[1] = (pixel >> 8) & 0xff;
          p[2] = (pixel >> 16) & 0xff;
        }
        break;
      case 4:
        *(Uint32*)p = pixel;
        break;
    }
  }

  Uint32 ReadPixel(SDL_Surface* surface, int x, int y) {
    int bpp = surface->format->BytesPerPixel;
    /* Here p is the address to the pixel we want to retrieve */
    Uint8* p = (Uint8*)surface->pixels + y * surface->pitch + x * bpp;

    switch (bpp) {
      case 1:
        return *p;
        break;

      case 2:
        return *(Uint16*)p;
        break;

      case 3:
        if (SDL_BYTEORDER == SDL_BIG_ENDIAN)
          return p[0] << 16 | p[1] << 8 | p[2];
        else
          return p[0] | p[1] << 8 | p[2] << 16;
        break;

      case 4:
        return *(Uint32*)p;
        break;

      default:
        return 0; /* shouldn't happen, but avoids warnings */
    }
  }

  SDL_Surface* CreateCharTextureAnyway(const wchar_t* symbol, SDL_Color font_color) {
    auto texture = TTF_RenderUNICODE_Blended(this->font, (Uint16*)symbol, font_color);
    if (texture == NULL) {
      texture = TTF_RenderUNICODE_Blended(this->font, (Uint16*)"x", font_color); // render this in case of error
    }
    return texture;
  }

  // LRUCache<std::wstring, SDL_Surface*> cache{ TEXTURE_CACHE_SIZE }; // we can do it without caching textures cause we have cached texpos
  TTF_Font* font;
  int shift_from_top = 0;

  static const int frame_width = 8;
  static const int frame_height = 12;
};