/*
* Descent 3 
* Copyright (C) 2024 Parallax Software
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.

--- HISTORICAL COMMENTS FOLLOW ---

 * $Logfile: /DescentIII/Main/ddio_lnx/lnxjoy.cpp $
 * $Revision: 1.3 $
 * $Date: 2001/02/07 09:16:45 $
 * $Author: icculus $
 *
 * Linux joystick routines
 *
 * $Log: sdljoy.cpp,v $
 * Revision 1.3  2001/02/07 09:16:45  icculus
 * More robust debugging information.
 *
 * Revision 1.2  2000/06/29 22:15:25  hercules
 * Fixed hat motion
 *
 * Revision 1.1  2000/06/29 09:53:00  hercules
 * Use SDL joystick support (hats off to you! :)
 *
 * Revision 1.3  2000/06/24 01:15:15  icculus
 * patched to compile.
 *
 * Revision 1.2  2000/05/29 05:21:09  icculus
 * Changed a fprintf(stderr, ...) to an mprintf()...
 *
 * Revision 1.1.1.1  2000/04/18 00:00:33  icculus
 * initial checkin
 *
 *
 * 9     8/22/99 5:55p Jeff
 * fixed assert
 *
 * 8     8/19/99 3:46p Jeff
 * removed mprintfs
 *
 * 7     8/19/99 3:22p Jeff
 * added support for joystick driver version pre 1.0
 *
 * 6     8/18/99 9:47p Jeff
 * joystick support! for kernel 2..2+ clients...need to handle before that
 * still.
 *
 * 5     8/17/99 2:32p Jeff
 * fixed joy_GetPos
 *
 * 4     7/14/99 9:06p Jeff
 * added comment header
 *
 * $NoKeywords: $
 */

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <SDL3/SDL.h>

// rcg06182000 need this for specific joystick stuff.
#include "args.h"
#include "joystick.h"
#include "log.h"
#include "ddio_common.h" // for the KEY_* codes joy_MenuKey returns

//	---------------------------------------------------------------------------
//	globals

static int specificJoy = -1;
//	The six axes tJoyPos carries - x, y, z, r, u and v - and the width of the
//	button mask it carries them beside. Named here rather than taken from the
//	controller headers, which this file does not otherwise depend on.
#define JOY_NUM_AXES 6
#define JOY_BUTTON_BITS 32

static struct {
  SDL_Joystick *handle;
  tJoyInfo caps;
  //	Each axis also appears as a pair of buttons, so that a shoulder trigger -
  //	which XInput reports as an axis rather than as a button - can be bound to
  //	fire something. real_btns is how many buttons the pad actually has, before
  //	those; axis_rest is where each axis sits untouched, which is what the pair
  //	is measured from; axis_btn_state carries the hysteresis between polls.
  int real_btns;
  int axis_rest[JOY_NUM_AXES];
  uint32_t axis_btn_state;
} Joysticks[MAX_JOYSTICKS];

//	Deflection from rest at which an axis counts as a button press, and the lower
//	figure at which it stops counting. The gap keeps an axis resting near the
//	threshold from chattering.
#define JOY_AXIS_BUTTON_ON 16384
#define JOY_AXIS_BUTTON_OFF 11000

static int joyGetNumDevs(void);

//		closes connection with controller.
static void joy_CloseStick(tJoystick joy);

//	initializes a joystick
//		if server_adr is valid, a link is opened to another machine with a controller.
static bool joy_InitStick(tJoystick joy, char *server_adr);

//	---------------------------------------------------------------------------
//	functions

//	joystick system initialization
bool joy_Init() {
  // Keep reading the pad even when the window does not hold input focus.
  //
  // SDL discards joystick input while the window is unfocused. Under a
  // compositor that manages focus itself - gamescope, which is how these
  // handhelds run games - the window can be the only thing on screen and still
  // not be what SDL considers focused, and then the pad is dead with no
  // diagnostic: the sticks open, and every axis reads zero forever.
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

  //	reinitialize joystick if already initialized.
  joy_Close();
  if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK)) {
    LOG_ERROR << "Could not initialize Joystick";
    return false;
  }

  //	check if this OS supports joysticks
  if (!joyGetNumDevs()) {
    return false;
  }

  // rcg06182000 specific joystick support.
  if (specificJoy >= 0) {
    joy_InitStick((tJoystick)specificJoy, nullptr);
  } // if
  else {
    //	initialize joystick list
    for (int i = 0; i < MAX_JOYSTICKS; i++) {
      joy_InitStick((tJoystick)i, nullptr);
    }
  } // else
  return true;
}

void joy_Close() {
  //	initialize joystick list
  for (int i = 0; i < MAX_JOYSTICKS; i++) {
    joy_CloseStick((tJoystick)i);
  }
  SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
}

//	initializes a joystick
//		if server_adr is valid, a link is opened to another machine with a controller.
static bool joy_InitStick(tJoystick joy, char *server_adr) {
  //	close down already open joystick.
  joy_CloseStick(joy);

  //	okay, now if this is a remote joystick, open it
  if (server_adr) {
    return false;
  }
  SDL_Joystick *stick = SDL_OpenJoystick(joy);
  Joysticks[joy].handle = stick;
  if (stick) {
    tJoyInfo caps;

    memset(&caps, 0, (sizeof(caps)));
    strncpy(caps.name, SDL_GetJoystickNameForID(joy), sizeof(caps.name) - 1);
    caps.num_btns = SDL_GetNumJoystickButtons(stick);
    int axes = SDL_GetNumJoystickAxes(stick);
    switch (axes) {
    default:
      // Fall through to 6 axes
    case 6:
      caps.axes_mask |= JOYFLAG_VVALID;
      caps.minv = -32767;
      caps.maxv = 32768;
    case 5:
      caps.axes_mask |= JOYFLAG_UVALID;
      caps.minu = -32767;
      caps.maxu = 32768;
    case 4:
      caps.axes_mask |= JOYFLAG_RVALID;
      caps.minr = -32767;
      caps.maxr = 32768;
    case 3:
      caps.axes_mask |= JOYFLAG_ZVALID;
      caps.minz = -32767;
      caps.maxz = 32768;
    case 2:
      caps.axes_mask |= JOYFLAG_YVALID;
      caps.miny = -32767;
      caps.maxy = 32768;
    case 1:
      caps.axes_mask |= JOYFLAG_XVALID;
      caps.minx = -32767;
      caps.maxx = 32768;
    case 0:
      break;
    }
    int hats = SDL_GetNumJoystickHats(stick);
    switch (hats) {
    default:
      // Fall through to 4 hats
    case 4:
      caps.axes_mask |= JOYFLAG_POV4VALID;
    case 3:
      caps.axes_mask |= JOYFLAG_POV3VALID;
    case 2:
      caps.axes_mask |= JOYFLAG_POV2VALID;
    case 1:
      caps.axes_mask |= JOYFLAG_POVVALID;
    case 0:
      break;
    }
    //	Ask SDL where each axis is sitting before anyone has touched it. This is
    //	the whole difficulty with triggers: one rests at the far end of its
    //	travel, so measured from zero it reads as fully deflected all the time
    //	and its button would be permanently held.
    SDL_UpdateJoysticks();
    Joysticks[joy].real_btns = caps.num_btns;
    Joysticks[joy].axis_btn_state = 0;
    for (int i = 0; i < JOY_NUM_AXES; i++)
      Joysticks[joy].axis_rest[i] = (i < axes) ? SDL_GetJoystickAxis(stick, i) : 0;

    //	Two more buttons per axis, after the real ones. The mask is 32 bits wide
    //	and CT_MAX_BUTTONS is 32, so stop there rather than run off either.
    unsigned synthetic = caps.num_btns + 2 * std::min(axes, JOY_NUM_AXES);
    caps.num_btns = (synthetic > JOY_BUTTON_BITS) ? JOY_BUTTON_BITS : synthetic;

    Joysticks[joy].caps = caps;

    LOG_DEBUG.printf("JOYSTICK: Initialized stick named [%s].", caps.name);
    LOG_DEBUG.printf("JOYSTICK: (%d) axes, (%d) hats, and (%d) buttons (%d real).", axes, hats, caps.num_btns,
                     Joysticks[joy].real_btns);
    LOG_DEBUG.printf("JOYSTICK: axes rest at %d %d %d %d %d %d.", Joysticks[joy].axis_rest[0],
                     Joysticks[joy].axis_rest[1], Joysticks[joy].axis_rest[2], Joysticks[joy].axis_rest[3],
                     Joysticks[joy].axis_rest[4], Joysticks[joy].axis_rest[5]);
  }

  return (Joysticks[joy].handle != NULL);
}

//  closes connection with controller.
static void joy_CloseStick(tJoystick joy) {
  SDL_CloseJoystick(Joysticks[joy].handle);
  Joysticks[joy].handle = nullptr;
}

//	returns true if joystick valid
bool joy_IsValid(tJoystick joy) {
  if (specificJoy >= 0) {
    if (joy != specificJoy) {
      return false;
    }
  }
  return (Joysticks[joy].handle != NULL);
}

//	retreive information about joystick.
void joy_GetJoyInfo(tJoystick joy, tJoyInfo *info) { memcpy(info, &Joysticks[(int)joy].caps, sizeof(tJoyInfo)); }

//	retreive uncalibrated position of joystick
#define LNX_JOYAXIS_RANGE 65535
void joy_GetRawPos(tJoystick joy, tJoyPos *pos) {
  joy_GetPos(joy, pos);

  pos->x = (pos->x + 32767);
  pos->y = (pos->y + 32767);
  pos->z = (pos->z + 32767);
  pos->r = (pos->r + 32767);
  pos->u = (pos->u + 32767);
  pos->v = (pos->v + 32767);
}

static inline uint32_t map_hat(Uint8 value) {
  uint32_t mapped = 0;

  switch (value) {
  case SDL_HAT_CENTERED:
    mapped = JOYPOV_CENTER;
    break;
  case SDL_HAT_UP:
    mapped = 0x00;
    break;
  case SDL_HAT_UP | SDL_HAT_RIGHT:
    mapped = 0x20;
    break;
  case SDL_HAT_RIGHT:
    mapped = 0x40;
    break;
  case SDL_HAT_RIGHT | SDL_HAT_DOWN:
    mapped = 0x60;
    break;
  case SDL_HAT_DOWN:
    mapped = 0x80;
    break;
  case SDL_HAT_DOWN | SDL_HAT_LEFT:
    mapped = 0xA0;
    break;
  case SDL_HAT_LEFT:
    mapped = 0xC0;
    break;
  case SDL_HAT_LEFT | SDL_HAT_UP:
    mapped = 0xE0;
    break;
  }
  return mapped;
}

//	returns the state of a stick, remote or otherwise
//	An axis read from where it actually rests, spread back over the full range.
//
//	A stick rests in the middle and this changes nothing for it. A shoulder
//	trigger rests at one end of its travel, and read raw it is therefore fully
//	deflected the whole time it is not being touched - which is why binding any
//	axis on the controls screen captured Z-axis on its own, that being the first
//	axis found past the threshold. Untouched now reads zero, whoever is asking:
//	the config screen, the bindings, and the pair of buttons below.
static int joy_AxisFromRest(tJoystick joy, int axis) {
  int raw = SDL_GetJoystickAxis(Joysticks[joy].handle, axis);
  int rest = Joysticks[joy].axis_rest[axis];
  int d = raw - rest;

  if (d >= 0) {
    int range = 32767 - rest;
    return (range > 0) ? (int)((int64_t)d * 32767 / range) : 0;
  }
  int range = rest + 32768;
  return (range > 0) ? (int)((int64_t)d * 32768 / range) : 0;
}

void joy_GetPos(tJoystick joy, tJoyPos *pos) {
  SDL_Joystick *stick;
  int i;

  memset(pos, 0, (sizeof(*pos)));

  //	retrieve joystick info from the net, or locally.
  stick = Joysticks[joy].handle;
  if (stick) {
    uint32_t mask;

    mask = Joysticks[joy].caps.axes_mask;
    if (mask & JOYFLAG_XVALID) {
      pos->x = joy_AxisFromRest(joy, 0);
    }
    if (mask & JOYFLAG_YVALID) {
      pos->y = joy_AxisFromRest(joy, 1);
    }
    if (mask & JOYFLAG_ZVALID) {
      pos->z = joy_AxisFromRest(joy, 2);
    }
    if (mask & JOYFLAG_RVALID) {
      pos->r = joy_AxisFromRest(joy, 3);
    }
    if (mask & JOYFLAG_UVALID) {
      pos->u = joy_AxisFromRest(joy, 4);
    }
    if (mask & JOYFLAG_VVALID) {
      pos->v = joy_AxisFromRest(joy, 5);
    }
    for (i = 0; i < JOYPOV_NUM; ++i) {
      if (mask & (JOYFLAG_POVVALID << i)) {
        pos->pov[i] = map_hat(SDL_GetJoystickHat(stick, i));
      }
    }
    for (i = Joysticks[joy].real_btns; i >= 0; --i) {
      if (SDL_GetJoystickButton(stick, i)) {
        pos->buttons |= (1 << i);
      }
    }

    //	And the pair each axis makes, negative deflection then positive, measured
    //	from where the axis rests rather than from zero. Hysteresis: one that is
    //	already down has to come back further than it took to press it.
    for (i = 0; i < JOY_NUM_AXES; i++) {
      int bit = Joysticks[joy].real_btns + 2 * i;

      if (bit + 1 >= JOY_BUTTON_BITS)
        break;

      int d = joy_AxisFromRest(joy, i);
      bool was_minus = (Joysticks[joy].axis_btn_state & (1 << bit)) != 0;
      bool was_plus = (Joysticks[joy].axis_btn_state & (1 << (bit + 1))) != 0;
      bool minus = d < -(was_minus ? JOY_AXIS_BUTTON_OFF : JOY_AXIS_BUTTON_ON);
      bool plus = d > (was_plus ? JOY_AXIS_BUTTON_OFF : JOY_AXIS_BUTTON_ON);

      if (minus) {
        pos->buttons |= (1 << bit);
        Joysticks[joy].axis_btn_state |= (1 << bit);
      } else {
        Joysticks[joy].axis_btn_state &= ~(1 << bit);
      }
      if (plus) {
        pos->buttons |= (1 << (bit + 1));
        Joysticks[joy].axis_btn_state |= (1 << (bit + 1));
      } else {
        Joysticks[joy].axis_btn_state &= ~(1 << (bit + 1));
      }
    }
  }
}

static int joyGetNumDevs(void) {
  int found = 0;

  int joyCount = 0;
  SDL_JoystickID *joysticks = SDL_GetJoysticks(&joyCount);

  // rcg06182000 add support for specific joydev.
  int rc = FindArgChar("-joystick", 'j');
  specificJoy = -1;
  if ((rc > 0) && (GameArgs[rc + 1] != NULL)) {
    specificJoy = atoi(GameArgs[rc + 1]);
    if ((specificJoy >= 0) && (specificJoy < joyCount)) {
      found = 1;
    } else {
      specificJoy = -1;
    }
  }
  if (specificJoy < 0) {
    found = joyCount;
  }

  LOG_INFO.printf("Joystick: Found %d joysticks.", found);
  SDL_free(joysticks);
  return found;
}

void ddio_InternalJoyFrame(void) {
  // All the work is done already in SDL_PumpEvents()
}

//	---------------------------------------------------------------------------
//	Driving the menus from a gamepad.
//
//	The interface reads the mouse and the keyboard, and ui_KeyPoll has carried a
//	comment asking "possibly joystick?" since 1999. On a handheld with neither a
//	mouse nor a keyboard that leaves the pilot screen unreachable, and with it
//	the whole game.
//
//	Nothing in the interface needs to change: UIWindow already moves the focus on
//	the arrow keys and on Tab, UIButton takes Enter and Space, Esc works the
//	cancel gadget, and the list boxes take up and down. It simply never sees a
//	pad. So translate the pad into those keys, here where the button numbering is
//	known, and let ui_KeyPoll offer it whenever the keyboard has nothing.

#define JOY_MENU_REPEAT_DELAY 350 // ms held before a direction repeats
#define JOY_MENU_REPEAT_RATE 100  // ms between repeats after that
#define JOY_MENU_AXIS_THRESHOLD (32767 / 2)

//	Numbered for an XInput-shaped pad, which is what these handhelds present.
#define JOY_MENU_BTN_A 0
#define JOY_MENU_BTN_B 1
#define JOY_MENU_BTN_X 2
#define JOY_MENU_BTN_LB 4
#define JOY_MENU_BTN_RB 5
#define JOY_MENU_BTN_BACK 6
#define JOY_MENU_BTN_START 7

enum { JOY_MENU_UP, JOY_MENU_DOWN, JOY_MENU_LEFT, JOY_MENU_RIGHT, JOY_MENU_DIRS };

//	Arrow keys repeat when held, and a pilot list is unusable without it. The
//	directions are read as levels rather than as events - a stick has no key-up -
//	so the repeat has to be timed here.
static bool joy_MenuRepeat(int dir, bool down, uint64_t now) {
  static uint64_t next[JOY_MENU_DIRS] = {0, 0, 0, 0};

  if (!down) {
    next[dir] = 0;
    return false;
  }
  if (!next[dir]) { // went down this frame: act at once, then wait
    next[dir] = now + JOY_MENU_REPEAT_DELAY;
    return true;
  }
  if (now < next[dir])
    return false;
  next[dir] = now + JOY_MENU_REPEAT_RATE;
  return true;
}

//	How far the left stick is pushed, as a fraction of full deflection, for
//	moving the cursor. Descent 3's interface is a point-and-click one underneath
//	- the Android port drove its menus with a finger rather than with keys - and
//	some screens have no keyboard traversal at all. Returns false when the stick
//	is centred, so a caller can leave the cursor alone.
//	Whether the pad's click button is held. X, which nothing else uses: A sends
//	Enter for whatever has the focus and the shoulders send Tab to move it, so
//	the pointer and the keyboard routes never fight over a button.
bool joy_MenuClick(void) {
  for (int j = 0; j < MAX_JOYSTICKS; j++) {
    tJoyPos pos;

    if (!joy_IsValid((tJoystick)j))
      continue;
    joy_GetPos((tJoystick)j, &pos);
    if (pos.buttons & (1 << JOY_MENU_BTN_X))
      return true;
  }
  return false;
}

bool joy_MenuStick(float *x, float *y) {
  *x = *y = 0.0f;

  for (int j = 0; j < MAX_JOYSTICKS; j++) {
    tJoyPos pos;

    if (!joy_IsValid((tJoystick)j))
      continue;
    joy_GetPos((tJoystick)j, &pos);

    //	A wide deadzone: this is a cursor, and a stick that rests a little off
    //	centre should not send it wandering across the screen on its own.
    float fx = (float)pos.x / 32768.0f;
    float fy = (float)pos.y / 32768.0f;
    if (fabsf(fx) > 0.25f)
      *x = fx;
    if (fabsf(fy) > 0.25f)
      *y = fy;
  }
  return (*x != 0.0f) || (*y != 0.0f);
}

//	What the pad means while flying: one thing only.
//
//	The player's own bindings do the flying, and a general translation here would
//	make every unbound button do something surprising. Start is offered because
//	the in-game menu is reached by Escape and nothing else, and a handheld has no
//	Escape key - so without this there is no way out of a level at all.
int joy_GameKey(void) {
  static uint32_t last_buttons[MAX_JOYSTICKS] = {0};

  for (int j = 0; j < MAX_JOYSTICKS; j++) {
    tJoyPos pos;
    uint32_t pressed;

    if (!joy_IsValid((tJoystick)j))
      continue;
    joy_GetPos((tJoystick)j, &pos);
    pressed = pos.buttons & ~last_buttons[j];
    last_buttons[j] = pos.buttons;
    if (pressed & (1 << JOY_MENU_BTN_START))
      return KEY_ESC;
  }
  return 0;
}

//	What the pad has to say, as a key code, or 0 for nothing.
//
//	Every pad that is open, not just the first: which slot the one in the
//	player's hands lands in is not up to us, since the physical controls are
//	often hidden behind a virtual pad the compositor makes and another that Steam
//	layers on top.
int joy_MenuKey(void) {
  static uint32_t last_buttons[MAX_JOYSTICKS] = {0};
  //	The d-pad sends the arrows, which is what the contents of a screen want:
  //	a list box scrolls its items on up and down, and a group of gadgets moves
  //	between them. Moving between the gadgets themselves is Tab, and that is on
  //	the shoulders below - UIWindow acts on the arrows only inside a group, so
  //	neither one alone can reach everything.
  static const int dir_keys[JOY_MENU_DIRS] = {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT};
  static const struct {
    int button;
    int key;
  } button_keys[] = {
      {JOY_MENU_BTN_A, KEY_ENTER}, // accept, and work a button
      //	Start is deliberately absent. It is what opens the in-game menu, and a
      //	button that opened a confirmation and answered it in the same press
      //	walked straight out of the game: joy_GameKey turned it into Escape for
      //	the game loop, and this turned the same press into Enter for the dialog
      //	that came up. Start means "menu", and nothing else.
      {JOY_MENU_BTN_B, KEY_ESC},      // back out
      {JOY_MENU_BTN_BACK, KEY_ESC},
      {JOY_MENU_BTN_LB, KEY_SHIFTED + KEY_TAB}, // and the shoulders move
      {JOY_MENU_BTN_RB, KEY_TAB},               // between the gadgets
  };

  bool down[JOY_MENU_DIRS] = {false, false, false, false};
  uint64_t now = SDL_GetTicks();
  int key = 0;

  for (int j = 0; j < MAX_JOYSTICKS; j++) {
    tJoyPos pos;
    uint32_t pressed;

    if (!joy_IsValid((tJoystick)j))
      continue;
    joy_GetPos((tJoystick)j, &pos);

    //	The hat, as a compass: 0 is up and it runs clockwise, with 0xff for
    //	centred. Each direction claims the two diagonals beside it.
    if (pos.pov[0] != JOYPOV_CENTER) {
      uint32_t pov = pos.pov[0];
      if (pov >= 0xE0 || pov <= 0x20)
        down[JOY_MENU_UP] = true;
      if (pov >= 0x20 && pov <= 0x60)
        down[JOY_MENU_RIGHT] = true;
      if (pov >= 0x60 && pov <= 0xA0)
        down[JOY_MENU_DOWN] = true;
      if (pov >= 0xA0 && pov <= 0xE0)
        down[JOY_MENU_LEFT] = true;
    }

    //	The stick is deliberately not read here. It pushes the cursor instead,
    //	through joy_MenuStick, and a control doing two jobs at once - scrolling a
    //	list and moving a pointer on the same push - is worse than either.

    //	Buttons need no repeat, so take the edge: pressed since last time.
    pressed = pos.buttons & ~last_buttons[j];
    last_buttons[j] = pos.buttons;
    for (size_t i = 0; i < std::size(button_keys); i++)
      if ((pressed & (1 << button_keys[i].button)) && !key)
        key = button_keys[i].key;
  }

  //	Every direction every time, even once one has fired: the ones that were let
  //	go have to be seen to be let go, or they will not repeat properly next time.
  for (int i = 0; i < JOY_MENU_DIRS; i++)
    if (joy_MenuRepeat(i, down[i], now) && !key)
      key = dir_keys[i];

  return key;
}
