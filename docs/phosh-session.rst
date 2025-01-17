.. _phosh-session(1):

=============
phosh-session
=============

--------------------------------
Session startup script for phosh
--------------------------------

SYNOPSIS
--------
|   **phosh-session** [OPTIONS...]


DESCRIPTION
-----------

``phosh-session`` is the script to startup phosh including required
components like the Wayland compositor ``phoc`` and an on screen
keyboard like ``squeekboard``.  The script is rarely invoked by a user
but rather activated by a display manager or systemd unit. However
it's perfectly valid to e.g. log into a tty and run ``phosh-session``
to bring up phosh and needed components.

OPTIONS
-------

``-h``, ``--help``
   Print help and exit

ENVIRONMENT VARIABLES
---------------------

``phosh-session`` honors the following environment variables:

- ``WLR_BACKENDS``: The backends the wlroots library should use when phoc launches. See
  https://gitlab.freedesktop.org/wlroots/wlroots/-/blob/master/docs/env_vars.md

See also
--------

``phosh(1)``
