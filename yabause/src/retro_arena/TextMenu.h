/*  Text-mode in-game menu for the Vulkan build: black screen, a list of lines, one highlighted row. No widgets, no OpenGL.
    Input arrives through MenuLayer (InputManager translates pad, d-pad, analog stick and keyboard into "up/down/left/right/a/b"). Pages are a stack
    of item lists, so further pages (cheats, RetroAchievements) can be added later without changing the draw or input code. */
#ifndef TEXTMENU_H
#define TEXTMENU_H

#include "MenuLayer.h"
#include <functional>
#include <string>
#include <vector>
#include <stdint.h>

class TextMenu : public MenuLayer {
public:
  TextMenu(const std::string & configfile, const std::string & gamepath);

  /* SDL event codes (registered by main.cpp), same meaning as MenuScreen's. */
  void setToggleMenuEventCode(uint32_t t) { ev_toggle_menu_ = t; }
  void setResetMenuEventCode(uint32_t t) { ev_reset_ = t; }
  void setSaveStateEventCode(uint32_t t) { ev_save_ = t; }
  void setLoadStateEventCode(uint32_t t) { ev_load_ = t; }
  void setOpenTrayMenuEventCode(uint32_t t) { ev_open_tray_ = t; }
  void setCloseTrayMenuEventCode(uint32_t t) { ev_close_tray_ = t; }
  void setUpdateConfig(uint32_t t) { ev_update_config_ = t; }
  void setCurrentGamePath(const std::string & p) { gamepath_ = p; configfile_ = p; }   /* Preference(path) names the per-game config */
  void setCurrentGameId(const std::string & id) { gameid_ = id; }   /* prefix of the save state files, e.g. "<savepath>/<itemnum>_" */
  void setKeymapFile(const std::string & f) { keymapfile_ = f; }   /* keymapv2.json: where the Controls page reads and writes the bindings */

  /* Called when the menu is opened: back to the root page, config re-read. */
  void onOpen();

  /* MenuLayer */
  bool keyboardEvent(std::string & keycode, int scancode, int action, int modifiers) override;
  int onBackButtonPressed() override;
  /* Controls page: while a binding is being captured this takes the next button / d-pad / key press (returns 0), otherwise -1 = not handled. */
  int onRawInputEvent(InputManager & imp, const std::string & deviceguid, const std::string & type, int id, int value) override;

  /* What to draw: a title, the rows (label, value, selectable), the selected row and a footer line. */
  struct Row { std::string label, value; bool selectable; };
  void getView(std::string & title, std::vector<Row> & rows, int & selected, std::string & footer);

  /* Text version of the view for logs and tests. */
  std::string dump();

private:
  struct Item {
    std::string label;
    std::function<std::string()> value;          /* shown right of the label */
    std::function<void(int dir)> change;         /* left (-1) / right (+1) */
    std::function<void()> accept;                /* A */
    bool selectable = true;
  };
  struct Page { std::string title; std::vector<Item> items; int sel = 0; };

  std::vector<Page> stack_;
  std::string configfile_, gamepath_, gameid_, status_, keymapfile_;

  /* Controls page state */
  std::string capture_key_;          /* Saturn button being bound ("a", "start", ...); empty = not capturing */
  uint32_t capture_deadline_ = 0;    /* SDL_GetTicks() at which an unanswered capture is cancelled */
  uint32_t capture_done_ms_ = 0;     /* when the last binding was stored (the release of that button must not count as "back") */
  uint32_t capture_start_ms_ = 0;    /* when the capture began: the press that started it also arrives as a raw event and must be ignored */
  bool captureActive();              /* true while waiting for a press; cancels itself after the timeout */
  Page buildControls();
  std::string bindingText(const std::string & key);
  void playerSection(std::string & section, std::string & devname, bool & keyboard);   /* keymap section name of player 1 */

  uint32_t ev_toggle_menu_ = 0, ev_reset_ = 0, ev_save_ = 0, ev_load_ = 0, ev_open_tray_ = 0, ev_close_tray_ = 0, ev_update_config_ = 0;

  Page buildRoot();
  Page buildVideo();
  Page buildSound();
  /* (buildControls is declared above with the Controls state) */
  Page buildStates(bool save);
  void push(Page p) { stack_.push_back(std::move(p)); }
  void pop();
  void close();
  void moveSel(int dir);
  void pushEvent(uint32_t type, int code);
  bool stateExists(int slot);
  std::string stateStamp(int slot);
  void nextDisc();
};

#endif
