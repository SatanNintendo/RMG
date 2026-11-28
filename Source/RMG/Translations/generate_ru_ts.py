#!/usr/bin/env python3
"""
Generate a complete RMG_ru.ts file with ALL translatable strings
from RMG, RMG-Input, RMG-Audio, and integrated Qt video plugins.

This script:
1. Parses all .ui files to extract (context, string) pairs
2. Parses C++ files for tr("...") and QCoreApplication::translate() calls
3. Translates the human-readable parts of AboutDialog rich text without
   changing markup, links, project names, or placeholders
4. Looks up Russian translations from a built-in dictionary
5. Outputs a .ts file ready for lrelease and reports anything unfinished

The translation dictionary covers all UI strings found in the codebase.
Strings not in the dictionary are output with type="unfinished" and
the source text as a starting point.
"""

import os
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from collections import defaultdict
from xml.sax.saxutils import escape as xml_escape


# =========================================================================
# Russian translation dictionary.
# Key = English source string (exactly as it appears in the .ui/.cpp file)
# Value = Russian translation
#
# Strings NOT in this dictionary will be output as type="unfinished".
# Ampersands (&) in menu/action text are mnemonic markers and are
# preserved in the translation.
# =========================================================================
TRANSLATIONS = {
    # === MainWindow ===
    "Rosalie's Mupen GUI (VERSION)": "Rosalie's Mupen GUI (VERSION)",
    "S&ystem": "С&истема",
    "&Reset": "&Сброс",
    "&Current Save State": "&Текущий слот сохранения",
    "Speed &Factor": "Множитель &скорости",
    "Setti&ngs": "Настр&ойки",
    "View": "Вид",
    "He&lp": "Спр&авка",
    "Netplay": "Сетевая игра",
    "toolBar": "Панель инструментов",
    "&Start ROM": "&Запустить ROM",
    "Start Co&mbo": "Запуск-ко&мбо",
    "S&hutdown": "В&ыключить",
    "&Soft Reset": "&Сброс",
    "&Hard Reset": "&Жёсткая перезагрузка",
    "&Pause": "&Пауза",
    "Scree&nshot": "С&криншот",
    "&Limit FPS": "&Ограничить FPS",
    "Sa&ve State": "Со&хранить состояние",
    "Save As...": "Сохранить как...",
    "L&oad State": "З&агрузить состояние",
    "Loa&d...": "Загрузит&ь...",
    "&Slot 0": "Слот &0",
    "Slot &1": "Слот &1",
    "Slot &2": "Слот &2",
    "Slot &3": "Слот &3",
    "Slot &4": "Слот &4",
    "Slot &5": "Слот &5",
    "Slot &6": "Слот &6",
    "Slot &7": "Слот &7",
    "Slot &8": "Слот &8",
    "Slot &9": "Слот &9",
    "Cheats...": "Чит-коды...",
    "&GS Button": "Кнопка &GameShark",
    "&Exit": "В&ыход",
    "&Graphics": "&Графика",
    "&Input": "&Ввод",
    "&RSP": "&RSP",
    "&Audio": "&Звук",
    "&Settings": "&Настройки",
    "&Fullscreen": "Во &весь экран",
    "&Github Repository": "Репозиторий &GitHub",
    "&About RMG": "&О программе RMG",
    "&Toolbar": "&Панель инструментов",
    "&Status Bar": "Строка &состояния",
    "&Game List": "Список &игр",
    "Ga&me Grid": "Сетка &игр",
    "&Refresh ROMs": "&Обновить список ROM",
    "&Log": "&Журнал",
    "&Clear ROM Cache": "О&чистить кэш ROM",
    "&Uniform Size (Grid View)": "&Одинаковый размер (сетка)",
    "&25%": "&25%",
    "&50%": "&50%",
    "&100%": "&100%",
    "125%": "125%",
    "150%": "150%",
    "2&00%": "2&00%",
    "&75%": "&75%",
    "175%": "175%",
    "225%": "225%",
    "250%": "250%",
    "275%": "275%",
    "&300%": "&300%",
    "&Check For Updates": "&Проверить обновления",
    "&Create Session": "&Создать сессию",
    "Browse Sessions": "Просмотр сессий",
    "View Session": "Открыть сессию",
    "Search": "Поиск",
    # tr() strings from MainWindow.cpp
    "Open N64 ROM or 64DD Disk": "Открыть ROM N64 или диск 64DD",
    "Open N64 ROM": "Открыть ROM N64",
    "Open 64DD Disk": "Открыть диск 64DD",
    "Save State": "Сохранить состояние",
    "Save State (*.state);;Project64 Save State (*.pj);;All Files (*)": "Сохранение состояния (*.state);;Project64 (*.pj);;Все файлы (*)",
    "Open Save State": "Открыть сохранение состояния",
    "Save State (*.dat *.state *.st* *.pj*);;All Files (*)": "Сохранения (*.dat *.state *.st* *.pj*);;Все файлы (*)",
    "Select ROM Directory": "Выбрать папку с ROM",

    # === SettingsDialog ===
    "Settings": "Настройки",
    "Interface": "Интерфейс",
    "Plugins": "Плагины",
    "Hotkeys": "Горячие клавиши",
    "Core": "Ядро",
    "Game": "Игра",
    "Directories": "Папки",
    "64DD": "64DD",
    "General": "Общие",
    "Emulation": "Эмуляция",
    "ROM Browser": "Браузер ROM",
    "Log": "Журнал",
    "OSD": "OSD (экранные уведомления)",
    "Theme": "Тема",
    "Native": "Системная",
    "Fusion": "Fusion",
    "Fusion Dark": "Fusion (тёмная)",
    "Icon theme": "Тема значков",
    "Automatic": "Автоматически",
    "White": "Белая",
    "Black": "Чёрная",
    "Language": "Язык",
    "System Default": "По умолчанию (системный)",
    'Select the interface language. Choose "System Default" to use the operating system locale. Changes will be applied on next application run.': 'Выберите язык интерфейса. Выберите «По умолчанию (системный)», чтобы использовать язык операционной системы. Изменения вступят в силу при следующем запуске приложения.',
    "Check for updates": "Проверять обновления",
    "Changes will be applied on next application run": "Изменения вступят в силу при следующем запуске приложения",
    "Changes will be applied on next emulation run": "Изменения вступят в силу при следующем запуске эмуляции",
    "Hide cursor during emulation": "Скрывать курсор во время эмуляции",
    "Hide cursor during fullscreen emulation": "Скрывать курсор при полноэкранной эмуляции",
    "Pause emulation on focus loss": "Пауза эмуляции при потере фокуса",
    "Resume emulation on focus gain": "Продолжать эмуляцию при возврате фокуса",
    "Automatically switch to fullscreen on emulation start": "Автоматически переходить в полный экран при запуске эмуляции",
    "Ask for confirmation during drag and drop": "Запрашивать подтверждение при перетаскивании",
    "Ask for confirmation on exit": "Запрашивать подтверждение при выходе",
    "Statusbar message duration": "Длительность сообщений в строке состояния",
    "OpenGL type": "Тип OpenGL",
    "OpenGL": "OpenGL",
    "OpenGL ES": "OpenGL ES",
    "Search sub-directories": "Искать во вложенных папках",
    "ROM search limit": "Максимум ROM при поиске",
    "Show verbose messages": "Показывать подробные сообщения",
    "Enable On-Screen Display": "Включить экранное уведомление (OSD)",
    "Location": "Расположение",
    "Top Left": "Сверху слева",
    "Top Right": "Сверху справа",
    "Bottom Left": "Снизу слева",
    "Bottom Right": "Снизу справа",
    "Horizontal padding": "Горизонтальный отступ",
    "Vertical padding": "Вертикальный отступ",
    "Duration": "Длительность",
    "Background color": "Цвет фона",
    "Text color": "Цвет текста",
    "Change": "Изменить",
    "The On-Screen Display will only work with OpenGL video plugins": "Экранные уведомления (OSD) работают только с OpenGL-плагинами видео",
    "Nickname": "Никнейм",
    "Server list URL": "URL списка серверов",
    "Video Plugin": "Видеоплагин",
    "Audio Plugin": "Звуковой плагин",
    "Input Plugin": "Плагин ввода",
    "Reality Signal Processor Plugin": "Плагин RSP (Reality Signal Processor)",
    "Screenshot Directory": "Папка скриншотов",
    "Save (State) Directory": "Папка сохранений (состояний)",
    "Save (SRAM) Directory": "Папка сохранений (SRAM)",
    "Override game specific settings": "Переопределять настройки игры",
    "Override core settings": "Переопределять настройки ядра",
    "Randomize PI/SI interrupt timing": "Случайные интервалы прерываний PI/SI",
    "CPU emulator": "Эмулятор ЦП",
    "Pure Interpreter": "Чистый интерпретатор",
    "Cached Interpreter": "Кэширующий интерпретатор",
    "Dynamic Recompiler": "Динамический рекомпилятор",
    "Counter Factor": "Коэффициент счётчика",
    "Memory Size": "Размер памяти",
    "SI DMA Duration": "Длительность SI DMA",
    "Save filename format": "Формат имени файла сохранения",
    "Disk save type": "Тип сохранения диска",
    "Video capture backend": "Бэкенд захвата видео",
    "Remove duplicate keybindings": "Удалять дубликаты назначений клавиш",
    "Save Type": "Тип сохранения",
    "Transfer Pak": "Transfer Pak",
    "Controller pak": "Controller Pak",
    "None": "Нет",
    "4 KB EEPROM": "4 КБ EEPROM",
    "16 KB EEPROM": "16 КБ EEPROM",
    "SRAM": "SRAM",
    "Flash RAM": "Flash RAM",
    "RAM Area Only": "Только область RAM",
    "Full Disk Copy": "Полная копия диска",
    "Good Name": "Название GoodN64",
    "Internal ROM Name": "Внутреннее название ROM",
    "&Use PIF ROM": "&Использовать PIF ROM",
    "NTSC PIF ROM": "PIF ROM (NTSC)",
    "PAL PIF ROM": "PIF ROM (PAL)",
    "Japanese Retail 64DD IPL ROM": "Японский розничный 64DD IPL ROM",
    "American Retail 64DD IPL ROM": "Американский розничный 64DD IPL ROM",
    "Development 64DD IPL ROM": "Разработческий 64DD IPL ROM",
    "Make sure to use a LLE RSP plugin (e.g. paraLLEl RSP) when using a LLE Video plugin (e.g. paraLLEl)": "При использовании LLE-плагина видео (например paraLLEl) убедитесь, что выбран LLE-плагин RSP (например paraLLEl RSP)",
    "Randomize PI/SI Interrupt Timing": "Случайные интервалы прерываний PI/SI",
    "CPU Emulator": "Эмулятор ЦП",
    "Overclocking Factor": "Коэффициент разгона",
    "Current Save State": "Текущий слот сохранения",
    "Speed Factor": "Множитель скорости",
    "System": "Система",
    "Audio": "Звук",
    "Start ROM": "Запустить ROM",
    "Start Combo": "Запустить комбо",
    "Shutdown": "Выключить",
    "Soft Reset": "Сброс",
    "Hard Reset": "Жёсткая перезагрузка",
    "Pause": "Пауза",
    "Capture Screenshot": "Сделать скриншот",
    "Full Screen": "Во весь экран",
    "Limit FPS": "Ограничить FPS",
    "Load State": "Загрузить состояние",
    "Load": "Загрузить",
    "Refresh ROM List": "Обновить список ROM",
    "Exit": "Выход",
    "Graphics": "Графика",
    "Input": "Ввод",
    "RSP": "RSP",
    "GS Button": "Кнопка GameShark",
    "Increase Volume": "Увеличить громкость",
    "Decrease Volume": "Уменьшить громкость",
    "Toggle Mute Volume": "Включить/выключить звук",
    "Slot 0": "Слот 0",
    "Slot 1": "Слот 1",
    "Slot 2": "Слот 2",
    "Slot 3": "Слот 3",
    "Slot 4": "Слот 4",
    "Slot 5": "Слот 5",
    "Slot 6": "Слот 6",
    "Slot 7": "Слот 7",
    "Slot 8": "Слот 8",
    "Slot 9": "Слот 9",
    "25%": "25%",
    "50%": "50%",
    "75%": "75%",
    "100%": "100%",
    "200%": "200%",
    "300%": "300%",
    "SDL3": "SDL3",
    "4 MB": "4 МБ",
    "8 MB": "8 МБ",
    "No": "Нет",
    "1": "1",
    "2": "2",
    "3": "3",
    "4": "4",
    "5": "5",
    "6": "6",
    # tr() strings from SettingsDialog.cpp
    "Select Screenshot Directory": "Выбрать папку скриншотов",
    "Select Save (State) Directory": "Выбрать папку сохранений (состояний)",
    "Select Save (SRAM) Directory": "Выбрать папку сохранений (SRAM)",
    "Open Japanese Retail 64DD IPL": "Открыть японский розничный 64DD IPL",
    "Open American Retail 64DD IPL": "Открыть американский розничный 64DD IPL",
    "Open Japanese Development 64DD IPL": "Открыть японский разработческий 64DD IPL",
    "Open NTSC PIF ROM": "Открыть PIF ROM (NTSC)",
    "Open PAL PIF ROM": "Открыть PIF ROM (PAL)",

    # === AboutDialog ===
    "About RMG": "О программе RMG",

    # === LogDialog ===

    # === RomInfoDialog ===
    "ROM Information": "Информация о ROM",
    "Name": "Название",
    "File Name": "Имя файла",
    "MD5": "MD5",
    "CRC1": "CRC1",
    "CRC2": "CRC2",
    "Game I.D.": "ID игры",
    "Game Region": "Регион игры",
    "Game Type": "Тип игры",
    "System Region": "Регион системы",

    # === RomBrowserEmptyWidget ===
    "<html><head/><body><p><span style=\" font-weight:700;\">No ROMs in supported formats were found.</span></p><p>Please select a directory with ROMs to begin.</p><p>ROMs in the following formats will be scanned and listed: </p></body></html>": "<html><head/><body><p><span style=\" font-weight:700;\">Не найдено ROM в поддерживаемых форматах.</span></p><p>Выберите папку с ROM для начала работы.</p><p>Будут просканированы и показаны ROM в следующих форматах: </p></body></html>",
    ".n64/.z64/.v64 (N64 Roms)": ".n64/.z64/.v64 (ROM N64)",
    ".ndd/.d64 (64DD Disks)": ".ndd/.d64 (диски 64DD)",
    ".zip/.7z (Archive)": ".zip/.7z (архив)",
    "Select ROM Directory...": "Выбрать папку с ROM...",
    "Refresh": "Обновить",

    # === RomBrowserWidget (tr() calls) ===
    "Open Cover Image": "Открыть обложку",
    "Cover Image (*.png *.jpeg *.jpg)": "Обложка (*.png *.jpeg *.jpg)",

    # === AddCheatDialog ===
    "Add Cheat": "Добавить чит",
    "Author": "Автор",
    "Code:": "Код:",
    "<address> <value>": "<адрес> <значение>",
    "Options:": "Опции:",
    "<value> <label>": "<значение> <метка>",
    "Notes:": "Заметки:",

    # === CheatsDialog ===
    "Cheats": "Чит-коды",
    "Add": "Добавить",
    "Edit": "Изменить",
    "Remove": "Удалить",
    "Notes": "Заметки",

    # === ChooseCheatOptionDialog ===
    "Choose Cheat Option": "Выбрать опцию чита",
    "Cheat Options": "Опции чита",

    # === CreateNetplaySessionDialog ===
    "Create Netplay Session": "Создать сессию сетевой игры",
    "Server": "Сервер",
    "Server ping": "Пинг сервера",
    "Session name": "Название сессии",
    "Password": "Пароль",

    # === NetplaySessionBrowserDialog ===
    "Netplay Session Browser": "Браузер сессий сетевой игры",

    # === NetplayCommon ===
    "Servers have not been configured": "Список серверов не настроен",
    "You can configure the server list in the netplay tab of the settings dialog": "Настроить список серверов можно на вкладке «Сетевая игра» в окне настроек.",

    # === NetplaySessionDialog ===
    "Netplay Session": "Сессия сетевой игры",
    "Game name": "Название игры",
    "Chat": "Чат",
    "Send": "Отправить",
    "Players": "Игроки",

    # === NetplaySessionPasswordDialog ===
    "Enter Password": "Введите пароль",
    "Password:": "Пароль:",

    # === DownloadUpdateDialog ===
    "Downloading Update": "Загрузка обновления",
    "Downloading ...": "Загрузка...",

    # === InstallUpdateDialog ===
    "Installing Update": "Установка обновления",
    "Installing...": "Установка...",

    # === UpdateDialog ===
    "A new version is available": "Доступна новая версия",
    "Don't check for updates again": "Больше не проверять обновления",

    # === CreateNetplaySessionEmptyWidget ===
    "<html><head/><body><p><span style=\" font-weight:700;\">No ROMs were found.</span></p><p>You can configure the ROM browser directory or refresh the ROM browser.</p></body></html>": "<html><head/><body><p><span style=\" font-weight:700;\">ROM не найдены.</span></p><p>Вы можете настроить папку с ROM или обновить браузер ROM.</p></body></html>",

    # === NetplaySessionBrowserEmptyWidget ===
    "<html><head/><body><p><span style=\" font-weight:700;\">No sessions were found.</span></p><p>You can create your own session or refresh the session list.</p></body></html>": "<html><head/><body><p><span style=\" font-weight:700;\">Сессии не найдены.</span></p><p>Вы можете создать свою сессию или обновить список сессий.</p></body></html>",

    # === RMG-Input: MainDialog ===
    "Rosalie's Mupen GUI - Input Plugin": "Rosalie's Mupen GUI — плагин ввода",

    # === Integrated third-party plugins (RMG SettingsDialog) ===
    "AziAudio-Plus HLE Audio (Mupen64Plus)": "AziAudio-Plus HLE — звуковой плагин (Mupen64Plus)",
    "SoftRDP-Mupen64Plus": "SoftRDP-Mupen64Plus — видеоплагин",
    "Player 1": "Игрок 1",
    "Player 2": "Игрок 2",
    "Player 3": "Игрок 3",
    "Player 4": "Игрок 4",

    # === RMG-Input: ControllerWidget ===
    "ControllerWidget": "Виджет контроллера",
    "Profile": "Профиль",
    "Input Device": "Устройство ввода",
    "Deadzone: 25%": "Мёртвая зона: 25%",
    "Digital Pad": "Крестовина",
    "Up:": "Вверх:",
    "Down:": "Вниз:",
    "Left:": "Влево:",
    "Right:": "Вправо:",
    "+": "+",
    "-": "-",
    "Analog Stick": "Аналоговый стик",
    "Analog Stick Sensitivity: 100%": "Чувствительность аналогового стика: 100%",
    "L-Shoulder: ": "Кнопка L: ",
    "R-Shoulder: ": "Кнопка R: ",
    "Z-Trigger: ": "Z-триггер: ",
    "Start: ": "Старт: ",
    "C-Buttons": "Кнопки C",
    "B:": "B:",
    "A:": "A:",
    "Auto-Configure": "Автонастройка",
    "Reset": "Сброс",
    "Options": "Опции",
    "Hotkeys": "Горячие клавиши",

    # === RMG-Input: OptionsDialog ===
    "Options": "Опции",
    "Controller": "Контроллер",
    "Controller Pak": "Controller Pak",
    "Memory": "Память",
    "Rumble": "Вибрация",
    "Transfer": "Переносной",
    "Test Rumble": "Тест вибрации",
    "Transfer Pak": "Transfer Pak",
    "Gameboy ROM": "ROM Gameboy",
    "Gameboy save": "Сохранение Gameboy",
    "User Interface": "Пользовательский интерфейс",
    "Remove duplicate mappings": "Удалять дубликаты назначений",
    "Filter events based on joystick type for buttons": "Фильтровать события по типу джойстика для кнопок",
    "Filter events based on joystick type for axis": "Фильтровать события по типу джойстика для осей",
    "Advanced": "Дополнительно",
    "SDL controller mode": "Режим SDL-контроллера",
    "Joystick": "Джойстик",
    "Gamepad": "Геймпад",

    # === RMG-Input: HotkeysDialog ===
    "These hotkeys are only for controllers": "Эти горячие клавиши только для контроллеров",
    "Increase Slot": "Следующий слот",
    "Decrease Slot": "Предыдущий слот",
    "Switch to memory pak": "Переключить на Memory Pak",
    "Switch to rumble pak": "Переключить на Rumble Pak",
    "Remove pak": "Извлечь Pak",

    # === RMG-Audio: MainDialog ===
    "Rosalie's Mupen GUI - Audio Plugin": "Rosalie's Mupen GUI — звуковой плагин",
    "Volume": "Громкость",
    "Mute": "Без звука",
    "Default frequency": "Частота по умолчанию",
    "Resampler": "Ресэмплер",
    "trivial": "trivial",
    "Swap left and right channel": "Поменять местами левый и правый каналы",
    "Synchronize": "Синхронизировать",
    "Primary Buffer Size": "Размер первичного буфера",
    "Primary Buffer Target": "Цель первичного буфера",
    "Secondary Buffer Size": "Размер вторичного буфера",
    "Simple Backend": "Простой бэкенд",

    # Remaining strings that were unfinished
    "Analog Stick Sensitivity: ": "Чувствительность аналогового стика: ",
    "L-Shoulder:": "Кнопка L:",
    "R-Shoulder:": "Кнопка R:",
    "Start:": "Старт:",
    "Z-Trigger:": "Z-триггер:",
    "Form": "Форма",
    "TextLabel": "Текстовая метка",
    "speex-fixed-0": "speex-fixed-0",
    "speex-fixed-1": "speex-fixed-1",
    "speex-fixed-2": "speex-fixed-2",
    "speex-fixed-3": "speex-fixed-3",
    "speex-fixed-4": "speex-fixed-4",
    "speex-fixed-5": "speex-fixed-5",
    "speex-fixed-6": "speex-fixed-6",
    "speex-fixed-7": "speex-fixed-7",
    "speex-fixed-8": "speex-fixed-8",
    "speex-fixed-9": "speex-fixed-9",
    "speex-fixed-10": "speex-fixed-10",
    "src-linear": "src-linear",
    "src-sinc-best-quality": "src-sinc-best-quality",
    "src-sinc-fastest": "src-sinc-fastest",
    "src-sinc-medium-quality": "src-sinc-medium-quality",
    "src-zero-order-hold": "src-zero-order-hold",
    "Open Gameboy ROM": "Открыть ROM Gameboy",
    "Open Gameboy Save": "Открыть сохранение Gameboy",
    "Save As": "Сохранить как",
    "Yes": "Да",
    "px": "пикс",
    "seconds": "секунд",
    "v0.2.2 Available": "v0.2.2 доступна",

    # === Newly wrapped strings (tr() added in source) ===
    # RomBrowserWidget.cpp — column headers
    "Name": "Название",
    "Internal Name": "Внутреннее название",
    "MD5": "MD5",
    "Format": "Формат",
    "File Name": "Имя файла",
    "File Ext.": "Расширение",
    "File Size": "Размер",
    "I.D.": "ID",
    "Region": "Регион",
    "Game Format": "Формат игры",
    "File Extension": "Расширение файла",
    "Game I.D.": "ID игры",
    "Game Region": "Регион игры",
    # RomBrowserWidget.cpp — context menu actions
    "Play Game": "Запустить игру",
    "Play Game with Disk": "Запустить игру с диском",
    "Play Game with State": "Запустить игру с сохранением",
    "Refresh ROM List": "Обновить список ROM",
    "Open ROM Directory": "Открыть папку с ROM",
    "Change ROM Directory...": "Сменить папку с ROM...",
    "ROM Information": "Информация о ROM",
    "Edit Game Settings": "Изменить настройки игры",
    "Edit Game Input Settings": "Изменить настройки ввода игры",
    "Edit Cheats": "Изменить чит-коды",
    "Reset Column Sizes": "Сбросить размеры столбцов",
    "Show/Hide Columns": "Показать/скрыть столбцы",
    "Set Cover Image...": "Установить обложку...",
    "Remove Cover Image": "Удалить обложку",
    "Play Game with Cartridge...": "Запустить игру с картриджем...",
    "Play Game with Disk...": "Запустить игру с диском...",
    "Change Cover Image...": "Изменить обложку...",
    "Browse...": "Обзор...",
    "Disk": "Диск",
    "Cartridge": "Картридж",
    "%1 MB": "%1 МБ",
    "Slot %1 - ": "Слот %1 — ",
    "Slot %1": "Слот %1",
    # RomBrowserLoadingWidget.cpp
    "Loading": "Загрузка",
    # RomBrowserSearchWidget.cpp
    "Search games...": "Поиск игр...",
    "Close": "Закрыть",
    # NetplaySessionBrowserDialog.cpp
    "Join": "Присоединиться",
    "Calculating...": "Вычисление...",
    # NetplaySessionBrowserWidget.cpp — column headers
    "Game": "Игра",
    "Game MD5": "MD5 игры",
    "Password?": "Пароль?",
    "No": "Нет",
    # NetplaySessionBrowserLoadingWidget / CreateNetplaySessionWidget
    "Creating server": "Создание сервера",
    # CreateNetplaySessionDialog.cpp
    "Create": "Создать",
    # NetplaySessionDialog.cpp
    "Start": "Начать",
    "Cheats": "Чит-коды",
    # AddCheatDialog.cpp
    "Edit Cheat": "Изменить чит-код",
    # UpdateDialog.cpp
    "Update": "Обновить",
    "%1 Available": "%1 доступна",
    # RomInfoDialog.cpp — Disk/Cartridge already above
    # KeybindButton.cpp
    "Press key... [%1]": "Нажмите клавишу... [%1]",
    # ControllerWidget.cpp — dynamic slider titles
    "Deadzone: %1%": "Мёртвая зона: %1%",
    "Error": "Ошибка",
    "Are you sure you want to clear the main profile?": "Вы уверены, что хотите очистить основной профиль?",
    "Controller doesn't support rumble": "Контроллер не поддерживает вибрацию",
    # MainWindow.cpp
    "Information": "Информация",
    # RMG-Input-GCA MainDialog.cpp
    "Sensitivity: %1%": "Чувствительность: %1%",
    "Trigger treshold: %1%": "Порог триггера: %1%",
    "C button treshold: %1%": "Порог кнопок C: %1%",
    # RMG-Input-GCA MainDialog.ui — static defaults and labels
    "Deadzone: 100%": "Мёртвая зона: 100%",
    "Sensitivity: 100%": "Чувствительность: 100%",
    "Trigger treshold: 100%": "Порог триггера: 100%",
    "C button treshold: 100%": "Порог кнопок C: 100%",
    "Buttons": "Кнопки",
    "Swap Z and L": "Поменять местами Z и L",
    "Rosalie's Mupen GUI - GameCube Adapter Input Plugin": "Rosalie's Mupen GUI — плагин ввода через адаптер GameCube",
    # MainWindow.cpp — file dialog filters (already used tr() but missing from dict)
    "N64 ROMs & Disks (*.n64 *.z64 *.v64 *.ndd *.d64 *.zip *.7z)": "ROM и диски N64 (*.n64 *.z64 *.v64 *.ndd *.d64 *.zip *.7z)",
    "N64 ROMs (*.n64 *.z64 *.v64 *.zip *.7z)": "ROM N64 (*.n64 *.z64 *.v64 *.zip *.7z)",
    "N64DD Disk Image (*.ndd *.d64 *.zip *.7z)": "Образ диска N64DD (*.ndd *.d64 *.zip *.7z)",
    "IPL ROMs (*.n64)": "IPL ROM (*.n64)",
    "PIF ROMs (*.rom)": "PIF ROM (*.rom)",
    # NetplaySessionBrowserDialog.cpp
    "Open %1": "Открыть %1",

    # === Newly wrapped strings (tr() added to previously-hardcoded user-visible text) ===
    # These strings were hardcoded in C++ source and are now wrapped in tr() so they
    # can be translated. Each entry below mirrors the new tr() call added to the code.

    # --- SettingsDialog.cpp ---
    "**Use Core Plugin Settings**": "**Использовать настройки плагина ядра**",
    " (not found)": " (не найден)",
    "Failed to open file": "Не удалось открыть файл",
    "QFile::open() Failed": "Сбой QFile::open()",
    "MD5 hashing failed": "Не удалось вычислить MD5-хеш",
    "QCryptographicHash::addData() Failed": "Сбой QCryptographicHash::addData()",
    "MD5 mismatch": "Несовпадение MD5",
    "Expected file with MD5: \"%1\"": "Ожидаемый MD5 файла: «%1»",
    "CoreApplyPluginSettings() Failed": "Сбой CoreApplyPluginSettings()",

    # --- ControllerWidget.cpp (RMG-Input) ---
    "Create New Profile": "Создать новый профиль",
    "New profile name:": "Имя нового профиля:",
    "Profile name cannot contain ';', '[' or ']'!": "Имя профиля не может содержать «;», «[» или «]»!",
    "Profile with the same name already exists!": "Профиль с таким именем уже существует!",

    # --- MainWindow.cpp ---
    "CoreInit() Failed": "Сбой CoreInit()",
    "SetupVidExt() Failed": "Сбой SetupVidExt()",
    "CoreCallbacks::Init() Failed": "Сбой CoreCallbacks::Init()",
    "Are you sure you want to exit RMG?": "Вы уверены, что хотите выйти из RMG?",
    "Don't ask for confirmation again": "Больше не запрашивать подтверждение",
    "Are you sure you want to launch the drag & dropped ROM?": "Вы уверены, что хотите запустить перетащенный ROM?",
    "You're already on the latest version": "У вас уже установлена последняя версия",
    "Failed to check for updates": "Не удалось проверить обновления",
    "EmulationThread::run Failed": "Сбой EmulationThread::run",
    "Cannot start emulation when netplay session is active": "Невозможно запустить эмуляцию во время активной сессии сетевой игры",
    "Cannot start emulation when emulation is already running or being started": "Невозможно запустить эмуляцию, когда эмуляция уже запущена или запускается",
    "CoreArePluginsReady() Failed": "Сбой CoreArePluginsReady()",
    "CoreStopEmulation() Failed": "Сбой CoreStopEmulation()",
    "CoreResetEmulation() Failed": "Сбой CoreResetEmulation()",
    "CoreTakeScreenshot() Failed": "Сбой CoreTakeScreenshot()",
    "CoreSetSpeedLimiterState() Failed": "Сбой CoreSetSpeedLimiterState()",
    "CoreSetSpeedFactor() Failed": "Сбой CoreSetSpeedFactor()",
    "CoreSaveState() Failed": "Сбой CoreSaveState()",
    "CoreLoadSaveState() Failed": "Сбой CoreLoadSaveState()",
    "CoreSetSaveStateSlot() Failed": "Сбой CoreSetSaveStateSlot()",
    "CorePressGamesharkButton() Failed": "Сбой CorePressGamesharkButton()",
    "CoreToggleFullscreen() Failed": "Сбой CoreToggleFullscreen()",
    "CoreClearRomHeaderAndSettingsCache() Failed": "Сбой CoreClearRomHeaderAndSettingsCache()",
    "CoreToggleMuteVolume() Failed": "Сбой CoreToggleMuteVolume()",
    "CoreGetCachedRomHeaderAndSettings() Failed": "Сбой CoreGetCachedRomHeaderAndSettings()",
    "CorePluginsOpenROMConfig() Failed": "Сбой CorePluginsOpenROMConfig()",
    "Core Error": "Ошибка ядра",
    "Failed to load save state": "Не удалось загрузить сохранение состояния",
    "OpenGL": "OpenGL",
    "OpenGL ES": "OpenGL ES",
    "Vulkan": "Vulkan",

    # --- DownloadUpdateDialog.cpp ---
    "Downloading %1...": "Загрузка %1...",
    "Failed to download update file": "Не удалось загрузить файл обновления",
    "Failed to create temporary directory": "Не удалось создать временную папку",
    "APPIMAGE variable is empty or invalid": "Переменная APPIMAGE пуста или недействительна",
    "std::rename() Failed": "Сбой std::rename()",

    # --- InstallUpdateDialog.cpp ---
    "Executing %1...": "Выполнение %1...",
    "Extracting %1...": "Распаковка %1...",
    "Executing update script...": "Выполнение скрипта обновления...",
    "QDir::mkdir() Failed": "Сбой QDir::mkdir()",
    "CoreUnzip() Failed": "Сбой CoreUnzip()",

    # --- UpdateDialog.cpp ---
    "Failed to find update file": "Не удалось найти файл обновления",

    # --- NetplaySessionBrowserDialog.cpp ---
    "Server Error": "Ошибка сервера",
    "Failed to open server list json: %1": "Не удалось открыть JSON списка серверов: %1",
    "Failed to retrieve server list json: %1": "Не удалось получить JSON списка серверов: %1",
    "Connection Failed": "Сбой подключения",
    "Incorrect ROM Selected": "Выбран некорректный ROM",
    "Expected MD5: %1\n": "Ожидаемый MD5: %1\n",
    "Received MD5: %1": "Полученный MD5: %1",
    "RSP Plugin Mismatch": "Несовпадение плагина RSP",
    "Expected RSP Plugin: %1\n": "Ожидаемый плагин RSP: %1\n",
    "Current  RSP Plugin: %1": "Текущий    плагин RSP: %1",
    "GFX Plugin Mismatch": "Несовпадение плагина видео",
    "Expected GFX Plugin: %1\n": "Ожидаемый плагин видео: %1\n",
    "Current  GFX Plugin: %1": "Текущий    плагин видео: %1",

    # --- NetplaySessionDialog.cpp ---
    "Failed to parse cheats json: %1": "Не удалось разобрать JSON чит-кодов: %1",
    "CheatsCommon::ParseCheatJson() Failed": "Сбой CheatsCommon::ParseCheatJson()",
    "CoreSetNetplayCheats() Failed": "Сбой CoreSetNetplayCheats()",

    # --- AddCheatDialog.cpp ---
    "CoreParseCheat() Failed": "Сбой CoreParseCheat()",
    "CoreAddCheat() Failed": "Сбой CoreAddCheat()",
    "CoreUpdateCheat() Failed": "Сбой CoreUpdateCheat()",
    "Validating Cheat Failed": "Сбой проверки чит-кода",

    # --- CheatsDialog.cpp ---
    "CoreGetCurrentCheats() Failed": "Сбой CoreGetCurrentCheats()",
    "CoreRemoveCheat() Failed": "Сбой CoreRemoveCheat()",
    "CoreApplyCheats() Failed": "Сбой CoreApplyCheats()",

    # --- MainWindow.cpp — On-Screen Display (OSD) messages ---
    # These are shown as overlay text during emulation via ImGui.
    "Saved state to slot: %1": "Сохранено состояние в слот: %1",
    "Saved state to: %1": "Сохранено состояние в: %1",
    "State loaded from slot: %1": "Состояние загружено из слота: %1",
    "State loaded from: %1": "Состояние загружено из: %1",
    "Selected save slot: %1": "Выбран слот сохранения: %1",
    " [N/A]": " [н/д]",
    "Playback speed: %1%": "Скорость воспроизведения: %1%",
    "Volume: %1%": "Громкость: %1%",
    "Volume unmuted": "Звук включён",
    "Volume muted": "Звук выключен",
    "Failed to load save state.": "Не удалось загрузить сохранение состояния.",
    "Loaded save state.": "Сохранение состояния загружено.",
    "Failed to save state.": "Не удалось сохранить состояние.",
    "Saved state.": "Состояние сохранено.",
    "Failed to capture screenshot.": "Не удалось сделать снимок экрана.",
    "Captured screenshot.": "Снимок экрана сделан.",

    # === paraLLEl-RDP plugin (MainDialog.ui) ===
    " px": " пикс",
    "1/2": "1/2",
    "1/4": "1/4",
    "1/8": "1/8",
    "1024 x 768": "1024 x 768",
    "1152 x 864": "1152 x 864",
    "1280 x 1024": "1280 x 1024",
    "1280 x 960": "1280 x 960",
    "1440 x 1080": "1440 x 1080",
    "1600 x 1024": "1600 x 1024",
    "1600 x 1200": "1600 x 1200",
    "2x": "2x",
    "4x": "4x",
    "320 x 240": "320 x 240",
    "400 x 300": "400 x 300",
    "480 x 360": "480 x 360",
    "640 x 480": "640 x 480",
    "800 x 600": "800 x 600",
    "960 x 720": "960 x 720",
    "8x (hi-end GPUs only)": "8x (только для мощных видеокарт)",
    "Bilinear filter": "Билинейный фильтр",
    "Bob": "Bob",
    "Deinterlacer": "Деинтерлейсер",
    "Downscaling of upscaled buffer": "Уменьшение увеличенного буфера",
    "Force widescreen": "Принудительный широкоэкранный режим",
    "Integer scaling": "Целочисленное масштабирование",
    "Low latency mode (exclusive fullscreen)": "Режим низкой задержки (эксклюзивный полный экран)",
    "N64 resolution upscale": "Апскейл разрешения N64",
    "Native textrects": "Нативные textrects",
    "Native texture LOD": "Нативный LOD текстур",
    "Overscan": "Оверскан",
    "Reduces input lag by ~1 frame using exclusive fullscreen and a 2-image swapchain. NVIDIA/AMD only (not Intel). On Windows also set GRANITE_EXCLUSIVE_FULL_SCREEN=1 environment variable before launching the emulator.": "Снижает входную задержку примерно на 1 кадр за счёт эксклюзивного полноэкранного режима и цепочки из 2 буферов. Только NVIDIA/AMD (не Intel). На Windows также установите переменную окружения GRANITE_EXCLUSIVE_FULL_SCREEN=1 перед запуском эмулятора.",
    "Screen size": "Размер экрана",
    "Supersampled RDRAM reads": "Суперсэмплинг при чтении RDRAM",
    "Supersampled dither": "Суперсэмплинг-дизеринг",
    "VI AA": "VI: сглаживание",
    "VI bilinear filtering": "VI: билинейная фильтрация",
    "VI de-dithering": "VI: де-дизеринг",
    "VI divot filter": "VI: фильтр divot",
    "VI gamma dither": "VI: гамма-дизеринг",
    "Vertical stretch": "Вертикальное растяжение",
    "Vertical sync": "Вертикальная синхронизация",
    "Video": "Видео",
    "Weave": "Weave",
    "paraLLEl-RDP": "paraLLEl-RDP",

    # === Angrylion-Plus plugin (MainDialog.ui) ===
    "16:9 widescreen": "Широкоэкранный режим 16:9",
    "Angrylion-Plus": "Angrylion-Plus",
    "Bilinear": "Билинейный",
    "Compatibility": "Совместимость",
    "Coverage": "Покрытие",
    "Depth": "Глубина",
    "Deinterlacing": "Деинтерлейсинг",
    "Only applies in Filtered VI mode with interlaced (SERRATE) output.": "Применяется только в фильтрованном режиме VI при чересстрочном выводе (SERRATE).",
    "Off: native VI output. Bob: vertical interpolation of the current field. Adaptive: per-pixel blend between the previous field (full detail where the picture is static) and Bob (where it moves).": "Выкл.: исходный вывод VI. Боб: вертикальная интерполяция текущего поля. Адаптивный: попиксельное смешивание с предыдущим полем (полная детализация на статичных участках) и режимом «Боб» (на движущихся участках).",
    "Off": "Выкл.",
    "Bob": "Боб",
    "Adaptive": "Адаптивный",
    "Enable Vsync": "Включить Vsync",
    "Enable busyloop": "Включить busyloop",
    "Fast": "Быстрый",
    "Filtered": "С фильтрацией",
    "Hide overscan": "Скрывать оверскан",
    "Hybrid": "Гибридный",
    "Integer pixel scaling": "Целочисленное масштабирование пикселей",
    "Interpolation": "Интерполяция",
    "Moderate": "Умеренный",
    "Multithreaded rendering": "Многопоточный рендеринг",
    "Nearest-neighbor": "Метод ближайшего соседа",
    "Output Mode": "Режим вывода",
    "Slow": "Медленный",
    "Threads (0 = auto)": "Потоки (0 = авто)",
    "Unfiltered": "Без фильтрации",
}

# Messages of the mupen64plus core shown in the status bar, see
# Source/RMG/Utilities/CoreMessageTranslator.cpp (context "CoreMessage").
# %1, %2, ... are the printf arguments of the core message in order.
TRANSLATIONS.update({
    "Emulation continued.": "Эмуляция возобновлена.",
    "Emulation paused.": "Эмуляция приостановлена.",
    "Stopping emulation.": "Эмуляция остановлена.",
    "Rom closed.": "ROM закрыт.",
    "Disk closed.": "Диск закрыт.",
    "Speed limiter enabled": "Ограничитель скорости включён",
    "Speed limiter disabled": "Ограничитель скорости выключен",
    "Playback speed: %d%%": "Скорость воспроизведения: %1%",
    "Selected state slot: %d": "Выбран слот сохранения: %1",
    "Saved state to: %s": "Состояние сохранено: %1",
    "State loaded from: %s": "Состояние загружено: %1",
    "Captured screenshot for frame %i.": "Сделан скриншот кадра %1.",
    "No Mupen64Plus/PJ64 state file found for slot %i": "Файл состояния Mupen64Plus/PJ64 для слота %1 не найден",
    "Could not open state file: %s": "Не удалось открыть файл состояния: %1",
    "Could not open state file %s": "Не удалось открыть файл состояния %1",
    "Could not read from state file %s": "Не удалось прочитать файл состояния %1",
    "Could not read header from state file %s": "Не удалось прочитать заголовок файла состояния %1",
    "Could not write data to state file: %s": "Не удалось записать данные в файл состояния: %1",
    "Could not read Mupen64Plus savestate 1.0 data from %s": "Не удалось прочитать данные сохранения Mupen64Plus 1.0 из %1",
    "Could not read Mupen64Plus savestate 1.1 data from %s": "Не удалось прочитать данные сохранения Mupen64Plus 1.1 из %1",
    "Could not read Mupen64Plus savestate 1.2+ data from %s": "Не удалось прочитать данные сохранения Mupen64Plus 1.2+ из %1",
    "Could not create PJ64 state file: %s": "Не удалось создать файл состояния PJ64: %1",
    "Could not read header from Project64 savestate %s": "Не удалось прочитать заголовок сохранения Project64 %1",
    "Could not read savestate data from Project64 savestate %s": "Не удалось прочитать данные из сохранения Project64 %1",
    "Couldn't write data to Project64 state file %s.": "Не удалось записать данные в файл состояния Project64 %1.",
    "Failed to open savestate file %s": "Не удалось открыть файл сохранения %1",
    "Unknown state file type %s": "Неизвестный тип файла состояния %1",
    "Zip error. Could not open state file: %s": "Ошибка ZIP. Не удалось открыть файл состояния: %1",
    "Zip error. Could not create state file: %s": "Ошибка ZIP. Не удалось создать файл состояния: %1",
    "Insufficient memory to load state.": "Недостаточно памяти для загрузки состояния.",
    "Insufficient memory to save state.": "Недостаточно памяти для сохранения состояния.",
    "Invalid Project64 state RDRAM size.": "Недопустимый размер RDRAM в состоянии Project64.",
    "Invalid event queue in savestate: %s": "Недопустимая очередь событий в сохранении: %1",
    "State ROM MD5 does not match current ROM.": "MD5 ROM в состоянии не совпадает с текущим ROM.",
    "State ROM header does not match current ROM.": "Заголовок ROM в состоянии не совпадает с текущим ROM.",
    "State file: %s is not a valid Mupen64plus savestate.": "Файл состояния %1 не является допустимым сохранением Mupen64plus.",
    "State file: %s is not a valid Project64 savestate. Unrecognized file format.": "Файл состояния %1 не является допустимым сохранением Project64. Неизвестный формат файла.",
    "State version (%08x) isn't compatible. Please update Mupen64Plus.": "Версия состояния (%1) несовместима. Обновите Mupen64Plus.",
    "Controller %u pak changed to %s": "Аксессуар контроллера %1 изменён на %2",
    "Removing pak from controller %u": "Извлечение аксессуара из контроллера %1",
    "Game controller %u (%s) has a %s plugged in": "К игровому контроллеру %1 (%2) подключён аксессуар: %3",
    "Game controller %u (%s) has nothing plugged in": "К игровому контроллеру %1 (%2) ничего не подключено",
    "Inserting GB cart %s into transferpak %u": "Установка картриджа GB %1 в Transfer Pak %2",
    "Removing GB cart from transferpak %u": "Извлечение картриджа GB из Transfer Pak %1",
    "Providing default RAM content": "Используется содержимое RAM по умолчанию",
    "Using NTSC PIF ROM": "Используется NTSC PIF ROM",
    "Using PAL PIF ROM": "Используется PAL PIF ROM",
    "Using CIC type %s": "Используется тип CIC %1",
    "Starting R4300 emulator: Pure Interpreter": "Запуск эмулятора R4300: чистый интерпретатор",
    "Starting R4300 emulator: Cached Interpreter": "Запуск эмулятора R4300: кэширующий интерпретатор",
    "Starting R4300 emulator: Dynamic Recompiler": "Запуск эмулятора R4300: динамический рекомпилятор",
    "R4300 emulator finished.": "Эмулятор R4300 завершил работу.",
    "Couldn't open configuration file '%s'.  Using defaults.": "Не удалось открыть файл конфигурации «%1». Используются значения по умолчанию.",
    "Waiting until user has approved video access": "Ожидание разрешения пользователя на доступ к видеоустройству",
    "Using a %d bytes GB RAM": "Используется ОЗУ GB размером %1 байт",
    "DD IPL ROM: %s": "IPL ROM 64DD: %1",
    "input plugin did not specify a render callback; there will be no on screen display by the input plugin.": "Плагин ввода не задал обратный вызов отрисовки; экранное меню плагина ввода отображаться не будет.",
    "two events of type 0x%x in interrupt queue": "Два события типа 0x%1 в очереди прерываний",
    "Initializing %u RDRAM modules for a total of %u MB": "Инициализация модулей RDRAM: %1 шт., всего %2 МБ",
    "Setting video mode: %ix%i": "Установка видеорежима: %1x%2",
    "Setting %i-bit video mode: %ix%i": "Установка %1-битного видеорежима: %2x%3",
    "Video closed": "Видео закрыто",
    "Init new dynarec": "Инициализация нового динамического рекомпилятора",
    "Using compressed mem base": "Используется сжатая база памяти",
    "Using full mem base": "Используется полная база памяти",
    "Goodname: %s": "Название GoodN64: %1",
    "Name: %s": "Название: %1",
    "MD5: %s": "MD5: %1",
    "CRC: %08X %08X": "CRC: %1 %2",
    "Imagetype: %s": "Формат образа: %1",
    "Rom size: %d bytes (or %d Mb or %d Megabits)": "Размер ROM: %1 байт (%2 МБ или %3 Мбит)",
    "Version: %X": "Версия: %1",
    "Manufacturer: Nintendo": "Производитель: Nintendo",
    "Manufacturer: %X": "Производитель: %1",
    "Country: %s": "Страна: %1",
    "Could not open state file %s": "Не удалось открыть файл состояния %1",
    "Could not read from state file %s": "Не удалось прочитать файл состояния %1",
    "Unknown state file type %s": "Неизвестный тип файла состояния %1",
    "Video successfully opened: %s": "Видео успешно открыто: %1",
    "Using video capture backend: %s": "Используется бэкенд видеозахвата: %1",
    "Loading a saved disk": "Загрузка сохранённого диска",
    "Failed to load DD Disk save: %s.": "Не удалось загрузить сохранение диска 64DD: %1.",
    "Failed to load DD Disk RAM area (*.ram): %s.": "Не удалось загрузить область RAM диска 64DD (*.ram): %1.",
    "Failed to load DD Disk: %s.": "Не удалось загрузить диск 64DD: %1.",
    "Failed to load DD IPL ROM: %s. Disabling 64DD": "Не удалось загрузить IPL ROM 64DD: %1. 64DD отключён",
    "Invalid DD IPL ROM: Disabling 64DD.": "Недопустимый IPL ROM 64DD: 64DD отключён.",
    "Failed to load ROM file: %s": "Не удалось загрузить файл ROM: %1",
    "Invalid PIF ROM": "Недопустимый PIF ROM",
    "Wrong disk format": "Неверный формат диска",
    "open_rom(): not a valid ROM image": "open_rom(): недопустимый образ ROM",
    "open_disk(): not a valid disk image": "open_disk(): недопустимый образ диска",
    "Invalid speed setting %i percent": "Недопустимая настройка скорости: %1 %",
    "No video plugin attached.  There will be no video output.": "Видеоплагин не подключён. Изображения не будет.",
    "No audio plugin attached.  There will be no sound output.": "Звуковой плагин не подключён. Звука не будет.",
    "No input plugin attached.  You won't be able to control the game.": "Плагин ввода не подключён. Вы не сможете управлять игрой.",
    "No RSP plugin attached.  The video output will be corrupted.": "RSP-плагин не подключён. Изображение будет искажено.",
    "incompatible Video plugin": "несовместимый видеоплагин",
    "incompatible Audio plugin": "несовместимый звуковой плагин",
    "incompatible Input plugin": "несовместимый плагин ввода",
    "incompatible RSP plugin": "несовместимый RSP-плагин",
    "broken Video plugin; function(s) not found.": "повреждённый видеоплагин: функции не найдены.",
    "broken Audio plugin; function(s) not found.": "повреждённый звуковой плагин: функции не найдены.",
    "broken Input plugin; function(s) not found.": "повреждённый плагин ввода: функции не найдены.",
    "broken RSP plugin; function(s) not found.": "повреждённый RSP-плагин: функции не найдены.",
})


def extract_ui_strings(ui_path):
    """Extract (context, [strings]) from a .ui file."""
    tree = ET.parse(ui_path)
    root = tree.getroot()

    class_elem = root.find("class")
    if class_elem is None or not class_elem.text:
        return None, []

    context = class_elem.text.strip()

    strings = []
    for string_elem in root.iter("string"):
        # Use itertext to get ALL text content including child elements
        # (for rich text HTML). Then join and strip.
        parts = list(string_elem.itertext())
        raw = "".join(parts)
        text = raw.strip()
        if text:
            # uic emits the string exactly as written, so plain one-line
            # strings with padding (e.g. "Start: ", " px") must keep it,
            # otherwise the runtime lookup never matches.
            if raw != text and "\n" not in raw and not text.startswith("<"):
                text = raw
            strings.append(text)

    return context, strings


def qualify_class_name(cpp_path, class_name):
    """Return the context Qt really uses for tr() in this class.

    QObject::tr() uses staticMetaObject.className(), which moc emits WITH the
    enclosing namespaces (e.g. "UserInterface::Widget::RomBrowserLoadingWidget"),
    so a bare class name never matches at runtime.  .ui files are different:
    uic emits the bare <class> name, which is why they are not qualified here.
    """
    cpp = Path(cpp_path)
    candidates = [cpp.with_suffix(".hpp"), cpp.with_suffix(".h"), cpp]
    decl = re.compile(r'^\s*(?:class|struct)\s+' + re.escape(class_name) + r'\b[^;]*$')
    for cand in candidates:
        if not cand.exists():
            continue
        stack = []          # (namespace name, brace depth at which it opened)
        depth = 0
        pending = None
        in_block_comment = False
        for line in cand.read_text(encoding="utf-8", errors="replace").splitlines():
            code = line
            if in_block_comment:
                if "*/" not in code:
                    continue
                code = code.split("*/", 1)[1]
                in_block_comment = False
            code = re.sub(r'/\*.*?\*/', '', code)
            if "/*" in code:
                code = code.split("/*", 1)[0]
                in_block_comment = True
            code = code.split("//", 1)[0]
            m = re.match(r'^\s*(?:inline\s+)?namespace\s+(\w+)\s*(\{)?', code)
            if m:
                pending = m.group(1)
                if m.group(2):
                    stack.append((pending, depth))
                    depth += 1
                    pending = None
                continue
            if pending and code.strip().startswith("{"):
                stack.append((pending, depth))
                pending = None
                depth += 1
                continue
            if decl.match(code) and cand.suffix in (".hpp", ".h"):
                return "::".join([n for n, _ in stack] + [class_name])
            depth += code.count("{") - code.count("}")
            while stack and depth <= stack[-1][1]:
                stack.pop()
    return class_name


def extract_cpp_tr_strings(cpp_path):
    """Extract tr("...") and QCoreApplication::translate("Ctx", "...") calls from a C++ file."""
    text = Path(cpp_path).read_text(encoding="utf-8", errors="replace")

    class_name = Path(cpp_path).stem
    m = re.search(r'^class\s+(\w+)\s*:', text, re.MULTILINE)
    if m:
        class_name = m.group(1)

    class_name = qualify_class_name(cpp_path, class_name)

    results = []
    pattern = r'\btr\s*\(\s*"((?:[^"\\]|\\.)*)"\s*[,)]'
    for m in re.finditer(pattern, text):
        raw = m.group(1)
        # Unescape C++ string escapes
        s = raw.replace('\\"', '"').replace('\\n', '\n').replace('\\t', '\t').replace('\\\\', '\\')
        results.append((class_name, s))

    # Match QCoreApplication::translate("Context", "Source") (and
    # QApplication::translate / QT_TRANSLATE_NOOP). The first quoted argument is the
    # context, the second is the source string.
    qapp_pattern = r'(?:\bQ(?:CoreApplication|Application)::translate|\bQT_TRANSLATE_NOOP)\s*\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"'
    for m in re.finditer(qapp_pattern, text):
        ctx = m.group(1)
        raw = m.group(2)
        s = raw.replace('\\"', '"').replace('\\n', '\n').replace('\\t', '\t').replace('\\\\', '\\')
        results.append((ctx, s))

    return results



ABOUT_HTML_REPLACEMENTS = {
    "is an easy to use &amp; cross-platform mupen64plus front-end written in C++ &amp; Qt.":
        "— это удобная кроссплатформенная графическая оболочка для mupen64plus, написанная на C++ и Qt.",
    "Version: {version}": "Версия: {version}",
    "License: GPLv3": "Лицензия: GPLv3",
    "Contributors:": "Участники проекта:",
    "Special thanks:": "Особая благодарность:",
    "for providing custom cheats": "за предоставленные пользовательские чит-коды",
    "for making the controller image for RMG-Input": "за создание изображения контроллера для RMG-Input",
    "It uses the following third party projects:": "Используются следующие сторонние проекты:",
    "It includes the following third party projects:": "В состав входят следующие сторонние проекты:",
}


def translate_source(context, source):
    """Return a translation or None when the source still needs review."""
    if source in TRANSLATIONS:
        return TRANSLATIONS[source]

    # Padded plain strings (" px", "Start: "): reuse the stripped entry and
    # keep the original leading/trailing whitespace.
    stripped = source.strip()
    if stripped != source and stripped in TRANSLATIONS:
        lead = source[:len(source) - len(source.lstrip())]
        trail = source[len(source.rstrip()):]
        return lead + TRANSLATIONS[stripped] + trail

    # AboutDialog stores several paragraphs as a single rich-text string.
    # Translate only the human-readable copy and preserve its HTML, links,
    # contributor names, project names, and the {version} placeholder.
    if context == "AboutDialog" and source.startswith("<!DOCTYPE HTML"):
        translated = source
        for english, russian in ABOUT_HTML_REPLACEMENTS.items():
            translated = translated.replace(english, russian)
        if translated != source:
            return translated

    return None

def main():
    repo_root = Path(__file__).resolve().parent.parent.parent.parent

    scan_dirs = [
        repo_root / "Source" / "RMG",
        repo_root / "Source" / "RMG-Input",
        repo_root / "Source" / "RMG-Input-GCA",
        repo_root / "Source" / "RMG-Audio",
        # 3rd-party plugins with Qt UIs (share the MainDialog context
        # with RMG-Input/RMG-Audio; string overlap analysis showed 0 conflicts)
        repo_root / "Source" / "3rdParty" / "mupen64plus-video-parallel",
        repo_root / "Source" / "3rdParty" / "mupen64plus-video-angrylion-plus",
    ]

    # context -> set of source strings (sorted, deduplicated)
    all_strings = defaultdict(set)

    for scan_dir in scan_dirs:
        if not scan_dir.exists():
            continue
        for ui_file in scan_dir.rglob("*.ui"):
            context, strings = extract_ui_strings(ui_file)
            if context:
                for s in strings:
                    all_strings[context].add(s)
        for cpp_file in scan_dir.rglob("*.cpp"):
            pairs = extract_cpp_tr_strings(cpp_file)
            for ctx, s in pairs:
                all_strings[ctx].add(s)

    # Generate .ts XML
    lines = []
    lines.append('<?xml version="1.0" encoding="utf-8"?>')
    lines.append('<!DOCTYPE TS>')
    lines.append('<!--')
    lines.append('  RMG Russian translation.')
    lines.append('  Generated by generate_ru_ts.py from source code extraction.')
    lines.append('')
    lines.append('  To refresh after changing source strings:')
    lines.append('      python Source/RMG/Translations/generate_ru_ts.py > Source/RMG/Translations/RMG_ru.ts')
    lines.append('  Review any new unfinished entries in Qt Linguist.')
    lines.append('-->')
    lines.append('<TS version="2.1" language="ru_RU">')

    translated_count = 0
    unfinished_count = 0

    for context in sorted(all_strings.keys()):
        lines.append(f'    <context>')
        lines.append(f'        <name>{xml_escape(context)}</name>')
        for source in sorted(all_strings[context]):
            lines.append(f'        <message>')
            lines.append(f'            <source>{xml_escape(source)}</source>')

            trans = translate_source(context, source)
            if trans is not None:
                lines.append(f'            <translation>{xml_escape(trans)}</translation>')
                translated_count += 1
            else:
                lines.append(f'            <translation type="unfinished">{xml_escape(source)}</translation>')
                unfinished_count += 1

            lines.append(f'        </message>')
        lines.append(f'    </context>')

    lines.append('</TS>')

    output = "\n".join(lines) + "\n"

    # Write to stdout
    sys.stdout.write(output)

    # Print stats to stderr
    print(f"\n# Stats: {translated_count} translated, {unfinished_count} unfinished, {len(all_strings)} contexts", file=sys.stderr)


if __name__ == "__main__":
    main()
