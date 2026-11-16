#include "TextMenu.h"
#include "Preference.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>

/* provided by main.cpp / the core */
extern int g_EnagleFPS;
extern int g_frame_skip;
#include "yabause.h"   /* VDP_SETTING_* enum and VideoSetSetting() (an earlier hand-written fallback used 5 = ROTATE_SCREEN: the resolution setting never reached the renderer) */
extern "C" {
  void ScspSetCddaVolume(int v);
  void ScspSetSfxVolume(int v);
  void EnableAutoFrameSkip(void);
  void DisableAutoFrameSkip(void);
}

static const char * RESOLUTIONS[] = { "Native", "4x", "2x", "Original", "720p", "1080p" };
static const int RESOLUTIONS_N = 6;
static const char * ASPECTS[] = { "Original", "4:3", "16:9", "Full Screen" };
static const int ASPECTS_N = 4;
static const int SNDVOL[] = { 0, 25, 50, 75, 100, 125, 150 };
static const int SNDVOL_N = 7;

static bool ends_with_ci(std::string s, const char * suffix) {
  std::transform(s.begin(), s.end(), s.begin(), ::tolower);
  const size_t n = strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

#include "InputManager.h"
#include <fstream>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

TextMenu::TextMenu(const std::string & configfile, const std::string & gamepath) : configfile_(configfile), gamepath_(gamepath) {}

void TextMenu::onOpen() {
  stack_.clear();
  status_.clear();
  push(buildRoot());
}

void TextMenu::pushEvent(uint32_t type, int code) {
  if (type == 0) return;
  SDL_Event ev = {};
  ev.type = type;
  ev.user.code = code;
  ev.user.data1 = 0;
  ev.user.data2 = 0;
  SDL_PushEvent(&ev);
}

void TextMenu::close() { pushEvent(ev_toggle_menu_, 0); }

void TextMenu::pop() {
  if (stack_.size() > 1) stack_.pop_back();
  else close();
}

void TextMenu::moveSel(int dir) {
  if (stack_.empty()) return;
  Page & p = stack_.back();
  const int n = (int)p.items.size();
  if (n == 0) return;
  int i = p.sel;
  for (int k = 0; k < n; k++) {
    i = (i + dir + n) % n;
    if (p.items[i].selectable) { p.sel = i; return; }
  }
}

bool TextMenu::keyboardEvent(std::string & keycode, int scancode, int action, int modifiers) {
  (void)scancode; (void)modifiers;
  if (captureActive()) return true;   /* Controls page is waiting for a press: navigation is suspended */
  if (action == 0 || stack_.empty()) return true;
  Page & p = stack_.back();
  if (keycode == "up") moveSel(-1);
  else if (keycode == "down") moveSel(1);
  else if (keycode == "left" || keycode == "right") {
    Item & it = p.items[p.sel];
    if (it.change) it.change(keycode == "left" ? -1 : 1);
  }
  else if (keycode == "a") {
    Item & it = p.items[p.sel];
    if (it.accept) it.accept();
    else if (it.change) it.change(1);
  }
  else if (keycode == "b") pop();
  return true;
}

int TextMenu::onBackButtonPressed() {
  /* While a binding is being captured, or just after one was stored, the back button's release is not a "back" (-1 = not handled, so the
     caller does not toggle the menu either). */
  if (captureActive()) { capture_key_.clear(); status_ = "Binding cancelled."; return -1; }
  if (capture_done_ms_ && SDL_GetTicks() - capture_done_ms_ < 400) return -1;
  /* the physical back button: one page up, or close the menu from the root */
  pop();
  return 0;
}

/* ------------------------------------------------------------------------------------------------------------------------------ controls */

static const struct { const char * key; const char * label; } PAD_BUTTONS[] = {
  { "up", "Up" }, { "down", "Down" }, { "left", "Left" }, { "right", "Right" }, { "start", "Start" },
  { "a", "A" }, { "b", "B" }, { "c", "C" }, { "x", "X" }, { "y", "Y" }, { "z", "Z" }, { "l", "L (left trigger)" }, { "r", "R (right trigger)" },
};
static const int PAD_BUTTONS_N = (int)(sizeof(PAD_BUTTONS) / sizeof(PAD_BUTTONS[0]));

bool TextMenu::captureActive() {
  if (capture_key_.empty()) return false;
  if ((int32_t)(SDL_GetTicks() - capture_deadline_) >= 0) { capture_key_.clear(); status_ = "No input received: binding cancelled."; return false; }
  return true;
}

/* Player 1's section name in keymapv2.json, built exactly the way the core looks it up: "<DeviceID>_<deviceName>_<deviceGUID>". */
void TextMenu::playerSection(std::string & section, std::string & devname, bool & keyboard) {
  section.clear(); devname.clear(); keyboard = false;
  try {
    std::ifstream f(keymapfile_);
    json j; f >> j;
    if (j.count("player1") == 0) return;
    const json & p = j["player1"];
    const int id = p.value("DeviceID", -1);
    devname = p.value("deviceName", std::string());
    keyboard = (id == -1);
    if (keyboard) {
      /* the core's two code paths name the keyboard section differently ("-1_Keyboard_-1" in the file written from the ES config, "-1" in the built-in default): use the one that exists */
      section = (j.count("-1_Keyboard_-1") == 0 && j.count("-1") != 0) ? "-1" : "-1_Keyboard_-1";
      if (devname.empty()) devname = "Keyboard";
      return;
    }
    InputManager::genJoyString(section, id, devname, p.value("deviceGUID", std::string()));
  } catch (...) { section.clear(); }
}

std::string TextMenu::bindingText(const std::string & key) {
  std::string section, dev; bool kb = false;
  playerSection(section, dev, kb);
  if (section.empty()) return "?";
  try {
    std::ifstream f(keymapfile_);
    json j; f >> j;
    if (j.count(section) == 0 || j[section].count(key) == 0) return "(none)";
    const json & b = j[section][key];
    const int id = b.value("id", -1), val = b.value("value", 0);
    const std::string t = b.value("type", std::string());
    if (id < 0 || t.empty()) return "(none)";
    if (t == "button") return "Button " + std::to_string(id);
    if (t == "axis") return "Axis " + std::to_string(id);
    if (t == "hat") {
      const char * d = (val & 1) ? "up" : (val & 2) ? "right" : (val & 4) ? "down" : (val & 8) ? "left" : "?";
      return "Hat " + std::to_string(id) + " " + d;
    }
    if (t == "key") { const char * n = SDL_GetKeyName((SDL_Keycode)id); return (n && *n) ? std::string("Key ") + n : "Key " + std::to_string(id); }
    return t + " " + std::to_string(id);
  } catch (...) { return "?"; }
}

int TextMenu::onRawInputEvent(InputManager & imp, const std::string & deviceguid, const std::string & type, int id, int value) {
  (void)deviceguid;
  if (!captureActive()) return -1;
  /* the press that started the capture is delivered a second time as a raw event: ignore it */
  if (SDL_GetTicks() - capture_start_ms_ < 250) return 0;

  std::string section, dev; bool kb = false;
  playerSection(section, dev, kb);
  if (section.empty()) { capture_key_.clear(); status_ = "No player 1 device found in the keymap."; return 0; }

  if (type == "key" && id == SDLK_ESCAPE) { capture_key_.clear(); status_ = "Binding cancelled."; return 0; }
  const bool ok = kb ? (type == "key") : (type == "button" || type == "hat");
  if (!ok) return -1;   /* the wrong kind of input for player 1's device (e.g. a key press while a pad is player 1) */

  const std::string key = capture_key_;
  imp.saveInputConfig(section, key, type, id, value);
  capture_key_.clear();
  capture_done_ms_ = SDL_GetTicks();
  pushEvent(ev_update_config_, 0);   /* main.cpp reloads the keymap: the new binding is live at once */

  std::string label = key;
  for (int i = 0; i < PAD_BUTTONS_N; i++) if (key == PAD_BUTTONS[i].key) label = PAD_BUTTONS[i].label;
  status_ = "Saved: " + label + " = " + bindingText(key);
  /* warn when the same input now drives another Saturn button */
  try {
    std::ifstream f(keymapfile_);
    json j; f >> j;
    std::string also;
    for (int i = 0; i < PAD_BUTTONS_N; i++) {
      const std::string k = PAD_BUTTONS[i].key;
      if (k == key || j[section].count(k) == 0) continue;
      const json & b = j[section][k];
      if (b.value("type", std::string()) == type && b.value("id", -1) == id && (type != "hat" || b.value("value", 0) == value)) also += std::string(also.empty() ? "" : ", ") + PAD_BUTTONS[i].label;
    }
    if (!also.empty()) status_ += "   (also: " + also + ")";
  } catch (...) {}
  return 0;
}

TextMenu::Page TextMenu::buildControls() {
  Page p; p.title = "CONTROLS (PLAYER 1)";
  {
    Item i; i.label = "Device"; i.selectable = false;
    i.value = [this]() { std::string s, d; bool kb = false; playerSection(s, d, kb); return d.empty() ? std::string("?") : d; };
    p.items.push_back(i);
  }
  for (int k = 0; k < PAD_BUTTONS_N; k++) {
    const std::string key = PAD_BUTTONS[k].key, label = PAD_BUTTONS[k].label;
    Item i; i.label = label;
    i.value = [this, key]() { return capture_key_ == key ? std::string("<press the new input>") : bindingText(key); };
    i.accept = [this, key, label]() {
      capture_key_ = key;
      capture_start_ms_ = SDL_GetTicks();
      capture_deadline_ = capture_start_ms_ + 6000;
      status_ = "Press the new input for " + label + "  (6 seconds, Esc on a keyboard cancels)";
    };
    p.items.push_back(i);
  }
  p.sel = 1;   /* the first row is the (fixed) device line */
  return p;
}

void TextMenu::getView(std::string & title, std::vector<Row> & rows, int & selected, std::string & footer) {
  rows.clear();
  if (stack_.empty()) { title = "Menu"; selected = 0; footer.clear(); return; }
  Page & p = stack_.back();
  title = p.title;
  for (Item & it : p.items) rows.push_back({ it.label, it.value ? it.value() : std::string(), it.selectable });
  selected = p.sel;
  footer = status_.empty() ? "Up/Down: select   Left/Right: change   A: enter   B: back" : status_;
}

std::string TextMenu::dump() {
  std::string t, f; std::vector<Row> rows; int sel = 0;
  getView(t, rows, sel, f);
  std::string out = "== " + t + " ==\n";
  for (size_t i = 0; i < rows.size(); i++) out += std::string(i == (size_t)sel ? "> " : "  ") + rows[i].label + (rows[i].value.empty() ? "" : "   " + rows[i].value) + "\n";
  return out + f + "\n";
}

/* ------------------------------------------------------------------------------------------------------------------------------ pages */

TextMenu::Page TextMenu::buildRoot() {
  Page p; p.title = "YABA SANSHIRO";
  { Item i; i.label = "Resume game"; i.accept = [this]() { close(); }; p.items.push_back(i); }
  { Item i; i.label = "Video settings"; i.value = []() { return std::string(">"); }; i.accept = [this]() { push(buildVideo()); }; p.items.push_back(i); }
  { Item i; i.label = "Sound settings"; i.value = []() { return std::string(">"); }; i.accept = [this]() { push(buildSound()); }; p.items.push_back(i); }
  { Item i; i.label = "Controls"; i.value = []() { return std::string(">"); }; i.accept = [this]() { push(buildControls()); }; p.items.push_back(i); }
  { Item i; i.label = "Save state"; i.value = []() { return std::string(">"); }; i.accept = [this]() { push(buildStates(true)); }; p.items.push_back(i); }
  { Item i; i.label = "Load state"; i.value = []() { return std::string(">"); }; i.accept = [this]() { push(buildStates(false)); }; p.items.push_back(i); }
  { Item i; i.label = "Reset game"; i.accept = [this]() { pushEvent(ev_reset_, 0); }; p.items.push_back(i); }
  { Item i; i.label = "Next disc"; i.accept = [this]() { nextDisc(); }; p.items.push_back(i); }
  { Item i; i.label = "Exit emulator"; i.accept = [this]() { SDL_Event q = {}; q.type = SDL_QUIT; SDL_PushEvent(&q); }; p.items.push_back(i); }
  return p;
}

TextMenu::Page TextMenu::buildVideo() {
  Page p; p.title = "VIDEO SETTINGS";
  {
    Item i; i.label = "Resolution";
    i.value = [this]() { Preference pr(configfile_); int v = pr.getInt("Resolution", 3); if (v < 0 || v >= RESOLUTIONS_N) v = 3; return std::string(RESOLUTIONS[v]); };
    i.change = [this](int dir) {
      Preference pr(configfile_); int v = pr.getInt("Resolution", 3);
      v = (v + dir + RESOLUTIONS_N) % RESOLUTIONS_N;
      pr.setInt("Resolution", v);
      VideoSetSetting(VDP_SETTING_RESOLUTION_MODE, v);   /* live, as the old menu did */
      status_ = "Resolution applied.";
    };
    p.items.push_back(i);
  }
  {
    Item i; i.label = "Rotating backgrounds";
    i.value = [this]() { Preference pr(configfile_); int v = pr.getInt("Rotate screen resolution", 0); static const char * n[] = { "Original", "2x", "720p", "1080p", "Native" }; if (v < 0 || v > 4) v = 0; return std::string(n[v]) + " (restart)"; };
    i.change = [this](int dir) {
      Preference pr(configfile_); int v = pr.getInt("Rotate screen resolution", 0);
      v = (v + dir + 5) % 5;
      pr.setInt("Rotate screen resolution", v);
      status_ = "Resolution of the rotating layer (RBG0): applies after a restart.";
    };
    p.items.push_back(i);
  }
  {
    Item i; i.label = "Aspect rate";
    i.value = [this]() { Preference pr(configfile_); int v = pr.getInt("Aspect rate", 0); if (v < 0 || v >= ASPECTS_N) v = 0; return std::string(ASPECTS[v]); };
    i.change = [this](int dir) {
      Preference pr(configfile_); int v = pr.getInt("Aspect rate", 0);
      v = (v + dir + ASPECTS_N) % ASPECTS_N;
      pr.setInt("Aspect rate", v);
      status_ = "Aspect rate applies when you resume.";
    };
    p.items.push_back(i);
  }
  {
    Item i; i.label = "Show FPS";
    i.value = []() { return std::string(g_EnagleFPS ? "On" : "Off"); };
    i.change = [this](int) { g_EnagleFPS = g_EnagleFPS ? 0 : 1; Preference pr(configfile_); pr.setInt("Show Fps", g_EnagleFPS); };
    p.items.push_back(i);
  }
  {
    Item i; i.label = "Frame skip";
    i.value = []() { return std::string(g_frame_skip ? "On" : "Off"); };
    i.change = [this](int) {
      if (g_frame_skip) { g_frame_skip = 0; DisableAutoFrameSkip(); } else { g_frame_skip = 1; EnableAutoFrameSkip(); }
      Preference pr(configfile_); pr.setBool("frame skip", g_frame_skip != 0);
    };
    p.items.push_back(i);
  }
  /* "Use compute shader" is not offered here: the Vulkan renderer always uses its compute path for the rotating backgrounds (the setting only exists for the OpenGL renderer). */
  return p;
}

TextMenu::Page TextMenu::buildSound() {
  Page p; p.title = "SOUND SETTINGS";
  for (int cd = 1; cd >= 0; cd--) {
    Item i; i.label = cd ? "CD music volume" : "Sound effects volume";
    const char * key = cd ? "cd volume" : "sfx volume";
    i.value = [this, key]() { Preference pr(configfile_); return std::to_string(pr.getInt(key, 100)) + "%"; };
    i.change = [this, key, cd](int dir) {
      Preference pr(configfile_); int cur = pr.getInt(key, 100);
      int idx = 0; for (int k = 0; k < SNDVOL_N; k++) if (SNDVOL[k] <= cur) idx = k;
      idx = std::max(0, std::min(SNDVOL_N - 1, idx + dir));
      pr.setInt(key, SNDVOL[idx]);
      if (cd) ScspSetCddaVolume(SNDVOL[idx]); else ScspSetSfxVolume(SNDVOL[idx]);
    };
    p.items.push_back(i);
  }
  return p;
}

std::string TextMenu::stateStamp(int slot) {
  std::string f = gameid_ + std::to_string(slot) + ".yss";
  struct stat st;
  if (stat(f.c_str(), &st) != 0) return "";
  char buf[64]; struct tm lt; time_t t = st.st_mtime; localtime_r(&t, &lt);
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &lt);
  return buf;
}
bool TextMenu::stateExists(int slot) { return !stateStamp(slot).empty(); }

TextMenu::Page TextMenu::buildStates(bool save) {
  Page p; p.title = save ? "SAVE STATE" : "LOAD STATE";
  for (int s = 0; s < 5; s++) {
    Item i; i.label = std::string(save ? "Save slot " : "Load slot ") + std::to_string(s);
    i.value = [this, s]() { std::string t = stateStamp(s); return t.empty() ? std::string("(empty)") : t; };
    if (save) i.accept = [this, s]() { pushEvent(ev_save_, s); };
    else i.accept = [this, s]() { if (stateExists(s)) pushEvent(ev_load_, s); else status_ = "That slot is empty."; };
    p.items.push_back(i);
  }
  return p;
}

void TextMenu::nextDisc() {
  /* same behaviour as the old menu: open the tray and close it again with the next image in the game's folder */
  size_t pos = gamepath_.find_last_of("/");
  if (pos == std::string::npos) return;
  std::string base = gamepath_.substr(0, pos);
  std::vector<std::string> discs;
  if (DIR * d = opendir(base.c_str())) {
    while (struct dirent * e = readdir(d)) {
      std::string n = e->d_name;
      if (ends_with_ci(n, ".cue") || ends_with_ci(n, ".mdf") || ends_with_ci(n, ".ccd") || ends_with_ci(n, ".chd")) discs.push_back(base + "/" + n);
    }
    closedir(d);
  }
  std::sort(discs.begin(), discs.end());
  if (discs.size() < 2) { status_ = "Only one disc image in this folder."; return; }
  size_t idx = 0;
  for (size_t i = 0; i < discs.size(); i++) if (discs[i] == gamepath_) { idx = i; break; }
  std::string goal = discs[(idx + 1) % discs.size()];
  pushEvent(ev_open_tray_, 0);
  SDL_Event ev = {};
  ev.type = ev_close_tray_;
  ev.user.code = 0;
  ev.user.data1 = malloc(goal.size() + 1);
  strcpy((char *)ev.user.data1, goal.c_str());
  ev.user.data2 = 0;
  SDL_PushEvent(&ev);
  gamepath_ = goal;
}
