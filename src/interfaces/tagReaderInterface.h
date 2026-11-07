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

#ifndef N_TAG_READER_INTERFACE_H
#define N_TAG_READER_INTERFACE_H

#include "global.h"
#include <QMap>
#include <QObject>

#define TAGREADER_INTERFACE "Nulloy/NTagReaderInterface/0.9.5"

class NTagReaderInterface : public QObject
{
public:
    NTagReaderInterface(QObject *parent = 0) : QObject(parent) {}
    virtual ~NTagReaderInterface() {}

    static QString interfaceString() { return TAGREADER_INTERFACE; }

    virtual void setSource(const QString &file) = 0;
    virtual void setEncoding(const QString &encoding) { Q_UNUSED(encoding); }
    virtual QString getTag(QChar ch) const = 0;
    virtual N::Tag tagFromKey(const QString &) const { return N::UnknownTag; }
    virtual QString tagToKey(N::Tag) const { return ""; }

    virtual bool isWriteSupported() const { return false; }
    virtual QMap<QString, QStringList> getTags() const { return QMap<QString, QStringList>(); }
    virtual QMap<QString, QStringList> setTags(const QMap<QString, QStringList> &)
    {
        return QMap<QString, QStringList>();
    }

    // Changes only the given fields of the current file (an empty list removes
    // the field) and keeps every other tag, picture and frame as it is.
    // Returns the fields that couldn't be written, or "Error" => "Write" if
    // saving the file failed; empty on success.
    virtual QMap<QString, QStringList> updateTags(const QMap<QString, QStringList> &changes)
    {
        return changes;
    }

    // ID3v2 version written to MP3, WAV and AIFF files: 3 or 4
    virtual void setId3v2Version(int version) { Q_UNUSED(version); }

    // The kinds of tag the current file has, e.g. ("ID3v2.4", "ID3v1"), and
    // whether saving writes ID3v2 (so the ID3v2 version setting applies)
    virtual QStringList tagFormats() const { return QStringList(); }
    virtual bool savesId3v2() const { return false; }
};

Q_DECLARE_INTERFACE(NTagReaderInterface, TAGREADER_INTERFACE)

#endif
