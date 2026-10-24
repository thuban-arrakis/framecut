# Third-party dependencies

Framecut uses Qt 6 and FFmpeg. Preserve the licenses, copyright notices, version information, and source availability required by the exact builds shipped.

Qt offers commercial and open-source licensing options. The applicable terms depend on the selected Qt modules and distribution. See [Qt licensing](https://doc.qt.io/qt-6/licensing.html). This project dynamically links its Qt libraries and does not choose an organization-wide Qt licensing arrangement.

FFmpeg's license depends on its configured components: enabling GPL components changes the applicable FFmpeg license. Before redistributing a bundled binary, retain its license notices and provide the corresponding-source materials or other mechanism required by that build's license. See [FFmpeg licensing](https://ffmpeg.org/legal.html). The macOS packager records FFmpeg version/build information and a library-origin manifest; those records do not themselves satisfy every redistribution obligation.

The macOS package may contain additional dependencies pulled in by the installed FFmpeg and Qt builds. Review the manifest in `Framecut.app/Contents/Resources/bundled-library-manifest.json` and preserve the notices for those libraries as well. Debian packages use distribution-provided dependencies and their accompanying notices.

The app icon, landscape, and sample animation were generated specifically for Framecut and contain no downloaded stock footage. Framecut is not affiliated with CapCut.

## Included Qt translation catalog

`translations/qtbase_ru.qm` is an unchanged Russian translation catalog from Qt Translations 6.11.3.

Copyright (C) The Qt Company Ltd. and other contributors.

The Qt 6.11.3 SDK's `qttranslations-6.11.3.spdx.json` identifies the `Translations` component with the license expression `LicenseRef-Qt-Commercial OR BSD-3-Clause` and the copyright notice above. The [BSD-3-Clause license text](licenses/qt-translations-BSD-3-Clause.txt) is reproduced unchanged from [Qt's official translation repository](https://github.com/qt/qttranslations/blob/dev/LICENSES/BSD-3-Clause.txt). This notice applies to the included Qt catalog; it does not assign a license to Framecut's original source or assets.
