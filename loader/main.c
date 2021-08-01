/* main.c -- Battlefield: Bad Company 2 .so loader
 *
 * Copyright (C) 2021 Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/audioout.h>
#include <psp2/ctrl.h>
#include <psp2/power.h>
#include <psp2/touch.h>
#include <kubridge.h>
#include <vitashark.h>
#include <vitaGL.h>

#include <malloc.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <math.h>

#include <errno.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "main.h"
#include "config.h"
#include "dialog.h"
#include "so_util.h"

int _newlib_heap_size_user = MEMORY_NEWLIB_MB * 1024 * 1024;

static so_module bc2_mod;

void *__wrap_memcpy(void *dest, const void *src, size_t n) {
  return sceClibMemcpy(dest, src, n);
}

void *__wrap_memmove(void *dest, const void *src, size_t n) {
  return sceClibMemmove(dest, src, n);
}

void *__wrap_memset(void *s, int c, size_t n) {
  return sceClibMemset(s, c, n);
}

int debugPrintf(char *text, ...) {
#ifdef DEBUG
  va_list list;
  char string[512];

  va_start(list, text);
  vsprintf(string, text, list);
  va_end(list);

  SceUID fd = sceIoOpen("ux0:data/bc2_log.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
  if (fd >= 0) {
    sceIoWrite(fd, string, strlen(string));
    sceIoClose(fd);
  }
#endif
  return 0;
}

int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
#ifdef DEBUG
  va_list list;
  char string[512];

  va_start(list, fmt);
  vsprintf(string, fmt, list);
  va_end(list);

  debugPrintf("%s", string);
#endif
  return 0;
}

int ret0(void) {
  return 0;
}

int ret1(void) {
  return 1;
}

int mkdir(const char *pathname, mode_t mode) {
  if (sceIoMkdir(pathname, mode) < 0)
    return -1;
  return 0;
}

int rmdir(const char *pathname) {
  if (sceIoRmdir(pathname) < 0)
    return -1;
  return 0;
}

char *getcwd(char *buf, size_t size) {
  if (buf) {
    buf[0] = '\0';
    return buf;
  }
  return NULL;
}

enum {
  ACTION_DOWN = 1,
  ACTION_UP   = 2,
  ACTION_MOVE = 3,
};

enum {
  AKEYCODE_DPAD_UP = 19,
  AKEYCODE_DPAD_DOWN = 20,
  AKEYCODE_DPAD_LEFT = 21,
  AKEYCODE_DPAD_RIGHT = 22,
  AKEYCODE_A = 29,
  AKEYCODE_B = 30,
  AKEYCODE_BUTTON_X = 99,
  AKEYCODE_BUTTON_Y = 100,
  AKEYCODE_BUTTON_L1 = 102,
  AKEYCODE_BUTTON_R1 = 103,
  AKEYCODE_BUTTON_START = 108,
  AKEYCODE_BUTTON_SELECT = 109,
};

typedef struct {
  uint32_t sce_button;
  uint32_t android_button;
} ButtonMapping;

static ButtonMapping mapping[] = {
  { SCE_CTRL_UP,        AKEYCODE_DPAD_UP },
  { SCE_CTRL_DOWN,      AKEYCODE_DPAD_DOWN },
  { SCE_CTRL_LEFT,      AKEYCODE_DPAD_LEFT },
  { SCE_CTRL_RIGHT,     AKEYCODE_DPAD_RIGHT },
  { SCE_CTRL_CROSS,     AKEYCODE_A },
  { SCE_CTRL_CIRCLE,    AKEYCODE_B },
  { SCE_CTRL_SQUARE,    AKEYCODE_BUTTON_X },
  { SCE_CTRL_TRIANGLE,  AKEYCODE_BUTTON_Y },
  { SCE_CTRL_L1,        AKEYCODE_BUTTON_L1 },
  { SCE_CTRL_R1,        AKEYCODE_BUTTON_R1 },
  { SCE_CTRL_START,     AKEYCODE_BUTTON_START },
  { SCE_CTRL_SELECT,    AKEYCODE_BUTTON_SELECT },
};

int ctrl_thread(SceSize args, void *argp) {
  int (* Android_Karisma_AppOnTouchEvent)(int type, int x, int y, int id) = (void *)so_symbol(&bc2_mod, "Android_Karisma_AppOnTouchEvent");
  int (* Android_Karisma_AppOnJoystickEvent)(int type, float x, float y, int id) = (void *)so_symbol(&bc2_mod, "Android_Karisma_AppOnJoystickEvent");
  int (* Android_Karisma_AppOnKeyEvent)(int type, int keycode) = (void *)so_symbol(&bc2_mod, "Android_Karisma_AppOnKeyEvent");

  int lastX[2] = { -1, -1 };
  int lastY[2] = { -1, -1 };

  float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;
  uint32_t old_buttons = 0, current_buttons = 0, pressed_buttons = 0, released_buttons = 0;

  while (1) {
    SceTouchData touch;
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

    for (int i = 0; i < 2; i++) {
      if (i < touch.reportNum) {
        int x = (int)((float)touch.report[i].x * (float)SCREEN_W / 1920.0f);
        int y = (int)((float)touch.report[i].y * (float)SCREEN_H / 1088.0f);

        if (lastX[i] != -1 || lastY[i] != -1)
          Android_Karisma_AppOnTouchEvent(ACTION_DOWN, x, y, i);
        else
          Android_Karisma_AppOnTouchEvent(ACTION_MOVE, x, y, i);
        lastX[i] = x;
        lastY[i] = y;
      } else {
        if (lastX[i] != -1 || lastY[i] != -1)
          Android_Karisma_AppOnTouchEvent(ACTION_UP, lastX[i], lastY[i], i);
        lastX[i] = -1;
        lastY[i] = -1;
      }
    }

    SceCtrlData pad;
    sceCtrlPeekBufferPositiveExt2(0, &pad, 1);

    old_buttons = current_buttons;
    current_buttons = pad.buttons;
    pressed_buttons = current_buttons & ~old_buttons;
    released_buttons = ~current_buttons & old_buttons;

    for (int i = 0; i < sizeof(mapping) / sizeof(ButtonMapping); i++) {
      if (pressed_buttons & mapping[i].sce_button)
        Android_Karisma_AppOnKeyEvent(0, mapping[i].android_button);
      if (released_buttons & mapping[i].sce_button)
        Android_Karisma_AppOnKeyEvent(1, mapping[i].android_button);
    }

    lx = ((float)pad.lx - 128.0f) / 128.0f;
    ly = ((float)pad.ly - 128.0f) / 128.0f;
    rx = ((float)pad.rx - 128.0f) / 128.0f;
    ry = ((float)pad.ry - 128.0f) / 128.0f;

    if (fabsf(lx) < 0.25f)
      lx = 0.0f;
    if (fabsf(ly) < 0.25f)
      ly = 0.0f;
    if (fabsf(rx) < 0.25f)
      rx = 0.0f;
    if (fabsf(ry) < 0.25f)
      ry = 0.0f;

    // TODO: send stop event only once
    Android_Karisma_AppOnJoystickEvent(3, lx, ly, 0);
    Android_Karisma_AppOnJoystickEvent(3, rx, ry, 1);

    sceKernelDelayThread(1000);
  }

  return 0;
}

static int audio_port = 0;
static int disable_sound = 0;

void SetShortArrayRegion(void *env, int array, size_t start, size_t len, const uint8_t *buf) {
  sceAudioOutOutput(audio_port, buf);
}

int sound_thread(SceSize args, void *argp) {
  int (* Java_com_dle_bc2_KarismaBridge_nativeUpdateSound)(void *env, int unused, int type, size_t length) = (void *)so_symbol(&bc2_mod, "Java_com_dle_bc2_KarismaBridge_nativeUpdateSound");

  audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, AUDIO_SAMPLES_PER_BUF / 2, AUDIO_SAMPLE_RATE, SCE_AUDIO_OUT_MODE_STEREO);

  static char fake_env[0x1000];
  memset(fake_env, 'A', sizeof(fake_env));
  *(uintptr_t *)(fake_env + 0x00) = (uintptr_t)fake_env; // just point to itself...
  *(uintptr_t *)(fake_env + 0x348) = (uintptr_t)SetShortArrayRegion;

  while (1) {
    if (disable_sound)
      sceKernelDelayThread(1000);
    else
      Java_com_dle_bc2_KarismaBridge_nativeUpdateSound(fake_env, 0, 0, AUDIO_SAMPLES_PER_BUF);
  }

  return 0;
}

int main_thread(SceSize args, void *argp) {
  vglSetupRuntimeShaderCompiler(SHARK_OPT_UNSAFE, SHARK_ENABLE, SHARK_ENABLE, SHARK_ENABLE);
  vglUseVram(GL_TRUE);
  vglInitExtended(0, SCREEN_W, SCREEN_H, MEMORY_VITAGL_THRESHOLD_MB * 1024 * 1024, SCE_GXM_MULTISAMPLE_4X);

  int (* Android_Karisma_AppInit)(void) = (void *)so_symbol(&bc2_mod, "Android_Karisma_AppInit");
  int (* Android_Karisma_AppUpdate)(void) = (void *)so_symbol(&bc2_mod, "Android_Karisma_AppUpdate");

  Android_Karisma_AppInit();

  SceUID ctrl_thid = sceKernelCreateThread("ctrl_thread", (SceKernelThreadEntry)ctrl_thread, 0x10000100, 128 * 1024, 0, 0, NULL);
  sceKernelStartThread(ctrl_thid, 0, NULL);

  SceUID sound_thid = sceKernelCreateThread("sound_thread", (SceKernelThreadEntry)sound_thread, 0x10000100, 128 * 1024, 0, 0, NULL);
  sceKernelStartThread(sound_thid, 0, NULL);

  while (1) {
    Android_Karisma_AppUpdate();
    vglSwapBuffers(GL_FALSE);
  }

  return 0;
}

char *Android_KarismaBridge_GetAppReadPath(void) {
  return DATA_PATH;
}

char *Android_KarismaBridge_GetAppWritePath(void) {
  return DATA_PATH;
}

void Android_KarismaBridge_EnableSound(void) {
  disable_sound = 0;
}

void Android_KarismaBridge_DisableSound(void) {
  disable_sound = 1;
}

typedef struct {
  void *vtable;
  char *path;
  size_t pathLen;
} CPath;

int krm__krt__io__CPath__IsRoot(CPath **this) {
  char *path = (*this)->path;
  if (strcmp(path, "ux0:") == 0)
    return 1;
  else
    return 0;
}

void patch_game(void) {
  *(int *)so_symbol(&bc2_mod, "_ZN3krm3sal12SCREEN_WIDTHE") = SCREEN_W;
  *(int *)so_symbol(&bc2_mod, "_ZN3krm3sal13SCREEN_HEIGHTE") = SCREEN_H;

  hook_arm(so_symbol(&bc2_mod, "_ZN3krm10krtNetInitEv"), (uintptr_t)&ret0);
  hook_arm(so_symbol(&bc2_mod, "_ZN3krm3krt3dbg15krtDebugMgrInitEPNS0_16CApplicationBaseE"), (uintptr_t)&ret0);

  hook_arm(so_symbol(&bc2_mod, "_ZNK3krm3krt2io5CPath6IsRootEv"), (uintptr_t)&krm__krt__io__CPath__IsRoot);

  hook_thumb(so_symbol(&bc2_mod, "Android_KarismaBridge_GetAppReadPath"), (uintptr_t)&Android_KarismaBridge_GetAppReadPath);
  hook_thumb(so_symbol(&bc2_mod, "Android_KarismaBridge_GetAppWritePath"), (uintptr_t)&Android_KarismaBridge_GetAppWritePath);

  hook_thumb(so_symbol(&bc2_mod, "Android_KarismaBridge_GetKeyboardOpened"), (uintptr_t)&ret0);

  hook_thumb(so_symbol(&bc2_mod, "Android_KarismaBridge_EnableSound"), (uintptr_t)&Android_KarismaBridge_EnableSound);
  hook_thumb(so_symbol(&bc2_mod, "Android_KarismaBridge_DisableSound"), (uintptr_t)&Android_KarismaBridge_DisableSound);
  hook_thumb(so_symbol(&bc2_mod, "Android_KarismaBridge_LockSound"), (uintptr_t)&ret0);
  hook_thumb(so_symbol(&bc2_mod, "Android_KarismaBridge_UnlockSound"), (uintptr_t)&ret0);
}

extern void *_ZdaPv;
extern void *_ZdlPv;
extern void *_Znaj;
extern void *_Znwj;

extern void *__aeabi_atexit;
extern void *__aeabi_d2f;
extern void *__aeabi_d2ulz;
extern void *__aeabi_dcmpgt;
extern void *__aeabi_dmul;
extern void *__aeabi_f2d;
extern void *__aeabi_f2iz;
extern void *__aeabi_f2ulz;
extern void *__aeabi_fadd;
extern void *__aeabi_fcmpge;
extern void *__aeabi_fcmpgt;
extern void *__aeabi_fcmple;
extern void *__aeabi_fcmplt;
extern void *__aeabi_fdiv;
extern void *__aeabi_fsub;
extern void *__aeabi_idiv;
extern void *__aeabi_idivmod;
extern void *__aeabi_l2d;
extern void *__aeabi_l2f;
extern void *__aeabi_ldivmod;
extern void *__aeabi_uidiv;
extern void *__aeabi_uidivmod;
extern void *__aeabi_uldivmod;
extern void *__cxa_guard_acquire;
extern void *__cxa_guard_release;
extern void *__cxa_pure_virtual;
extern void *__dso_handle;
extern void *__sF;
extern void *__stack_chk_fail;
extern void *__stack_chk_guard;

static int __stack_chk_guard_fake = 0x42424242;
static FILE __sF_fake[0x100][3];

struct tm *localtime_hook(time_t *timer) {
  struct tm *res = localtime(timer);
  if (res)
    return res;
  // Fix an uninitialized variable bug.
  time(timer);
  return localtime(timer);
}

static DynLibFunction dynlib_functions[] = {
  { "_ZdaPv", (uintptr_t)&_ZdaPv },
  { "_ZdlPv", (uintptr_t)&_ZdlPv },
  { "_Znaj", (uintptr_t)&_Znaj },
  { "_Znwj", (uintptr_t)&_Znwj },
  { "__aeabi_atexit", (uintptr_t)&__aeabi_atexit },
  { "__aeabi_d2f", (uintptr_t)&__aeabi_d2f },
  { "__aeabi_d2ulz", (uintptr_t)&__aeabi_d2ulz },
  { "__aeabi_dcmpgt", (uintptr_t)&__aeabi_dcmpgt },
  { "__aeabi_dmul", (uintptr_t)&__aeabi_dmul },
  { "__aeabi_f2d", (uintptr_t)&__aeabi_f2d },
  { "__aeabi_f2iz", (uintptr_t)&__aeabi_f2iz },
  { "__aeabi_f2ulz", (uintptr_t)&__aeabi_f2ulz },
  { "__aeabi_fadd", (uintptr_t)&__aeabi_fadd },
  { "__aeabi_fcmpge", (uintptr_t)&__aeabi_fcmpge },
  { "__aeabi_fcmpgt", (uintptr_t)&__aeabi_fcmpgt },
  { "__aeabi_fcmple", (uintptr_t)&__aeabi_fcmple },
  { "__aeabi_fcmplt", (uintptr_t)&__aeabi_fcmplt },
  { "__aeabi_fdiv", (uintptr_t)&__aeabi_fdiv },
  { "__aeabi_fsub", (uintptr_t)&__aeabi_fsub },
  { "__aeabi_idiv", (uintptr_t)&__aeabi_idiv },
  { "__aeabi_idivmod", (uintptr_t)&__aeabi_idivmod },
  { "__aeabi_l2d", (uintptr_t)&__aeabi_l2d },
  { "__aeabi_l2f", (uintptr_t)&__aeabi_l2f },
  { "__aeabi_ldivmod", (uintptr_t)&__aeabi_ldivmod },
  { "__aeabi_uidiv", (uintptr_t)&__aeabi_uidiv },
  { "__aeabi_uidivmod", (uintptr_t)&__aeabi_uidivmod },
  { "__aeabi_uldivmod", (uintptr_t)&__aeabi_uldivmod },
  { "__android_log_print", (uintptr_t)&__android_log_print },
  { "__cxa_guard_acquire", (uintptr_t)&__cxa_guard_acquire },
  { "__cxa_guard_release", (uintptr_t)&__cxa_guard_release },
  { "__cxa_pure_virtual", (uintptr_t)&__cxa_pure_virtual },
  { "__dso_handle", (uintptr_t)&__dso_handle },
  { "__errno", (uintptr_t)&__errno },
  { "__sF", (uintptr_t)&__sF_fake },
  { "__stack_chk_fail", (uintptr_t)&__stack_chk_fail },
  { "__stack_chk_guard", (uintptr_t)&__stack_chk_guard_fake },
  { "acos", (uintptr_t)&acos },
  { "asin", (uintptr_t)&asin },
  { "atan", (uintptr_t)&atan },
  { "atan2", (uintptr_t)&atan2 },
  { "atoi", (uintptr_t)&atoi },
  { "ceil", (uintptr_t)&ceil },
  { "close", (uintptr_t)&close },
  { "cos", (uintptr_t)&cos },
  { "difftime", (uintptr_t)&difftime },
  { "eglSwapBuffers", (uintptr_t)&eglSwapBuffers },
  { "fclose", (uintptr_t)&fclose },
  { "fflush", (uintptr_t)&fflush },
  { "fgets", (uintptr_t)&fgets },
  { "fileno", (uintptr_t)&fileno },
  { "floor", (uintptr_t)&floor },
  { "fmod", (uintptr_t)&fmod },
  { "fopen", (uintptr_t)&fopen },
  { "fprintf", (uintptr_t)&fprintf },
  { "fread", (uintptr_t)&fread },
  { "free", (uintptr_t)&free },
  { "fseek", (uintptr_t)&fseek },
  { "fstat", (uintptr_t)&fstat },
  { "ftell", (uintptr_t)&ftell },
  { "fwrite", (uintptr_t)&fwrite },
  { "getcwd", (uintptr_t)&getcwd },
  { "gettimeofday", (uintptr_t)&gettimeofday },
  { "glActiveTexture", (uintptr_t)&glActiveTexture },
  { "glAlphaFunc", (uintptr_t)&glAlphaFunc },
  { "glBindBuffer", (uintptr_t)&glBindBuffer },
  { "glBindFramebufferOES", (uintptr_t)&glBindFramebuffer },
  { "glBindTexture", (uintptr_t)&glBindTexture },
  { "glBlendFunc", (uintptr_t)&glBlendFunc },
  { "glClear", (uintptr_t)&glClear },
  { "glClearColor", (uintptr_t)&glClearColor },
  { "glClearDepthf", (uintptr_t)&glClearDepthf },
  { "glClearStencil", (uintptr_t)&glClearStencil },
  { "glClientActiveTexture", (uintptr_t)&glClientActiveTexture },
  { "glColorMask", (uintptr_t)&glColorMask },
  { "glColorPointer", (uintptr_t)&glColorPointer },
  { "glCompressedTexImage2D", (uintptr_t)&glCompressedTexImage2D },
  { "glCullFace", (uintptr_t)&glCullFace },
  { "glDeleteBuffers", (uintptr_t)&glDeleteBuffers },
  { "glDeleteTextures", (uintptr_t)&glDeleteTextures },
  { "glDepthFunc", (uintptr_t)&glDepthFunc },
  { "glDepthMask", (uintptr_t)&glDepthMask },
  { "glDepthRangef", (uintptr_t)&glDepthRangef },
  { "glDisable", (uintptr_t)&glDisable },
  { "glDisableClientState", (uintptr_t)&glDisableClientState },
  { "glDrawArrays", (uintptr_t)&glDrawArrays },
  { "glDrawElements", (uintptr_t)&glDrawElements },
  { "glEnable", (uintptr_t)&glEnable },
  { "glEnableClientState", (uintptr_t)&glEnableClientState },
  { "glFogf", (uintptr_t)&glFogf },
  { "glFogfv", (uintptr_t)&glFogfv },
  { "glFrontFace", (uintptr_t)&glFrontFace },
  { "glGenTextures", (uintptr_t)&glGenTextures },
  { "glGetError", (uintptr_t)&glGetError },
  { "glGetIntegerv", (uintptr_t)&glGetIntegerv },
  { "glGetString", (uintptr_t)&glGetString },
  { "glLoadIdentity", (uintptr_t)&glLoadIdentity },
  { "glLoadMatrixf", (uintptr_t)&glLoadMatrixf },
  { "glMatrixMode", (uintptr_t)&glMatrixMode },
  { "glNormalPointer", (uintptr_t)&ret0 },
  { "glReadPixels", (uintptr_t)&glReadPixels },
  { "glScissor", (uintptr_t)&glScissor },
  { "glStencilFunc", (uintptr_t)&glStencilFunc },
  { "glStencilOp", (uintptr_t)&glStencilOp },
  { "glTexCoordPointer", (uintptr_t)&glTexCoordPointer },
  { "glTexEnvf", (uintptr_t)&glTexEnvf },
  { "glTexEnvfv", (uintptr_t)&glTexEnvfv },
  { "glTexImage2D", (uintptr_t)&glTexImage2D },
  { "glTexParameteri", (uintptr_t)&glTexParameteri },
  { "glVertexPointer", (uintptr_t)&glVertexPointer },
  { "glViewport", (uintptr_t)&glViewport },
  { "ldexp", (uintptr_t)&ldexp },
  { "localtime", (uintptr_t)&localtime_hook },
  { "log", (uintptr_t)&log },
  { "lrand48", (uintptr_t)&lrand48 },
  { "malloc", (uintptr_t)&malloc },
  { "memcmp", (uintptr_t)&memcmp },
  { "memcpy", (uintptr_t)&memcpy },
  { "memmove", (uintptr_t)&memmove },
  { "memset", (uintptr_t)&memset },
  { "mkdir", (uintptr_t)&mkdir },
  { "pow", (uintptr_t)&pow },
  { "printf", (uintptr_t)&ret0 },
  { "read", (uintptr_t)&read },
  { "realloc", (uintptr_t)&realloc },
  { "rmdir", (uintptr_t)&rmdir },
  { "sin", (uintptr_t)&sin },
  { "snprintf", (uintptr_t)&snprintf },
  { "sqrt", (uintptr_t)&sqrt },
  { "sscanf", (uintptr_t)&sscanf },
  { "strchr", (uintptr_t)&strchr },
  { "strcmp", (uintptr_t)&strcmp },
  { "strerror", (uintptr_t)&strerror },
  { "strlen", (uintptr_t)&strlen },
  { "strncat", (uintptr_t)&strncat },
  { "strncmp", (uintptr_t)&strncmp },
  { "strncpy", (uintptr_t)&strncpy },
  { "strrchr", (uintptr_t)&strrchr },
  { "strstr", (uintptr_t)&strstr },
  { "strtoll", (uintptr_t)&strtoll },
  { "tan", (uintptr_t)&tan },
  { "time", (uintptr_t)&time },
  { "tolower", (uintptr_t)&tolower },
  { "toupper", (uintptr_t)&toupper },
  { "unlink", (uintptr_t)&unlink },
  { "vsnprintf", (uintptr_t)&vsnprintf },
  { "write", (uintptr_t)&write },
};

int check_kubridge(void) {
  int search_unk[2];
  return _vshKernelSearchModuleByName("kubridge", search_unk);
}

int file_exists(const char *path) {
  SceIoStat stat;
  return sceIoGetstat(path, &stat) >= 0;
}

int main(int argc, char *argv[]) {
  sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

  scePowerSetArmClockFrequency(444);
  scePowerSetBusClockFrequency(222);
  scePowerSetGpuClockFrequency(222);
  scePowerSetGpuXbarClockFrequency(166);

  if (check_kubridge() < 0)
    fatal_error("Error kubridge.skprx is not installed.");

  if (!file_exists("ur0:/data/libshacccg.suprx") && !file_exists("ur0:/data/external/libshacccg.suprx"))
    fatal_error("Error libshacccg.suprx is not installed.");

  if (so_load(&bc2_mod, SO_PATH) < 0)
    fatal_error("Error could not load %s.", SO_PATH);

  so_relocate(&bc2_mod);
  so_resolve(&bc2_mod, dynlib_functions, sizeof(dynlib_functions) / sizeof(DynLibFunction), 1);

  patch_game();
  so_flush_caches(&bc2_mod);

  so_initialize(&bc2_mod);

  SceUID thid = sceKernelCreateThread("main_thread", (SceKernelThreadEntry)main_thread, 0x40, 128 * 1024, 0, 0, NULL);
  sceKernelStartThread(thid, 0, NULL);
  return sceKernelExitDeleteThread(0);
}
