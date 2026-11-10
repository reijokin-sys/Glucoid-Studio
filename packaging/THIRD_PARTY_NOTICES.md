# Third-party notices

Glucoid Studio source code: Copyright 2026 Reijo Kinnunen.
SPDX-License-Identifier: GPL-3.0-or-later. The complete GPL version 3 text is
provided in LICENSE. Synthetic test fixtures are authored for this project.
The bundled Glucoid Studio icon is original project artwork, based on the
author's sketch, and is also GPL-3.0-or-later.

The application dynamically links distribution-provided Qt 6 (including
Qt Graphs), KDE Frameworks (KWallet and KF6I18n) and SQLite libraries. It
uses installed Qt Quick and Kirigami QML modules and the desktop icon theme.
Qt translation catalogs from qt6-translations-l10n are embedded as data for
the English and Finnish interface; their copyright notice is included in
packaging/licenses. No Qt, KDE, SQLite, font, third-party icon or Juggluco
implementation binary is bundled in the executable.
Qt Graphs is used under its GPLv3 terms, compatible with this application.
The Qt translation catalogs are used under GPL-3 with the Qt 1.0 exception.

The licenses directory includes the copyright and licensing notices from
the actual Ubuntu packages used for this release. The system libraries and
QML packages retain their own licenses and notices with their distribution
packages. Build/install scripts and application code are GPL-3.0-or-later.
The GPLv3 text includes the standard warranty disclaimer.

The software communicates with Juggluco and Nightscout through their export
and HTTP APIs. Their upstream implementation code is not distributed here.
