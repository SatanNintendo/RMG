/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2026 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "CoreMessageTranslator.hpp"

#include <QCoreApplication>
#include <QList>
#include <QRegularExpression>

using namespace Utilities;

// The core sends untranslated, printf-style formatted messages.
// Every entry below is the exact format string used by the core,
// the translation (see the "CoreMessage" context in the .ts files)
// refers to the arguments as %1, %2, ... in order of appearance.
static const char* const l_CoreMessages[] =
{
	// status messages
	QT_TRANSLATE_NOOP("CoreMessage", "Emulation continued."),
	QT_TRANSLATE_NOOP("CoreMessage", "Emulation paused."),
	QT_TRANSLATE_NOOP("CoreMessage", "Stopping emulation."),
	QT_TRANSLATE_NOOP("CoreMessage", "Rom closed."),
	QT_TRANSLATE_NOOP("CoreMessage", "Disk closed."),
	QT_TRANSLATE_NOOP("CoreMessage", "Init new dynarec"),
	QT_TRANSLATE_NOOP("CoreMessage", "Using compressed mem base"),
	QT_TRANSLATE_NOOP("CoreMessage", "Using full mem base"),
	QT_TRANSLATE_NOOP("CoreMessage", "Goodname: %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Name: %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "MD5: %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "CRC: %08X %08X"),
	QT_TRANSLATE_NOOP("CoreMessage", "Imagetype: %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Rom size: %d bytes (or %d Mb or %d Megabits)"),
	QT_TRANSLATE_NOOP("CoreMessage", "Version: %X"),
	QT_TRANSLATE_NOOP("CoreMessage", "Manufacturer: Nintendo"),
	QT_TRANSLATE_NOOP("CoreMessage", "Manufacturer: %X"),
	QT_TRANSLATE_NOOP("CoreMessage", "Country: %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Could not open state file %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Could not read from state file %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Unknown state file type %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Speed limiter enabled"),
	QT_TRANSLATE_NOOP("CoreMessage", "Speed limiter disabled"),
	QT_TRANSLATE_NOOP("CoreMessage", "Playback speed: %d%%"),
	QT_TRANSLATE_NOOP("CoreMessage", "Selected state slot: %d"),
	QT_TRANSLATE_NOOP("CoreMessage", "Saved state to: %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "State loaded from: %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Captured screenshot for frame %i."),
	QT_TRANSLATE_NOOP("CoreMessage", "No Mupen64Plus/PJ64 state file found for slot %i"),
	QT_TRANSLATE_NOOP("CoreMessage", "Could not open state file: %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Could not open state file %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Could not read from state file %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Could not read header from state file %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Could not write data to state file: %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Could not read Mupen64Plus savestate 1.0 data from %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Could not read Mupen64Plus savestate 1.1 data from %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Could not read Mupen64Plus savestate 1.2+ data from %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Could not create PJ64 state file: %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Could not read header from Project64 savestate %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Could not read savestate data from Project64 savestate %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Couldn't write data to Project64 state file %s."),
	QT_TRANSLATE_NOOP("CoreMessage", "Failed to open savestate file %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Unknown state file type %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Zip error. Could not open state file: %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Zip error. Could not create state file: %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Insufficient memory to load state."),
	QT_TRANSLATE_NOOP("CoreMessage", "Insufficient memory to save state."),
	QT_TRANSLATE_NOOP("CoreMessage", "Invalid Project64 state RDRAM size."),
	QT_TRANSLATE_NOOP("CoreMessage", "Invalid event queue in savestate: %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "State ROM MD5 does not match current ROM."),
	QT_TRANSLATE_NOOP("CoreMessage", "State ROM header does not match current ROM."),
	QT_TRANSLATE_NOOP("CoreMessage", "State file: %s is not a valid Mupen64plus savestate."),
	QT_TRANSLATE_NOOP("CoreMessage", "State file: %s is not a valid Project64 savestate. Unrecognized file format."),
	QT_TRANSLATE_NOOP("CoreMessage", "State version (%08x) isn't compatible. Please update Mupen64Plus."),

	// information messages
	QT_TRANSLATE_NOOP("CoreMessage", "Controller %u pak changed to %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Removing pak from controller %u"),
	QT_TRANSLATE_NOOP("CoreMessage", "Game controller %u (%s) has a %s plugged in"),
	QT_TRANSLATE_NOOP("CoreMessage", "Game controller %u (%s) has nothing plugged in"),
	QT_TRANSLATE_NOOP("CoreMessage", "Inserting GB cart %s into transferpak %u"),
	QT_TRANSLATE_NOOP("CoreMessage", "Removing GB cart from transferpak %u"),
	QT_TRANSLATE_NOOP("CoreMessage", "Providing default RAM content"),
	QT_TRANSLATE_NOOP("CoreMessage", "Using NTSC PIF ROM"),
	QT_TRANSLATE_NOOP("CoreMessage", "Using PAL PIF ROM"),
	QT_TRANSLATE_NOOP("CoreMessage", "Using CIC type %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Starting R4300 emulator: Pure Interpreter"),
	QT_TRANSLATE_NOOP("CoreMessage", "Starting R4300 emulator: Cached Interpreter"),
	QT_TRANSLATE_NOOP("CoreMessage", "Starting R4300 emulator: Dynamic Recompiler"),
	QT_TRANSLATE_NOOP("CoreMessage", "R4300 emulator finished."),
	QT_TRANSLATE_NOOP("CoreMessage", "Initializing %u RDRAM modules for a total of %u MB"),
	QT_TRANSLATE_NOOP("CoreMessage", "Setting video mode: %ix%i"),
	QT_TRANSLATE_NOOP("CoreMessage", "Setting %i-bit video mode: %ix%i"),
	QT_TRANSLATE_NOOP("CoreMessage", "Video closed"),
	QT_TRANSLATE_NOOP("CoreMessage", "Video successfully opened: %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Using video capture backend: %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Couldn't open configuration file '%s'.  Using defaults."),
	QT_TRANSLATE_NOOP("CoreMessage", "Waiting until user has approved video access"),
	QT_TRANSLATE_NOOP("CoreMessage", "Using a %d bytes GB RAM"),
	QT_TRANSLATE_NOOP("CoreMessage", "DD IPL ROM: %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "input plugin did not specify a render callback; there will be no on screen display by the input plugin."),

	// warnings and errors
	QT_TRANSLATE_NOOP("CoreMessage", "Loading a saved disk"),
	QT_TRANSLATE_NOOP("CoreMessage", "Failed to load DD Disk save: %s."),
	QT_TRANSLATE_NOOP("CoreMessage", "Failed to load DD Disk RAM area (*.ram): %s."),
	QT_TRANSLATE_NOOP("CoreMessage", "Failed to load DD Disk: %s."),
	QT_TRANSLATE_NOOP("CoreMessage", "Failed to load DD IPL ROM: %s. Disabling 64DD"),
	QT_TRANSLATE_NOOP("CoreMessage", "Invalid DD IPL ROM: Disabling 64DD."),
	QT_TRANSLATE_NOOP("CoreMessage", "Failed to load ROM file: %s"),
	QT_TRANSLATE_NOOP("CoreMessage", "Invalid PIF ROM"),
	QT_TRANSLATE_NOOP("CoreMessage", "two events of type 0x%x in interrupt queue"),
	QT_TRANSLATE_NOOP("CoreMessage", "Wrong disk format"),
	QT_TRANSLATE_NOOP("CoreMessage", "open_rom(): not a valid ROM image"),
	QT_TRANSLATE_NOOP("CoreMessage", "open_disk(): not a valid disk image"),
	QT_TRANSLATE_NOOP("CoreMessage", "Invalid speed setting %i percent"),
	QT_TRANSLATE_NOOP("CoreMessage", "No video plugin attached.  There will be no video output."),
	QT_TRANSLATE_NOOP("CoreMessage", "No audio plugin attached.  There will be no sound output."),
	QT_TRANSLATE_NOOP("CoreMessage", "No input plugin attached.  You won't be able to control the game."),
	QT_TRANSLATE_NOOP("CoreMessage", "No RSP plugin attached.  The video output will be corrupted."),
	QT_TRANSLATE_NOOP("CoreMessage", "incompatible Video plugin"),
	QT_TRANSLATE_NOOP("CoreMessage", "incompatible Audio plugin"),
	QT_TRANSLATE_NOOP("CoreMessage", "incompatible Input plugin"),
	QT_TRANSLATE_NOOP("CoreMessage", "incompatible RSP plugin"),
	QT_TRANSLATE_NOOP("CoreMessage", "broken Video plugin; function(s) not found."),
	QT_TRANSLATE_NOOP("CoreMessage", "broken Audio plugin; function(s) not found."),
	QT_TRANSLATE_NOOP("CoreMessage", "broken Input plugin; function(s) not found."),
	QT_TRANSLATE_NOOP("CoreMessage", "broken RSP plugin; function(s) not found."),
};

namespace
{
	struct CoreMessagePattern
	{
		QRegularExpression regex;
		const char*        source;
	};
}

// converts a printf-style format string into an anchored regular expression
static QRegularExpression formatToRegex(const QString& format)
{
	static const QRegularExpression specifier("^%(?:0?\\d*|\\.\\d+)(?:ll|z)?[sdiuxXf]");

	QString pattern;
	int i = 0;
	while (i < format.size())
	{
		if (format[i] != '%')
		{
			pattern += QRegularExpression::escape(format[i]);
			i++;
			continue;
		}

		if (i + 1 < format.size() && format[i + 1] == '%')
		{
			pattern += "%";
			i += 2;
			continue;
		}

		const QRegularExpressionMatch match = specifier.match(format.mid(i));
		if (!match.hasMatch())
		{
			pattern += QRegularExpression::escape(format[i]);
			i++;
			continue;
		}

		switch (match.captured(0).back().toLatin1())
		{
			case 's':
				pattern += "(.+?)";
				break;
			case 'x':
			case 'X':
				pattern += "([0-9a-fA-F]+)";
				break;
			case 'f':
				pattern += "(-?[0-9.]+)";
				break;
			default:
				pattern += "(-?[0-9]+)";
				break;
		}
		i += match.capturedLength(0);
	}

	return QRegularExpression(QRegularExpression::anchoredPattern(pattern));
}

static const QList<CoreMessagePattern>& getPatterns(void)
{
	static const QList<CoreMessagePattern> patterns = []()
	{
		QList<CoreMessagePattern> list;
		for (const char* source : l_CoreMessages)
		{
			list.append({formatToRegex(QString::fromLatin1(source)), source});
		}
		return list;
	}();
	return patterns;
}

// replaces %1, %2, ... in text with the given arguments, arguments
// are inserted verbatim (so they can't be mistaken for placeholders)
static QString insertArguments(const QString& text, const QStringList& arguments)
{
	QString result;
	int i = 0;
	while (i < text.size())
	{
		if (text[i] == '%' && i + 1 < text.size() && text[i + 1].isDigit())
		{
			const int index = text[i + 1].digitValue() - 1;
			if (index >= 0 && index < arguments.size())
			{
				result += arguments[index];
				i += 2;
				continue;
			}
		}
		result += text[i];
		i++;
	}
	return result;
}

QString CoreMessageTranslator::Translate(const QString& message)
{
	// the core terminates some messages with a newline
	const QString trimmedMessage = message.trimmed();

	for (const CoreMessagePattern& pattern : getPatterns())
	{
		const QRegularExpressionMatch match = pattern.regex.match(trimmedMessage);
		if (!match.hasMatch())
		{
			continue;
		}

		const QString translated = QCoreApplication::translate("CoreMessage", pattern.source);
		if (translated == QLatin1String(pattern.source))
		{ // no translation available
			return message;
		}

		return insertArguments(translated, match.capturedTexts().mid(1));
	}

	return message;
}
