/*
 * Copyright (C) 2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 *         Evangelos Ribeiro Tzaras <devrtz@fortysixandtwo.eu>
 */

#define G_LOG_DOMAIN "phosh-gnome-shell-manager"

#include "phosh-config.h"

#include "gnome-shell-manager.h"
#include "osd-window.h"
#include "shell.h"
#include "util.h"
#include "lockscreen-manager.h"

#ifdef PHOSH_HAVE_GNOME_PLATFORM_VERSION
#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-desktop-version.h>
#endif

#define PHOSH_VERSION_SUFFIX " (phosh " PHOSH_VERSION ")"

/**
 * PhoshGnomeShellManager:
 *
 * Provides the org.gnome.Shell DBus interface
 *
 */

#define GNOME_SHELL_DBUS_NAME "org.gnome.Shell"
#define OSD_HIDE_TIMEOUT 1 /* seconds */

static void phosh_gnome_shell_manager_gnome_shell_iface_init (PhoshDBusGnomeShellIface *iface);

enum {
  PROP_0,
  PROP_ACTION_MODE,
  /* overriden properties */
  PROP_SHELL_VERSION,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

typedef struct _PhoshGnomeShellManager {
  PhoshDBusGnomeShellSkeleton parent;

  GHashTable                 *info_by_action;
  guint                       last_action_id;
  int                         dbus_name_id;
  ShellActionMode             action_mode;

  PhoshOsdWindow             *osd;
  gint                        osd_timeoutid;
  gboolean                    osd_continue;
} PhoshGnomeShellManager;

G_DEFINE_TYPE_WITH_CODE (PhoshGnomeShellManager,
                         phosh_gnome_shell_manager,
                         PHOSH_DBUS_TYPE_GNOME_SHELL_SKELETON,
                         G_IMPLEMENT_INTERFACE (
                           PHOSH_DBUS_TYPE_GNOME_SHELL,
                           phosh_gnome_shell_manager_gnome_shell_iface_init));

static void accelerator_activated_action (GSimpleAction *action, GVariant *param, gpointer data);

typedef struct _AcceleratorInfo {
  guint                            action_id;
  gchar                           *accelerator;
  gchar                           *sender;
  guint                            mode_flags;
  guint                            grab_flags;
} AcceleratorInfo;

static void
remove_action_entries (gchar *accelerator)
{
  GStrv action_names = (char*[]){ accelerator, NULL };

  phosh_shell_remove_global_keyboard_action_entries (phosh_shell_get_default (),
                                                     action_names);
}

static void
free_accelerator_info_from_hash_table (gpointer data)
{
  AcceleratorInfo *info = (AcceleratorInfo *) data;
  g_return_if_fail (info != NULL);

  remove_action_entries (info->accelerator);
  g_free (info->accelerator);
  g_free (info->sender);
  g_free (info);
}

/* DBus handlers */
static gboolean
handle_show_monitor_labels (PhoshDBusGnomeShell   *skeleton,
                            GDBusMethodInvocation *invocation,
                            GVariant              *arg_params)
{
  PhoshGnomeShellManager *self = PHOSH_GNOME_SHELL_MANAGER (skeleton);

  g_return_val_if_fail (PHOSH_IS_GNOME_SHELL_MANAGER (self), FALSE);
  g_debug ("DBus show monitor labels");

  phosh_dbus_gnome_shell_complete_show_monitor_labels (
    skeleton, invocation);

  return TRUE;
}

static gboolean
handle_hide_monitor_labels (PhoshDBusGnomeShell   *skeleton,
                            GDBusMethodInvocation *invocation)
{
  PhoshGnomeShellManager *self = PHOSH_GNOME_SHELL_MANAGER (skeleton);

  g_return_val_if_fail (PHOSH_IS_GNOME_SHELL_MANAGER (self), FALSE);
  g_debug ("DBus hide monitor labels");

  phosh_dbus_gnome_shell_complete_hide_monitor_labels (
    skeleton, invocation);

  return TRUE;
}


static gboolean
on_osd_timeout (PhoshGnomeShellManager *self)
{
  gboolean ret;
  ret = self->osd_continue ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
  if (!self->osd_continue) {
      g_debug ("Closing osd");
      self->osd_timeoutid = 0;
      if (self->osd)
        gtk_widget_destroy (GTK_WIDGET (self->osd));
  }
  self->osd_continue = FALSE;
  return ret;
}

static void
on_osd_destroyed (PhoshGnomeShellManager *self)
{
  self->osd = NULL;
  g_clear_handle_id (&self->osd_timeoutid, g_source_remove);
}


static gboolean
handle_show_osd (PhoshDBusGnomeShell   *skeleton,
                 GDBusMethodInvocation *invocation,
                 GVariant              *arg_params)
{
  PhoshGnomeShellManager *self = PHOSH_GNOME_SHELL_MANAGER (skeleton);
  GVariantDict dict;
  g_autofree char *connector = NULL, *icon = NULL, *label = NULL;
  gdouble level = 0.0, maxlevel = 1.0;

  g_return_val_if_fail (PHOSH_IS_GNOME_SHELL_MANAGER (self), FALSE);

  g_variant_dict_init (&dict, arg_params);
  g_variant_dict_lookup (&dict, "connector", "s", &connector);
  g_variant_dict_lookup (&dict, "icon", "s", &icon);
  g_variant_dict_lookup (&dict, "label", "s", &label);
  g_variant_dict_lookup (&dict, "level", "d", &level);
  g_variant_dict_lookup (&dict, "max_level", "d", &maxlevel);

  g_debug ("DBus show osd: connector: %s icon: %s, label: %s, level %f/%f",
           connector, icon, label, level, maxlevel);

  if (self->osd) {
    self->osd_continue = TRUE;
    g_object_set (self->osd,
                  "connector", connector,
                  "label", label,
                  "icon-name", icon,
                  "level", level,
                  "max-level", maxlevel,
                  NULL);
  } else {
    self->osd = PHOSH_OSD_WINDOW (phosh_osd_window_new (connector, label, icon, level, maxlevel));
    g_signal_connect_swapped (self->osd, "destroy", G_CALLBACK (on_osd_destroyed), self);
    gtk_widget_show (GTK_WIDGET (self->osd));
  }

  if (!self->osd_timeoutid) {
    self->osd_timeoutid = g_timeout_add_seconds (OSD_HIDE_TIMEOUT,
                                                 (GSourceFunc)on_osd_timeout,
                                                 self);
    g_source_set_name_by_id (self->osd_timeoutid, "[phosh] osd-timeout");
  }

  phosh_dbus_gnome_shell_complete_show_osd (
    skeleton, invocation);

  return TRUE;
}


static guint
grab_single_accelerator (PhoshGnomeShellManager *self,
                         const gchar            *accelerator,
                         guint                   mode_flags,
                         guint                   grab_flags,
                         const gchar            *sender,
                         GError                **error)
{
  AcceleratorInfo *info;
  const GActionEntry action_entries[] = {
    { .name = accelerator, .activate = accelerator_activated_action },
  };

  g_assert (PHOSH_IS_GNOME_SHELL_MANAGER (self));

  if (self->last_action_id == G_MAXUINT) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_ADDRESS_IN_USE, "All action ids taken");
    return 0;
  }

  /* this should never happen */
  if (g_hash_table_contains (self->info_by_action, GUINT_TO_POINTER (self->last_action_id + 1))) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "action id %d already taken", self->last_action_id + 1);
    return 0;
  }

  info = g_new0 (AcceleratorInfo, 1);
  info->accelerator = g_strdup (accelerator);
  info->action_id = ++(self->last_action_id);
  info->sender = g_strdup (sender);
  info->mode_flags = mode_flags;
  info->grab_flags = grab_flags;

  g_debug ("Using action id %d for accelerator %s", info->action_id, info->accelerator);
  g_hash_table_insert (self->info_by_action, GUINT_TO_POINTER (info->action_id), info);

  if (g_strcmp0 (accelerator, "XF86PowerOff")) {
    phosh_shell_add_global_keyboard_action_entries (phosh_shell_get_default (),
                                                    action_entries,
                                                    G_N_ELEMENTS (action_entries),
                                                    info);
  } else {
    /*
     * FIXME: Don't allow binding of power keys so we can blank the screen
     * See https://gitlab.gnome.org/GNOME/gnome-settings-daemon/-/issues/703
     * We don't return an error as we want g-s-d to handle all the other keys
     */
    g_debug ("Skipping power key grab");
  }

  g_assert (info->action_id > 0);
  return info->action_id;
}

static gboolean
handle_grab_accelerator (PhoshDBusGnomeShell   *skeleton,
                         GDBusMethodInvocation *invocation,
                         const gchar           *arg_accelerator,
                         guint                  arg_modeFlags,
                         guint                  arg_grabFlags)
{
  PhoshGnomeShellManager *self = PHOSH_GNOME_SHELL_MANAGER (skeleton);
  g_autoptr (GError) error = NULL;
  const gchar * sender;
  guint action_id;

  g_return_val_if_fail (PHOSH_IS_GNOME_SHELL_MANAGER (self), FALSE);
  g_debug ("DBus grab accelerator %s", arg_accelerator);

  sender = g_dbus_method_invocation_get_sender (invocation);

  action_id = grab_single_accelerator (self,
                                       arg_accelerator,
                                       arg_modeFlags,
                                       arg_grabFlags,
                                       sender,
                                       &error);
  if (action_id == 0) {
    g_warning ("Error trying to grab accelerator %s: %s", arg_accelerator, error->message);
    g_dbus_method_invocation_return_error (invocation,
                                           error->domain,
                                           error->code,
                                           "%s",
                                           error->message);
    return TRUE;
  }

  phosh_dbus_gnome_shell_complete_grab_accelerator (
    skeleton, invocation, action_id);

  return TRUE;
}

static gboolean
handle_grab_accelerators (PhoshDBusGnomeShell   *skeleton,
                          GDBusMethodInvocation *invocation,
                          GVariant              *arg_accelerators)
{
  PhoshGnomeShellManager *self = PHOSH_GNOME_SHELL_MANAGER (skeleton);
  g_autoptr (GVariantBuilder) builder = NULL;
  g_autoptr (GVariantIter) arg_iter = NULL;
  gchar *accelerator_name;
  guint accelerator_mode_flags;
  guint accelerator_grab_flags;
  g_autoptr (GError) error = NULL;
  const gchar *sender;
  gboolean conflict = FALSE;

  g_return_val_if_fail (PHOSH_IS_GNOME_SHELL_MANAGER (self), FALSE);
  g_debug ("DBus grab accelerators");

  sender = g_dbus_method_invocation_get_sender (invocation);

  builder = g_variant_builder_new (G_VARIANT_TYPE ("au"));
  g_variant_get (arg_accelerators, "a(suu)", &arg_iter);

  while (g_variant_iter_loop (arg_iter, "(&suu)",
                              &accelerator_name,
                              &accelerator_mode_flags,
                              &accelerator_grab_flags)) {
    guint action_id;

    action_id = grab_single_accelerator (self,
                                         accelerator_name,
                                         accelerator_mode_flags,
                                         accelerator_grab_flags,
                                         sender,
                                         &error);
    if (action_id == 0) {
      g_warning ("Error trying to grab accelerator %s: %s", accelerator_name, error->message);
      g_dbus_method_invocation_return_error (invocation,
                                             error->domain,
                                             error->code,
                                             "%s",
                                             error->message);
      conflict = TRUE;
      break;
    }

    g_variant_builder_add (builder, "u", action_id);
  }

  if (conflict) { /* clean up existing bindings */
    g_autoptr (GVariant) unroll_ids = g_variant_builder_end (builder);
    gsize n = g_variant_n_children (unroll_ids);

    for (gsize i = 0; i < n; ++i) {
      guint action_id;

      g_variant_get_child (unroll_ids, i, "u", &action_id);
      g_hash_table_remove (self->info_by_action, GUINT_TO_POINTER (action_id));
    }
  } else { /* success */
    phosh_dbus_gnome_shell_complete_grab_accelerators (
      skeleton, invocation, g_variant_builder_end (builder));
  }

  return TRUE;
}

static gboolean
handle_ungrab_accelerator (PhoshDBusGnomeShell   *skeleton,
                           GDBusMethodInvocation *invocation,
                           guint                  arg_action)
{
  PhoshGnomeShellManager *self = PHOSH_GNOME_SHELL_MANAGER (skeleton);
  AcceleratorInfo *info;
  gboolean success = FALSE;
  const gchar *sender;

  g_return_val_if_fail (PHOSH_IS_GNOME_SHELL_MANAGER (self), FALSE);
  g_debug ("DBus ungrab accelerator (id %u)", arg_action);

  sender = g_dbus_method_invocation_get_sender (invocation);
  info = g_hash_table_lookup (self->info_by_action, GUINT_TO_POINTER (arg_action));

  if (info != NULL) {
    if (g_strcmp0 (info->sender, sender) == 0) {
      g_hash_table_remove (self->info_by_action, GUINT_TO_POINTER (info->action_id));
      success = TRUE;
    } else {
      g_debug ("Ungrab not allowed: Sender %s not allowed to ungrab (grabbed by %s)",
                 sender, info->sender);
    }
  }

  phosh_dbus_gnome_shell_complete_ungrab_accelerator (
    skeleton, invocation, success);

  return TRUE;
}

static gboolean
handle_ungrab_accelerators (PhoshDBusGnomeShell *skeleton,
                            GDBusMethodInvocation    *invocation,
                            GVariant                 *arg_actions)
{
  gsize n;
  PhoshGnomeShellManager *self = PHOSH_GNOME_SHELL_MANAGER (skeleton);
  AcceleratorInfo *info;
  gboolean success = TRUE;
  const gchar *sender;
  g_return_val_if_fail (PHOSH_IS_GNOME_SHELL_MANAGER (self), FALSE);

  sender = g_dbus_method_invocation_get_sender (invocation);
  n = g_variant_n_children (arg_actions);
  g_debug ("DBus ungrab %" G_GSIZE_FORMAT " accelerators", n);

  for (gsize i = 0; i < n; i++) {
    guint arg_action;
    g_variant_get_child (arg_actions, i, "u", &arg_action);
    info = g_hash_table_lookup (self->info_by_action, GUINT_TO_POINTER (arg_action));
    if (info == NULL) {
      g_warning ("Can't ungrab: No accelerator (id %u) found", arg_action);
      success = FALSE;
      continue;
    }

    if (g_strcmp0 (info->sender, sender) != 0) {
      g_warning ("Ungrab (id %u) not allowed: Sender %s not allowed to ungrab (grabbed by %s)",
                 arg_action, sender, info->sender);
      success = FALSE;
      continue;
    }
    g_hash_table_remove (self->info_by_action, GUINT_TO_POINTER (info->action_id));
  }
  phosh_dbus_gnome_shell_complete_ungrab_accelerators (
    skeleton, invocation, success);

  return TRUE;
}

static void
accelerator_activated (PhoshDBusGnomeShell *skeleton,
                       guint                arg_action,
                       GVariant            *arg_parameters)
{
  PhoshGnomeShellManager *self = PHOSH_GNOME_SHELL_MANAGER (skeleton);

  g_return_if_fail (PHOSH_IS_GNOME_SHELL_MANAGER (self));
  g_debug ("DBus emitting accelerator activated for action %u", arg_action);

  phosh_dbus_gnome_shell_emit_accelerator_activated (
    skeleton, arg_action, arg_parameters);

}

static void
phosh_gnome_shell_manager_gnome_shell_iface_init (PhoshDBusGnomeShellIface *iface)
{
  iface->handle_show_monitor_labels = handle_show_monitor_labels;
  iface->handle_hide_monitor_labels = handle_hide_monitor_labels;
  iface->handle_show_osd = handle_show_osd;
  iface->handle_grab_accelerator = handle_grab_accelerator;
  iface->handle_grab_accelerators = handle_grab_accelerators;
  iface->handle_ungrab_accelerator = handle_ungrab_accelerator;
  iface->handle_ungrab_accelerators = handle_ungrab_accelerators;
}


#ifndef GNOME_DESKTOP_PLATFORM_VERSION
typedef struct
{
  char *major;
  char *minor;
  char **current;
} VersionData;


static void
version_start_element_handler (GMarkupParseContext  *ctx,
                               const char           *element_name,
                               const char          **attr_names,
                               const char          **attr_values,
                               gpointer              user_data,
                               GError              **error)
{
  VersionData *data = user_data;
  if (g_str_equal (element_name, "platform"))
    data->current = &data->major;
  else if (g_str_equal (element_name, "minor"))
    data->current = &data->minor;
  else
    data->current = NULL;
}


static void
version_data_free (VersionData *data)
{
  g_free (data->major);
  g_free (data->minor);
  g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (VersionData, version_data_free);


static void
version_end_element_handler (GMarkupParseContext *ctx,
                             const char          *element_name,
                             gpointer             user_data,
                             GError             **error)
{
  VersionData *data = user_data;
  data->current = NULL;
}

static void
version_text_handler (GMarkupParseContext *ctx,
                      const char          *text,
                      gsize                text_len,
                      gpointer             user_data,
                      GError             **error)
{
  VersionData *data = user_data;
  if (data->current != NULL)
  {
    g_autofree char *stripped = NULL;

    stripped = g_strstrip (g_strdup (text));
    g_free (*data->current);
    *data->current = g_strdup (stripped);
  }
}
#endif


static char *
get_version (void)
{
#ifdef GNOME_DESKTOP_PLATFORM_VERSION
  /* We don't want to infringe on the GNOME version and not upset
     anyone so make it very visible this is not GNOMEs version */
  return g_strdup_printf ("%d%s", GNOME_DESKTOP_PLATFORM_VERSION, PHOSH_VERSION_SUFFIX);
#else
  /* Fallback for GNOME < 43*/
  g_autoptr (GError) error = NULL;
  g_autoptr (GMarkupParseContext) ctx = NULL;
  g_autoptr (VersionData) data = NULL;
  g_autofree char *contents = NULL;
  GMarkupParser version_parser = {
    version_start_element_handler,
    version_end_element_handler,
    version_text_handler,
    NULL,
    NULL,
  };
  gsize length;

  data = g_new0 (VersionData, 1);
  if (g_file_get_contents (PHOSH_DATA_DIR "/gnome/gnome-version.xml",
                           &contents,
                           &length,
                           &error)) {
     ctx = g_markup_parse_context_new (&version_parser, 0, data, NULL);

     if (!g_markup_parse_context_parse (ctx, contents, length, &error))
       g_warning ("Failed to parse version from XML");
  } else {
    g_warning ("Failed to read version XML");
  }

  if (data->major && data->minor)
    return g_strdup_printf ("%s.%s%s", data->major, data->minor, PHOSH_VERSION_SUFFIX);
  else
    return g_strdup_printf ("unknown" PHOSH_VERSION_SUFFIX);
#endif
}


static ShellActionMode
get_action_mode (PhoshShellStateFlags state)
{
  PhoshShell *shell = phosh_shell_get_default ();

  if (state & PHOSH_STATE_LOCKED) {
    PhoshLockscreenManager *lockscreen_manager = phosh_shell_get_lockscreen_manager (shell);
    PhoshLockscreenPage page = phosh_lockscreen_manager_get_page (lockscreen_manager);

    if (page == PHOSH_LOCKSCREEN_PAGE_UNLOCK)
      return SHELL_ACTION_MODE_UNLOCK_SCREEN;
    else
      return SHELL_ACTION_MODE_LOCK_SCREEN;
  }

  if (state & PHOSH_STATE_MODAL_SYSTEM_PROMPT)
    return SHELL_ACTION_MODE_SYSTEM_MODAL;

  if (state & PHOSH_STATE_OVERVIEW)
    return SHELL_ACTION_MODE_OVERVIEW;

  return SHELL_ACTION_MODE_NORMAL;
}

static void
accelerator_activated_action (GSimpleAction *action,
                              GVariant      *param,
                              gpointer       data)
{
  AcceleratorInfo *info = (AcceleratorInfo *) data;
  PhoshGnomeShellManager *self = phosh_gnome_shell_manager_get_default ();
  g_autoptr (GVariantBuilder) builder = NULL;
  GVariant *parameters;
  uint32_t action_id;

  action_id = info->action_id;
  g_debug ("accelerator action activated for id %u", action_id);

  if ((info->mode_flags & self->action_mode) == 0) {
    g_autofree gchar *str_shell_mode = g_flags_to_string (SHELL_TYPE_ACTION_MODE, self->action_mode);
    g_autofree gchar *str_grabbed_mode = g_flags_to_string (SHELL_TYPE_ACTION_MODE, info->mode_flags);
    g_debug ("Accelerator registered for mode %s, but shell is currently in %s",
             str_grabbed_mode,
             str_shell_mode);
    return;
  }

  builder = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
  /*
   * Fill some dummy values
   */
  g_variant_builder_add (builder, "{sv}", "device-id", g_variant_new_uint32 (0));
  g_variant_builder_add (builder, "{sv}", "timestamp", g_variant_new_uint32 (GDK_CURRENT_TIME));
  g_variant_builder_add (builder, "{sv}", "action-mode", g_variant_new_uint32 (0));
  g_variant_builder_add (builder, "{sv}", "device-id", g_variant_new_string ("/dev/input/event0"));
  parameters = g_variant_builder_end (builder);

  accelerator_activated (PHOSH_DBUS_GNOME_SHELL (self),
                         action_id,
                         parameters);

}


static void
on_name_acquired (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
  PhoshGnomeShellManager *self = PHOSH_GNOME_SHELL_MANAGER (user_data);

  g_debug ("Acquired name %s", name);
  g_return_if_fail (PHOSH_IS_GNOME_SHELL_MANAGER (self));
}


static void
on_name_lost (GDBusConnection *connection,
              const char      *name,
              gpointer         user_data)
{
  g_debug ("Lost or failed to acquire name %s", name);
}


static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  g_autoptr (GError) err = NULL;
  PhoshGnomeShellManager *self = PHOSH_GNOME_SHELL_MANAGER (user_data);
  PhoshSessionManager *sm;
  gboolean success;

  success = g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                              connection,
                                              "/org/gnome/Shell",
                                              &err);
  if (!success) {
    g_warning ("Failed to export shell interface: %s", err->message);
    return;
  }

  sm = phosh_shell_get_session_manager (phosh_shell_get_default ());
  phosh_session_manager_export_end_session (sm, connection);
}


static gboolean
transform_state_to_action_mode (GBinding     *binding,
                                const GValue *from_value,
                                GValue       *to_value,
                                gpointer      unused)
{
  PhoshShellStateFlags shell_state = g_value_get_flags (from_value);
  ShellActionMode action_mode = get_action_mode (shell_state);

  g_value_set_flags (to_value, action_mode);
  return TRUE;
}

static void
phosh_gnome_shell_manager_set_property (GObject      *object,
                                        guint         property_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  PhoshGnomeShellManager *self = PHOSH_GNOME_SHELL_MANAGER (object);

  switch (property_id) {
  case PROP_ACTION_MODE:
    self->action_mode = g_value_get_flags (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_gnome_shell_manager_get_property (GObject    *object,
                                        guint       property_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  PhoshGnomeShellManager *self = PHOSH_GNOME_SHELL_MANAGER (object);

  switch (property_id) {
  case PROP_ACTION_MODE:
    g_value_set_flags (value, self->action_mode);
    break;
  case PROP_SHELL_VERSION: {
    g_autofree char *version = get_version ();
    g_value_set_string (value, version);
    break;
  }
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
phosh_gnome_shell_manager_dispose (GObject *object)
{
  PhoshGnomeShellManager *self = PHOSH_GNOME_SHELL_MANAGER (object);

  g_clear_handle_id (&self->dbus_name_id, g_bus_unown_name);

  if (g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (self)))
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));

  g_clear_pointer (&self->info_by_action, g_hash_table_unref);

  G_OBJECT_CLASS (phosh_gnome_shell_manager_parent_class)->dispose (object);
}


static void
phosh_gnome_shell_manager_constructed (GObject *object)
{
  PhoshGnomeShellManager *self = PHOSH_GNOME_SHELL_MANAGER (object);
  PhoshShell *shell = phosh_shell_get_default ();

  G_OBJECT_CLASS (phosh_gnome_shell_manager_parent_class)->constructed (object);

  g_object_bind_property_full (shell, "shell-state",
                               object, "shell-action-mode",
                               G_BINDING_SYNC_CREATE,
                               (GBindingTransformFunc) transform_state_to_action_mode,
                               NULL, NULL, NULL);

  self->dbus_name_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                       GNOME_SHELL_DBUS_NAME,
                                       G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                       G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                       on_bus_acquired,
                                       on_name_acquired,
                                       on_name_lost,
                                       self,
                                       NULL);
}


static void
phosh_gnome_shell_manager_class_init (PhoshGnomeShellManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phosh_gnome_shell_manager_get_property;
  object_class->set_property = phosh_gnome_shell_manager_set_property;
  object_class->constructed = phosh_gnome_shell_manager_constructed;
  object_class->dispose = phosh_gnome_shell_manager_dispose;

  props[PROP_ACTION_MODE] =
    g_param_spec_flags ("shell-action-mode",
                        "Shell Action Mode",
                        "The active action mode (used for keygrabbing)",
                        SHELL_TYPE_ACTION_MODE,
                        SHELL_ACTION_MODE_NONE,
                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP - 1, props);

  g_object_class_override_property (object_class, PROP_SHELL_VERSION, "shell-version");
}


static void
phosh_gnome_shell_manager_init (PhoshGnomeShellManager *self)
{
  self->info_by_action = g_hash_table_new_full (g_direct_hash,
                                                g_direct_equal,
                                                NULL,
                                                free_accelerator_info_from_hash_table);
  self->last_action_id = 0;
}


PhoshGnomeShellManager *
phosh_gnome_shell_manager_get_default (void)
{
  static PhoshGnomeShellManager *instance;

  if (instance == NULL) {
    g_debug ("Creating gnome shell DBus manager");
    instance = g_object_new (PHOSH_TYPE_GNOME_SHELL_MANAGER, NULL);
    g_object_add_weak_pointer (G_OBJECT (instance), (gpointer *) &instance);
  }
  return instance;
}
