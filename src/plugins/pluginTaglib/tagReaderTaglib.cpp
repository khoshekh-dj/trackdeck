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

#include "tagReaderTaglib.h"

#include <aifffile.h>
#include <apefile.h>
#include <asffile.h>
#include <flacfile.h>
#include <id3v2tag.h>
#include <mpcfile.h>
#include <opusfile.h>
#include <vorbisfile.h>
#include <wavpackfile.h>
#include <aiffproperties.h>
#include <apeproperties.h>
#include <flacproperties.h>
#include <mp4file.h>
#include <mp4properties.h>
#include <mp4tag.h>
#include <mpegfile.h>
#include <taglib.h>
#include <tpropertymap.h>
#include <trueaudioproperties.h>
#include <wavpackproperties.h>
#include <wavfile.h>
#include <wavproperties.h>

#include <QFileInfo>
#include <QTextCodec>

#include "tagLibFileRef.h"

TagLib::FileRef *NTaglib::_tagRef;
QString NTaglib::_filePath;

NTagReaderTaglib::NTagReaderTaglib(QObject *parent) : NTagReaderInterface(parent)
{
    m_isValid = false;
    m_id3v2Version = 3;
    m_codec = nullptr;
    m_utf8Codec = QTextCodec::codecForName("UTF-8");
}

void NTagReaderTaglib::init()
{
    if (m_init) {
        return;
    }

    m_init = true;
    NTaglib::_tagRef = NULL;
}

void NTagReaderTaglib::setSource(const QString &file)
{
    if (NTaglib::_filePath == file) {
        return;
    }

    m_isValid = false;

    if (NTaglib::_tagRef) {
        delete NTaglib::_tagRef;
        NTaglib::_tagRef = NULL;
    }

    NTaglib::_filePath = "";

    if (!QFileInfo(file).exists()) {
        return;
    }

    NTaglib::_filePath = file;

#ifdef WIN32
    NTaglib::_tagRef = new TagLib::FileRef(reinterpret_cast<const wchar_t *>(file.constData()));
#else
    NTaglib::_tagRef = new TagLib::FileRef(file.toUtf8().data());
#endif

    m_isValid = NTaglib::_tagRef->file() && NTaglib::_tagRef->file()->isValid();
}

void NTagReaderTaglib::setEncoding(const QString &encoding)
{
    m_codec = QTextCodec::codecForName(encoding.toUtf8());
}

NTagReaderTaglib::~NTagReaderTaglib()
{
    if (!m_init) {
        return;
    }

    if (NTaglib::_tagRef) {
        delete NTaglib::_tagRef;
        NTaglib::_tagRef = NULL;
    }
}

// TagLib has already decoded the tag according to its declared encoding. Only
// text that fits in single bytes can be a mislabelled legacy encoding, so only
// that is second-guessed:
//  - bytes that form valid UTF-8 were UTF-8 stored as Latin-1 ("JoaquÃ\u00adn"),
//  - otherwise, if the user picked a legacy codec (e.g. Windows-1251 for old
//    Cyrillic tags), the bytes are decoded with it,
//  - otherwise the text is taken as TagLib read it ("Joaquín").
// Previously the last case also went through the user's codec, which with the
// default UTF-8 turned every accented Latin-1 or UTF-16 character into "\uFFFD".
QString NTagReaderTaglib::toUnicode(const TagLib::String &tstr) const
{
    if (tstr.isAscii() || !tstr.isLatin1()) {
        return TStringToQString(tstr);
    }

    QByteArray bytes(tstr.toCString(false)); // one byte per character
    QTextCodec::ConverterState state;
    QString asUtf8 = m_utf8Codec->toUnicode(bytes.constData(), bytes.size(), &state);
    if (state.invalidChars == 0 && state.remainingChars == 0) {
        return asUtf8;
    }
    if (m_codec && m_codec != m_utf8Codec && m_codec->mibEnum() != m_utf8Codec->mibEnum()) {
        return m_codec->toUnicode(bytes);
    }
    return TStringToQString(tstr);
}

static QString id3v2Name(TagLib::ID3v2::Tag *tag)
{
    return QString("ID3v2.%1").arg(tag->header()->majorVersion());
}

QStringList NTagReaderTaglib::tagFormats() const
{
    QStringList formats;
    if (!m_isValid) {
        return formats;
    }
    TagLib::File *f = NTaglib::_tagRef->file();
    if (auto *file = dynamic_cast<TagLib::MPEG::File *>(f)) {
        if (file->hasID3v2Tag()) formats << id3v2Name(file->ID3v2Tag());
        if (file->hasID3v1Tag()) formats << "ID3v1";
        if (file->hasAPETag()) formats << "APE";
    } else if (auto *file = dynamic_cast<TagLib::RIFF::AIFF::File *>(f)) {
        if (file->hasID3v2Tag()) formats << id3v2Name(file->tag());
    } else if (auto *file = dynamic_cast<TagLib::RIFF::WAV::File *>(f)) {
        if (file->hasID3v2Tag()) formats << id3v2Name(file->ID3v2Tag());
        if (file->hasInfoTag()) formats << "RIFF INFO";
    } else if (auto *file = dynamic_cast<TagLib::FLAC::File *>(f)) {
        if (file->hasXiphComment()) formats << "Vorbis comments";
        if (file->hasID3v2Tag()) formats << id3v2Name(file->ID3v2Tag());
        if (file->hasID3v1Tag()) formats << "ID3v1";
    } else if (dynamic_cast<TagLib::MP4::File *>(f)) {
        formats << "MP4 (iTunes)";
    } else if (dynamic_cast<TagLib::Ogg::Vorbis::File *>(f) || dynamic_cast<TagLib::Ogg::Opus::File *>(f)) {
        formats << "Vorbis comments";
    } else if (dynamic_cast<TagLib::ASF::File *>(f)) {
        formats << "ASF (WMA)";
    } else if (auto *file = dynamic_cast<TagLib::APE::File *>(f)) {
        if (file->hasAPETag()) formats << "APE";
        if (file->hasID3v1Tag()) formats << "ID3v1";
    } else if (auto *file = dynamic_cast<TagLib::WavPack::File *>(f)) {
        if (file->hasAPETag()) formats << "APE";
        if (file->hasID3v1Tag()) formats << "ID3v1";
    } else if (auto *file = dynamic_cast<TagLib::MPC::File *>(f)) {
        if (file->hasAPETag()) formats << "APE";
        if (file->hasID3v1Tag()) formats << "ID3v1";
    }
    return formats;
}

bool NTagReaderTaglib::savesId3v2() const
{
    if (!m_isValid) {
        return false;
    }
    TagLib::File *f = NTaglib::_tagRef->file();
    return dynamic_cast<TagLib::MPEG::File *>(f) || dynamic_cast<TagLib::RIFF::AIFF::File *>(f) ||
           dynamic_cast<TagLib::RIFF::WAV::File *>(f);
}

QString NTagReaderTaglib::getTag(QChar ch) const
{
    if (!m_isValid) {
        return "";
    }

    switch (ch.unicode()) {
        case 'a': // artist
            return toUnicode(NTaglib::_tagRef->tag()->artist());
        case 't': // title
            return toUnicode(NTaglib::_tagRef->tag()->title());
        case 'A': // album
            return toUnicode(NTaglib::_tagRef->tag()->album());
        case 'c': // comment
            return toUnicode(NTaglib::_tagRef->tag()->comment());
        case 'g': // genre
            return TStringToQString(NTaglib::_tagRef->tag()->genre());
        case 'y': { // year
            unsigned int res = NTaglib::_tagRef->tag()->year();
            if (res == 0) {
                return "";
            }
            return QString::number(res);
        }
        case 'n': { // track number
            unsigned int res = NTaglib::_tagRef->tag()->track();
            if (res == 0) {
                return "";
            }
            return QString::number(res);
        }
        case 'b': { // bit depth
            TagLib::AudioProperties *ap = NTaglib::_tagRef->audioProperties();
            if (auto *prop = dynamic_cast<TagLib::APE::Properties *>(ap)) {
                return QString::number(prop->bitsPerSample());
            } else if (auto *prop = dynamic_cast<TagLib::FLAC::Properties *>(ap)) {
                return QString::number(prop->bitsPerSample());
            } else if (auto *prop = dynamic_cast<TagLib::MP4::Properties *>(ap)) {
                return QString::number(prop->bitsPerSample());
            } else if (auto *prop = dynamic_cast<TagLib::RIFF::AIFF::Properties *>(ap)) {
                return QString::number(prop->bitsPerSample());
            } else if (auto *prop = dynamic_cast<TagLib::RIFF::WAV::Properties *>(ap)) {
                return QString::number(prop->bitsPerSample());
            } else if (auto *prop = dynamic_cast<TagLib::TrueAudio::Properties *>(ap)) {
                return QString::number(prop->bitsPerSample());
            } else if (auto *prop = dynamic_cast<TagLib::WavPack::Properties *>(ap)) {
                return QString::number(prop->bitsPerSample());
            } else {
                return "";
            }
        }
        case 'D': { // duration in seconds
            int seconds = NTaglib::_tagRef->audioProperties()->length();
            if (seconds == 0) {
                return "";
            }
            return QString::number(seconds);
        }
        case 'B': { // bitrate in Kbps
            int res = NTaglib::_tagRef->audioProperties()->bitrate();
            if (res == 0) {
                return "";
            }
            return QString::number(res);
        }
        case 's': { // sample rate in kHz
            int res = NTaglib::_tagRef->audioProperties()->sampleRate();
            if (res == 0) {
                return "";
            }
            return QString::number(res / (float)1000);
        }
        case 'H': { // number of channels
            int res = NTaglib::_tagRef->audioProperties()->channels();
            if (res == 0) {
                return "";
            }
            return QString::number(res);
        }
        case 'k': { // musical key (ID3 TKEY, MP4/Vorbis INITIALKEY)
            TagLib::PropertyMap props = NTaglib::_tagRef->file()->properties();
            for (const char *name : {"INITIALKEY", "KEY"}) {
                if (props.contains(name) && !props[name].isEmpty()) {
                    return TStringToQString(props[name].toString());
                }
            }
            return "";
        }
        case 'l': { // record label (ID3 TPUB, MP4/Vorbis LABEL)
            TagLib::PropertyMap props = NTaglib::_tagRef->file()->properties();
            for (const char *name : {"LABEL", "PUBLISHER", "ORGANIZATION"}) {
                if (props.contains(name) && !props[name].isEmpty()) {
                    return TStringToQString(props[name].toString());
                }
            }
            return "";
        }
        case 'M': { // beats per minute
            return TStringToQString(NTaglib::_tagRef->file()->properties()["BPM"].toString());
        }
        default: // unsupported, convert to a tag and return
            return QString('%') + ch;
    }
}

N::Tag NTagReaderTaglib::tagFromKey(const QString &key) const
{
    if (key == "ALBUM") {
        return N::AlbumTag;
    } else if (key == "ARTIST") {
        return N::ArtistTag;
    } else if (key == "BPM") {
        return N::BpmTag;
    } else if (key == "COMMENT") {
        return N::CommentTag;
    } else if (key == "COMPOSER") {
        return N::ComposerTag;
    } else if (key == "COPYRIGHT") {
        return N::CopyrightTag;
    } else if (key == "ENCODEDBY") {
        return N::EncodedByTag;
    } else if (key == "GENRE") {
        return N::GenreTag;
    } else if (key == "LABEL" || key == "PUBLISHER") {
        return N::LabelTag;
    } else if (key == "INITIALKEY") {
        return N::KeyTag;
    } else if (key == "ALBUMARTIST") {
        return N::AlbumArtistTag;
    } else if (key == "REMIXER") {
        return N::RemixerTag;
    } else if (key == "TITLE") {
        return N::TitleTag;
    } else if (key == "TRACKNUMBER") {
        return N::TrackNumberTag;
    } else if (key == "URL") {
        return N::UrlTag;
    } else if (key == "DATE") {
        return N::DateTag;
    } else {
        return N::UnknownTag;
    }
}

QString NTagReaderTaglib::tagToKey(N::Tag tag) const
{
    switch (tag) {
        case N::AlbumTag:
            return "ALBUM";
        case N::ArtistTag:
            return "ARTIST";
        case N::BpmTag:
            return "BPM";
        case N::CommentTag:
            return "COMMENT";
        case N::ComposerTag:
            return "COMPOSER";
        case N::CopyrightTag:
            return "COPYRIGHT";
        case N::EncodedByTag:
            return "ENCODEDBY";
        case N::GenreTag:
            return "GENRE";
        case N::LabelTag:
            return "LABEL"; // ID3v2 TPUB, the record label
        case N::KeyTag:
            return "INITIALKEY"; // ID3v2 TKEY
        case N::AlbumArtistTag:
            return "ALBUMARTIST"; // ID3v2 TPE2
        case N::RemixerTag:
            return "REMIXER"; // ID3v2 TPE4
        case N::TitleTag:
            return "TITLE";
        case N::TrackNumberTag:
            return "TRACKNUMBER";
        case N::UrlTag:
            return "URL";
        case N::DateTag:
            return "DATE";
    }
}

QMap<QString, QStringList>
NTagReaderTaglib::TMapToQMap(const TagLib::Map<TagLib::String, TagLib::StringList> &tmap) const
{
    QMap<QString, QStringList> qmap;
    for (auto iter = tmap.begin(); iter != tmap.end(); ++iter) {
        QStringList values;
        TagLib::StringList tlist = iter->second;
        for (auto iter = tlist.begin(); iter != tlist.end(); ++iter) {
            values << toUnicode((*iter));
        }
        qmap[TStringToQString(iter->first)] = values;
    }
    return qmap;
}

TagLib::Map<TagLib::String, TagLib::StringList>
NTagReaderTaglib::QMapToTMap(const QMap<QString, QStringList> &qmap) const
{
    TagLib::Map<TagLib::String, TagLib::StringList> tmap;
    foreach (QString key, qmap.keys()) {
        TagLib::StringList tlist;
        QStringList values = qmap[key];
        foreach (QString value, values) {
            tlist.append(QStringToTString(value));
        }
        tmap.insert(QStringToTString(key), tlist);
    }
    return tmap;
}

QMap<QString, QStringList> NTagReaderTaglib::getTags() const
{
    if (!m_isValid) { // workaround to relay the error
        QMap<QString, QStringList> tags;
        tags["Error"] = QStringList() << "Invalid";
        return tags;
    }
    return TMapToQMap(NTaglib::_tagRef->file()->properties());
}

QMap<QString, QStringList> NTagReaderTaglib::setTags(const QMap<QString, QStringList> &tags)
{
    TagLib::PropertyMap properties(QMapToTMap(tags));
    QMap<QString, QStringList> unsaved = TMapToQMap(
        NTaglib::_tagRef->file()->setProperties(properties));
    fixMp4Key(properties);
    if (unsaved.isEmpty()) {
        bool success = saveFile();
        if (!success) { // workaround to relay the error
            unsaved["Error"] = QStringList() << "Write";
        }
    }
    return unsaved;
}

QMap<QString, QStringList> NTagReaderTaglib::updateTags(const QMap<QString, QStringList> &changes)
{
    QMap<QString, QStringList> failed;
    if (!m_isValid || !NTaglib::_tagRef || NTaglib::_tagRef->isNull()) {
        failed["Error"] = QStringList() << "Write";
        return failed;
    }

    // start from the file's own TagLib strings so untouched fields are
    // written back exactly as they were, whatever the display encoding
    TagLib::File *file = NTaglib::_tagRef->file();
    TagLib::PropertyMap properties = file->properties();
    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
        TagLib::String key = QStringToTString(it.key());
        if (it.value().isEmpty()) {
            properties.erase(key);
        } else {
            TagLib::StringList values;
            for (const QString &value : it.value()) {
                values.append(QStringToTString(value));
            }
            properties.replace(key, values);
        }
    }

    TagLib::PropertyMap unsupported = file->setProperties(properties);
    fixMp4Key(properties);
    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
        if (unsupported.contains(QStringToTString(it.key()))) {
            failed[it.key()] = it.value();
        }
    }
    if (!failed.isEmpty()) {
        return failed;
    }

    if (!saveFile()) {
        failed["Error"] = QStringList() << "Write";
    }
    return failed;
}

void NTagReaderTaglib::fixMp4Key(const TagLib::PropertyMap &properties)
{
    // TagLib reads the musical key from the "initialkey" atom that iTunes,
    // Mixed In Key and DJ software use, but writes it to "INITIALKEY", leaving
    // the old value where those apps look. Keep it in "initialkey" only.
    auto *mp4 = dynamic_cast<TagLib::MP4::File *>(NTaglib::_tagRef->file());
    if (!mp4 || !mp4->tag()) {
        return;
    }
    TagLib::MP4::Tag *tag = mp4->tag();
    tag->removeItem("----:com.apple.iTunes:INITIALKEY");
    if (properties.contains("INITIALKEY") && !properties["INITIALKEY"].isEmpty()) {
        tag->setItem("----:com.apple.iTunes:initialkey",
                     TagLib::MP4::Item(properties["INITIALKEY"]));
    } else {
        tag->removeItem("----:com.apple.iTunes:initialkey");
    }
}

void NTagReaderTaglib::setId3v2Version(int version)
{
    m_id3v2Version = (version == 4) ? 4 : 3;
}

bool NTagReaderTaglib::saveFile()
{
    // Write ID3v2 in the chosen version (v2.3 by default: what DJ software,
    // car stereos and Windows Explorer read most reliably), keep the other tag
    // types as they are, and don't add an ID3v1 tag to files that have none.
    TagLib::File *file = NTaglib::_tagRef->file();
#if TAGLIB_MAJOR_VERSION >= 2
    TagLib::ID3v2::Version version = (m_id3v2Version == 4) ? TagLib::ID3v2::v4
                                                           : TagLib::ID3v2::v3;
    // TagLib keeps an in-memory ID3v1 (MP3) or INFO (WAV) tag even when the
    // file has none and fills it in too; only write the types already on disk.
    if (auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(file)) {
        int tags = TagLib::MPEG::File::ID3v2;
        if (mpeg->hasID3v1Tag()) {
            tags |= TagLib::MPEG::File::ID3v1;
        }
        if (mpeg->hasAPETag()) {
            tags |= TagLib::MPEG::File::APE;
        }
        return mpeg->save(tags, TagLib::File::StripNone, version, TagLib::File::DoNotDuplicate);
    }
    if (auto *wav = dynamic_cast<TagLib::RIFF::WAV::File *>(file)) {
        int tags = TagLib::RIFF::WAV::File::ID3v2;
        if (wav->hasInfoTag()) {
            tags |= TagLib::RIFF::WAV::File::Info;
        }
        return wav->save(static_cast<TagLib::RIFF::WAV::File::TagTypes>(tags),
                         TagLib::File::StripNone, version);
    }
    if (auto *aiff = dynamic_cast<TagLib::RIFF::AIFF::File *>(file)) {
        return aiff->save(version);
    }
#else
    if (auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(file)) {
        int tags = TagLib::MPEG::File::ID3v2;
        if (mpeg->hasID3v1Tag()) {
            tags |= TagLib::MPEG::File::ID3v1;
        }
        if (mpeg->hasAPETag()) {
            tags |= TagLib::MPEG::File::APE;
        }
        return mpeg->save(tags, false, m_id3v2Version, false);
    }
    if (auto *wav = dynamic_cast<TagLib::RIFF::WAV::File *>(file)) {
        int tags = TagLib::RIFF::WAV::File::ID3v2;
        if (wav->hasInfoTag()) {
            tags |= TagLib::RIFF::WAV::File::Info;
        }
        return wav->save(static_cast<TagLib::RIFF::WAV::File::TagTypes>(tags), false,
                         m_id3v2Version);
    }
#endif
    return file->save();
}
