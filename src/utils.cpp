/********************************************************************
**  Nulloy Music Player, http://nulloy.com
**  Copyright (C) 2010-2024 Sergey Vlasov <sergey@vlasov.me>
**
**  This program can be distributed under the terms of the GNU
**  General Public License version 3.0 as published by the Free
**  Software Foundation and appearing in the file LICENSE.GPL3
**  included in the packaging of this file.  Please review the
**  following information to ensure the GNU General Public License
**  version 3.0 requirements will be met:
**
**  http://www.gnu.org/licenses/gpl-3.0.html
**
*********************************************************************/

#include "utils.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QTextCodec>

#include "playlistStorage.h"
#include "settings.h"

namespace NUtils
{
    static QList<NPlaylistDataItem> _processPath(const QString &path,
                                                 const QStringList &nameFilters);
} // namespace NUtils

QList<NPlaylistDataItem> NUtils::_processPath(const QString &path, const QStringList &nameFilters)
{
    QList<NPlaylistDataItem> dataItemsList;
    QFileInfo fileInfo = QFileInfo(path);
    if (fileInfo.isDir()) {
        QStringList entryList;
        if (!nameFilters.isEmpty()) {
            entryList = QDir(path).entryList(nameFilters,
                                             QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
        } else {
            entryList = QDir(path).entryList(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
        }

        foreach (QString f, entryList)
            dataItemsList << _processPath(path + "/" + f, nameFilters);
    } else {
        if (QDir::match(nameFilters, fileInfo.fileName())) {
            if (path.endsWith(".m3u") || path.endsWith(".m3u8")) {
                dataItemsList << NPlaylistStorage::readM3u(path);
            } else {
                dataItemsList << NPlaylistDataItem(path);
            }
        }
    }

    return dataItemsList;
}

int NUtils::fillEncodingComboBox(QComboBox *comboBox, const QString &current)
{
    static const char *const encodings[][2] = {
        {"UTF-8", QT_TRANSLATE_NOOP("NUtils", "Automatic (Unicode)")},
        {"windows-1252", QT_TRANSLATE_NOOP("NUtils", "Western European (Windows-1252)")},
        {"windows-1250", QT_TRANSLATE_NOOP("NUtils", "Central European (Windows-1250)")},
        {"windows-1251", QT_TRANSLATE_NOOP("NUtils", "Cyrillic (Windows-1251)")},
        {"KOI8-R", QT_TRANSLATE_NOOP("NUtils", "Cyrillic (KOI8-R)")},
        {"windows-1253", QT_TRANSLATE_NOOP("NUtils", "Greek (Windows-1253)")},
        {"windows-1254", QT_TRANSLATE_NOOP("NUtils", "Turkish (Windows-1254)")},
        {"windows-1257", QT_TRANSLATE_NOOP("NUtils", "Baltic (Windows-1257)")},
        {"windows-1255", QT_TRANSLATE_NOOP("NUtils", "Hebrew (Windows-1255)")},
        {"windows-1256", QT_TRANSLATE_NOOP("NUtils", "Arabic (Windows-1256)")},
        {"Shift_JIS", QT_TRANSLATE_NOOP("NUtils", "Japanese (Shift-JIS)")},
        {"GBK", QT_TRANSLATE_NOOP("NUtils", "Chinese, Simplified (GBK)")},
        {"Big5", QT_TRANSLATE_NOOP("NUtils", "Chinese, Traditional (Big5)")},
        {"EUC-KR", QT_TRANSLATE_NOOP("NUtils", "Korean (EUC-KR)")},
        {"TIS-620", QT_TRANSLATE_NOOP("NUtils", "Thai (TIS-620)")},
    };
    comboBox->clear();
    QTextCodec *currentCodec = QTextCodec::codecForName(current.toUtf8());
    int utf8Index = 0;
    int currentIndex = -1;
    for (const auto &e : encodings) {
        QTextCodec *codec = QTextCodec::codecForName(e[0]);
        if (!codec) {
            continue;
        }
        int i = comboBox->count();
        comboBox->addItem(QCoreApplication::translate("NUtils", e[1]), QString(codec->name()));
        if (QByteArray(e[0]) == "UTF-8") {
            utf8Index = i;
        }
        if (currentCodec && codec->mibEnum() == currentCodec->mibEnum()) {
            currentIndex = i;
        }
    }
    if (currentCodec && currentIndex < 0) { // a codec chosen in an older version
        currentIndex = comboBox->count();
        comboBox->addItem(QString(currentCodec->name()), QString(currentCodec->name()));
    }
    comboBox->setCurrentIndex(currentIndex < 0 ? utf8Index : currentIndex);
    return utf8Index;
}

QList<NPlaylistDataItem> NUtils::dirListRecursive(const QString &path)
{
    QStringList nameFilters = NSettings::instance()->value("FileFilters").toString().split(' ');
    return _processPath(path, nameFilters);
}
