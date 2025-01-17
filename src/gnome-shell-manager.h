/*
 * Copyright (C) 2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 *         Evangelos Ribeiro Tzaras <devrtz@fortysixandtwo.eu>
 *
 * ShellActionMode is from GNOME Shell's shell-action-modes.h
 * which is GPL-2.0-or-later and authored by
 * Florian Müllner <fmuellner@gnome.org>
 */
#pragma once

#include "dbus/phosh-gnome-shell-dbus.h"
#include <glib-object.h>

/**
 * ShellActionMode:
 * @SHELL_ACTION_MODE_NONE: block action
 * @SHELL_ACTION_MODE_NORMAL: allow action when in window mode,
 *     e.g. when the focus is in an application window
 * @SHELL_ACTION_MODE_OVERVIEW: allow action while the overview
 *     is active
 * @SHELL_ACTION_MODE_LOCK_SCREEN: allow action when the screen
 *     is locked, e.g. when the screen shield is shown
 * @SHELL_ACTION_MODE_UNLOCK_SCREEN: allow action in the unlock
 *     dialog
 * @SHELL_ACTION_MODE_LOGIN_SCREEN: allow action in the login screen
 * @SHELL_ACTION_MODE_SYSTEM_MODAL: allow action when a system modal
 *     dialog (e.g. authentication or session dialogs) is open
 * @SHELL_ACTION_MODE_LOOKING_GLASS: allow action in looking glass
 * @SHELL_ACTION_MODE_POPUP: allow action while a shell menu is open
 * @SHELL_ACTION_MODE_ALL: always allow action
 *
 * Controls in which GNOME Shell states an action (like keybindings and gestures)
 * should be handled.
*/
typedef enum {
  SHELL_ACTION_MODE_NONE          = 0,
  SHELL_ACTION_MODE_NORMAL        = 1 << 0,
  SHELL_ACTION_MODE_OVERVIEW      = 1 << 1,
  SHELL_ACTION_MODE_LOCK_SCREEN   = 1 << 2,
  SHELL_ACTION_MODE_UNLOCK_SCREEN = 1 << 3,
  SHELL_ACTION_MODE_LOGIN_SCREEN  = 1 << 4,
  SHELL_ACTION_MODE_SYSTEM_MODAL  = 1 << 5,
  SHELL_ACTION_MODE_LOOKING_GLASS = 1 << 6,
  SHELL_ACTION_MODE_POPUP         = 1 << 7,

  SHELL_ACTION_MODE_ALL = ~0,
} ShellActionMode;

G_BEGIN_DECLS

#define PHOSH_TYPE_GNOME_SHELL_MANAGER             (phosh_gnome_shell_manager_get_type ())
G_DECLARE_FINAL_TYPE (PhoshGnomeShellManager, phosh_gnome_shell_manager, PHOSH, GNOME_SHELL_MANAGER,
                      PhoshDBusGnomeShellSkeleton)

PhoshGnomeShellManager *phosh_gnome_shell_manager_get_default      (void);

G_END_DECLS
