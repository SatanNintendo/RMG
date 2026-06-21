# RMG Translations

This directory contains the Qt Linguist translation source files (`.ts`)
for RMG's user interface. At build time, CMake invokes `lrelease` (via
`qt6_add_translations()`, see `Source/RMG/CMakeLists.txt`) to compile
each `.ts` file into a binary `.qm` file, which is then embedded into
the RMG executable as a Qt resource under `:/i18n/RMG_<locale>.qm`.

## Current languages

| File              | Locale code | Status                                       |
|-------------------|-------------|----------------------------------------------|
| `RMG_ru.ts`       | `ru`        | Complete (main UI strings)                   |
| `RMG_de.ts`       | `de`        | Skeleton (unfinished - falls back to English)|
| `RMG_fr.ts`       | `fr`        | Skeleton                                     |
| `RMG_es.ts`       | `es`        | Skeleton                                     |
| `RMG_it.ts`       | `it`        | Skeleton                                     |
| `RMG_pt_BR.ts`    | `pt_BR`     | Skeleton                                     |
| `RMG_ja.ts`       | `ja`        | Skeleton                                     |
| `RMG_zh.ts`       | `zh`        | Skeleton                                     |
| `RMG_pl.ts`       | `pl`        | Skeleton                                     |
| `RMG_ko.ts`       | `ko`        | Skeleton                                     |

A "skeleton" file has all message entries present (so it shows up in
the Settings dialog and produces a valid `.qm`) but with
`type="unfinished"` empty translations. Qt will render the source
English strings for any unfinished message, so picking a skeleton
language in Settings does not break the UI - it just looks English
until a translator fills in the file.

## How the language selector discovers these files

`SettingsDialog::populateLanguageComboBox()` (in
`Source/RMG/UserInterface/Dialog/SettingsDialog.cpp`) enumerates every
`RMG_*.qm` resource under `:/i18n` at runtime, extracts the locale
code from the filename, and adds an entry to the Language combobox
using `QLocale::languageToString()` to render a human-readable name
(e.g. "Russian", "Portuguese (Brazil)").

## Adding a new language

1. Create `RMG_<locale>.ts` in this directory.
   - Easiest: copy `RMG_ru.ts` to `RMG_<locale>.ts`, change the
     `language="..."` attribute on the `<TS>` tag, and replace each
     `<translation>...</translation>` body with your translation
     (or leave `type="unfinished"` for now and fill it in later).
   - Or run `lupdate` to extract source strings from the code:
     ```bash
     lupdate Source/RMG -ts Source/RMG/Translations/RMG_<locale>.ts
     ```
2. Re-run CMake (the build system globs `Translations/*.ts` automatically,
   so no CMakeLists edit is needed).
3. Rebuild RMG. The new language will now appear in
   Settings -> Interface -> General -> Language.

## Updating translations when source strings change

After editing any `.ui` file or any `tr()` / `QCoreApplication::translate()`
call in C++:

```bash
# Refresh all .ts files in one shot (existing translations are kept):
lupdate Source/RMG -ts Source/RMG/Translations/RMG_*.ts

# Or refresh just one:
lupdate Source/RMG -ts Source/RMG/Translations/RMG_ru.ts
```

Then open the `.ts` file in **Qt Linguist** (the GUI tool - much nicer
than editing XML by hand) and fill in the new/changed strings.

When done, rebuild RMG - `lrelease` runs automatically as part of the
build and regenerates the embedded `.qm` resources.

## Translating C++ strings (not just .ui strings)

Strings defined in `.ui` files are picked up automatically because
Qt's `uic` generates `retranslateUi()` calls that use
`QApplication::translate("ClassName", "Source text")`. Strings set
directly in C++ code (e.g. `showErrorMessage("CoreInit() Failed", ...)`)
are **not** automatically translated - they need to be wrapped in
`tr()` or `QCoreApplication::translate()`:

```cpp
// Before (always English, regardless of selected language):
this->showErrorMessage("CoreInit() Failed", ...);

// After (translatable):
this->showErrorMessage(tr("CoreInit() Failed"), ...);
```

After wrapping new strings in `tr()`, run `lupdate` again to add them
to all `.ts` files. The current implementation deliberately keeps this
as a separate, opt-in step so the existing C++ code is not destabilized.

## Utility script

`generate_skeleton_ts.py` regenerates the skeleton `.ts` files from
`RMG_ru.ts`. Run it after adding new strings to `RMG_ru.ts` and you
want the same set of source strings to appear (as `unfinished`) in all
the skeleton language files:

```bash
python3 Source/RMG/Translations/generate_skeleton_ts.py
```

## How runtime loading works

See `MainWindow::loadTranslator()` in
`Source/RMG/UserInterface/MainWindow.cpp`. It is called once during
`MainWindow::Init()`, *after* `CoreInit()` (so the `GUI_Language`
setting is readable) and *before* `setupUi()` (so that
`retranslateUi()` picks up the translations on the very first paint).

The translator reads the persisted `GUI_Language` setting:
- empty string -> use `QLocale::system()` (auto-detect from OS)
- non-empty    -> use `QLocale(stored_code)` (e.g. `QLocale("ru")`)

It then loads two QTranslator objects:
1. RMG's own translation from `:/i18n/RMG_<lang>.qm`
2. Qt's standard translation (button labels etc.) from
   `QLibraryInfo::path(QLibraryInfo::TranslationsPath)/qt_<lang>.qm`

Both translators stay installed for the lifetime of the application
(they are stored as `MainWindow` members). Changing the language in
Settings requires an application restart - this is intentional and
matches the behavior of the existing Theme setting.
