/*  In-game menu input interface.
    InputManager hands menu input to whatever is shown: the nanogui MenuScreen (OpenGL builds) or the text menu (Vulkan build). Both implement this. */
#ifndef MENULAYER_H
#define MENULAYER_H

#include <SDL2/SDL.h>
#include <map>
#include <string>

class InputManager;

class MenuLayer {
public:
  virtual ~MenuLayer() {}
  /* keycode is one of "up", "down", "left", "right", "a", "b", ...; action != 0 is a press. */
  virtual bool keyboardEvent(std::string & keycode, int scancode, int action, int modifiers) = 0;
  /* Raw input (used by key-binding pages); -1 = not handled. */
  virtual int onRawInputEvent(InputManager & imp, const std::string & deviceguid, const std::string & type, int id, int value) { (void)imp; (void)deviceguid; (void)type; (void)id; (void)value; return -1; }
  /* The physical back button; 0 = handled. */
  virtual int onBackButtonPressed() { return -1; }
  virtual void setCurrentInputDevices(std::map<SDL_JoystickID, SDL_Joystick*> & joysticks) { (void)joysticks; }
};

#endif
