/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2026 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "LogDialog.hpp"

#include <QFileInfo>
#include <QFont>
#include <QIcon>

using namespace UserInterface::Dialog;

LogDialog::LogDialog(QWidget *parent) : QDialog(parent)
{
    this->setupUi(this);
    this->setWindowIcon(QIcon(":Resource/RMG.png"));
    this->setWindowFlags(this->windowFlags() | Qt::WindowMinimizeButtonHint);

    // the following code crashes for some
    // users while using Windows,
    // so only change the font on Linux.
#ifndef _WIN32
    QFont font("monospace");
    this->plainTextEdit->setFont(font);
#endif
}

LogDialog::~LogDialog(void)
{
}

void LogDialog::changeEvent(QEvent *event)
{
    // When the application translator is installed (or the user changes the
    // language at runtime), Qt posts a LanguageChange event to all top-level
    // widgets. Because LogDialog is constructed as a MainWindow member before
    // the translator is loaded, its initial setupUi() runs without a
    // translator and the window title stays "Log". Re-running retranslateUi()
    // here picks up the now-installed translator and refreshes the title to
    // the localized form (e.g. "Журнал" in Russian).
    if (event->type() == QEvent::LanguageChange)
    {
        this->retranslateUi(this);
    }
    QDialog::changeEvent(event);
}

int LogDialog::GetLineCount(void)
{
    return this->plainTextEdit->document()->lineCount();
}

void LogDialog::AddMessages(const QList<CoreCallbackMessage>& messages)
{
    QString text;

    for (const auto& message : messages)
    {
        text = message.Context;

        switch (message.Type)
        {
        case CoreDebugMessageType::Error:
            text += "[ERROR]   ";
            break;
        case CoreDebugMessageType::Info:
            text += "[INFO]    ";
            break;
        case CoreDebugMessageType::Verbose:
            text += "[VERBOSE] ";
            break;
        case CoreDebugMessageType::Warning:
            text += "[WARNING] ";
            break;
        case CoreDebugMessageType::Status:
            text += "[STATUS]  ";
            break;
        }

        text += message.Message;

        this->plainTextEdit->appendPlainText(text);
    }
}

void LogDialog::Clear(void)
{
    this->plainTextEdit->clear();
}

