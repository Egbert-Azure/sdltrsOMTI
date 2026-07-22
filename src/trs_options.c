/*
 * Copyright (C) 2006-2011, Mark Grebe
 * Copyright (C) 2018-2026, Jens Guenther
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "error.h"
#include "trs.h"
#include "trs_cassette.h"
#include "trs_disk.h"
#include "trs_hard.h"
#include "trs_memory.h"
#include "trs_omti.h"
#include "trs_sdl_gui.h"
#include "trs_sdl_keyboard.h"
#include "trs_state_save.h"
#include "trs_stringy.h"
#include "trs_uart.h"
#include "trs_xebec.h"

#define MAX_SCALE   4
#define OPTIONS     (int)(sizeof(options) / sizeof(options[0]))

#define BLACK       0
#define GREEN       0x344843
#define WHITE       0xe0e0ff

#ifndef ROM_PATH
#define ROM_PATH    ""
#endif

/* Public data */
int aspect_ratio;
int border_width;
int foreground;
int background;
int gui_foreground;
int gui_background;
int trs_charset1;
int trs_charset3;
int trs_charset4;
int trs_paused;
int trs_emu_mouse;
int trs_show_led;
int fullscreen;
int grafyx_microlabs;
int lowe_le18;
int mousepointer = 1;
int resize3;
int resize4;
int scale;
int scanlines;
int scanshade;
int turbo_paste;
char scale_quality = '1';
char romfile1[FILENAME_MAX];
char romfile3[FILENAME_MAX];
char romfile4p[FILENAME_MAX];
char trs_disk_dir[FILENAME_MAX];
char trs_hard_dir[FILENAME_MAX];
char trs_cass_dir[FILENAME_MAX];
char trs_disk_set_dir[FILENAME_MAX];
char trs_state_dir[FILENAME_MAX];
char trs_printer_dir[FILENAME_MAX];
char trs_cmd_file[FILENAME_MAX];
char trs_config_file[FILENAME_MAX];
char trs_state_file[FILENAME_MAX];

/* Private data */
static int debugger;

static int disksizes[8];
#ifdef __linux__
static int disksteps[8];
#endif

static void opt_charset(const char *arg, int intarg, int *stringarg);
static void opt_clock(const char *arg, int intarg, int *stringarg);
static void opt_dirname(const char *arg, int intarg, int *stringarg);
static void opt_doubler(const char *arg, int intarg, int *stringarg);
#ifdef __linux__
static void opt_doublestep(const char *arg, int intarg, int *stringarg);
static void opt_stepmap(const char *arg, int intarg, int *stringarg);
#endif
static void opt_file(const char *arg, int intarg, int *stringarg);
static void opt_hexval(const char *arg, int intarg, int *variable);
static void opt_intval(const char *arg, int intarg, int *variable);
static void opt_joybuttonmap(const char *arg, int intarg, int *stringarg);
static void opt_memory(const char *arg, int intarg, int *variable);
static void opt_model(const char *arg, int intarg, int *stringarg);
static void opt_printer(const char *arg, int intarg, int *stringarg);
static void opt_scalequality(const char *arg, int intarg, int *stringarg);
static void opt_sizemap(const char *arg, int intarg, int *stringarg);
static void opt_speedup(const char *arg, int intarg, int *stringarg);
static void opt_value(const char *arg, int intarg, int *variable);
static void opt_window(const char *arg, int intarg, int *stringarg);

/* Option handling */
static const struct {
  const char *name;
  void (*handler)(const char *, int, int *);
  int const hasArg;
  int const intArg;
  void *strArg;
} options[] = {
  { "a",               opt_value,         0, 1, &aspect_ratio         },
  { "aspect",          opt_value,         0, 1, &aspect_ratio         },
  { "aspectratio",     opt_value,         0, 1, &aspect_ratio         },
  { "background",      opt_hexval,        1, 0, &background           },
  { "bg",              opt_hexval,        1, 0, &background           },
  { "borderwidth",     opt_intval,        1, 1, NULL                  },
  { "bw",              opt_intval,        1, 1, NULL                  },
  { "c",               opt_file,          1, 0, "c"                   },
  { "cas",             opt_file,          1, 0, "c"                   },
  { "cass",            opt_file,          1, 0, "c"                   },
  { "cassdir",         opt_dirname,       1, 0, trs_cass_dir          },
  { "cassette",        opt_file,          1, 0, "c"                   },
  { "cd",              opt_dirname,       1, 0, trs_cass_dir          },
  { "charset",         opt_charset,       1, 0, NULL                  },
  { "charset1",        opt_charset,       1, 1, NULL                  },
  { "charset3",        opt_charset,       1, 3, NULL                  },
  { "charset4",        opt_charset,       1, 4, NULL                  },
  { "cl",              opt_clock,         1, 0, NULL                  },
  { "cl1",             opt_clock,         1, 1, NULL                  },
  { "cl3",             opt_clock,         1, 3, NULL                  },
  { "cl4",             opt_clock,         1, 4, NULL                  },
  { "clock",           opt_clock,         1, 0, NULL                  },
  { "clock1",          opt_clock,         1, 1, NULL                  },
  { "clock3",          opt_clock,         1, 3, NULL                  },
  { "clock4",          opt_clock,         1, 4, NULL                  },
  { "cs",              opt_charset,       1, 0, NULL                  },
  { "cs1",             opt_charset,       1, 1, NULL                  },
  { "cs3",             opt_charset,       1, 3, NULL                  },
  { "cs4",             opt_charset,       1, 4, NULL                  },
#ifdef ZBX
  { "debug",           opt_value,         0, 1, &debugger             },
  { "di",              opt_hexval,        1, 0, &trs_disk_debug_flags },
  { "diskdebug",       opt_hexval,        1, 0, &trs_disk_debug_flags },
  { "io",              opt_hexval,        1, 0, &trs_io_debug_flags   },
  { "iodebug",         opt_hexval,        1, 0, &trs_io_debug_flags   },
  { "z",               opt_value,         0, 1, &debugger             },
  { "zbx",             opt_value,         0, 1, &debugger             },
#endif
  { "d",               opt_doubler,       1, 0, NULL                  },
  { "d0",              opt_file,          1, 0, "d"                   },
  { "d1",              opt_file,          1, 1, "d"                   },
  { "d2",              opt_file,          1, 2, "d"                   },
  { "d3",              opt_file,          1, 3, "d"                   },
  { "d4",              opt_file,          1, 4, "d"                   },
  { "d5",              opt_file,          1, 5, "d"                   },
  { "d6",              opt_file,          1, 6, "d"                   },
  { "d7",              opt_file,          1, 7, "d"                   },
  { "dam",             opt_value,         0, 1, &trs_disk_truedam     },
  { "dd",              opt_dirname,       1, 0, trs_disk_dir          },
  { "disk0",           opt_file,          1, 0, "d"                   },
  { "disk1",           opt_file,          1, 1, "d"                   },
  { "disk2",           opt_file,          1, 2, "d"                   },
  { "disk3",           opt_file,          1, 3, "d"                   },
  { "disk4",           opt_file,          1, 4, "d"                   },
  { "disk5",           opt_file,          1, 5, "d"                   },
  { "disk6",           opt_file,          1, 6, "d"                   },
  { "disk7",           opt_file,          1, 7, "d"                   },
  { "diskdir",         opt_dirname,       1, 0, trs_disk_dir          },
  { "diskset",         opt_file,          1, 0, "s"                   },
  { "disksetdir",      opt_dirname,       1, 0, trs_disk_set_dir      },
  { "doubler",         opt_doubler,       1, 0, NULL                  },
  { "ds",              opt_file,          1, 0, "s"                   },
  { "dsd",             opt_dirname,       1, 0, trs_disk_set_dir      },
#ifdef __linux__
  { "double",          opt_doublestep,    0, 2, NULL                  },
  { "doublestep",      opt_doublestep,    0, 2, NULL                  },
  { "nodouble",        opt_doublestep,    0, 1, NULL                  },
  { "nodoublestep",    opt_doublestep,    0, 1, NULL                  },
  { "step",            opt_stepmap,       1, 0, NULL                  },
  { "stepmap",         opt_stepmap,       1, 0, NULL                  },
#endif
  { "emtsafe",         opt_value,         0, 1, &trs_emtsafe          },
  { "esf",             opt_value,         0, 1, &stringy              },
  { "fdc",             opt_value,         0, 1, &trs_disk_controller  },
  { "floppy",          opt_value,         0, 1, &trs_disk_controller  },
  { "fg",              opt_hexval,        1, 0, &foreground           },
  { "foreground",      opt_hexval,        1, 0, &foreground           },
  { "fullscreen",      opt_value,         0, 1, &fullscreen           },
  { "fs",              opt_value,         0, 1, &fullscreen           },
  { "guibackground",   opt_hexval,        1, 0, &gui_background       },
  { "guibg",           opt_hexval,        1, 0, &gui_background       },
  { "guifg",           opt_hexval,        1, 0, &gui_foreground       },
  { "guiforeground",   opt_hexval,        1, 0, &gui_foreground       },
  { "halt",            opt_value,         1, 0, &Z80_HALT             },
  { "h0",              opt_file,          1, 0, "h"                   },
  { "h1",              opt_file,          1, 1, "h"                   },
  { "hard0",           opt_file,          1, 0, "h"                   },
  { "hard1",           opt_file,          1, 1, "h"                   },
  { "harddir",         opt_dirname,       1, 0, trs_hard_dir          },
  { "hd",              opt_dirname,       1, 0, trs_hard_dir          },
  { "hdboot",          opt_value,         0, 1, &trs_hd_boot          },
  { "hideled",         opt_value,         0, 0, &trs_show_led         },
  { "huff",            opt_memory,        0, 1, &huffman              },
  { "huffman",         opt_memory,        0, 1, &huffman              },
  { "hyper",           opt_memory,        0, 2, &hypermem             },
  { "hypermem",        opt_memory,        0, 2, &hypermem             },
  { "joyaxismapped",   opt_value,         0, 1, &jaxis_mapped         },
  { "joybuttonmap",    opt_joybuttonmap,  1, 0, NULL                  },
  { "joystick",        opt_intval,        1, 5, NULL                  },
  { "joysticknum",     opt_intval,        1, 5, NULL                  },
  { "js",              opt_intval,        1, 5, NULL                  },
  { "keypadjoy",       opt_value,         0, 1, &trs_keypad_joystick  },
  { "kp",              opt_value,         0, 1, &trs_keypad_joystick  },
  { "keystretch",      opt_intval,        1, 2, NULL                  },
  { "ks",              opt_intval,        1, 2, NULL                  },
  { "lc",              opt_value,         0, 1, &lowercase            },
  { "le18",            opt_value,         0, 1, &lowe_le18            },
  { "led",             opt_value,         0, 1, &trs_show_led         },
  { "lower",           opt_value,         0, 1, &lowercase            },
  { "lowercase",       opt_value,         0, 1, &lowercase            },
  { "lsb",             opt_memory,        0, 3, &lubomir              },
  { "lubomir",         opt_memory,        0, 3, &lubomir              },
  { "micro",           opt_value,         0, 1, &grafyx_microlabs     },
  { "microlabs",       opt_value,         0, 1, &grafyx_microlabs     },
  { "m1",              opt_value,         0, 1, &trs_model            },
  { "m3",              opt_value,         0, 3, &trs_model            },
  { "m4",              opt_value,         0, 4, &trs_model            },
  { "m4p",             opt_value,         0, 5, &trs_model            },
  { "mega",            opt_memory,        0, 4, &megamem              },
  { "megamem",         opt_memory,        0, 4, &megamem              },
  { "mem",             opt_intval,        1, 9, NULL                  },
  { "memsize",         opt_intval,        1, 9, NULL                  },
  { "m",               opt_model,         1, 0, NULL                  },
  { "model",           opt_model,         1, 0, NULL                  },
  { "mouse",           opt_value,         0, 1, &mousepointer         },
  { "mousepointer",    opt_value,         0, 1, &mousepointer         },
  { "mute",            opt_value,         0, 0, &trs_sound            },
  { "noa",             opt_value,         0, 0, &aspect_ratio         },
  { "noaspect",        opt_value,         0, 0, &aspect_ratio         },
  { "noaspectratio",   opt_value,         0, 0, &aspect_ratio         },
  { "nodam",           opt_value,         0, 0, &trs_disk_truedam     },
  { "noemtsafe",       opt_value,         0, 0, &trs_emtsafe          },
  { "noesf",           opt_value,         0, 0, &stringy              },
  { "nofdc",           opt_value,         0, 0, &trs_disk_controller  },
  { "nofloppy",        opt_value,         0, 0, &trs_disk_controller  },
  { "nofullscreen",    opt_value,         0, 0, &fullscreen           },
  { "nofs",            opt_value,         0, 0, &fullscreen           },
  { "nohdboot",        opt_value,         0, 0, &trs_hd_boot          },
  { "nohuff",          opt_value,         0, 0, &huffman              },
  { "nohuffman",       opt_value,         0, 0, &huffman              },
  { "nohyper",         opt_value,         0, 0, &hypermem             },
  { "nohypermem",      opt_value,         0, 0, &hypermem             },
  { "nojoyaxismapped", opt_value,         0, 0, &jaxis_mapped         },
  { "nokeypadjoy",     opt_value,         0, 0, &trs_keypad_joystick  },
  { "nokp",            opt_value,         0, 0, &trs_keypad_joystick  },
  { "nolc",            opt_value,         0, 0, &lowercase            },
  { "nole18",          opt_value,         0, 0, &lowe_le18            },
  { "noled",           opt_value,         0, 0, &trs_show_led         },
  { "nolower",         opt_value,         0, 0, &lowercase            },
  { "nolowercase",     opt_value,         0, 0, &lowercase            },
  { "nolsb",           opt_value,         0, 0, &lubomir              },
  { "nolubomir",       opt_value,         0, 0, &lubomir              },
  { "nomega",          opt_value,         0, 0, &megamem              },
  { "nomegamem",       opt_value,         0, 0, &megamem              },
  { "nomicro",         opt_value,         0, 0, &grafyx_microlabs     },
  { "nomicrolabs",     opt_value,         0, 0, &grafyx_microlabs     },
  { "nomouse",         opt_value,         0, 0, &mousepointer         },
  { "nomousepointer",  opt_value,         0, 0, &mousepointer         },
  { "noresize3",       opt_value,         0, 0, &resize3              },
  { "noresize4",       opt_value,         0, 0, &resize4              },
  { "nors3",           opt_value,         0, 0, &resize3              },
  { "nors4",           opt_value,         0, 0, &resize4              },
  { "nosafe",          opt_value,         0, 0, &trs_emtsafe          },
  { "nosb",            opt_value,         0, 0, &trs_kb_bracket_state },
  { "noscan",          opt_value,         0, 0, &scanlines            },
  { "noscanlines",     opt_value,         0, 0, &scanlines            },
  { "nosel",           opt_value,         0, 0, &selector             },
  { "noselector",      opt_value,         0, 0, &selector             },
  { "noshiftbracket",  opt_value,         0, 0, &trs_kb_bracket_state },
  { "nosound",         opt_value,         0, 0, &trs_sound            },
  { "nostringy",       opt_value,         0, 0, &stringy              },
  { "nosuper",         opt_value,         0, 0, &supermem             },
  { "nosupermem",      opt_value,         0, 0, &supermem             },
  { "not",             opt_value,         0, 0, &turbo_mode           },
  { "notp",            opt_value,         0, 0, &turbo_paste          },
  { "notruedam",       opt_value,         0, 0, &trs_disk_truedam     },
  { "noturbo",         opt_value,         0, 0, &turbo_mode           },
  { "noturbopaste",    opt_value,         0, 0, &turbo_paste          },
  { "noxmem",          opt_value,         0, 0, &xmem80               },
  { "noxmem80",        opt_value,         0, 0, &xmem80               },
  { "o0",              opt_file,          1, 0, "o"                   },
  { "omti0",           opt_file,          1, 0, "o"                   },
  { "pause",           opt_value,         0, 1, &trs_paused           },
  { "p",               opt_printer,       1, 0, NULL                  },
  { "pd",              opt_dirname,       1, 0, trs_printer_dir       },
  { "printer",         opt_printer,       1, 0, NULL                  },
  { "printerdir",      opt_dirname,       1, 0, trs_printer_dir       },
  { "r",               opt_file,          1, 0, "r"                   },
  { "r1",              opt_file,          1, 0, romfile1              },
  { "r3",              opt_file,          1, 0, romfile3              },
  { "r4p",             opt_file,          1, 0, romfile4p             },
  { "ram",             opt_intval,        1, 9, NULL                  },
  { "ramsize",         opt_intval,        1, 9, NULL                  },
  { "resize3",         opt_value,         0, 1, &resize3              },
  { "resize4",         opt_value,         0, 1, &resize4              },
  { "rom",             opt_file,          1, 0, "r"                   },
  { "rom1",            opt_file,          1, 0, romfile1              },
  { "rom3",            opt_file,          1, 0, romfile3              },
  { "rom4p",           opt_file,          1, 0, romfile4p             },
  { "romfile",         opt_file,          1, 0, "r"                   },
  { "romfile1",        opt_file,          1, 0, romfile1              },
  { "romfile3",        opt_file,          1, 0, romfile3              },
  { "romfile4p",       opt_file,          1, 0, romfile4p             },
  { "rs3",             opt_value,         0, 1, &resize3              },
  { "rs4",             opt_value,         0, 1, &resize4              },
  { "s",               opt_speedup,       1, 0, NULL                  },
  { "safe",            opt_value,         0, 1, &trs_emtsafe          },
  { "samplerate",      opt_intval,        1, 3, NULL                  },
  { "sb",              opt_value,         0, 1, &trs_kb_bracket_state },
  { "sc",              opt_intval,        1, 4, NULL                  },
  { "scale",           opt_intval,        1, 4, NULL                  },
  { "scalequality",    opt_scalequality,  1, 0, NULL                  },
  { "scan",            opt_value,         0, 1, &scanlines            },
  { "scanlines",       opt_value,         0, 1, &scanlines            },
  { "scanshade",       opt_intval,        1, 0, &scanshade            },
  { "sd",              opt_dirname,       1, 0, trs_state_dir         },
  { "sel",             opt_memory,        0, 5, &selector             },
  { "selector",        opt_memory,        0, 5, &selector             },
  { "serial",          opt_file,          1, 0, trs_uart_name         },
  { "shade",           opt_intval,        1, 0, &scanshade            },
  { "shiftbracket",    opt_value,         0, 1, &trs_kb_bracket_state },
  { "showled",         opt_value,         0, 1, &trs_show_led         },
  { "size",            opt_sizemap,       1, 0, NULL                  },
  { "sizemap",         opt_sizemap,       1, 0, NULL                  },
  { "sound",           opt_value,         0, 1, &trs_sound            },
  { "speedup",         opt_speedup,       1, 0, NULL                  },
  { "sq",              opt_scalequality,  1, 0, NULL                  },
  { "sr",              opt_intval,        1, 3, NULL                  },
  { "statedir",        opt_dirname,       1, 0, trs_state_dir         },
  { "stringy",         opt_value,         0, 1, &stringy              },
  { "super",           opt_intval,        1, 8, NULL                  },
  { "supermem",        opt_intval,        1, 8, NULL                  },
  { "sw",              opt_hexval,        1, 0, &trs_uart_switches    },
  { "switches",        opt_hexval,        1, 0, &trs_uart_switches    },
  { "t",               opt_value,         0, 1, &turbo_mode           },
  { "tp",              opt_value,         0, 1, &turbo_paste          },
  { "tr",              opt_intval,        1, 6, NULL                  },
  { "truedam",         opt_value,         0, 1, &trs_disk_truedam     },
  { "turbo",           opt_value,         0, 1, &turbo_mode           },
  { "turbopaste",      opt_value,         0, 1, &turbo_paste          },
  { "turborate",       opt_intval,        1, 6, NULL                  },
  { "w0",              opt_file,          1, 0, "w"                   },
  { "w1",              opt_file,          1, 1, "w"                   },
  { "w2",              opt_file,          1, 2, "w"                   },
  { "w3",              opt_file,          1, 3, "w"                   },
  { "w4",              opt_file,          1, 4, "w"                   },
  { "w5",              opt_file,          1, 5, "w"                   },
  { "w6",              opt_file,          1, 6, "w"                   },
  { "w7",              opt_file,          1, 7, "w"                   },
  { "wafer0",          opt_file,          1, 0, "w"                   },
  { "wafer1",          opt_file,          1, 1, "w"                   },
  { "wafer2",          opt_file,          1, 2, "w"                   },
  { "wafer3",          opt_file,          1, 3, "w"                   },
  { "wafer4",          opt_file,          1, 4, "w"                   },
  { "wafer5",          opt_file,          1, 5, "w"                   },
  { "wafer6",          opt_file,          1, 6, "w"                   },
  { "wafer7",          opt_file,          1, 7, "w"                   },
  { "window",          opt_window,        1, 0, NULL                  },
  { "x0",              opt_file,          1, 0, "x"                   },
  { "xebec0",          opt_file,          1, 0, "x"                   },
  { "xmem",            opt_memory,        0, 7, &xmem80               },
  { "xmem80",          opt_memory,        0, 7, &xmem80               },
  { "y",               opt_intval,        1, 7, NULL                  },
  { "year",            opt_intval,        1, 7, NULL                  },
};

static void strip(char *inputStr)
{
  char *pos = inputStr;

  while (*pos && *pos <= ' ')
    pos++;

  memmove(inputStr, pos, strlen(pos) + 1);
  pos = inputStr + strlen(inputStr) - 1;

  while (*pos && *pos <= ' ')
    pos--;

  *(pos + 1) = '\0';
}

static const char *charset_name(int charset)
{
  switch (charset) {
    case 0:
      return "early";
    case 1:
      return "stock";
    case 2:
      return "lcmod";
    case 3:
    default:
      return "wider";
    case 4:
    case 7:
      return "katakana";
    case 5:
    case 8:
      return "international";
    case 6:
    case 9:
      return "bold";
    case 10:
      return "genie";
    case 11:
      return "ht-1080z";
    case 12:
      return "meritum";
    case 13:
      return "ct80";
    case 14:
      return "videogenie";
  }
}

static const char *option(int trs_option)
{
  return trs_option ? "" : "no";
}

static void opt_charset(const char *arg, int intarg, int *stringarg)
{
  if (intarg == 0)
    intarg = trs_model;

  if (intarg == 1) {
    switch (TOLOWER(*arg)) {
      case 'e': /* early */
        trs_charset1 = 0;
        break;
      case 's': /* stock */
        trs_charset1 = 1;
        break;
      case 'l': /* lcmod */
        trs_charset1 = 2;
        break;
      case 'w': /* wider */
        trs_charset1 = 3;
        break;
      case 'g': /* genie or german */
        trs_charset1 = 10;
        break;
      case 'h': /* ht-1080z */
        trs_charset1 = 11;
        break;
      case 'm': /* meritum (uppercase only) */
        trs_charset1 = 12;
        lowercase = 0;
        break;
      case 'c': /* ct-80 */
        trs_charset1 = 13;
        break;
      case 'v': /* video genie */
        trs_charset1 = 14;
        break;
      default:
        goto fail;
    }
  } else {
    int charset;

    switch (TOLOWER(*arg)) {
      case 'k': /* katakana */
        charset = 4;
        break;
      case 'i': /* international */
        charset = 5;
        break;
      case 'b': /* bold */
        charset = 6;
        break;
      default:
        goto fail;
    }

    if (intarg == 3)
      trs_charset3 = charset;
    else
      trs_charset4 = charset + 3;
  }

  return;

fail:
  error("unknown charset%d: '%s'", intarg, arg);
}

static void opt_clock(const char *arg, int intarg, int *stringarg)
{
  float const clock_mhz = atof(arg);

  if (clock_mhz >= 0.1 && clock_mhz <= 99.0) {
    switch (intarg ? intarg : trs_model) {
      case 1:
        clock_mhz_1 = clock_mhz;
        break;
      case 3:
        clock_mhz_3 = clock_mhz;
        break;
      default:
        clock_mhz_4 = clock_mhz;
    }
  }
}

static void opt_dirname(const char *arg, int intarg, int *stringarg)
{
  struct stat st = { 0 };

  if (arg[strlen(arg) - 1] == DIR_SLASH)
    snprintf((char *)stringarg, FILENAME_MAX, "%s", arg);
  else
    snprintf((char *)stringarg, FILENAME_MAX, "%s%c", arg, DIR_SLASH);

  if (stat((char *)stringarg, &st) < 0) {
    snprintf((char *)stringarg, FILENAME_MAX, ".%c", DIR_SLASH);
    file_error("configuration: '%s'", arg);
  }
}

static void opt_doubler(const char *arg, int intarg, int *stringarg)
{
  switch (TOLOWER(*arg)) {
    case 'p':
      trs_disk_doubler = TRSDISK_PERCOM;
      break;
    case 'r':
    case 't':
      trs_disk_doubler = TRSDISK_TANDY;
      break;
    case 'b':
    default:
      trs_disk_doubler = TRSDISK_BOTH;
      break;
    case 'n':
      trs_disk_doubler = TRSDISK_NODOUBLER;
      break;
    }
}

#ifdef __linux__
static void opt_doublestep(const char *arg, int intarg, int *stringarg)
{
  int i;

  for (i = 0; i < 8; i++)
    disksteps[i] = intarg;
}

static void opt_stepmap(const char *arg, int intarg, int *stringarg)
{
  sscanf(arg, "%d,%d,%d,%d,%d,%d,%d,%d",
         &disksteps[0], &disksteps[1], &disksteps[2], &disksteps[3],
         &disksteps[4], &disksteps[5], &disksteps[6], &disksteps[7]);
}
#endif

static void opt_file(const char *arg, int intarg, int *stringarg)
{
  switch ((char)*stringarg) {
    case 'c':
      if (arg[0])
        trs_cassette_insert(arg);
      else
        trs_cassette_remove();
      break;
    case 'd':
      if (arg[0])
        trs_disk_insert(intarg, arg);
      else
        trs_disk_remove(intarg);
      break;
    case 'h':
      if (arg[0])
        trs_hard_attach(intarg, arg);
      else
        trs_hard_remove(intarg);
      break;
    case 'o':
      if (arg[0])
        trs_omti_attach(intarg, arg);
      else
        trs_omti_remove(intarg);
      break;
    case 'x':
      if (arg[0])
        trs_xebec_attach(intarg, arg);
      else
        trs_xebec_remove(intarg);
      break;
    case 'w':
      if (arg[0])
        stringy_insert(intarg, arg);
      else
        stringy_remove(intarg);
      break;
    case 's':
      if (arg[0])
        trs_diskset_load(arg);
      break;
    case 'r':
      switch (trs_model) {
        case 1:
          snprintf(romfile1, FILENAME_MAX, "%s", arg);
          break;
        case 3:
        case 4:
          snprintf(romfile3, FILENAME_MAX, "%s", arg);
          break;
        default:
          snprintf(romfile4p, FILENAME_MAX, "%s", arg);
      }
      break;
    default:
      snprintf((char *)stringarg, FILENAME_MAX, "%s", arg);
  }
}

static void opt_hexval(const char *arg, int intarg, int *variable)
{
  *variable = strtol(arg, NULL, 16);
}

static void opt_intval(const char *arg, int intarg, int *variable)
{
  int const value = atoi(arg);

  switch (intarg) {
    case 0:
      *variable = value & 0xFF;
      break;
    case 1: /* borderwidth */
      if (value >= 0 && value <= 50)
        border_width = value;
      break;
    case 2: /* keystretch */
      if (value >= 0)
        stretch_amount = value;
      break;
    case 3: /* samplerate */
      if (value >= 0 && value < MAX_SAMPLE_RATE)
        cassette_default_sample_rate = value;
      break;
    case 4: /* scale */
      if (value >= 1 && value <= MAX_SCALE)
        scale = value;
      break;
    case 5: /* joystick */
      if (TOLOWER(*arg) == 'n') /* none */
        trs_joystick = -1;
      else
        trs_joystick = value;
      break;
    case 6: /* turborate */
      if (value <= 0) {
        turbo_mode = 0;
        turbo_rate = 5;
      } else {
        turbo_rate = value;
      }
      break;
    case 7: /* year */
      if (value >= 0 && value <= 9999)
        trs_year = value;
      break;
    case 8: /* supermem */
      if (value >= 256 && value <= 1024) {
        supermem = (value / 256) << 3;
         huffman = hypermem = lubomir = selector = xmem80 = 0;
      }
      break;
    case 9: /* memsize */
      if (value >= 4 && value <= 48)
        trs_mem_size = (value << 10) + RAM_START;
      break;
  }
}

static void opt_joybuttonmap(const char *arg, int intarg, int *stringarg)
{
  int i;

  for (i = 0; i < JOY_BUTTONS; i++) {
    const char *ptr = strchr(arg, ',');

    if (sscanf(arg, "%d", &jbutton_map[i]) == 0)
      jbutton_map[i] = -1;

    if (ptr != NULL)
      arg = ptr + 1;
  }
}

static void opt_memory(const char *arg, int intarg, int *variable)
{
  *variable = 1;

  switch (intarg) {
    case HUFFMAN:
      megamem  = 0;
      hypermem = 0;
      supermem = 0;
      break;
    case HYPERMEM:
      huffman  = 0;
      supermem = 0;
      break;
    case LUBOMIR:
      selector = 0;
      supermem = 0;
      xmem80   = 0;
      break;
    case MEGAMEM:
      huffman  = 0;
      break;
    case SELECTOR:
      lubomir  = 0;
      supermem = 0;
      xmem80   = 0;
      break;
    case XMEM80:
      lubomir  = 0;
      selector = 0;
      supermem = 0;
      break;
  }
}

static void opt_model(const char *arg, int intarg, int *stringarg)
{
  if (strcmp(arg, "1") == 0 || strcasecmp(arg, "I") == 0)
    trs_model = 1;
  else if (strcmp(arg, "3") == 0 || strcasecmp(arg, "III") == 0)
    trs_model = 3;
  else if (strcmp(arg, "4") == 0 || strcasecmp(arg, "IV") == 0)
    trs_model = 4;
  else if (strcasecmp(arg, "4P") == 0 || strcasecmp(arg, "IVp") == 0)
    trs_model = 5;
  else
    error("TRS-80 Model '%s' not supported", arg);
}

static void opt_printer(const char *arg, int intarg, int *stringarg)
{
  switch (TOLOWER(*arg)) {
    case 'n': /* none */
    case '0':
      trs_printer = 0;
      break;
    case 'f': /* file */
    case '1':
      trs_printer = 1;
      break;
    case 't': /* text */
    case '2':
      trs_printer = 2;
      break;
    default:
      error("unknown printer type: '%s'", arg);
  }
}

static void opt_scalequality(const char *arg, int intarg, int *stringarg)
{
  switch (*arg) {
    case '0':
    case '1':
    case '2':
      scale_quality = *arg;
      break;
    default:
      error("unknown render scale quality: %s", arg);
  }
}

static void opt_sizemap(const char *arg, int intarg, int *stringarg)
{
  sscanf(arg, "%d,%d,%d,%d,%d,%d,%d,%d",
         &disksizes[0], &disksizes[1], &disksizes[2], &disksizes[3],
         &disksizes[4], &disksizes[5], &disksizes[6], &disksizes[7]);
}

static void opt_speedup(const char *arg, int intarg, int *stringarg)
{
  switch (TOLOWER(*arg)) {
    case 'n': /* None */
      speedup = 0;
      break;
    case 'a': /* Archbold */
      speedup = 1;
      break;
    case 'h': /* Holmes */
      speedup = 2;
      break;
    case 's': /* Seatronics */
      speedup = 3;
      break;
    case 'b': /* Banking */
      speedup = 4;
      break;
    case 'c': /* Aster CT-80 */
      speedup = 5;
      break;
    case 'l': /* LNW80 */
      speedup = 6;
      break;
    case 't': /* TCS SpeedMaster */
      speedup = 7;
      break;
    default:
      error("unknown speedup kit: '%s'", arg);
  }
}

static void opt_value(const char *arg, int intarg, int *variable)
{
  if (arg == NULL)
    *variable = intarg;
  else
    *variable = TOLOWER(*arg);
}

static void opt_window(const char *arg, int intarg, int *stringag)
{
  int x, y, w, h;

  sscanf(arg, "%d,%d,%d,%d", &x, &y, &w, &h);
  trs_set_window(x, y, w, h);
}


static void trs_disk_setparam(void)
{
  int i;

  for (i = 0; i < 8; i++) {
    if (disksizes[i] == 5 || disksizes[i] == 8)
      trs_disk_setsize(i, disksizes[i]);
    else
      error("bad value %d for disk %d size", disksizes[i], i);

#ifdef __linux__
    /* Disk Steps are 1 for Single Step or 2 for Double Step */
    if (disksteps[i] == 1 || disksteps[i] == 2)
      trs_disk_setstep(i, disksteps[i]);
    else
      error("bad value %d for disk %d single/double step", disksteps[i], i);
#endif
  }
}

int trs_load_config_file(void)
{
  char line[FILENAME_MAX];
  FILE *config_file;
  int i;

  for (i = 0; i < 8; i++)
    trs_disk_remove(i);

  for (i = 0; i < TRS_HARD_MAXDRIVES; i++)
    trs_hard_remove(i);

  for (i = 0; i < TRS_OMTI_MAXDRIVES; i++)
    trs_omti_remove(i);

  for (i = 0; i < TRS_XEBEC_MAXDRIVES; i++)
    trs_xebec_remove(i);

  for (i = 0; i < 8; i++)
    stringy_remove(i);

  trs_cassette_remove();

  aspect_ratio = 1;
  background = BLACK;
  border_width = 2;
  cassette_default_sample_rate = MAX_SAMPLE_RATE;
  /* Disk Sizes are 5" or 8"
     Corrected by Larry Kraemer 08-01-2011 */
  disksizes[0] = 5;
  disksizes[1] = 5;
  disksizes[2] = 5;
  disksizes[3] = 5;
  disksizes[4] = 8;
  disksizes[5] = 8;
  disksizes[6] = 8;
  disksizes[7] = 8;
#ifdef __linux__
  /* Disk Steps are 1 for Single Step, 2 for Double Step
     Corrected by Larry Kraemer 08-01-2011 */
  disksteps[0] = 1;
  disksteps[1] = 1;
  disksteps[2] = 1;
  disksteps[3] = 1;
  disksteps[4] = 1;
  disksteps[5] = 1;
  disksteps[6] = 1;
  disksteps[7] = 1;
#endif
  foreground = WHITE;
  fullscreen = 0;
  grafyx_microlabs = 0;
  gui_background = GREEN;
  gui_foreground = WHITE;
  huffman = 0;
  hypermem = 0;
  lowe_le18 = 0;
  lowercase = 1;
  lubomir = 0;
  megamem = 0;
  mousepointer = 1;
  resize3 = 1;
  resize4 = 0;
  scale = 1;
  scanlines = 0;
  scanshade = 127;
  selector = 0;
  snprintf(romfile1, FILENAME_MAX, "%slevel2.rom", ROM_PATH);
  snprintf(romfile3, FILENAME_MAX, "%smodel3.rom", ROM_PATH);
  snprintf(romfile4p, FILENAME_MAX, "%smodel4p.rom", ROM_PATH);
  stringy = 0;
  speedup = 1;
  snprintf(trs_cass_dir, 3, "%c%c", '.', DIR_SLASH);
  snprintf(trs_disk_dir, 3, "%c%c", '.', DIR_SLASH);
  snprintf(trs_disk_set_dir, 3, "%c%c", '.', DIR_SLASH);
  snprintf(trs_hard_dir, 3, "%c%c", '.', DIR_SLASH);
  snprintf(trs_printer_dir, 3, "%c%c", '.', DIR_SLASH);
  snprintf(trs_state_dir, 3, "%c%c", '.', DIR_SLASH);
  stretch_amount = STRETCH_AMOUNT;
  supermem = 0;
  turbo_mode = 0;
  turbo_rate = 5;
  trs_charset1 = 3;
  trs_charset3 = 4;
  trs_charset4 = 8;
  trs_disk_controller = 1;
  trs_disk_doubler = TRSDISK_BOTH;
  trs_disk_truedam = 0;
  trs_emtsafe = 1;
  trs_hd_boot = 0;
  trs_joystick = 0;
  trs_kb_bracket_state = 0;
  trs_keypad_joystick = 1;
  trs_mem_size = Z80_ADDRESS_LIMIT;
  trs_model = 1;
  trs_printer = 0;
  trs_show_led = 1;
  trs_sound = 1;
  trs_uart_switches = 0x7 | TRS_UART_NOPAR | TRS_UART_WORD8;
  trs_year = 0;
  turbo_paste = 0;
  xmem80 = 0;
  Z80_HALT = 0;

  if (trs_config_file[0] == 0) {
    const char *home = getenv("HOME");

    if (home)
      snprintf(trs_config_file, FILENAME_MAX, "%s/.sdltrs.t8c", home);
    else
      snprintf(trs_config_file, FILENAME_MAX, "./sdltrs.t8c");

    if ((config_file = fopen(trs_config_file, "r")) == NULL) {
      debug("create default configuration: '%s'\n", trs_config_file);
      trs_disk_setparam(); /* Set default disk parameters */
      trs_write_config_file(trs_config_file);
      return -1;
    }
  } else {
    if ((config_file = fopen(trs_config_file, "r")) == NULL) {
      file_error("read configuration: '%s'", trs_config_file);
      return -1;
    }
  }

  while (fgets(line, sizeof(line), config_file)) {
    char *arg = strchr(line, '=');

    if (arg != NULL) {
      *arg++ = '\0';
      strip(arg);
    }

    strip(line);

    for (i = 0; i < OPTIONS; i++) {
      if (strcasecmp(line, options[i].name) == 0) {
        if (options[i].hasArg) {
          if (arg)
            (*options[i].handler)(arg, options[i].intArg, options[i].strArg);
        } else
          (*options[i].handler)(NULL, options[i].intArg, options[i].strArg);
        break;
      }
    }
  }

  fclose(config_file);

  trs_disk_setparam();
  return 0;
}

int trs_parse_command_line(int argc, char **argv)
{
  int i, j;

  /* Check for config or state files and CMD file on the command line */
  trs_config_file[0] = 0;
  trs_state_file[0] = 0;
  trs_cmd_file[0] = 0;

  for (i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      for (j = 0; j < OPTIONS; j++) {
        if (strcasecmp(&argv[i][1], options[j].name) == 0) {
          if (options[j].hasArg)
            i++;
          break;
        }
      }
    } else {
      int const len = strlen(argv[i]) - 4;

      if (len > 0) {
        if (strcasecmp(&argv[i][len], ".t8c") == 0)
          snprintf(trs_config_file, FILENAME_MAX, "%s", argv[i]);
        else if (strcasecmp(&argv[i][len], ".t8s") == 0)
          snprintf(trs_state_file, FILENAME_MAX, "%s", argv[i]);
        else if (strcasecmp(&argv[i][len], ".cmd") == 0)
          snprintf(trs_cmd_file, FILENAME_MAX, "%s", argv[i]);
      }
    }
  }

  trs_load_config_file();

  for (i = 1; i < argc; i++) {
    int const argAvail = ((i + 1) < argc); /* is argument available? */

    for (j = 0; j < OPTIONS; j++) {
      if (argv[i][0] == '-') {
        if (strcasecmp(&argv[i][1], options[j].name) == 0) {
          if (options[j].hasArg) {
            if (argAvail)
              (*options[j].handler)(argv[++i], options[j].intArg, options[j].strArg);
          } else
            (*options[j].handler)(NULL, options[j].intArg, options[j].strArg);
          break;
        }
      }
    }

    if (j == OPTIONS && argv[i][0] == '-')
      error("unrecognized option: '%s'", argv[i]);
  }

  trs_disk_setparam();
  return debugger;
}

int trs_write_config_file(const char *filename)
{
  int i;
  int x, y, w, h;
  FILE *config_file = fopen(filename, "w");

  if (config_file == NULL) {
    file_error("write configuration: '%s'", filename);
    return -1;
  }

  fprintf(config_file, "%saspectratio\n", option(aspect_ratio));
  fprintf(config_file, "background\t= 0x%06X\n", background);
  fprintf(config_file, "borderwidth\t= %d\n", border_width);
  fprintf(config_file, "cassdir\t\t= %s\n", trs_cass_dir);
  fprintf(config_file, "cassette\t= %s\n", trs_cassette_getfilename());
  fprintf(config_file, "charset1\t= %s\n", charset_name(trs_charset1));
  fprintf(config_file, "charset3\t= %s\n", charset_name(trs_charset3));
  fprintf(config_file, "charset4\t= %s\n", charset_name(trs_charset4));
  fprintf(config_file, "clock1\t\t= %.2f\n", clock_mhz_1);
  fprintf(config_file, "clock3\t\t= %.2f\n", clock_mhz_3);
  fprintf(config_file, "clock4\t\t= %.2f\n", clock_mhz_4);

  for (i = 0; i < 8; i++)
    fprintf(config_file, "disk%d\t\t= %s\n", i, trs_disk_getfilename(i));

  fprintf(config_file, "diskdir\t\t= %s\n", trs_disk_dir);
  fprintf(config_file, "disksetdir\t= %s\n", trs_disk_set_dir);

  fprintf(config_file, "doubler\t\t= %s\n",
      trs_disk_doubler == TRSDISK_PERCOM ? "percom" :
      trs_disk_doubler == TRSDISK_TANDY  ? "tandy"  :
      trs_disk_doubler == TRSDISK_BOTH   ? "both"   : "none");

  fprintf(config_file, "%semtsafe\n", option(trs_emtsafe));
  fprintf(config_file, "%sfdc\n", option(trs_disk_controller));
  fprintf(config_file, "%sfullscreen\n", option(fullscreen));
  fprintf(config_file, "foreground\t= 0x%06X\n", foreground);
  fprintf(config_file, "guibackground\t= 0x%06X\n", gui_background);
  fprintf(config_file, "guiforeground\t= 0x%06X\n", gui_foreground);

  fprintf(config_file, "halt\t\t= %s\n",
      Z80_HALT == 'd' ? "debug" :
      Z80_HALT == 'h' ? "halt"  :
      Z80_HALT == 'r' ? "reset" : "");

  for (i = 0; i < TRS_HARD_MAXDRIVES; i++)
    fprintf(config_file, "hard%d\t\t= %s\n", i, trs_hard_getfilename(i));

  fprintf(config_file, "harddir\t\t= %s\n", trs_hard_dir);
  fprintf(config_file, "%shdboot\n", option(trs_hd_boot));

  for (i = 0; i < TRS_OMTI_MAXDRIVES; i++)
    fprintf(config_file, "omti%d\t\t= %s\n", i, trs_omti_getfilename(i));

  fprintf(config_file, "%shuffman\n", option(huffman));
  fprintf(config_file, "%shypermem\n", option(hypermem));
  fprintf(config_file, "%sjoyaxismapped\n", option(jaxis_mapped));

  fprintf(config_file, "joybuttonmap\t= ");
  for (i = 0; i < JOY_BUTTONS; i++)
    fprintf(config_file, i < JOY_BUTTONS - 1 ? "%d," : "%d\n", jbutton_map[i]);

  fprintf(config_file, "joystick\t= ");
  if (trs_joystick == -1)
    fprintf(config_file, "none\n");
  else
    fprintf(config_file, "%d\n", trs_joystick);

  fprintf(config_file, "%skeypadjoy\n", option(trs_keypad_joystick));
  fprintf(config_file, "keystretch\t= %d\n", stretch_amount);
  fprintf(config_file, "%sle18\n", option(lowe_le18));
  fprintf(config_file, "%sled\n", option(trs_show_led));
  fprintf(config_file, "%slowercase\n", option(lowercase));
  fprintf(config_file, "%slubomir\n", option(lubomir));
  fprintf(config_file, "%smegamem\n", option(megamem));
  fprintf(config_file, "memsize\t\t= %d\n", (trs_mem_size - RAM_START) >> 10);
  fprintf(config_file, "%smicrolabs\n", option(grafyx_microlabs));

  fprintf(config_file, "model\t\t= %d%s\n",
      trs_model == 5 ? 4 : trs_model, trs_model == 5 ? "P" : "");

  fprintf(config_file, "%smousepointer\n", option(mousepointer));

  fprintf(config_file, "printer\t\t= %s\n",
      trs_printer == 1 ? "file" :
      trs_printer == 2 ? "text" : "none");

  fprintf(config_file, "printerdir\t= %s\n", trs_printer_dir);
  fprintf(config_file, "%sresize3\n", option(resize3));
  fprintf(config_file, "%sresize4\n", option(resize4));
  fprintf(config_file, "romfile1\t= %s\n", romfile1);
  fprintf(config_file, "romfile3\t= %s\n", romfile3);
  fprintf(config_file, "romfile4p\t= %s\n", romfile4p);
  fprintf(config_file, "samplerate\t= %d\n", cassette_default_sample_rate);
  fprintf(config_file, "scale\t\t= %d\n", scale);
  fprintf(config_file, "scalequality\t= %c\n", scale_quality);
  fprintf(config_file, "%sscanlines\n", option(scanlines));
  fprintf(config_file, "scanshade\t= %d\n", scanshade);
  fprintf(config_file, "%sselector\n", option(selector));
  fprintf(config_file, "serial\t\t= %s\n", trs_uart_name);
  fprintf(config_file, "%sshiftbracket\n", option(trs_kb_bracket_state));

  fprintf(config_file, "sizemap\t\t= %d,%d,%d,%d,%d,%d,%d,%d\n",
      trs_disk_getsize(0), trs_disk_getsize(1), trs_disk_getsize(2), trs_disk_getsize(3),
      trs_disk_getsize(4), trs_disk_getsize(5), trs_disk_getsize(6), trs_disk_getsize(7));

  fprintf(config_file, "%ssound\n", option(trs_sound));

  fprintf(config_file, "speedup\t\t= %s\n",
      speedup == 1 ? "archbold"   :
      speedup == 2 ? "holmes"     :
      speedup == 3 ? "seatronics" :
      speedup == 4 ? "banking"    :
      speedup == 5 ? "ct80"       :
      speedup == 6 ? "lnw80"      :
      speedup == 7 ? "tcs-models" : "none");

  fprintf(config_file, "statedir\t= %s\n", trs_state_dir);
#ifdef __linux__
  /* Corrected to trs_disk_getstep vs getsize by Larry Kraemer 08-01-2011 */
  fprintf(config_file, "stepmap\t\t= %d,%d,%d,%d,%d,%d,%d,%d\n",
      trs_disk_getstep(0), trs_disk_getstep(1), trs_disk_getstep(2), trs_disk_getstep(3),
      trs_disk_getstep(4), trs_disk_getstep(5), trs_disk_getstep(6), trs_disk_getstep(7));
#endif
  fprintf(config_file, "%sstringy\n", option(stringy));
  fprintf(config_file, "supermem\t= %d\n", supermem << 5);
  fprintf(config_file, "switches\t= 0x%02X\n", trs_uart_switches);
  fprintf(config_file, "%struedam\n", option(trs_disk_truedam));
  fprintf(config_file, "%sturbo\n", option(turbo_mode));
  fprintf(config_file, "%sturbopaste\n", option(turbo_paste));
  fprintf(config_file, "turborate\t= %d\n", turbo_rate);

  for (i = 0; i < 8; i++)
    fprintf(config_file, "wafer%d\t\t= %s\n", i, stringy_get_name(i));

  trs_get_window(&x, &y, &w, &h);
  fprintf(config_file, "window\t\t= %d,%d,%d,%d\n", x, y, w, h);

  for (i = 0; i < TRS_XEBEC_MAXDRIVES; i++)
    fprintf(config_file, "xebec%d\t\t= %s\n", i, trs_xebec_getfilename(i));

  fprintf(config_file, "%sxmem80\n", option(xmem80));
  fprintf(config_file, "year\t\t= %d\n", trs_year);

  fclose(config_file);
  return 0;
}
