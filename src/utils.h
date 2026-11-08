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

#ifndef N_UTILS_H
#define N_UTILS_H

#include "playlistDataItem.h"

class QComboBox;

namespace NUtils
{
    QList<NPlaylistDataItem> dirListRecursive(const QString &path);

    // Fills a combo box with the text encodings used for old, non-Unicode
    // tags, named by language ("Cyrillic (Windows-1251)"), with the codec
    // name as item data, and selects `current` (added by its own name if it
    // isn't in the list). Returns the index of "Automatic (Unicode)".
    int fillEncodingComboBox(QComboBox *comboBox, const QString &current);

    // Position on the Camelot wheel, 0..23 (1A, 1B, 2A ... 12B), for a key
    // written as Camelot ("8A"), Open Key ("1m") or a note name ("Am",
    // "F# minor"); -1 if unrecognised.
    int camelotIndex(const QString &key);
    // A key in the chosen notation: "tag" (unchanged), "camelot" ("8A") or
    // "musical" ("Am"). Keys it can't read are returned unchanged.
    QString formatKey(const QString &key, const QString &notation);
} // namespace NUtils

#endif
