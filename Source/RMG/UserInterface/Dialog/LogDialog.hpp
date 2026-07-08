/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2026 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef LOGDIALOG_HPP
#define LOGDIALOG_HPP

#include "Callbacks.hpp"

#include <QDialog>
#include <QWidget>
#include <QList>
#include <QEvent>

#include <RMG-Core/Callback.hpp>

#include "ui_LogDialog.h"

namespace UserInterface
{
namespace Dialog
{
class LogDialog : public QDialog, private Ui::LogDialog
{
    Q_OBJECT

  private:

  protected:
    // Re-applies the UI translations (window title etc.) when the application
    // language changes. This is required because LogDialog is instantiated as
    // a member of MainWindow BEFORE MainWindow::loadTranslator() runs, so the
    // initial setupUi() call happens with no translator installed and the
    // window title is left as the English source "Log". When loadTranslator()
    // later calls QCoreApplication::installTranslator(), Qt posts a
    // LanguageChange event to all top-level widgets; without this override the
    // default QWidget::changeEvent() ignores the event and the title is never
    // refreshed, leaving it stuck on "Log" instead of "Журнал".
    void changeEvent(QEvent *event) override;

  public:
    LogDialog(QWidget* parent = nullptr);
    ~LogDialog(void);

    int GetLineCount(void);

    void AddMessages(const QList<CoreCallbackMessage>& messages);
    void Clear(void);
};
} // namespace Dialog
} // namespace UserInterface

#endif // LOGDIALOG_HPP