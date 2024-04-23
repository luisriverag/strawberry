/*
 * Strawberry Music Player
 * Copyright 2018-2021, Jonas Kvinge <jonas@jkvinge.net>
 *
 * Strawberry is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Strawberry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <QObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QProcess>
#include <QDesktopServices>
#include <QMessageBox>

#include "filemanagerutils.h"

namespace Utilities {

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
void OpenInFileManager(const QString &path, const QUrl &url);
void OpenInFileManager(const QString &path, const QUrl &url) {

  if (!url.isLocalFile()) return;

  QProcess proc;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
  proc.startCommand(QStringLiteral("xdg-mime query default inode/directory"));
#else
  proc.start(QStringLiteral("xdg-mime"), QStringList() << QStringLiteral("query") << QStringLiteral("default") << QStringLiteral("inode/directory"));
#endif
  proc.waitForFinished();
  QString desktop_file = QString::fromUtf8(proc.readLine()).simplified();
  QString xdg_data_dirs = QString::fromUtf8(qgetenv("XDG_DATA_DIRS"));
  if (xdg_data_dirs.isEmpty()) {
    xdg_data_dirs = QStringLiteral("/usr/local/share/:/usr/share/");
  }
  const QStringList data_dirs = xdg_data_dirs.split(QStringLiteral(":"));

  QString command;
  QStringList command_params;
  for (const QString &data_dir : data_dirs) {
    QString desktop_file_path = QStringLiteral("%1/applications/%2").arg(data_dir, desktop_file);
    if (!QFile::exists(desktop_file_path)) continue;
    QSettings setting(desktop_file_path, QSettings::IniFormat);
    setting.beginGroup(QStringLiteral("Desktop Entry"));
    if (setting.contains(QStringLiteral("Exec"))) {
      QString cmd = setting.value(QStringLiteral("Exec")).toString();
      if (cmd.isEmpty()) break;
      cmd = cmd.remove(QRegularExpression(QStringLiteral("[%][a-zA-Z]*( |$)"), QRegularExpression::CaseInsensitiveOption));
#  if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
      command_params = cmd.split(QLatin1Char(' '), Qt::SkipEmptyParts);
#  else
      command_params = cmd.split(QLatin1Char(' '), QString::SkipEmptyParts);
#  endif
      command = command_params.first();
      command_params.removeFirst();
    }
    setting.endGroup();
    if (!command.isEmpty()) break;
  }

  if (command.startsWith(QLatin1String("/usr/bin/"))) {
    command = command.split(QStringLiteral("/")).last();
  }

  if (command.isEmpty() || command == QStringLiteral("exo-open")) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
  }
  else if (command.startsWith(QLatin1String("nautilus"))) {
    proc.startDetached(command, QStringList() << command_params << QStringLiteral("--select") << url.toLocalFile());
  }
  else if (command.startsWith(QLatin1String("dolphin")) || command.startsWith(QLatin1String("konqueror")) || command.startsWith(QLatin1String("kfmclient"))) {
    proc.startDetached(command, QStringList() << command_params << QStringLiteral("--select") << url.toLocalFile());
  }
  else if (command.startsWith(QLatin1String("caja"))) {
    proc.startDetached(command, QStringList() << command_params << QStringLiteral("--no-desktop") << path);
  }
  else if (command.startsWith(QLatin1String("pcmanfm")) || command.startsWith(QLatin1String("thunar")) || command.startsWith(QLatin1String("spacefm"))) {
    proc.startDetached(command, QStringList() << command_params << path);
  }
  else {
    proc.startDetached(command, QStringList() << command_params << url.toLocalFile());
  }

}
#endif

#ifdef Q_OS_MACOS
// Better than openUrl(dirname(path)) - also highlights file at path
void RevealFileInFinder(const QString &path) {
  QProcess::execute(QStringLiteral("/usr/bin/open"), QStringList() << QStringLiteral("-R") << path);
}
#endif  // Q_OS_MACOS

#ifdef Q_OS_WIN
void ShowFileInExplorer(const QString &path);
void ShowFileInExplorer(const QString &path) {
  QProcess::execute(QStringLiteral("explorer.exe"), QStringList() << QStringLiteral("/select,") << QDir::toNativeSeparators(path));
}
#endif

void OpenInFileBrowser(const QList<QUrl> &urls) {

  QMap<QString, QUrl> dirs;

  for (const QUrl &url : urls) {
    if (!url.isLocalFile()) {
      continue;
    }
    QString path = url.toLocalFile();
    if (!QFile::exists(path)) continue;

    const QString directory = QFileInfo(path).dir().path();
    if (dirs.contains(directory)) continue;
    dirs.insert(directory, url);
  }

  if (dirs.count() > 50) {
    QMessageBox messagebox(QMessageBox::Critical, QObject::tr("Show in file browser"), QObject::tr("Too many songs selected."));
    messagebox.exec();
    return;
  }

  if (dirs.count() > 5) {
    QMessageBox messagebox(QMessageBox::Information, QObject::tr("Show in file browser"), QObject::tr("%1 songs in %2 different directories selected, are you sure you want to open them all?").arg(urls.count()).arg(dirs.count()), QMessageBox::Open|QMessageBox::Cancel);
    messagebox.setTextFormat(Qt::RichText);
    int result = messagebox.exec();
    switch (result) {
      case QMessageBox::Open:
        break;
      case QMessageBox::Cancel:
      default:
        return;
    }
  }

  QMap<QString, QUrl>::iterator i;
  for (i = dirs.begin(); i != dirs.end(); ++i) {
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    OpenInFileManager(i.key(), i.value());
#elif defined(Q_OS_MACOS)
    // Revealing multiple files in the finder only opens one window, so it also makes sense to reveal at most one per directory
    RevealFileInFinder(i.value().toLocalFile());
#elif defined(Q_OS_WIN32)
    ShowFileInExplorer(i.value().toLocalFile());
#endif
  }

}

}  // namespace Utilities
