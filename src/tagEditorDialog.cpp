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

#include "tagEditorDialog.h"

#include "coverWidget.h"
#include "pluginLoader.h"
#include "settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QMetaEnum>
#include <QProgressDialog>
#include <QPushButton>
#include <QTabBar>
#include <QTextEdit>

#ifdef Q_OS_MAC
#include <sys/stat.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS 0x00400000
#endif
#endif

NTagEditorDialog::NTagEditorDialog(const QStringList &files, QWidget *parent) : QDialog(parent)
{
    ui.setupUi(this);
    // give the right-hand fields (Album artist, Label...) room as well
    ui.tagSplitter->setStretchFactor(0, 3);
    ui.tagSplitter->setStretchFactor(1, 2);
    ui.tagSplitter->setSizes(QList<int>() << 450 << 300);

    m_valid = false;
    m_hasChanges = false;
    m_loading = false;
    m_encodingUtf8Index = -1;
    m_encodingPreviousIndex = 0;
    m_encodingSettingsIndex = 0;

    m_tagReader = dynamic_cast<NTagReaderInterface *>(NPluginLoader::getPlugin(N::TagReader));
    m_tagReader->setId3v2Version(NSettings::instance()->value("TagEditorID3v2Version").toInt());

    QString settingsEncoding = NSettings::instance()->value("EncodingTrackInfo").toString();
    m_tagReader->setEncoding(settingsEncoding);

    // leave out the files that can't be edited right now
    QStringList editable;
    QStringList skipped;
    for (const QString &file : files) {
        QString reason;
        if (isReadable(file, &reason)) {
            m_tagReader->setSource(file);
            if (m_tagReader->getTags().value("Error").join("") == "Invalid") {
                reason = tr("this file type doesn't have tags");
            }
            m_tagReader->setSource(""); // release the file
        }
        if (reason.isEmpty()) {
            editable << file;
        } else {
            skipped << QFileInfo(file).fileName() + " (" + reason + ")";
        }
    }
    if (!skipped.isEmpty()) {
        QMessageBox msgBox(QMessageBox::Information, tr("Tag Editor"),
                           editable.isEmpty()
                               ? tr("These tracks can't be edited right now:")
                               : (skipped.size() == 1 ? tr("1 track will be left out:") : tr("%1 tracks will be left out:").arg(skipped.size())),
                           QMessageBox::Ok, parent);
        QStringList shown = skipped.mid(0, 12);
        if (skipped.size() > shown.size()) {
            shown << tr("and %n more", "", skipped.size() - shown.size());
        }
        msgBox.setInformativeText(shown.join("\n"));
        msgBox.exec();
    }
    if (editable.isEmpty()) {
        return;
    }

    connect(ui.buttonBox->button(QDialogButtonBox::Save), SIGNAL(clicked()), this,
            SLOT(onSaveClicked()));
    ui.buttonBox->button(QDialogButtonBox::Reset)->setText(tr("Revert"));

    if (editable.size() > 1) {
        m_files = editable;
        setupMultiTrack();
        m_valid = true;
        return;
    }

    m_file = editable.first();
    QString file = m_file;

    m_coverReader = dynamic_cast<NCoverReaderInterface *>(NPluginLoader::getPlugin(N::CoverReader));
    m_encodingUtf8Index = -1;
    m_hasChanges = false;

    ui.tabWidget->setCornerWidget(ui.encodingFrame);
    int i = 0;
    foreach (int mib, QTextCodec::availableMibs()) {
        QString codecName = QTextCodec::codecForMib(mib)->name();
        ui.encodingComboBox->addItem(codecName, mib);
        if (codecName == "UTF-8") {
            m_encodingUtf8Index = i;
        }
        if (codecName == settingsEncoding) {
            ui.encodingComboBox->setCurrentIndex(i);
            m_encodingPreviousIndex = i;
            m_encodingSettingsIndex = i;
        }
        ++i;
    }
    connect(ui.encodingResetButton, &QPushButton::clicked, [=] {
        ui.encodingComboBox->setCurrentIndex(m_encodingUtf8Index);
        on_encodingComboBox_activated(-1);
    });
    connect(ui.editAsUtf8Button, &QPushButton::clicked, [=] {
        ui.encodingComboBox->setCurrentIndex(m_encodingUtf8Index);
        m_encodingPreviousIndex = m_encodingUtf8Index;
        m_hasChanges = true;
        setReadOnlyMode(false);
    });
    on_encodingComboBox_activated(-1);

    setWindowTitle("\"" + QFileInfo(file).fileName() + "\" — " + tr("Tag Editor"));

    m_coverReader->setSource(m_file);
    QList<QImage> images = m_coverReader->getImages();
    for (QImage image : images) {
        QLabel *label = new QLabel;
        label->setFixedSize(100, 100);
        label->setScaledContents(true);
        label->setPixmap(QPixmap::fromImage(image));
        QListWidgetItem *widgetItem = new QListWidgetItem;
        widgetItem->setSizeHint(label->size());
        ui.artworkListWidget->addItem(widgetItem);
        ui.artworkListWidget->setItemWidget(widgetItem, label);
    }
    m_coverReader->setSource(""); // release the file
    ui.artworkListWidget->setMinimumHeight(111);

    connect(ui.buttonBox->button(QDialogButtonBox::Reset), &QPushButton::clicked, [=] {
        ui.encodingComboBox->setCurrentIndex(m_encodingSettingsIndex);
        readTags();
        setReadOnlyMode(ui.encodingComboBox->currentIndex() != m_encodingUtf8Index);
    });

    m_valid = true;
}

bool NTagEditorDialog::isReadable(const QString &file, QString *reason)
{
    QString why;
    QFileInfo info(file);
    if (!info.exists()) {
        why = tr("file not found");
    } else if (!info.isWritable()) {
        why = tr("the file is read-only");
    }
#ifdef Q_OS_MAC
    struct stat st;
    if (why.isEmpty() && ::stat(QFile::encodeName(file).constData(), &st) == 0 &&
        (st.st_flags & SF_DATALESS)) {
        why = tr("not downloaded from iCloud yet");
    }
#endif
#ifdef Q_OS_WIN
    DWORD attributes = GetFileAttributesW(
        reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(file).utf16()));
    if (why.isEmpty() && attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & (FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_OFFLINE))) {
        why = tr("not downloaded from the cloud yet");
    }
#endif
    if (reason) {
        *reason = why;
    }
    return why.isEmpty();
}

QWidget *NTagEditorDialog::fieldWidget(N::Tag tag) const
{
    QString enumKey = ENUMERATOR(N, Tag).valueToKey(tag);
    QString prefix = enumKey.replace(0, 1, enumKey[0].toLower());
    if (QLineEdit *lineEdit = ui.generalTab->findChild<QLineEdit *>(prefix + "LineEdit")) {
        return lineEdit;
    }
    return ui.generalTab->findChild<QPlainTextEdit *>(prefix + "PlainTextEdit");
}

void NTagEditorDialog::markChanged()
{
    m_hasChanges = true;
    ui.buttonBox->button(QDialogButtonBox::Save)->setEnabled(true);
    ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(true);
}

void NTagEditorDialog::setupMultiTrack()
{
    setWindowTitle(tr("Tag Editor") + " \u2014 " + tr("%1 tracks").arg(m_files.size()));

    // one General page: no raw tags, encoding or artwork for several tracks
    ui.tabWidget->removeTab(ui.tabWidget->indexOf(ui.rawTagsTab));
    ui.tabWidget->tabBar()->hide();
    ui.encodingFrame->hide();
    ui.label->hide();
    ui.artworkListWidget->hide();

    QLabel *hint = new QLabel(tr("Changes apply to all %n tracks. Fields marked (various) differ "
                                 "between them and are left as they are unless you type a new "
                                 "value.",
                                 "", m_files.size()));
    hint->setWordWrap(true);
    ui.verticalLayout->insertWidget(0, hint);

    QMetaEnum enumerator = ENUMERATOR(N, Tag);
    for (int index = 1; index < enumerator.keyCount(); ++index) { // skip UnknownTag
        N::Tag tag = (N::Tag)enumerator.value(index);
        QString key = m_tagReader->tagToKey(tag);
        QWidget *widget = fieldWidget(tag);
        if (!widget || key.isEmpty()) {
            continue;
        }
        if (tag == N::TitleTag || tag == N::TrackNumberTag) {
            // these are different for every track
            widget->setEnabled(false);
            widget->setToolTip(tr("Edit one track at a time to change this"));
            continue;
        }
        m_fieldWidgets[key] = widget;
        if (QLineEdit *lineEdit = qobject_cast<QLineEdit *>(widget)) {
            connect(lineEdit, &QLineEdit::textEdited, [=] {
                m_changedKeys.insert(key);
                markChanged();
            });
        } else if (QPlainTextEdit *textEdit = qobject_cast<QPlainTextEdit *>(widget)) {
            connect(textEdit, &QPlainTextEdit::textChanged, [=] {
                if (m_loading) {
                    return;
                }
                m_changedKeys.insert(key);
                markChanged();
            });
        }
    }

    connect(ui.buttonBox->button(QDialogButtonBox::Reset), &QPushButton::clicked,
            [=] { readMultiTrackTags(); });

    readMultiTrackTags();
}

void NTagEditorDialog::readMultiTrackTags()
{
    m_loading = true;
    m_changedKeys.clear();
    m_mixedKeys.clear();

    m_tagReader->setEncoding(NSettings::instance()->value("EncodingTrackInfo").toString());
    QList<QMap<QString, QStringList>> allTags;
    for (const QString &file : m_files) {
        m_tagReader->setSource(file);
        allTags << m_tagReader->getTags();
    }
    m_tagReader->setSource(""); // release the file

    for (auto it = m_fieldWidgets.constBegin(); it != m_fieldWidgets.constEnd(); ++it) {
        QSet<QString> values;
        for (const QMap<QString, QStringList> &tags : allTags) {
            values.insert(tags.value(it.key()).value(0));
        }
        bool mixed = values.size() > 1;
        QString text = mixed ? QString() : *values.constBegin();
        QString placeholder = mixed ? tr("(various)") : QString();
        if (mixed) {
            m_mixedKeys.insert(it.key());
        }
        if (QLineEdit *lineEdit = qobject_cast<QLineEdit *>(it.value())) {
            lineEdit->setText(text);
            lineEdit->setPlaceholderText(placeholder);
        } else if (QPlainTextEdit *textEdit = qobject_cast<QPlainTextEdit *>(it.value())) {
            textEdit->setPlainText(text);
            textEdit->setPlaceholderText(placeholder);
        }
    }

    m_hasChanges = false;
    ui.buttonBox->button(QDialogButtonBox::Save)->setEnabled(false);
    ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(false);
    m_loading = false;
}

bool NTagEditorDialog::writeMultiTrackTags()
{
    QMap<QString, QStringList> changes;
    for (const QString &key : m_changedKeys) {
        QWidget *widget = m_fieldWidgets.value(key);
        QString text;
        if (QLineEdit *lineEdit = qobject_cast<QLineEdit *>(widget)) {
            text = lineEdit->text().trimmed();
        } else if (QPlainTextEdit *textEdit = qobject_cast<QPlainTextEdit *>(widget)) {
            text = textEdit->toPlainText().trimmed();
        }
        if (text.isEmpty() && m_mixedKeys.contains(key)) {
            continue; // emptied a "different values" field: leave each track's value alone
        }
        changes[key] = text.isEmpty() ? QStringList() : QStringList(text);
    }
    if (changes.isEmpty()) {
        return true;
    }

    QProgressDialog progress(tr("Saving tags\u2026"), tr("Stop"), 0, m_files.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);

    QStringList failed;
    for (int i = 0; i < m_files.size(); ++i) {
        if (progress.wasCanceled()) {
            break;
        }
        progress.setValue(i);
        const QString &file = m_files.at(i);
        m_tagReader->setSource(file);
        QMap<QString, QStringList> unsaved = m_tagReader->updateTags(changes);
        m_tagReader->setSource(""); // release the file
        if (unsaved.isEmpty()) {
            if (!m_savedFiles.contains(file)) {
                m_savedFiles << file;
            }
        } else if (unsaved.contains("Error")) {
            failed << QFileInfo(file).fileName() + " (" + tr("couldn't write the file") + ")";
        } else {
            failed << QFileInfo(file).fileName() + " (" +
                          tr("not supported: %1").arg(unsaved.keys().join(", ")) + ")";
        }
    }
    progress.setValue(m_files.size());

    if (!failed.isEmpty()) {
        QMessageBox msgBox(QMessageBox::Warning, tr("Save Fail"),
                           (failed.size() == 1 ? tr("1 track couldn't be saved:") : tr("%1 tracks couldn't be saved:").arg(failed.size())),
                           QMessageBox::Close, this);
        QStringList shown = failed.mid(0, 12);
        if (failed.size() > shown.size()) {
            shown << tr("and %n more", "", failed.size() - shown.size());
        }
        msgBox.setInformativeText(shown.join("\n"));
        msgBox.exec();
    }
    return failed.isEmpty();
}

void NTagEditorDialog::setReadOnlyMode(bool readOnly)
{
    ui.generalTab->setEnabled(!readOnly);
    ui.rawTagsTab->setEnabled(!readOnly);
    ui.encodingResetButton->setVisible(readOnly);
    ui.readOnlyLabel->setVisible(readOnly);
    ui.editAsUtf8Button->setVisible(readOnly);
    ui.buttonBox->button(QDialogButtonBox::Save)->setEnabled(!readOnly && m_hasChanges);
    ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(!readOnly && m_hasChanges);
}

void NTagEditorDialog::on_encodingComboBox_activated(int index)
{
    if (m_hasChanges) {
        ui.encodingComboBox->setCurrentIndex(m_encodingPreviousIndex);
        QMessageBox msgBox(QMessageBox::Warning, tr("Warning"),
                           tr("Tags have been modified.") + QString("%1").arg(" ", 50, ' '),
                           QMessageBox::Close, this);
        msgBox.setInformativeText(tr("Save or revert the changes before switching encoding."));
        msgBox.exec();
        return;
    }
    if (index == -1) {
        index = ui.encodingComboBox->currentIndex();
    }

    setReadOnlyMode(index != m_encodingUtf8Index);
    m_encodingPreviousIndex = index;

    readTags();
}

void NTagEditorDialog::readTags()
{
    m_hasChanges = false;
    QString encoding = ui.encodingComboBox->itemText(ui.encodingComboBox->currentIndex());
    m_tagReader->setEncoding(encoding);

    QLayout *oldFormLayout = ui.rawTagsScrollArea->widget()->layout();
    if (oldFormLayout) {
        delete oldFormLayout;
        qDeleteAll(ui.rawTagsScrollArea->widget()->children());
    }

    // create raw widgets for standard tags:
    QRegularExpression widgetNameRegex("Tag");
    m_tagReader->setSource(m_file);
    QMap<QString, QStringList> tags = m_tagReader->getTags();
    m_tagReader->setSource(""); // release the file
    QFormLayout *rawTagsFormLayout = new QFormLayout;
    rawTagsFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    QMetaEnum enumerator = ENUMERATOR(N, Tag);
    for (int index = 1; index < enumerator.keyCount(); ++index) { // skip UnknownTag
        QString enumKey = enumerator.key(index);

        // create line edit for standard raw tag(s):
        QString tagKey = m_tagReader->tagToKey((N::Tag)enumerator.value(index));
        QString label = tagKey + ":";
        QStringList values = tags.take(tagKey);
        if (values.isEmpty()) { // create at least one line edit
            values << "";
        }
        foreach (QString value, values) {
            QLineEdit *rawTagLineEdit = new QLineEdit(value);
            rawTagsFormLayout->addRow(label, rawTagLineEdit);

            connect(rawTagLineEdit, &QLineEdit::textEdited, [=](const QString &newValue) {
                m_hasChanges = true;
                ui.buttonBox->button(QDialogButtonBox::Save)->setEnabled(true);
                ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(true);
            });

            if (label.isEmpty()) { // connect widgets only for the first tag value
                continue;
            }

            QString widgetNamePrefix = enumKey.replace(0, 1, enumKey[0].toLower());
            if (QLineEdit *standardTagLineEdit = ui.generalTab->findChild<QLineEdit *>(
                    widgetNamePrefix + "LineEdit")) {
                standardTagLineEdit->setText(values.first());
                QMetaObject::Connection connection =
                    connect(standardTagLineEdit, &QLineEdit::textEdited,
                            [=](const QString &newValue) {
                                rawTagLineEdit->setText(newValue);
                                m_hasChanges = true;
                                ui.buttonBox->button(QDialogButtonBox::Save)->setEnabled(true);
                                ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(true);
                            });
                connect(rawTagLineEdit, &QObject::destroyed,
                        [=] { QObject::disconnect(connection); });
                connect(rawTagLineEdit, &QLineEdit::textEdited,
                        [=](const QString &newValue) { standardTagLineEdit->setText(newValue); });
            } else if (QPlainTextEdit *standardTagPlainTextEdit =
                           ui.generalTab->findChild<QPlainTextEdit *>(widgetNamePrefix +
                                                                      "PlainTextEdit")) {
                standardTagPlainTextEdit->setPlainText(values.first());
                QMetaObject::Connection connection =
                    connect(standardTagPlainTextEdit, &QPlainTextEdit::textChanged, [=] {
                        rawTagLineEdit->setText(standardTagPlainTextEdit->toPlainText());
                        m_hasChanges = true;
                        ui.buttonBox->button(QDialogButtonBox::Save)->setEnabled(true);
                        ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(true);
                    });
                connect(rawTagLineEdit, &QObject::destroyed,
                        [=] { QObject::disconnect(connection); });
                connect(rawTagLineEdit, &QLineEdit::textEdited, [=](const QString &newValue) {
                    QTextCursor cursor = standardTagPlainTextEdit->textCursor();
                    standardTagPlainTextEdit->setPlainText(newValue);
                    standardTagPlainTextEdit->setTextCursor(cursor);
                });
            } else {
                QMessageBox::critical(NULL, tr("Match error"),
                                      tr("No widget for tag: %1").arg(enumKey), QMessageBox::Close);
            }

            label = "";
        }
    }

    // create line edits for the rest for raw tags:
    foreach (QString tagKey, tags.keys()) {
        QString label = tagKey + ":";
        QStringList values = tags.take(tagKey);

        foreach (QString value, values) {
            QLineEdit *rawTagLineEdit = new QLineEdit(value);
            rawTagsFormLayout->addRow(label, rawTagLineEdit);

            label = "";
        }
    }

    ui.rawTagsScrollArea->widget()->setLayout(rawTagsFormLayout);
    ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(false);
    ui.buttonBox->button(QDialogButtonBox::Save)->setEnabled(false);
}

bool NTagEditorDialog::writeTags()
{
    QMap<QString, QStringList> tags;

    QFormLayout *rawTagsFormLayout = qobject_cast<QFormLayout *>(
        ui.rawTagsScrollArea->widget()->layout());

    QString tagName;
    QStringList valueList;

    for (size_t i = 0; i < rawTagsFormLayout->rowCount(); ++i) {
        QLayoutItem *item = rawTagsFormLayout->itemAt(i, QFormLayout::LabelRole);
        if (item) {
            if (!valueList.isEmpty()) {
                tags[tagName] = valueList;
                valueList.clear();
            }
            QLabel *label = qobject_cast<QLabel *>(item->widget());
            tagName = label->text().chopped(1);
        }
        QString value = qobject_cast<QLineEdit *>(
                            rawTagsFormLayout->itemAt(i, QFormLayout::FieldRole)->widget())
                            ->text();
        if (!value.isEmpty()) {
            valueList << value;
        }
    }
    if (!valueList.isEmpty()) {
        tags[tagName] = valueList;
        valueList.clear();
    }

    m_tagReader->setSource(m_file);
    QMap<QString, QStringList> unsaved = m_tagReader->setTags(tags);
    m_tagReader->setSource(""); // release the file
    if (!unsaved.isEmpty()) {
        if (unsaved.value("Error").join("") == "Write") {
            QMessageBox msgBox(QMessageBox::Critical, tr("Write Fail"),
                               tr("Write operation did not succeed."), QMessageBox::Close, this);
            msgBox.exec();
        } else {
            QMessageBox msgBox(QMessageBox::Warning, tr("Save Fail"),
                               tr("Saving aborted. Failed tags: %1").arg(unsaved.keys().join(", ")),
                               QMessageBox::Close, this);
            msgBox.exec();
        }
        return false;
    }

    return true;
}

NTagEditorDialog::~NTagEditorDialog() {}

void NTagEditorDialog::onSaveClicked()
{
    if (NSettings::instance()->value("DisplayTagEditorConfirmDialog").toBool()) {
        QCheckBox *checkBox = new QCheckBox(tr("Don't show this dialog anymore"));

        QMessageBox msgBox(QMessageBox::Question, tr("Confirmation"),
                           tr("Do you want to save your changes?"),
                           QMessageBox::Save | QMessageBox::Cancel, this);
        msgBox.setDefaultButton(QMessageBox::Save);
        msgBox.setCheckBox(checkBox);
        int res = msgBox.exec();

        if (res != QMessageBox::Save) {
            return;
        }

        NSettings::instance()->setValue("DisplayTagEditorConfirmDialog", !checkBox->isChecked());
    }

    if (!m_files.isEmpty()) {
        writeMultiTrackTags();
        readMultiTrackTags();
        return;
    }

    if (writeTags()) {
        if (!m_savedFiles.contains(m_file)) {
            m_savedFiles << m_file;
        }
        readTags();
    }
}
