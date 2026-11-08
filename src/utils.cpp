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
#include <QMap>
#include <QRegularExpression>
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

// Position on the Camelot wheel: 1A, 1B, 2A ... 12B (A minor, B major), so
// harmonically related keys sort next to each other. Accepts Camelot ("8A"),
// Open Key ("1m", "6d") and classic names ("Am", "A minor", "F#", "Gbm").
// -1 if unrecognised.
int NUtils::camelotIndex(const QString &rawKey)
{
    QString key = rawKey.trimmed();
    if (key.isEmpty()) {
        return -1;
    }

    static const QRegularExpression camelot("^(\\d{1,2})\\s*([ABab])$");
    QRegularExpressionMatch m = camelot.match(key);
    if (m.hasMatch()) {
        int n = m.captured(1).toInt();
        if (n >= 1 && n <= 12) {
            return (n - 1) * 2 + (m.captured(2).toUpper() == "B" ? 1 : 0);
        }
    }
    static const QRegularExpression openKey("^(\\d{1,2})\\s*([mdMD])$");
    m = openKey.match(key);
    if (m.hasMatch()) {
        int n = m.captured(1).toInt();
        if (n >= 1 && n <= 12) {
            int camelotNumber = (n - 1 + 7) % 12 + 1; // 1m = 8A (A minor), 1d = 8B (C major)
            return (camelotNumber - 1) * 2 + (m.captured(2).toLower() == "d" ? 1 : 0);
        }
    }

    // classic notation: note name, optional #/b, then minor/major marker
    static const QRegularExpression classic(
        "^([A-Ga-g])\\s*([#♯b♭]?)\\s*(m|min|minor|maj|major|M)?$");
    m = classic.match(key);
    if (!m.hasMatch()) {
        return -1;
    }
    static const QMap<QString, int> semitone = {{"C", 0}, {"D", 2}, {"E", 4}, {"F", 5},
                                                {"G", 7}, {"A", 9}, {"B", 11}};
    int note = semitone.value(m.captured(1).toUpper());
    QString accidental = m.captured(2);
    if (accidental == "#" || accidental == "♯") {
        note += 1;
    } else if (accidental == "b" || accidental == "♭") {
        note -= 1;
    }
    note = (note + 12) % 12;
    QString quality = m.captured(3);
    bool minor = quality == "m" || quality.startsWith("min");
    // Camelot numbers by semitone: majors are B, minors are A
    static const int majorNumber[12] = {8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1};  // C, C#, D ...
    static const int minorNumber[12] = {5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10};  // Cm, C#m ...
    int n = minor ? minorNumber[note] : majorNumber[note];
    return (n - 1) * 2 + (minor ? 0 : 1);
}

QString NUtils::formatKey(const QString &rawKey, const QString &notation)
{
    if (notation != "camelot" && notation != "musical") {
        return rawKey; // as tagged
    }
    int index = camelotIndex(rawKey);
    if (index < 0) {
        return rawKey; // not a key we recognise: show it as it is
    }
    int number = index / 2 + 1;
    bool major = index % 2 == 1;
    if (notation == "camelot") {
        return QString::number(number) + (major ? "B" : "A");
    }
    // the names DJ software uses, by Camelot number 1..12
    static const char *minorName[12] = {"Abm", "Ebm", "Bbm", "Fm", "Cm",  "Gm",
                                        "Dm",  "Am",  "Em",  "Bm", "F#m", "Dbm"};
    static const char *majorName[12] = {"B",  "F#", "Db", "Ab", "Eb", "Bb",
                                        "F",  "C",  "G",  "D",  "A",  "E"};
    return QString::fromLatin1(major ? majorName[number - 1] : minorName[number - 1]);
}
