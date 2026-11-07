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

#ifndef N_TAG_EDITOR_DIALOG_H
#define N_TAG_EDITOR_DIALOG_H

#include <QDialog>
#include <QMap>
#include <QSet>
#include <QStringList>

#include "coverReaderInterface.h"
#include "global.h"
#include "pluginLoader.h"
#include "tagReaderInterface.h"
#include "ui_tagEditorDialog.h"

class QEvent;

class NTagEditorDialog : public QDialog
{
    Q_OBJECT

private:
    Ui::TagEditorDialog ui;
    NTagReaderInterface *m_tagReader;
    NCoverReaderInterface *m_coverReader;
    QString m_file;      // single-track mode
    QStringList m_files; // multi-track mode
    QStringList m_savedFiles;
    bool m_valid;
    int m_encodingPreviousIndex;
    int m_encodingUtf8Index;
    int m_encodingSettingsIndex;
    bool m_hasChanges;
    bool m_loading;
    QSet<QString> m_changedKeys;            // multi-track mode: fields the user edited
    QSet<QString> m_mixedKeys;              // multi-track mode: fields that differ between tracks
    QMap<QString, QWidget *> m_fieldWidgets; // multi-track mode: tag key -> editor
    void readTags();
    bool writeTags();
    void setupMultiTrack();
    void readMultiTrackTags();
    bool writeMultiTrackTags();
    void markChanged();
    QWidget *fieldWidget(N::Tag tag) const;

public:
    NTagEditorDialog(const QStringList &files, QWidget *parent = 0);
    ~NTagEditorDialog();

    bool isValid() const { return m_valid; }
    // files whose tags were saved, so the playlist can show the new values
    QStringList savedFiles() const { return m_savedFiles; }

    // false for files that can't be read without blocking: missing, or cloud
    // placeholders (iCloud Drive, OneDrive) that reading would download
    static bool isReadable(const QString &file, QString *reason = nullptr);

private slots:
    void on_encodingComboBox_activated(int index);
    void updateTagFormatLabel();
    void onSaveClicked();
    void setReadOnlyMode(bool readOnly);
};

#endif
