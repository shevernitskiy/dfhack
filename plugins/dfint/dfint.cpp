#include "Core.h"
#include "Debug.h"
#include "LuaTools.h"
#include "PluginManager.h"

#include "df/enabler.h"
#include "df/graphic_viewportst.h"
#include "df/init.h"
#include "df/viewscreen_titlest.h"
#include "modules/Screen.h"

#include "cache.hpp"
#include "dictionary.hpp"
#include "texpos.hpp"
#include "ttf.hpp"
#include "utils.hpp"

#include <format>
#include <string>
#include <vector>

#define TEXTURE_CACHE_SIZE 500

enum ScreenType {
  MAIN,
  TOP
};

struct TextString {
  std::wstring str;
  int32_t x;
  long y;
  std::pair<long, long> clipx;
  std::pair<long, long> clipy;
  uint8_t justification;
  int space;
  int flag;
  ScreenType screen_type = ScreenType::MAIN;
};

static std::vector<TextString> g_screen_text{};
static LRUCache<std::wstring, long> g_texpos_cache{ TEXTURE_CACHE_SIZE };

// static LRUCache<std::wstring, TexposManager::TexposHandle> g_texpos_hanlde_cache{ TEXTURE_CACHE_SIZE };

// ============== TEMPORARY HOOKING PART ==============

#define WIN32_LEAN_AND_MEAN
#define NOSOUND          // Sound driver routines
#define NOTEXTMETRIC     // typedef TEXTMETRIC and associated routines
#define NOWH             // SetWindowsHook and WH_*
#define NOWINOFFSETS     // GWL_*, GCL_*, associated routines
#define NOCOMM           // COMM driver routines
#define NOKANJI          // Kanji support stuff.
#define NOHELP           // Help engine interface.
#define NOPROFILER       // Profiler interface.
#define NODEFERWINDOWPOS // DeferWindowPos routines
#define NOMCX            // Modem Configuration Extensions
#define NOSYSMETRICS     // SM_*
#define NOMENUS          // MF_*
#define NOICONS          // IDI_*
#define NOKEYSTATES      // MK_*
#define NOSYSCOMMANDS    // SC_*
#define NORASTEROPS      // Binary and Tertiary raster ops
#define OEMRESOURCE      // OEM Resource values
#define NOATOM           // Atom Manager routines
#define NOCLIPBOARD      // Clipboard routines
#define NOCOLOR          // Screen colors
#define NODRAWTEXT       // DrawText() and DT_*
#define NOGDI            // All GDI defines and routines
#define NOKERNEL         // All KERNEL defines and routines
#define NOMEMMGR         // GMEM_*, LMEM_*, GHND, LHND, associated routines
#define NOMETAFILE       // typedef METAFILEPICT
#define NOMINMAX         // Macros min(a,b) and max(a,b)
#define NOOPENFILE       // OpenFile(), OemToAnsi, AnsiToOem, and OF_*
#define NOSCROLL         // SB_* and scrolling routines
#define NOSERVICE        // All Service Controller routines, SERVICE_ equates, etc.
#include <Windows.h>

#include "deps/detours/detours.h"

inline auto module_handle = reinterpret_cast<uintptr_t>(GetModuleHandle(0));

#define SETUP_ORIG_FUNC_OFFSET(fn_name, shift) auto fn_name##_orig = reinterpret_cast<fn_name>(module_handle + shift);
#define ATTACH(fn_name)                        DetourAttach(&(reinterpret_cast<void*&>(fn_name##_orig)), (void*)fn_name##_hook);
#define DETACH(fn_name)                        DetourDetach(&(reinterpret_cast<void*&>(fn_name##_orig)), (void*)fn_name##_hook);
#define HOOK(fn_name)                          fn_name##_hook
#define ORIGINAL(fn_name)                      fn_name##_orig

typedef void(__fastcall* addst)(df::graphic* gps_, std::string& str, uint8_t justify, int space);
SETUP_ORIG_FUNC_OFFSET(addst, 0x7F1680)
void __fastcall HOOK(addst)(df::graphic* gps_, std::string& str, uint8_t justify, int space) {
  // idk why, but we can't resize incoming string
  // on next frame we can't use it for translation
  // not cheap withouta  doubt
  auto tmp = str;
  if (auto translation = Dictionary::instance().Get(str); translation) {
    auto ws = s2ws(translation.value());
    g_screen_text.push_back(TextString{ ws, gps_->screenx, gps_->screeny, std::make_pair(gps_->clipx[0], gps_->clipx[1]),
                                        std::make_pair(gps_->clipy[0], gps_->clipy[1]), justify, space, 0 });
    tmp.resize(ws.size());
  } else {
    g_screen_text.push_back(TextString{ c2wc(str), gps_->screenx, gps_->screeny, std::make_pair(gps_->clipx[0], gps_->clipx[1]),
                                        std::make_pair(gps_->clipy[0], gps_->clipy[1]), justify, space, 0 });
  }
  ORIGINAL(addst)(gps_, tmp, justify, space);
}

typedef void(__fastcall* addst_top)(df::graphic* gps_, std::string& str, __int64 a3);
SETUP_ORIG_FUNC_OFFSET(addst_top, 0x7F1760);
void __fastcall HOOK(addst_top)(df::graphic* gps_, std::string& str, __int64 a3) {
  auto tmp = str;
  if (auto translation = Dictionary::instance().Get(str); translation) {
    auto ws = s2ws(translation.value());
    g_screen_text.push_back(TextString{ ws, gps_->screenx, gps_->screeny, std::make_pair(gps_->clipx[0], gps_->clipx[1]),
                                        std::make_pair(gps_->clipy[0], gps_->clipy[1]), 0, 0, 0, ScreenType::TOP });
    tmp.resize(ws.size());
  } else {
    g_screen_text.push_back(TextString{ c2wc(str), gps_->screenx, gps_->screeny, std::make_pair(gps_->clipx[0], gps_->clipx[1]),
                                        std::make_pair(gps_->clipy[0], gps_->clipy[1]), 0, 0, 0, ScreenType::TOP });
  }
  ORIGINAL(addst_top)(gps_, tmp, a3);
}

typedef void(__fastcall* addst_flag)(df::graphic* gps_, std::string& str, __int64 a3, __int64 a4, int flag);
SETUP_ORIG_FUNC_OFFSET(addst_flag, 0x7F13F0);
void __fastcall HOOK(addst_flag)(df::graphic* gps_, std::string& str, __int64 a3, __int64 a4, int flag) {
  auto tmp = str;
  if (auto translation = Dictionary::instance().Get(str); translation) {
    auto ws = s2ws(translation.value());
    g_screen_text.push_back(TextString{ ws, gps_->screenx, gps_->screeny, std::make_pair(gps_->clipx[0], gps_->clipx[1]),
                                        std::make_pair(gps_->clipy[0], gps_->clipy[1]), 0, 0, flag });
    tmp.resize(ws.size(), '\0');
  } else {
    g_screen_text.push_back(TextString{ c2wc(str), gps_->screenx, gps_->screeny, std::make_pair(gps_->clipx[0], gps_->clipx[1]),
                                        std::make_pair(gps_->clipy[0], gps_->clipy[1]), 0, 0, flag });
  }
  ORIGINAL(addst_flag)(gps_, tmp, a3, a4, flag);
}

void install_hooks() {
  DetourRestoreAfterWith();
  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  ATTACH(addst);
  ATTACH(addst_top);
  ATTACH(addst_flag);
  DetourTransactionCommit();
}

// ============== TEMPORARY HOOKING PART ==============

using namespace DFHack;
using df::global::enabler;
using df::global::gps;
using df::global::init;

DFHACK_PLUGIN("dfint");

namespace DFHack {
  DBG_DECLARE(dfint, log, DebugCategory::LDEBUG);
}

DFhackCExport command_result plugin_init(color_ostream& out, std::vector<PluginCommand>& commands) {
  DEBUG(log, out).print("initializing %s\n", plugin_name);
  Dictionary::instance().LoadCsv("./dfint_data/dfint_dictionary_ru_utf.csv");
  TTFManager::instance().LoadFont("terminus_bold.ttf", 14, 2);
  install_hooks();
  return CR_OK;
}

DFhackCExport command_result plugin_shutdown(color_ostream& out) {
  DEBUG(log, out).print("shutting down %s\n", plugin_name);
  TTFManager::instance().Quit();
  return CR_OK;
}

DFhackCExport command_result plugin_onstatechange(color_ostream& out, state_change_event event) {
  switch (event) {
    case SC_VIEWSCREEN_CHANGED:
      g_texpos_cache.Clear();
      // TexposManager::instance().ResetTexpos();
      break;
    default:
      break;
  }

  return CR_OK;
}

static bool paint_tile(const Screen::Pen& pen, int x, int y, ScreenType screen_type = ScreenType::MAIN) {
  bool use_graphics = Screen::inGraphicsMode();

  if (x < 0 || x >= gps->dimx || y < 0 || y >= gps->dimy) return false;

  size_t index = (x * gps->dimy) + y;
  uint8_t* screen = &gps->screen[index * 8];

  if (screen > gps->screen_limit) return false;

  long* texpos = &gps->screentexpos[index];
  long* texpos_lower = &gps->screentexpos_lower[index];
  uint32_t* flag = &gps->screentexpos_flag[index];

  // keep SCREENTEXPOS_FLAG_ANCHOR_SUBORDINATE so occluded anchored textures
  // don't appear corrupted
  uint32_t flag_mask = 0x4;
  if (pen.write_to_lower) flag_mask |= 0x18;

  *screen = 0;
  *texpos = 0;
  if (!pen.keep_lower) *texpos_lower = 0;
  gps->screentexpos_anchored[index] = 0;
  *flag &= flag_mask;

  // paint to top screen, if there is someting but not space
  if (gps->top_in_use && screen_type == ScreenType::TOP && gps->screen_top[index * 8]) {
    screen = &gps->screen_top[index * 8];
    texpos = &gps->screentexpos_top[index];
    texpos_lower = &gps->screentexpos_top_lower[index];
    flag = &gps->screentexpos_top_flag[index];

    *screen = 0;
    *texpos = 0;
    if (!pen.keep_lower) *texpos_lower = 0;
    gps->screentexpos_top_anchored[index] = 0;
    *flag &= flag_mask;
  }

  uint8_t fg = pen.fg | (pen.bold << 3);
  uint8_t bg = pen.bg;

  if (pen.tile_mode == Screen::Pen::CharColor)
    *flag |= 2; // SCREENTEXPOS_FLAG_ADDCOLOR
  else if (pen.tile_mode == Screen::Pen::TileColor) {
    *flag |= 1; // SCREENTEXPOS_FLAG_GRAYSCALE
    if (pen.tile_fg) fg = pen.tile_fg;
    if (pen.tile_bg) bg = pen.tile_bg;
  }

  if (pen.tile && use_graphics) {
    if (pen.write_to_lower)
      *texpos_lower = pen.tile;
    else
      *texpos = pen.tile;

    if (pen.top_of_text || pen.bottom_of_text) {
      screen[0] = uint8_t(pen.ch);
      if (pen.top_of_text) *flag |= 0x8;
      if (pen.bottom_of_text) *flag |= 0x10;
    }
  } else if (pen.ch) {
    screen[0] = uint8_t(pen.ch);
    if (pen.top_of_text) *flag |= 0x8;
    if (pen.bottom_of_text) *flag |= 0x10;
    // *texpos_lower = df::global::init->texpos_border_interior; // basic black background
  }

  auto rgb_fg = &gps->uccolor[fg][0];
  auto rgb_bg = &gps->uccolor[bg][0];
  screen[1] = rgb_fg[0];
  screen[2] = rgb_fg[1];
  screen[3] = rgb_fg[2];
  screen[4] = rgb_bg[0];
  screen[5] = rgb_bg[1];
  screen[6] = rgb_bg[2];

  return true;
}

static uint8_t to_16_bit_color(uint8_t* rgb) {
  for (uint8_t c = 0; c < 16; ++c) {
    if (rgb[0] == gps->uccolor[c][0] && rgb[1] == gps->uccolor[c][1] && rgb[2] == gps->uccolor[c][2]) {
      return c;
    }
  }
  return 0;
}

static Screen::Pen read_tile(int x, int y, ScreenType screen_type = ScreenType::MAIN) {
  if (!gps) return Screen::Pen(0, 0, 0, -1);
  bool use_graphics = Screen::inGraphicsMode();

  if (x < 0 || x >= gps->dimx || y < 0 || y >= gps->dimy) return Screen::Pen(0, 0, 0, -1);

  size_t index = (x * gps->dimy) + y;
  uint8_t* screen = &gps->screen[index * 8];

  if (screen > gps->screen_limit) return Screen::Pen(0, 0, 0, -1);

  long* texpos = &gps->screentexpos[index];
  uint32_t* flag = &gps->screentexpos_flag[index];

  if (gps->top_in_use && screen_type == ScreenType::TOP && (gps->screen_top[index * 8] || (use_graphics && gps->screentexpos_top[index]))) {
    screen = &gps->screen_top[index * 8];
    texpos = &gps->screentexpos_top[index];
    flag = &gps->screentexpos_top_flag[index];
  }

  char ch = *screen;
  uint8_t fg = to_16_bit_color(&screen[1]);
  uint8_t bg = to_16_bit_color(&screen[4]);
  int tile = 0;
  if (use_graphics) tile = *texpos;

  // AsIs
  auto pen = Screen::Pen(ch, fg, bg, tile, false);

  if (*flag & 1) {
    // TileColor
    pen = Screen::Pen(ch, fg & 7, bg, !!(fg & 8), tile, fg, bg);
  } else if (*flag & 2) {
    // CharColor
    pen = Screen::Pen(ch, fg, bg, tile, true);
  }
  if (*flag & 0x8) {
    pen.top_of_text = true;
  }
  if (*flag & 0x10) {
    pen.bottom_of_text = true;
  }

  return pen;
}

long add_texture(SDL_Surface* texture) {
  auto texpos = enabler->textures.raws.size();
  enabler->textures.raws.push_back(texture);
  return texpos;
}

static void renderOverlay() {
  Screen::paintString(Screen::Pen{}, 2, 50, std::format("total strings: {}", g_screen_text.size()));
  Screen::paintString(Screen::Pen{}, 2, 51, std::format("dict: {}", Dictionary::instance().Size()));
  Screen::paintString(Screen::Pen{}, 2, 52, std::format("texpos cache: {}", g_texpos_cache.Size()));
  Screen::paintString(Screen::Pen{}, 2, 53, std::format("texpos size: {}", enabler->textures.raws.size()));
  // Screen::paintString(Screen::Pen{}, 2, 54, std::format("tm texpos: {}", TexposManager::instance().SizeTexpos()));
  // Screen::paintString(Screen::Pen{}, 2, 55, std::format("tm surface: {}", TexposManager::instance().SizeSurface()));

  for (auto& text : g_screen_text) {
    for (auto i = 0; i < text.str.size(); i++) {
      if (text.x + i >= text.clipx.second - 4) break; // why it's do not work?
      auto pen = read_tile(text.x + i, text.y, text.screen_type);

      std::wstring ws{ text.str[i] };
      auto flag = TTFManager::Viewbox::NORMAL;

      if (pen.top_of_text && pen.bottom_of_text) {
        // TODO: handle BOTH_HALF
      } else if (pen.top_of_text) {
        flag = TTFManager::Viewbox::UPPER_HALF;
        ws += L"UPPER_HALF";
      } else if (pen.bottom_of_text) {
        flag = TTFManager::Viewbox::BOTTOM_HALF;
        ws += L"BOTTOM_HALF";
      }

      // use cached texpos or get a new one long texpos = 0;
      long texpos = 0;
      if (auto cached_texpos = g_texpos_cache.Get(ws); cached_texpos) {
        texpos = cached_texpos.value().get();
      } else {
        auto texture = TTFManager::instance().GetTextureWS(std::wstring{ text.str[i] }, flag);
        texpos = add_texture(texture);
        g_texpos_cache.Put(ws, texpos);
      }

      // auto& tm = TexposManager::instance();
      // long texpos = 0;
      // if (auto cached_texpos_handle = g_texpos_hanlde_cache.Get(ws); cached_texpos_handle) {
      //   texpos = tm.getTexposByHandle(cached_texpos_handle.value()).value(); // handle NULLOPT!
      // } else {
      //   auto texture = TTFManager::instance().GetTextureWS(std::wstring{ text.str[i] }, flag);
      //   auto handle = tm.getNewHandle(texture);
      //   g_texpos_hanlde_cache.Put(ws, handle);
      //   texpos = tm.getTexposByHandle(cached_texpos_handle.value()).value();
      // }

      pen.ch = 0;                                       // clear default char on the tile
      pen.tile_mode = Screen::Pen::TileMode::CharColor; // use colors from original char
      pen.tile = texpos;                                // our texture here
      pen.keep_lower = true;                            // keep background

      paint_tile(pen, text.x + i, text.y, text.screen_type);
    }
  }
  g_screen_text.clear();
}

DFHACK_PLUGIN_LUA_FUNCTIONS{ DFHACK_LUA_FUNCTION(renderOverlay), DFHACK_LUA_END };