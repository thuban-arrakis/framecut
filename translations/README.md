# Russian translation catalogs

`framecut_ru.ts` is Framecut's editable Russian translation catalog. `framecut_ru.qm` is its compiled form, embedded in the application. Both are original Framecut content.

After changing user-facing source text, update and compile the catalog with Qt Linguist:

```sh
lupdate src -no-obsolete -ts translations/framecut_ru.ts
lrelease translations/framecut_ru.ts -qm translations/framecut_ru.qm
```

`qtbase_ru.qm` contains Qt's own Russian translations for standard Qt dialogs and controls. It was copied unchanged from the official **Qt 6.11.3 macOS desktop `qttranslations` archive**, distributed through [Qt's official download repository](https://download.qt.io/online/qtsdkrepository/mac_x64/desktop/qt6_6113/). Its copyright attribution and license metadata are recorded in [third-party notices](../docs/third-party-notices.md#included-qt-translation-catalog), with the [BSD-3-Clause license text](../docs/licenses/qt-translations-BSD-3-Clause.txt) included. See also [Qt translations source](https://code.qt.io/cgit/qt/qttranslations.git/). Native macOS file dialogs follow the operating system's language.

The application uses standard `QTranslator` resources. Switching language updates the existing window rather than rebuilding the media session. Theme and language choices are stored with `QSettings`; tests set `FRAMECUT_SETTINGS_FILE` to an isolated temporary INI file.

Version 0.3 includes Russian text for trim handles, the Trim inspector, undo/redo, range validation, and export status. Before committing a refreshed catalog, check that no entries are unfinished, each `%1`/`%2`/`%n` placeholder matches its source, and each Russian numerus entry retains all three plural forms. Compile the `.qm` after the final source text change so packaged builds receive the latest translations.
