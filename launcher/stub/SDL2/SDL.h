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
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    SDL_CONTROLLER_BUTTON_DPAD_UP,
    SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT
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
#define SDL_WINDOWEVENT             0x200
#define SDL_CONTROLLERAXISMOTION    0x650
#define SDL_CONTROLLERBUTTONDOWN    0x651
#define SDLK_ESCAPE                 27
#define SDLK_RETURN                 13
#define SDLK_BACKSPACE              8
#define SDLK_SPACE                  32
#define SDLK_RIGHT                  0x4000004F
#define SDLK_LEFT                   0x40000050
#define SDLK_DOWN                   0x40000051
#define SDLK_UP                     0x40000052
#define SDLK_KP_ENTER               0x40000058
#define SDLK_n                      110
#define SDLK_h                      104
#define SDLK_F1                     0x4000003A

typedef struct { int sym; } SDL_Keysym;
typedef struct { Uint32 type; SDL_Keysym keysym; } SDL_KeyboardEvent;
typedef struct { Uint32 type; int which; } SDL_ControllerDeviceEvent;
typedef struct { Uint32 type; SDL_JoystickID which; Uint8 button; } SDL_ControllerButtonEvent;
typedef struct { Uint32 type; SDL_JoystickID which; Uint8 axis; Sint16 value; } SDL_ControllerAxisEvent;
typedef union {
    Uint32 type;
    SDL_KeyboardEvent key;
    SDL_ControllerDeviceEvent cdevice;
    SDL_ControllerButtonEvent cbutton;
    SDL_ControllerAxisEvent caxis;
} SDL_Event;

/* video, for the front-end renderer (hp2_fedraw.c, fe_main.c) */
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct { int x, y, w, h; } SDL_Rect;
typedef struct { float x, y; } SDL_FPoint;
typedef struct { Uint8 r, g, b, a; } SDL_Color;
typedef struct { SDL_FPoint position; SDL_Color color; SDL_FPoint tex_coord; } SDL_Vertex;
typedef struct { Uint32 flags; void *format; int w, h, pitch; void *pixels; } SDL_Surface;
typedef enum { SDL_BLENDMODE_NONE = 0, SDL_BLENDMODE_BLEND = 1 } SDL_BlendMode;
#define SDL_PIXELFORMAT_RGBA32          0x16762004u
#define SDL_TEXTUREACCESS_STATIC        0
#define SDL_RENDERER_SOFTWARE           0x1u
#define SDL_RENDERER_ACCELERATED        0x2u
#define SDL_RENDERER_PRESENTVSYNC       0x4u
#define SDL_WINDOW_FULLSCREEN           0x1u
#define SDL_WINDOW_HIDDEN               0x8u
#define SDL_WINDOW_RESIZABLE            0x20u
#define SDL_WINDOW_FULLSCREEN_DESKTOP   0x1001u
#define SDL_WINDOW_ALLOW_HIGHDPI        0x2000u
#define SDL_WINDOWPOS_CENTERED          0x2FFF0000u
#define SDL_HINT_VIDEODRIVER            "SDL_VIDEODRIVER"
#define SDL_HINT_RENDER_SCALE_QUALITY   "SDL_RENDER_SCALE_QUALITY"

/* audio, for the menu music (fe_main.c) */
#define SDL_INIT_AUDIO          0x00000010u
#define AUDIO_S16SYS            0x8010
typedef Uint32 SDL_AudioDeviceID;
typedef struct {
    int freq; Uint16 format; Uint8 channels; Uint8 silence; Uint16 samples; Uint16 padding;
    Uint32 size; void *callback; void *userdata;
} SDL_AudioSpec;
SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture, const SDL_AudioSpec *desired,
                                      SDL_AudioSpec *obtained, int allowed_changes);
int    SDL_QueueAudio(SDL_AudioDeviceID dev, const void *data, Uint32 len);
Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID dev);
void   SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on);
void   SDL_CloseAudioDevice(SDL_AudioDeviceID dev);
void   SDL_ClearQueuedAudio(SDL_AudioDeviceID dev);
int    SDL_WaitEventTimeout(SDL_Event *event, int timeout);
Uint32 SDL_GetTicks(void);

SDL_bool SDL_SetHint(const char *name, const char *value);
int   SDL_WaitEvent(SDL_Event *event);
SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags);
void  SDL_DestroyWindow(SDL_Window *w);
int   SDL_SetWindowFullscreen(SDL_Window *w, Uint32 flags);
SDL_Renderer *SDL_CreateRenderer(SDL_Window *w, int index, Uint32 flags);
void  SDL_DestroyRenderer(SDL_Renderer *r);
int   SDL_GetRendererOutputSize(SDL_Renderer *r, int *w, int *h);
int   SDL_SetRenderDrawColor(SDL_Renderer *r, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca);
int   SDL_RenderClear(SDL_Renderer *r);
void  SDL_RenderPresent(SDL_Renderer *r);
int   SDL_RenderSetClipRect(SDL_Renderer *r, const SDL_Rect *rect);
int   SDL_RenderGeometry(SDL_Renderer *r, SDL_Texture *t, const SDL_Vertex *v, int nv, const int *idx, int ni);
int   SDL_RenderReadPixels(SDL_Renderer *r, const SDL_Rect *rect, Uint32 format, void *pixels, int pitch);
SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, Uint32 format, int access, int w, int h);
int   SDL_UpdateTexture(SDL_Texture *t, const SDL_Rect *rect, const void *pixels, int pitch);
int   SDL_SetTextureBlendMode(SDL_Texture *t, SDL_BlendMode mode);
void  SDL_DestroyTexture(SDL_Texture *t);
SDL_Surface *SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int w, int h, int depth, Uint32 format);
void  SDL_FreeSurface(SDL_Surface *s);
int   SDL_SaveBMP(SDL_Surface *s, const char *file);   /* a macro in real SDL */

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
