/* NOT SDL. A minimal stand-in declaring only the SDL2 symbols this project
 * uses, so the SDL-dependent sources can be compile-checked on a machine
 * without SDL installed (`make stubcheck`). It is never linked and must never
 * be on the include path of a real build.
 */
#ifndef HP2_SDL_STUB_H
#define HP2_SDL_STUB_H

#include <stdio.h>

typedef unsigned char  Uint8;
typedef signed short   Sint16;
typedef unsigned short Uint16;
typedef unsigned int   Uint32;
typedef int            SDL_JoystickID;
typedef enum { SDL_FALSE = 0, SDL_TRUE = 1 } SDL_bool;

#define SDL_INIT_GAMECONTROLLER 0x00002000u
#define SDL_INIT_VIDEO          0x00000020u

typedef struct SDL_GameController SDL_GameController;
typedef struct SDL_Joystick SDL_Joystick;

typedef enum {
    SDL_CONTROLLER_TYPE_UNKNOWN = 0,
    SDL_CONTROLLER_TYPE_XBOX360,
    SDL_CONTROLLER_TYPE_XBOXONE,
    SDL_CONTROLLER_TYPE_PS3,
    SDL_CONTROLLER_TYPE_PS4,
    SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO,
    SDL_CONTROLLER_TYPE_VIRTUAL,
    SDL_CONTROLLER_TYPE_PS5,
    SDL_CONTROLLER_TYPE_AMAZON_LUNA,
    SDL_CONTROLLER_TYPE_GOOGLE_STADIA,
    SDL_CONTROLLER_TYPE_NVIDIA_SHIELD,
    SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT,
    SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT,
    SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR
} SDL_GameControllerType;
#define SDL_CONTROLLER_TYPE_VIRTUAL SDL_CONTROLLER_TYPE_VIRTUAL
#define SDL_CONTROLLER_TYPE_AMAZON_LUNA SDL_CONTROLLER_TYPE_AMAZON_LUNA
#define SDL_CONTROLLER_TYPE_GOOGLE_STADIA SDL_CONTROLLER_TYPE_GOOGLE_STADIA
#define SDL_CONTROLLER_TYPE_NVIDIA_SHIELD SDL_CONTROLLER_TYPE_NVIDIA_SHIELD
#define SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR

typedef enum {
    SDL_CONTROLLER_BUTTON_INVALID = -1,
    SDL_CONTROLLER_BUTTON_A = 0,
    SDL_CONTROLLER_BUTTON_B,
    SDL_CONTROLLER_BUTTON_X,
    SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_BACK,
    SDL_CONTROLLER_BUTTON_GUIDE,
    SDL_CONTROLLER_BUTTON_START,
    SDL_CONTROLLER_BUTTON_LEFTSTICK,
    SDL_CONTROLLER_BUTTON_RIGHTSTICK,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
} SDL_GameControllerButton;

typedef enum {
    SDL_CONTROLLER_AXIS_INVALID = -1,
    SDL_CONTROLLER_AXIS_LEFTX = 0,
    SDL_CONTROLLER_AXIS_LEFTY,
    SDL_CONTROLLER_AXIS_RIGHTX,
    SDL_CONTROLLER_AXIS_RIGHTY,
    SDL_CONTROLLER_AXIS_TRIGGERLEFT,
    SDL_CONTROLLER_AXIS_TRIGGERRIGHT
} SDL_GameControllerAxis;

#define SDL_QUIT                    0x100
#define SDL_KEYDOWN                 0x300
#define SDL_CONTROLLERDEVICEADDED   0x653
#define SDL_CONTROLLERDEVICEREMOVED 0x654
#define SDLK_ESCAPE                 27

typedef struct { int sym; } SDL_Keysym;
typedef struct { Uint32 type; SDL_Keysym keysym; } SDL_KeyboardEvent;
typedef struct { Uint32 type; int which; } SDL_ControllerDeviceEvent;
typedef union {
    Uint32 type;
    SDL_KeyboardEvent key;
    SDL_ControllerDeviceEvent cdevice;
} SDL_Event;

int   SDL_Init(Uint32 flags);
int   SDL_InitSubSystem(Uint32 flags);
void  SDL_QuitSubSystem(Uint32 flags);
void  SDL_Quit(void);
const char *SDL_GetError(void);
void  SDL_Delay(Uint32 ms);
int   SDL_PollEvent(SDL_Event *event);
int   SDL_NumJoysticks(void);
SDL_bool SDL_IsGameController(int index);
SDL_GameController *SDL_GameControllerOpen(int index);
void  SDL_GameControllerClose(SDL_GameController *c);
const char *SDL_GameControllerName(SDL_GameController *c);
SDL_GameControllerType SDL_GameControllerGetType(SDL_GameController *c);
Sint16 SDL_GameControllerGetAxis(SDL_GameController *c, SDL_GameControllerAxis a);
Uint8 SDL_GameControllerGetButton(SDL_GameController *c, SDL_GameControllerButton b);
int   SDL_GameControllerRumble(SDL_GameController *c, Uint16 lo, Uint16 hi, Uint32 ms);
SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *c);
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *j);

#endif /* HP2_SDL_STUB_H */
