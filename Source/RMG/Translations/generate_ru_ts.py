#!/usr/bin/env python3
"""
Generate a complete RMG_ru.ts file with ALL translatable strings
from RMG, RMG-Input, and RMG-Audio.

This script:
1. Parses all .ui files to extract (context, string) pairs
2. Parses all .cpp files for tr("...") calls
3. Looks up Russian translations from a built-in dictionary
4. Outputs a complete .ts file ready for lrelease

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
    "Speed &Factor": "Скорость &воспроизведения",
    "Setti&ngs": "Настр&ойки",
    "View": "Вид",
    "He&lp": "Спр&авка",
    "Netplay": "Сетевая игра",
    "toolBar": "Панель инструментов",
    "&Start ROM": "&Запустить ROM",
    "Start Co&mbo": "Запустить ко&мбо",
    "S&hutdown": "О&становить",
    "&Soft Reset": "&Мягкий сброс",
    "&Hard Reset": "&Жёсткий сброс",
    "&Pause": "&Пауза",
    "Scree&nshot": "С&нимок экрана",
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
    "&About RMG": "&О RMG",
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
    "Open N64 ROM or 64DD Disk": "Открыть N64 ROM или 64DD диск",
    "Open N64 ROM": "Открыть N64 ROM",
    "Open 64DD Disk": "Открыть 64DD диск",
    "Save State": "Сохранить состояние",
    "Save State (*.state);;Project64 Save State (*.pj);;All Files (*)": "Сохранение состояния (*.state);;Project64 (*.pj);;Все файлы (*)",
    "Open Save State": "Открыть сохранение состояния",
    "Save State (*.dat *.state *.st* *.pj*);;All Files (*)": "Сохранения (*.dat *.state *.st* *.pj*);;Все файлы (*)",
    "Select ROM Directory": "Выбрать каталог ROM",

    # === SettingsDialog ===
    "Settings": "Настройки",
    "Interface": "Интерфейс",
    "Plugins": "Плагины",
    "Hotkeys": "Горячие клавиши",
    "Core": "Ядро",
    "Game": "Игра",
    "Directories": "Каталоги",
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
    "Search sub-directories": "Искать в подкаталогах",
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
    "Video Plugin": "Плагин видео",
    "Audio Plugin": "Плагин звука",
    "Input Plugin": "Плагин ввода",
    "Reality Signal Processor Plugin": "Плагин RSP (Reality Signal Processor)",
    "Screenshot Directory": "Каталог скриншотов",
    "Save (State) Directory": "Каталог сохранений (State)",
    "Save (SRAM) Directory": "Каталог сохранений (SRAM)",
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
    "Controller pak": "Контроллер Pak",
    "None": "Нет",
    "4 KB EEPROM": "4 КБ EEPROM",
    "16 KB EEPROM": "16 КБ EEPROM",
    "SRAM": "SRAM",
    "Flash RAM": "Flash RAM",
    "RAM Area Only": "Только область RAM",
    "Full Disk Copy": "Полная копия диска",
    "Good Name": "Корректное имя",
    "Internal ROM Name": "Внутреннее имя ROM",
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
    "Speed Factor": "Скорость",
    "System": "Система",
    "Audio": "Звук",
    "Start ROM": "Запустить ROM",
    "Start Combo": "Запустить комбо",
    "Shutdown": "Остановить",
    "Soft Reset": "Мягкий сброс",
    "Hard Reset": "Жёсткий сброс",
    "Pause": "Пауза",
    "Capture Screenshot": "Сделать снимок экрана",
    "Full Screen": "Полный экран",
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
    "Select Screenshot Directory": "Выбрать каталог скриншотов",
    "Select Save (State) Directory": "Выбрать каталог сохранений (State)",
    "Select Save (SRAM) Directory": "Выбрать каталог сохранений (SRAM)",
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
    "<html><head/><body><p><span style=\" font-weight:700;\">No ROMs in supported formats were found.</span></p><p>Please select a directory with ROMs to begin.</p><p>ROMs in the following formats will be scanned and listed: </p></body></html>": "<html><head/><body><p><span style=\" font-weight:700;\">Не найдено ROM в поддерживаемых форматах.</span></p><p>Выберите каталог с ROM для начала работы.</p><p>Будут просканированы и показаны ROM в следующих форматах: </p></body></html>",
    ".n64/.z64/.v64 (N64 Roms)": ".n64/.z64/.v64 (ROM N64)",
    ".ndd/.d64 (64DD Disks)": ".ndd/.d64 (диски 64DD)",
    ".zip/.7z (Archive)": ".zip/.7z (архив)",
    "Select ROM Directory...": "Выбрать каталог ROM...",
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
    "<html><head/><body><p><span style=\" font-weight:700;\">No ROMs were found.</span></p><p>You can configure the ROM browser directory or refresh the ROM browser.</p></body></html>": "<html><head/><body><p><span style=\" font-weight:700;\">ROM не найдены.</span></p><p>Вы можете настроить каталог ROM или обновить браузер ROM.</p></body></html>",

    # === NetplaySessionBrowserEmptyWidget ===
    "<html><head/><body><p><span style=\" font-weight:700;\">No sessions were found.</span></p><p>You can create your own session or refresh the session list.</p></body></html>": "<html><head/><body><p><span style=\" font-weight:700;\">Сессии не найдены.</span></p><p>Вы можете создать свою сессию или обновить список сессий.</p></body></html>",

    # === RMG-Input: MainDialog ===
    "Rosalie's Mupen GUI - Input Plugin": "Rosalie's Mupen GUI — плагин ввода",
    "Player 1": "Игрок 1",
    "Player 2": "Игрок 2",
    "Player 3": "Игрок 3",
    "Player 4": "Игрок 4",

    # === RMG-Input: ControllerWidget ===
    "ControllerWidget": "Виджет контроллера",
    "Profile": "Профиль",
    "Input Device": "Устройство ввода",
    "Deadzone: 25%": "Мёртвая зона: 25%",
    "Digital Pad": "Цифровой крест",
    "Up:": "Вверх:",
    "Down:": "Вниз:",
    "Left:": "Влево:",
    "Right:": "Вправо:",
    "+": "+",
    "-": "-",
    "Analog Stick": "Аналоговый стик",
    "Analog Stick Sensitivity: 100%": "Чувствительность аналогового стика: 100%",
    "L-Shoulder: ": "Лево-плечо: ",
    "R-Shoulder: ": "Право-плечо: ",
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
    "Controller Pak": "Контроллер Pak",
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
    "Default frequency": "Базовая частота",
    "Resampler": "Передискретизатор",
    "trivial": "trivial",
    "Swap left and right channel": "Поменять местами левый и правый каналы",
    "Synchronize": "Синхронизировать",
    "Primary Buffer Size": "Размер первичного буфера",
    "Primary Buffer Target": "Цель первичного буфера",
    "Secondary Buffer Size": "Размер вторичного буфера",
    "Simple Backend": "Простой бэкенд",

    # Remaining strings that were unfinished
    "Analog Stick Sensitivity: ": "Чувствительность аналогового стика: ",
    "L-Shoulder:": "Лево-плечо:",
    "R-Shoulder:": "Право-плечо:",
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
    "Internal Name": "Внутреннее имя",
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
    "Play Game with State": "Запустить игру со сохранением",
    "Refresh ROM List": "Обновить список ROM",
    "Open ROM Directory": "Открыть каталог ROM",
    "Change ROM Directory...": "Сменить каталог ROM...",
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
    "Join": "Подключиться",
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
    "IPL ROMs (*.n64)": "ROM IPL (*.n64)",
    "PIF ROMs (*.rom)": "ROM PIF (*.rom)",
    # NetplaySessionBrowserDialog.cpp
    "Open %1": "Открыть %1",
}


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
        text = "".join(parts).strip()
        if text:
            strings.append(text)

    return context, strings


def extract_cpp_tr_strings(cpp_path):
    """Extract tr("...") and QCoreApplication::translate("Ctx", "...") calls from a C++ file."""
    text = Path(cpp_path).read_text(encoding="utf-8", errors="replace")

    class_name = Path(cpp_path).stem
    m = re.search(r'^class\s+(\w+)\s*:', text, re.MULTILINE)
    if m:
        class_name = m.group(1)

    results = []
    pattern = r'\btr\s*\(\s*"((?:[^"\\]|\\.)*)"\s*[,)]'
    for m in re.finditer(pattern, text):
        raw = m.group(1)
        # Unescape C++ string escapes
        s = raw.replace('\\"', '"').replace('\\n', '\n').replace('\\t', '\t').replace('\\\\', '\\')
        results.append((class_name, s))

    # Match QCoreApplication::translate("Context", "Source") (and
    # QApplication::translate). The first quoted argument is the
    # context, the second is the source string.
    qapp_pattern = r'\bQ(?:CoreApplication|Application)::translate\s*\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"'
    for m in re.finditer(qapp_pattern, text):
        ctx = m.group(1)
        raw = m.group(2)
        s = raw.replace('\\"', '"').replace('\\n', '\n').replace('\\t', '\t').replace('\\\\', '\\')
        results.append((ctx, s))

    return results


def main():
    repo_root = Path(__file__).resolve().parent.parent.parent.parent

    scan_dirs = [
        repo_root / "Source" / "RMG",
        repo_root / "Source" / "RMG-Input",
        repo_root / "Source" / "RMG-Input-GCA",
        repo_root / "Source" / "RMG-Audio",
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
    lines.append('      lupdate Source/RMG Source/RMG-Input Source/RMG-Audio -ts Source/RMG/Translations/RMG_ru.ts')
    lines.append('  Then edit in Qt Linguist to fill in any new unfinished entries.')
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

            if source in TRANSLATIONS:
                trans = TRANSLATIONS[source]
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
