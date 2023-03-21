/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * st-settings.h: Global settings
 *
 * Copyright 2019 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#if !defined(ST_H_INSIDE) && !defined(ST_COMPILATION)
#error "Only <st/st.h> can be included directly.h"
#endif

#ifndef __ST_SETTINGS_H__
#define __ST_SETTINGS_H__

#include <glib-object.h>
#include <gdesktop-enums.h>

G_BEGIN_DECLS

typedef enum {
  ST_SYSTEM_COLOR_SCHEME_DEFAULT = G_DESKTOP_COLOR_SCHEME_DEFAULT,
  ST_SYSTEM_COLOR_SCHEME_PREFER_DARK = G_DESKTOP_COLOR_SCHEME_PREFER_DARK,
  ST_SYSTEM_COLOR_SCHEME_PREFER_LIGHT = G_DESKTOP_COLOR_SCHEME_PREFER_LIGHT,
} StSystemColorScheme;

typedef enum {
  ST_SYSTEM_ACCENT_COLOR_BLUE = G_DESKTOP_ACCENT_COLOR_BLUE,
  ST_SYSTEM_ACCENT_COLOR_TEAL = G_DESKTOP_ACCENT_COLOR_TEAL,
  ST_SYSTEM_ACCENT_COLOR_GREEN = G_DESKTOP_ACCENT_COLOR_GREEN,
  ST_SYSTEM_ACCENT_COLOR_YELLOW = G_DESKTOP_ACCENT_COLOR_YELLOW,
  ST_SYSTEM_ACCENT_COLOR_ORANGE = G_DESKTOP_ACCENT_COLOR_ORANGE,
  ST_SYSTEM_ACCENT_COLOR_RED = G_DESKTOP_ACCENT_COLOR_RED,
  ST_SYSTEM_ACCENT_COLOR_PINK = G_DESKTOP_ACCENT_COLOR_PINK,
  ST_SYSTEM_ACCENT_COLOR_PURPLE = G_DESKTOP_ACCENT_COLOR_PURPLE,
  ST_SYSTEM_ACCENT_COLOR_SLATE = G_DESKTOP_ACCENT_COLOR_SLATE,
} StSystemAccentColor;

#define ST_TYPE_SETTINGS (st_settings_get_type ())
G_DECLARE_FINAL_TYPE (StSettings, st_settings, ST, SETTINGS, GObject)

StSettings * st_settings_get (void);

void st_settings_inhibit_animations (StSettings *settings);

void st_settings_uninhibit_animations (StSettings *settings);

G_END_DECLS

#endif /* __ST_SETTINGS_H__ */
