# Third-party runtime notices — Clipboard History 0.2.0

Clipboard History original application code is MIT licensed; see LICENSE.
The application source and build instructions are available at:
https://github.com/proacane/ClipboardHistoryQT/tree/v0.2.0

This distribution dynamically links unmodified Qt 6.11.1 libraries and plugins
from the official Windows MinGW kit, including Qt Core, Gui, Network, SQL, QML,
Quick, Quick Controls, Quick Dialogs, OpenGL and SVG. Qt is copyright The Qt
Company Ltd. and other contributors, and is distributed under LGPL version 3
with applicable third-party licenses. Full LGPLv3 and GPLv3 texts, and Qt's
component copyright/license notices, are included in the licenses directory.
The HTML files can be opened locally; their license text does not require CSS.
Qt documentation is licensed under GNU FDL 1.3 (LICENSE.FDL included).
Not every component described in the supplied notices is used on Windows.

Corresponding unmodified Qt 6.11.1 source archives are available from:
https://download.qt.io/archive/qt/6.11/6.11.1/submodules/
In particular: qtbase, qtdeclarative, qtsvg and qtshadertools.
The DLLs/plugins can be replaced with compatible builds. Reverse engineering
for debugging modifications of the LGPL-covered libraries is permitted.
Application build instructions in the source README enable rebuilding with
a compatible Qt kit; no code signing or installation check blocks replacement.

MinGW-w64 / GCC 13.1 runtime libraries are also included. See licenses/mingw/
for their copyright notices, GPL text and GCC Runtime Library Exception.
Toolchain source: https://github.com/niXman/mingw-builds-binaries/releases/tag/13.1.0-rt_v11-rev1

Qt's bundled third-party notices include SQLite, font/shaping libraries,
image codecs and other dependencies; their original licenses apply.
These libraries and notices are not relicensed under the application's MIT license.
