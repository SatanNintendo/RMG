# RMG Translations

This directory contains the Qt Linguist translation source files (`.ts`)
for RMG's user interface. At build time, CMake invokes `lrelease` (via
`qt6_add_translations()`, see `Source/RMG/CMakeLists.txt`) to compile
each `.ts` file into a binary `.qm` file, which is then embedded into
the RMG executable as a Qt resource under `:/i18n/RMG_<locale>.qm`.

## Current languages

| File              | Locale code | Status                                                       |
|-------------------|-------------|--------------------------------------------------------------|
| `RMG_en.ts`       | `en`        | **English reference** — source of truth for translators. Every translatable string in RMG appears here, with `<translation>` = `<source>`. |
| `RMG_ru.ts`       | `ru`        | **Russian** — fully translated (444 of 445 strings; the only unfinished one is the long HTML credits block in AboutDialog). |

## How the language selector discovers these files

`SettingsDialog::populateLanguageComboBox()` (in
`Source/RMG/UserInterface/Dialog/SettingsDialog.cpp`) enumerates every
`RMG_*.qm` resource under `:/i18n` at runtime, extracts the locale
code from the filename, and adds an entry to the Language combobox
using `QLocale::languageToString()` to render a human-readable name
(e.g. "Russian", "English").

## Why only English and Russian ship by default

Earlier versions of this patch shipped 9 empty "skeleton" language
files (de, fr, es, it, pt_BR, ja, zh, pl, ko). Each contained all 445
strings but with `type="unfinished"` and empty `<translation>` bodies.
Qt's `lrelease` skips unfinished entries, so the resulting `.qm` files
were empty and the languages didn't actually work when selected — they
just fell back to English source strings.

To avoid shipping broken translations, only complete translations are
now included: **English** (the source-language reference) and
**Russian** (fully translated).

## Adding a new language

### Option A: From the English reference file (recommended)

1. Copy `RMG_en.ts` to `RMG_<locale>.ts` (e.g. `RMG_fr.ts`).
2. Change `language="en"` to `language="<locale>"` on the `<TS>` tag
   (e.g. `language="fr"` or `language="pt_BR"`).
3. For each `<message>`:
   - Replace the `<translation>` body with your translated string.
   - Remove any `type="unfinished"` attribute once the entry is
     final (or leave it on entries you want to revisit later).
4. Drop the file into this directory.
5. Re-run CMake. The new language appears in Settings automatically.

### Option B: Use the skeleton generator script

```bash
# Create a skeleton for Ukrainian (uk)
python3 Source/RMG/Translations/generate_skeleton_ts.py uk uk_UK
```

This produces `RMG_uk.ts` with every English source string copied into
the `<translation>` body, marked as `type="unfinished"`. Open it in
Qt Linguist to fill in the translations, then rebuild.

### Option C: Use lupdate directly

```bash
# Extract all current source strings from the code into a fresh .ts:
lupdate Source/RMG Source/RMG-Input Source/RMG-Audio \
        -ts Source/RMG/Translations/RMG_<locale>.ts
```

This is the canonical Qt way. `lupdate` re-scans the source code, so
the resulting `.ts` will always match the current state of the
codebase. Use this when the source files have changed since the
English reference was last regenerated.

## Updating translations when source strings change

After editing any `.ui` file or any `tr()` / `QCoreApplication::translate()`
call in C++:

```bash
# Refresh the English reference (regenerates from source):
python3 Source/RMG/Translations/generate_en_ts.py > Source/RMG/Translations/RMG_en.ts

# Refresh all .ts files via lupdate (keeps existing translations,
# adds new entries as type="unfinished"):
lupdate Source/RMG Source/RMG-Input Source/RMG-Audio \
        -ts Source/RMG/Translations/RMG_*.ts
```

Then open each `.ts` file in **Qt Linguist** (the GUI tool — much nicer
than editing XML by hand) and fill in the new/changed strings.

When done, rebuild RMG — `lrelease` runs automatically as part of the
build and regenerates the embedded `.qm` resources.

## How translations work across plugins (RMG-Input, RMG-Audio)

Translations are embedded into the main RMG binary as Qt resources
under `:/i18n/`. `MainWindow::loadTranslator()` installs the
`QTranslator` on `QCoreApplication`, which means **every plugin
loaded by RMG** (RMG-Input, RMG-Audio) inherits the same translator.

This is why the single `RMG_en.ts` / `RMG_ru.ts` file contains
contexts from all three targets (`MainWindow`, `ControllerWidget`,
`MainDialog`, etc.) — the translation file is shared across the
whole application.

The extraction scripts (`generate_en_ts.py`, `generate_ru_ts.py`)
scan `Source/RMG/`, `Source/RMG-Input/`, and `Source/RMG-Audio/` to
capture every translatable string in one pass.

## Translating C++ strings (not just .ui strings)

Strings defined in `.ui` files are picked up automatically because
Qt's `uic` generates `retranslateUi()` calls that use
`QApplication::translate("ClassName", "Source text")`. Strings set
directly in C++ code (e.g. `QFileDialog::getOpenFileName(this, tr("Open N64 ROM"), ...)`)
are also picked up — the extraction scripts look for `tr("...")` calls
and use the C++ class name as the context.

If you add a new translatable string in C++, wrap it in `tr()`:

```cpp
// Before (always English, regardless of selected language):
this->showErrorMessage("CoreInit() Failed", ...);

// After (translatable):
this->showErrorMessage(tr("CoreInit() Failed"), ...);
```

After wrapping new strings in `tr()`, re-run the generator script
(or `lupdate`) to add them to all `.ts` files.

## How runtime loading works

See `MainWindow::loadTranslator()` in
`Source/RMG/UserInterface/MainWindow.cpp`. It is called once during
`MainWindow::Init()`, *after* `configureTheme()` (so the dark palette
is already set) and *before* `setupUi()` (so that `retranslateUi()`
picks up the translations on the very first paint).

The translator reads the persisted `GUI_Language` setting:
- empty string -> use `QLocale::system()` (auto-detect from OS)
- non-empty    -> use `QLocale(stored_code)` (e.g. `QLocale("ru")`)

It then loads two QTranslator objects:
1. RMG's own translation from `:/i18n/RMG_<lang>.qm`
2. Qt's standard translation (button labels etc.) from
   `QLibraryInfo::path(QLibraryInfo::TranslationsPath)/qt_<lang>.qm`

Both translators stay installed for the lifetime of the application
(they are stored as `MainWindow` members). Changing the language in
Settings requires an application restart — this is intentional and
matches the behavior of the existing Theme setting.

## Utility scripts

| Script | Purpose |
|--------|---------|
| `generate_en_ts.py` | Regenerate `RMG_en.ts` from source code (the English reference). Run after adding/changing source strings. |
| `generate_ru_ts.py` | Regenerate `RMG_ru.ts` from source code, using a built-in Russian translation dictionary. Run after adding new source strings to keep the Russian file in sync. |
| `extract_strings.py` | Dump all (context, string) pairs to stdout — useful for debugging which strings the extractor sees. |
| `generate_skeleton_ts.py <code> [<lang>]` | Create a new skeleton `.ts` file for a new language, pre-filled with English source as `type="unfinished"`. |
