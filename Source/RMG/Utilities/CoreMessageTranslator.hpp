/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2026 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef CORE_MESSAGE_TRANSLATOR_HPP
#define CORE_MESSAGE_TRANSLATOR_HPP

#include <QString>

namespace Utilities
{
namespace CoreMessageTranslator
{
	// Translates a message coming from the mupen64plus core
	// (printf-style formatted English text) using the "CoreMessage"
	// translation context. Returns the message unchanged when there
	// is no known pattern or no translation for it.
	QString Translate(const QString& message);
}
} // namespace Utilities

#endif // CORE_MESSAGE_TRANSLATOR_HPP
