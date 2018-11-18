/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 LoBo (https://github.com/loboris)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef _MODDISPLAY_TFT_H_
#define _MODDISPLAY_TFT_H_

#include "sdkconfig.h"

#ifdef CONFIG_MICROPY_USE_TFT

#include "py/obj.h"

MP_DECLARE_CONST_FUN_OBJ_KW(display_tft_drawPixel_obj);
MP_DECLARE_CONST_FUN_OBJ_KW(display_tft_drawCircle_obj);
MP_DECLARE_CONST_FUN_OBJ_KW(display_tft_drawLine_obj);
MP_DECLARE_CONST_FUN_OBJ_KW(display_tft_drawLineByAngle_obj);
MP_DECLARE_CONST_FUN_OBJ_KW(display_tft_drawTriangle_obj);
MP_DECLARE_CONST_FUN_OBJ_KW(display_tft_drawEllipse_obj);
MP_DECLARE_CONST_FUN_OBJ_KW(display_tft_drawArc_obj);
MP_DECLARE_CONST_FUN_OBJ_KW(display_tft_drawPoly_obj);
MP_DECLARE_CONST_FUN_OBJ_KW(display_tft_drawRect_obj);
MP_DECLARE_CONST_FUN_OBJ_KW(display_tft_drawRoundRect_obj);
MP_DECLARE_CONST_FUN_OBJ_KW(display_tft_setFont_obj);
MP_DECLARE_CONST_FUN_OBJ_KW(display_tft_getFontSize_obj);
MP_DECLARE_CONST_FUN_OBJ_KW(display_tft_print_obj);
MP_DECLARE_CONST_FUN_OBJ_KW(display_tft_getSize_obj);
MP_DECLARE_CONST_FUN_OBJ_KW(display_tft_setclipwin_obj);
MP_DECLARE_CONST_FUN_OBJ_KW(display_tft_resetclipwin_obj);

#endif

#endif
