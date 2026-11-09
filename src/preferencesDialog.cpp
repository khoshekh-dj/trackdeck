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

#include "preferencesDialog.h"
#include "utils.h"

#include "action.h"
#include "i18nLoader.h"
#include "player.h"
#include "plugin.h"
#include "settings.h"
#include "skinFileSystem.h"
#ifndef _N_NO_UPDATE_CHECK_
#include "updateChecker.h"
#endif

#ifdef Q_OS_WIN
#include "w7TaskBar.h"
#endif

#ifndef _N_NO_SKINS_
#include "skinLoader.h"
#endif

#include <QApplication>
#include <QGroupBox>
#include <QColorDialog>
#include <QDir>
#include <QFileDialog>
#include <QComboBox>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpacerItem>
#include <QStyleFactory>
#include <QTextBrowser>
#include <QTextCodec>
#include <QVBoxLayout>

// Accent colour presets (hex, label)
static const QList<QPair<QString, QString>> _accentPresets = {
    {"#3D8BFF", "Electric blue"}, {"#1ED760", "Neon green"}, {"#FF3D9A", "Pink"},
    {"#FF8A3D", "Orange"},        {"#9B5CFF", "Purple"},     {"#FF4D5E", "Red"},
    {"#22C3C3", "Teal"},
};

static QIcon _swatch(const QColor &color)
{
    QPixmap pm(14, 14);
    pm.fill(color);
    return QIcon(pm);
}

// Drop-downs that store their choice in one setting: combo box name,
// setting, (value, label) choices. Labels are translated when added.
struct _ComboOption
{
    const char *combo;
    const char *setting;
    QList<QPair<QString, const char *>> choices;
};
static const QList<_ComboOption> _comboOptions = {
    {"waveformColorSchemeComboBox", "WaveformColorScheme",
     {
      {"rgb", QT_TRANSLATE_NOOP("NPreferencesDialog", "RGB (balanced)")},
      {"vivid", QT_TRANSLATE_NOOP("NPreferencesDialog", "Vivid RGB")},
      {"accent", QT_TRANSLATE_NOOP("NPreferencesDialog", "Accent colour")}}},
    {"openPlaylistBehaviourComboBox", "OpenPlaylistBehaviour",
     {
      {"ask", QT_TRANSLATE_NOOP("NPreferencesDialog", "Ask each time")},
      {"replace", QT_TRANSLATE_NOOP("NPreferencesDialog", "Replace the current list")},
      {"add", QT_TRANSLATE_NOOP("NPreferencesDialog", "Add to the current list")}}},
    {"id3v2VersionComboBox", "TagEditorID3v2Version",
     {
      {"3", QT_TRANSLATE_NOOP("NPreferencesDialog", "ID3v2.3 (most compatible)")},
      {"4", QT_TRANSLATE_NOOP("NPreferencesDialog", "ID3v2.4")}}},
    {"keyNotationComboBox", "KeyNotation",
     {
      {"tag", QT_TRANSLATE_NOOP("NPreferencesDialog", "As tagged")},
      {"camelot", QT_TRANSLATE_NOOP("NPreferencesDialog", "Camelot (8A, 8B)")},
      {"musical", QT_TRANSLATE_NOOP("NPreferencesDialog", "Musical (Am, C)")}}},
    {"visualiserStyleComboBox", "VisualiserStyle",
     {
      {"bars", QT_TRANSLATE_NOOP("NPreferencesDialog", "Bars")},
      {"segments", QT_TRANSLATE_NOOP("NPreferencesDialog", "LED segments")},
      {"mirrored", QT_TRANSLATE_NOOP("NPreferencesDialog", "Mirrored")},
      {"curve", QT_TRANSLATE_NOOP("NPreferencesDialog", "Smooth curve")}}},
    {"visualiserDensityComboBox", "VisualiserDensity",
     {
      {"wide", QT_TRANSLATE_NOOP("NPreferencesDialog", "Fewer, wider")},
      {"normal", QT_TRANSLATE_NOOP("NPreferencesDialog", "Normal")},
      {"narrow", QT_TRANSLATE_NOOP("NPreferencesDialog", "More, thinner")}}},
    {"visualiserSensitivityComboBox", "VisualiserSensitivity",
     {
      {"low", QT_TRANSLATE_NOOP("NPreferencesDialog", "Low")},
      {"normal", QT_TRANSLATE_NOOP("NPreferencesDialog", "Normal")},
      {"high", QT_TRANSLATE_NOOP("NPreferencesDialog", "High")}}},
    {"visualiserTrebleBoostComboBox", "VisualiserTrebleBoost",
     {
      {"off", QT_TRANSLATE_NOOP("NPreferencesDialog", "Off")},
      {"normal", QT_TRANSLATE_NOOP("NPreferencesDialog", "Normal")},
      {"strong", QT_TRANSLATE_NOOP("NPreferencesDialog", "Strong")}}},
    {"visualiserFallSpeedComboBox", "VisualiserFallSpeed",
     {
      {"slow", QT_TRANSLATE_NOOP("NPreferencesDialog", "Slow")},
      {"normal", QT_TRANSLATE_NOOP("NPreferencesDialog", "Normal")},
      {"fast", QT_TRANSLATE_NOOP("NPreferencesDialog", "Fast")}}},
    {"visualiserFrameRateComboBox", "VisualiserFrameRate",
     {
      {"30", QT_TRANSLATE_NOOP("NPreferencesDialog", "30 per second (lighter)")},
      {"60", QT_TRANSLATE_NOOP("NPreferencesDialog", "60 per second (smoother)")}}},
    {"visualiserColorsComboBox", "VisualiserColors",
     {
      {"waveform", QT_TRANSLATE_NOOP("NPreferencesDialog", "Follow waveform colours")},
      {"accent", QT_TRANSLATE_NOOP("NPreferencesDialog", "Accent colour")},
      {"gradient", QT_TRANSLATE_NOOP("NPreferencesDialog", "Custom gradient")}}},
};

// a colour button shows its colour as an icon and keeps it in a property
static void _setButtonColor(QPushButton *button, const QColor &color)
{
    button->setIcon(_swatch(color));
    button->setProperty("color", color.name(QColor::HexRgb).toUpper());
}


using namespace NPluginLoader;

static QStringList vNames = QStringList() << "Top"
                                          << "Middle"
                                          << "Bottom";
static QStringList hNames = QStringList() << "Left"
                                          << "Center"
                                          << "Right";
static const char *LANGUAGE = QT_TRANSLATE_NOOP("PreferencesDialog", "English");

NPreferencesDialog::~NPreferencesDialog() {}

NPreferencesDialog::NPreferencesDialog(NPlayer *player, QWidget *parent) : QDialog(parent)
{
    m_player = player;

    ui.setupUi(this);
    for (const _ComboOption &option : _comboOptions) {
        QComboBox *combo = findChild<QComboBox *>(option.combo);
        for (const auto &choice : option.choices) {
            combo->addItem(tr(choice.second), choice.first);
        }
    }
    connect(ui.waveformRgbCheckBox, &QCheckBox::toggled, ui.waveformColorSchemeComboBox,
            &QWidget::setEnabled);
    // "Custom..." opens a colour picker and becomes a regular entry
    connect(ui.accentColorComboBox, QOverload<int>::of(&QComboBox::activated), [this](int index) {
        if (ui.accentColorComboBox->itemData(index).toString() != "custom") {
            return;
        }
        QColor current(NSettings::instance()->value("AccentColor").toString());
        QColor picked = QColorDialog::getColor(current, this, tr("Accent colour"));
        if (!picked.isValid()) {
            ui.accentColorComboBox->setCurrentIndex(0);
            return;
        }
        QString hex = picked.name(QColor::HexRgb).toUpper();
        int existing = ui.accentColorComboBox->findData(hex);
        if (existing < 0) {
            ui.accentColorComboBox->insertItem(index, _swatch(picked), hex, hex);
            existing = index;
        }
        ui.accentColorComboBox->setCurrentIndex(existing);
    });
    connect(ui.playlistColumnsCheckBox, &QCheckBox::toggled, ui.playlistColumnsList,
            &QWidget::setEnabled);

    // playlists >>
    connect(ui.playlistsFolderBrowseButton, &QPushButton::clicked, [this]() {
        QString current = ui.playlistsFolderLineEdit->text();
        if (current.startsWith("~/")) {
            current = QDir::homePath() + current.mid(1);
        }
        QString dir = QFileDialog::getExistingDirectory(this, tr("Playlists Folder"), current);
        if (!dir.isEmpty()) {
            ui.playlistsFolderLineEdit->setText(dir);
        }
    });
    // << playlists

    // visualiser >>
    connect(ui.visualiserCheckBox, &QCheckBox::toggled, ui.visualiserOptions, &QWidget::setEnabled);
    connect(ui.visualiserColorsComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this]() {
                bool gradient = ui.visualiserColorsComboBox->currentData().toString() == "gradient";
                ui.visualiserGradientFromButton->setVisible(gradient);
                ui.visualiserGradientToButton->setVisible(gradient);
            });
    for (QPushButton *button : {ui.visualiserGradientFromButton, ui.visualiserGradientToButton}) {
        connect(button, &QPushButton::clicked, [this, button]() {
            QColor picked = QColorDialog::getColor(QColor(button->property("color").toString()),
                                                   this, button->toolTip());
            if (picked.isValid()) {
                _setButtonColor(button, picked);
            }
        });
    }
    // << visualiser

    connect(ui.buttonBox->button(QDialogButtonBox::Apply), SIGNAL(clicked()), this,
            SLOT(saveSettings()));
    connect(this, SIGNAL(accepted()), this, SLOT(saveSettings()));
    ui.buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Ok"));
    ui.buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    ui.buttonBox->button(QDialogButtonBox::Apply)->setText(tr("Apply"));

    connect(ui.singleInstanceCheckBox, SIGNAL(toggled(bool)), ui.enqueueFilesCheckBox,
            SLOT(setEnabled(bool)));
    connect(ui.enqueueFilesCheckBox, SIGNAL(toggled(bool)), ui.playEnqueuedCheckBox,
            SLOT(setEnabled(bool)));
    connect(ui.singleInstanceCheckBox, SIGNAL(toggled(bool)), ui.playEnqueuedCheckBox,
            SLOT(setEnabled(bool)));
    connect(ui.restorePlaylistCheckBox, SIGNAL(toggled(bool)), ui.startPausedCheckBox,
            SLOT(setEnabled(bool)));
    connect(ui.customFileManagerCheckBox, SIGNAL(toggled(bool)),
            ui.customFileManagerCommandLineEdit, SLOT(setEnabled(bool)));
    connect(ui.customTrashCheckBox, SIGNAL(toggled(bool)), ui.customTrashCommandLineEdit,
            SLOT(setEnabled(bool)));

    setWindowTitle(QCoreApplication::applicationName() + tr(" Preferences"));

#ifdef _N_NO_SKINS_
    ui.skinLabel->hide();
    ui.skinComboBox->hide();
#endif

#ifndef Q_OS_WIN
    ui.taskbarProgressContainer->hide();
#endif

#ifdef _N_NO_UPDATE_CHECK_
    ui.autoCheckUpdatesContainer->hide();
#else
    connect(&NUpdateChecker::instance(), &NUpdateChecker::versionChanged, this,
            &NPreferencesDialog::setVersionLabel);
#endif

#if defined Q_OS_WIN || defined Q_OS_MAC
    ui.customFileManagerContainer->hide();
    ui.customTrashContainer->hide();
#endif

#if defined Q_OS_MAC
    ui.minimizeToTrayContainer->hide();
#else
    ui.quitOnCloseContainer->hide();
#endif

    QRegularExpression re("Container$");
    for (QWidget *widget : ui.generalTab->findChildren<QWidget *>(re)) {
        if (widget->objectName() == "fileFiltersContainer") {
            continue;
        }
        int height = 27;
#if defined Q_OS_MAC
        height = 35;
#endif
        widget->setMinimumHeight(height);
        widget->setMaximumHeight(height);
    }

    QVBoxLayout *scrollLayout = new QVBoxLayout;
    ui.pluginsScrollArea->widget()->setLayout(scrollLayout);

    NFlagIterator<N::PluginType> iter(N::MaxPlugin);
    while (iter.hasNext()) {
        iter.next();
        N::PluginType type = iter.value();
        QGroupBox *groupBox = createGroupBox(type);
        if (groupBox) {
            scrollLayout->addWidget(groupBox);
        }
    }

    if (scrollLayout->count() > 0) {
        scrollLayout->addItem(
            new QSpacerItem(10, 10, QSizePolicy::Expanding, QSizePolicy::Expanding));
    } else {
        ui.tabWidget->removeTab(ui.tabWidget->indexOf(ui.pluginsTab));
    }

    ui.pluginsRestartLabel->setText(ui.pluginsRestartLabel->text());
    ui.pluginsRestartLabel->setVisible(false);

    ui.languageRestartLabel->setText(ui.languageRestartLabel->text());
    ui.languageRestartLabel->setVisible(false);

    ui.skinRestartLabel->setText(ui.skinRestartLabel->text());
    ui.skinRestartLabel->setVisible(false);
    connect(ui.skinComboBox, SIGNAL(activated(int)), ui.skinRestartLabel, SLOT(show()));

    ui.styleRestartLabel->setText(ui.styleRestartLabel->text());
    ui.styleRestartLabel->setVisible(false);
    connect(ui.styleComboBox, SIGNAL(activated(int)), ui.styleRestartLabel, SLOT(show()));

    ui.waveformTrackInfoTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    NUtils::fillEncodingComboBox(ui.encodingTrackInfoComboBox, "UTF-8");
}

void NPreferencesDialog::showEvent(QShowEvent *event)
{
    loadSettings();
    QDialog::showEvent(event);

    resize(ui.generalScrollAreaContents->sizeHint() + QSize(200, 0));

#ifndef _N_NO_UPDATE_CHECK_
    setVersionLabel(NUpdateChecker::instance().version());
#endif
}

#ifndef _N_NO_UPDATE_CHECK_
void NPreferencesDialog::setVersionLabel(const QString &version)
{
    if (!version.isEmpty()) {
        ui.versionLabel->setText(tr("Latest: ") + version);
    }
}

void NPreferencesDialog::on_versionCheckButton_clicked()
{
    ui.versionLabel->setText(tr("Checking..."));
    NUpdateChecker::instance().checkOnline();
}
#endif

void NPreferencesDialog::on_fileManagerHelpButton_clicked()
{
    QDialog *dialog = new QDialog(this);
    dialog->setWindowTitle(tr("File Manager Configuration"));

    QVBoxLayout *layout = new QVBoxLayout;
    dialog->setLayout(layout);

    QTextBrowser *textBrowser = new QTextBrowser(this);
    // clang-format off
    textBrowser->setHtml(
        tr("Supported parameters:") +
        "<ul>" +
            "<li><b>%F</b> - " + tr("File name") + "</li>" +
            "<li><b>%p</b> - " + tr("File name including absolute path") + "</li>" +
            "<li><b>%P</b> - " + tr("Directory path without file name") + "</li>" +
        "</ul>" +
        tr("Examples:") +
        "<ul style=\"font-family: 'Lucida Console', Monaco, monospace\">" +
            "<li>open -a '/Applications/Path Finder.app' '%p'</li>" +
            "<li>pcmanfm -n '%P' & sleep 1.5 && xdotool type '%F' && xdotool key Escape</li>" +
        "</ul>");
    // clang-format on
    textBrowser->setStyleSheet("QTextBrowser { background: transparent }");
    textBrowser->setFrameShape(QFrame::NoFrame);

    layout->addWidget(textBrowser);

    QPushButton *closeButton = new QPushButton(tr("Close"));
    connect(closeButton, SIGNAL(clicked()), dialog, SLOT(accept()));
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);
    buttonLayout->addStretch();
    layout->addLayout(buttonLayout);

    dialog->show();
    dialog->resize(640, 300);
}

void NPreferencesDialog::on_customTrashHelpButton_clicked()
{
#if !defined Q_OS_WIN && !defined Q_OS_MAC
    QDialog *dialog = new QDialog(this);
    dialog->setWindowTitle(tr("Trash Command Configuration"));

    QVBoxLayout *layout = new QVBoxLayout;
    dialog->setLayout(layout);

    QTextBrowser *textBrowser = new QTextBrowser(this);
    // clang-format off
    textBrowser->setHtml(
        tr("Supported parameters:") +
        "<ul>" +
            "<li><b>%F</b> - " + tr("File name") +
            "</li>" + "<li><b>%p</b> - " + tr("File name including absolute path") +
            "</li>" + "<li><b>%P</b> - " + tr("Directory path without file name") +
            "</li>" +
        "</ul>" +
        tr("Examples:") +
        "<ul style=\"font-family: 'Lucida Console', Monaco, monospace\">" +
            "<li>trash-put '%p'</li>" +
            "<li>mkdir -p \"$HOME/.Trash\" && mv '%p' \"$HOME/.Trash/\"</li>" +
        "</ul>");
    // clang-format on
    textBrowser->setStyleSheet("QTextBrowser { background: transparent }");
    textBrowser->setFrameShape(QFrame::NoFrame);

    layout->addWidget(textBrowser);

    QPushButton *closeButton = new QPushButton(tr("Close"));
    connect(closeButton, SIGNAL(clicked()), dialog, SLOT(accept()));
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);
    buttonLayout->addStretch();
    layout->addLayout(buttonLayout);

    dialog->show();
    dialog->resize(640, 300);
#endif
}

void NPreferencesDialog::on_titleFormatHelpButton_clicked()
{
    QDialog *dialog = new QDialog(this);
    dialog->setWindowTitle(tr("Title Formats"));

    QVBoxLayout *layout = new QVBoxLayout;
    dialog->setLayout(layout);

    QTextBrowser *textBrowser = new QTextBrowser(this);
    // clang-format off
    textBrowser->setHtml(
        tr("Supported parameters:") +
        "<ul>" +
            "<li><b>%a</b> - " + tr("Artist") + "</li>" +
            "<li><b>%t</b> - " + tr("Title") + "</li>" +
            "<li><b>%A</b> - " + tr("Album") + "</li>" +
            "<li><b>%c</b> - " + tr("Comment") + "</li>" +
            "<li><b>%g</b> - " + tr("Genre") + "</li>" +
            "<li><b>%y</b> - " + tr("Year") + "</li>" +
            "<li><b>%n</b> - " + tr("Track number") + "</li>" +
            "<li><b>%i</b> - " + tr("Track index in playlist (Playlist only)") + "</li>" +
            "<li><b>%T</b> - " + tr("Elapsed playback time (Waveform only)") + "</li>" +
            "<li><b>%r</b> - " + tr("Remaining playback time (Waveform only)") + "</li>" +
            "<li><b>%C</b> - " + tr("Time position under cursor (Tooltip only)") + "</li>" +
            "<li><b>%o</b> - " + tr("Time offset under cursor (Tooltip only)") + "</li>" +
            "<li><b>%d</b> - " + tr("Duration in format hh:mm:ss") + "</li>" +
            "<li><b>%D</b> - " + tr("Duration in seconds") + "</li>" +
            "<li><b>%L</b> - " + tr("Playlist duration in format hh:mm:ss") + "</li>" +
            "<li><b>%b</b> - " + tr("Bit depth") + "</li>" +
            "<li><b>%B</b> - " + tr("Bitrate in Kbps") + "</li>" +
            "<li><b>%s</b> - " + tr("Sample rate in kHz") + "</li>" +
            "<li><b>%H</b> - " + tr("Number of channels") + "</li>" +
            "<li><b>%M</b> - " + tr("BPM (beats per minute); a * means TrackDeck detected it") + "</li>" +
            "<li><b>%k</b> - " + tr("Musical key") + "</li>" +
            "<li><b>%l</b> - " + tr("Record label") + "</li>" +
            "<li><b>%f</b> - " + tr("File name without extension") + "</li>" +
            "<li><b>%F</b> - " + tr("File name") + "</li>" +
            "<li><b>%p</b> - " + tr("File name including absolute path") + "</li>" +
            "<li><b>%P</b> - " + tr("Directory path without file name") + "</li>" +
            "<li><b>%N</b> - " + tr("Directory name") + "</li>" +
            "<li><b>%e</b> - " + tr("File name extension") + "</li>" +
            "<li><b>%E</b> - " + tr("File name extension in uppercase") + "</li>" +
            "<li><b>%v</b> - " + tr("Application version number") + "</li>" +
            "<li><b>{</b> - " + tr("Start of a condition block. Use <b>\\{</b> to print <b>{</b> character.") + "</li>" +
            "<li><b>}</b> - " + tr("End of a condition block. Use <b>\\}</b> to print <b>}</b> character.") + "</li>" +
            "<li><b>|</b> - " + tr("Alternative separator, to be used inside a condition block. Use <b>\\|</b> to print <b>|</b> character.") + "</li>" +
            "<li><b>\\%</b> - " + tr("Print <b>%</b> character") + "</li>" +
        "</ul>" +
        tr("Examples:") +
        "<ul>" +
            "<li><b>%g</b> - " + tr("Print Genre. If not available, print nothing.") + "</li>" +
            "<li><b>{Comment: %c}</b> - " + tr("Print \"Comment: &lt;comment text&gt;\". If not available, print nothing.") + "</li>" +
            "<li><b>{%a - %t|%F}</b> - " + tr("Print Artist and Title, separated with \"-\". If either of the tags is not available, print file name instead.") + "</li>"
            "<li><b>{%B/%s|{%B}{%s}}</b> - " + tr("Print Bitrate and Sample rate, separated with \"/\". If either of the tags is not available, first try to print Bitrate, then try to print Sample rate.") + "</li>"
        "</ul>");
    // clang-format on
    textBrowser->setStyleSheet("QTextBrowser { background: transparent }");
    textBrowser->setFrameShape(QFrame::NoFrame);

    layout->addWidget(textBrowser);

    QPushButton *closeButton = new QPushButton(tr("Close"));
    connect(closeButton, SIGNAL(clicked()), dialog, SLOT(accept()));
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);
    buttonLayout->addStretch();
    layout->addLayout(buttonLayout);

    dialog->show();
    dialog->resize(640, 480);
}

QGroupBox *NPreferencesDialog::createGroupBox(N::PluginType type)
{
    QList<Descriptor> descriptors = NPluginLoader::descriptors();

    QString typeString = ENUM_TO_STR(N, PluginType, type);
    QString settingsContainer = NSettings::instance()->value("Plugins/" + typeString).toString();

    QList<int> indexesFilteredByType;
    for (int i = 0; i < descriptors.count(); ++i) {
        if (descriptors.at(i)[TypeRole] == type) {
            indexesFilteredByType << i;
        }
    }

    if (indexesFilteredByType.count() < 2) { // need at least two plugins to choose from
        return NULL;
    }

    QGroupBox *groupBox = new QGroupBox(typeString);
    QVBoxLayout *layout = new QVBoxLayout;
    layout->setContentsMargins(0, 5, 5, 0);
    layout->setSpacing(0);
    groupBox->setLayout(layout);

    foreach (int i, indexesFilteredByType) {
        QString containerName = descriptors.at(i)[ContainerNameRole].toString();
        QRadioButton *button = new QRadioButton(containerName);
        connect(button, SIGNAL(toggled(bool)), ui.pluginsRestartLabel, SLOT(show()));
        if (containerName == settingsContainer) {
            button->setChecked(true);
        }
        m_radioButtons[button] = descriptors.at(i);
        layout->addWidget(button);
    }

    return groupBox;
}

void NPreferencesDialog::on_languageComboBox_activated(int index)
{
    ui.languageRestartLabel->setVisible(true);

    QLocale locale = ui.languageComboBox->itemData(index).toLocale();
    QString newText = NI18NLoader::translate(locale.language(), "PreferencesDialog",
                                             "Switching languages requires restart");
    ui.languageRestartLabel->setText(
        ui.languageRestartLabel->text().replace(QRegExp("(.*)&nbsp;.*"), "\\1&nbsp;" + newText));
}

QString NPreferencesDialog::selectedContainer(N::PluginType type)
{
    QList<Descriptor> descriptors = NPluginLoader::descriptors();
    QList<int> indexesFilteredByType;
    for (int i = 0; i < descriptors.count(); ++i) {
        if (descriptors.at(i)[TypeRole] == type) {
            indexesFilteredByType << i;
        }
    }

    QString containerName;
    foreach (int i, indexesFilteredByType) {
        QRadioButton *button = m_radioButtons.key(descriptors.at(i));
        if (button && button->isChecked()) {
            containerName = descriptors.at(i)[ContainerNameRole].toString();
            break;
        }
    }

    return containerName;
}


// Columns offered in Preferences: "Header|track info format[|r]" (r = right-aligned)
static const QStringList _playlistColumnCatalog = {
    "Artist|%a",       "Title|{%t|%F}",  "Album|%A",           "Genre|%g",
    "Key|%k",          "Label|%l",       "BPM|%M|r",           "Year|%y|r",
    "Track|%n|r",      "Bitrate|{%B kbps}|r", "Duration|%d|r",
};

void NPreferencesDialog::loadSettings()
{
    for (const _ComboOption &option : _comboOptions) {
        QComboBox *combo = findChild<QComboBox *>(option.combo);
        int index = combo->findData(NSettings::instance()->value(option.setting).toString());
        combo->setCurrentIndex(qMax(0, index));
    }

    // general >>
    QList<QWidget *> widgets = findChildren<QWidget *>();
    foreach (QWidget *widget, widgets) {
        if (!widget->inherits("QCheckBox") && !widget->inherits("QLineEdit") &&
            !widget->inherits("NMultiLineEdit") && !widget->inherits("QAbstractSpinBox")) {
            continue;
        }
        QString className =
            QString(widget->metaObject()->className()).mid(1); // remove leading 'Q' or 'N'
        QString objectName = widget->objectName();
        if (objectName == "qt_spinbox_lineedit") { // QSpinBox inner widget
            continue;
        }
        if (objectName.startsWith("tooltipOffset")) {
            continue;
        }
        QString settingsName = objectName;
        settingsName.remove(className);
        settingsName[0] = settingsName.at(0).toUpper();
        if (widget->inherits("QCheckBox")) {
            qobject_cast<QCheckBox *>(widget)->setChecked(
                NSettings::instance()->value(settingsName).toBool());
        } else if (widget->inherits("QLineEdit")) {
            qobject_cast<QLineEdit *>(widget)->setText(
                NSettings::instance()->value(settingsName).toString());
        } else if (widget->inherits("NMultiLineEdit")) {
            qobject_cast<NMultiLineEdit *>(widget)->setText(
                NSettings::instance()->value(settingsName).toString());
        } else if (widget->inherits("QDoubleSpinBox")) {
            qobject_cast<QDoubleSpinBox *>(widget)->setValue(
                NSettings::instance()->value(settingsName).toDouble());
        }
    }

    ui.startPausedCheckBox->setEnabled(NSettings::instance()->value("RestorePlaylist").toBool());
    ui.enqueueFilesCheckBox->setEnabled(NSettings::instance()->value("SingleInstance").toBool());
    ui.playEnqueuedCheckBox->setEnabled(NSettings::instance()->value("SingleInstance").toBool() &&
                                        NSettings::instance()->value("EnqueueFiles").toBool());
    ui.customFileManagerCommandLineEdit->setEnabled(
        NSettings::instance()->value("CustomFileManager").toBool());
    ui.customTrashCommandLineEdit->setEnabled(NSettings::instance()->value("CustomTrash").toBool());
    ui.versionLabel->setText("");
    // << general

    // track info overlay >>
    for (int i = 0; i < ui.waveformTrackInfoTable->rowCount(); ++i) {
        for (int j = 0; j < ui.waveformTrackInfoTable->columnCount(); ++j) {
            QString objecName = ui.waveformTrackInfoTable->verticalHeaderItem(i)->text() +
                                ui.waveformTrackInfoTable->horizontalHeaderItem(j)->text();
            QTableWidgetItem *item = new QTableWidgetItem(
                NSettings::instance()->value("TrackInfo/" + vNames.at(i) + hNames.at(j)).toString());
            item->setTextAlignment(Qt::AlignCenter);
            ui.waveformTrackInfoTable->setItem(i, j, item);
        }
    }

    int height = ui.waveformTrackInfoTable->horizontalHeader()->height();
    for (int i = 0; i < ui.waveformTrackInfoTable->rowCount(); ++i) {
        height += ui.waveformTrackInfoTable->rowHeight(i);
    }
    height += 2; // frame
    ui.waveformTrackInfoTable->setMaximumHeight(height);
    // << track info overlay

    // waveform colours & accent >>
    {
        ui.waveformColorSchemeComboBox->setEnabled(ui.waveformRgbCheckBox->isChecked());

        QString accent = NSettings::instance()->value("AccentColor").toString().toUpper();
        ui.accentColorComboBox->clear();
        for (const auto &preset : _accentPresets) {
            ui.accentColorComboBox->addItem(_swatch(QColor(preset.first)), preset.second, preset.first);
        }
        if (ui.accentColorComboBox->findData(accent) < 0 && QColor(accent).isValid()) {
            ui.accentColorComboBox->addItem(_swatch(QColor(accent)), accent, accent);
        }
        ui.accentColorComboBox->addItem(tr("Custom..."), "custom");
        ui.accentColorComboBox->setCurrentIndex(qMax(0, ui.accentColorComboBox->findData(accent)));
    }
    // << waveform colours & accent

    // visualiser >>
    _setButtonColor(ui.visualiserGradientFromButton,
                    QColor(NSettings::instance()->value("VisualiserGradientFrom").toString()));
    _setButtonColor(ui.visualiserGradientToButton,
                    QColor(NSettings::instance()->value("VisualiserGradientTo").toString()));
    {
        bool gradient = ui.visualiserColorsComboBox->currentData().toString() == "gradient";
        ui.visualiserGradientFromButton->setVisible(gradient);
        ui.visualiserGradientToButton->setVisible(gradient);
    }
    ui.visualiserOptions->setEnabled(ui.visualiserCheckBox->isChecked());
    // << visualiser

    // playlist columns >>
    {
        ui.playlistColumnsList->clear();
        QStringList enabled = NSettings::instance()->value("PlaylistColumnsFormat").toStringList();
        QStringList all = enabled;
        for (const QString &spec : _playlistColumnCatalog) {
            if (!all.contains(spec)) {
                all << spec;
            }
        }
        for (const QString &spec : all) {
            QListWidgetItem *item = new QListWidgetItem(spec.section('|', 0, 0),
                                                        ui.playlistColumnsList);
            item->setData(Qt::UserRole, spec);
            item->setToolTip(spec.section('|', 1).remove(QRegExp("\\|r$")));
            item->setFlags((item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled) &
                           ~Qt::ItemIsDropEnabled);
            item->setCheckState(enabled.contains(spec) ? Qt::Checked : Qt::Unchecked);
        }
        ui.playlistColumnsList->setEnabled(ui.playlistColumnsCheckBox->isChecked());
    }
    // << playlist columns

    // track info encoding >>
    NUtils::fillEncodingComboBox(ui.encodingTrackInfoComboBox,
                                 NSettings::instance()->value("EncodingTrackInfo").toString());
    // << track info encoding

    // tooltip offset >>
    QStringList offsetList = NSettings::instance()->value("TooltipOffset").toStringList();
    ui.tooltipOffsetXSpinBox->setValue(offsetList.at(0).toInt());
    ui.tooltipOffsetYSpinBox->setValue(offsetList.at(1).toInt());
    // << tooltip offset

    // skins >>
#ifndef _N_NO_SKINS_
    int skinIndex;
    ui.skinComboBox->clear();
    foreach (QString str, NSkinLoader::skinIdentifiers()) {
        QString id = str.section('/', 2);
        QString userData = id;
        QString text = id.replace('/', ' ').replace(" (Built-in)", tr(" (Built-in)"));
        ui.skinComboBox->addItem(text, userData);
    }

    if (ui.skinComboBox->count() == 1) {
        ui.skinComboBox->setEnabled(false);
    }

    skinIndex = ui.skinComboBox->findData(NSettings::instance()->value("Skin"));
    if (skinIndex != -1) {
        ui.skinComboBox->setCurrentIndex(skinIndex);
    }
#endif
    // << skins

    // translations >>
    int localeIndex;
    ui.languageComboBox->clear();
    foreach (QLocale::Language language, NI18NLoader::translations()) {
        QString languageString = QLocale::languageToString(language);
        QString localizedString = NI18NLoader::translate(language, "PreferencesDialog", LANGUAGE);
        ui.languageComboBox->addItem(QString("%1 (%2)").arg(localizedString).arg(languageString),
                                     QLocale(language));
    }

    if (ui.languageComboBox->count() == 1) {
        ui.languageComboBox->setEnabled(false);
    }

    localeIndex = ui.languageComboBox->findData(
        QLocale(NSettings::instance()->value("Language").toString()));
    if (localeIndex != -1) {
        ui.languageComboBox->setCurrentIndex(localeIndex);
    }
    // << translations

    // styles >>
    ui.styleComboBox->clear();
    foreach (QString str, QStyleFactory::keys()) {
        ui.styleComboBox->addItem(str);
    }

    if (ui.styleComboBox->count() == 1) {
        ui.styleComboBox->setEnabled(false);
    }

    int styleIndex = ui.styleComboBox->findText(NSettings::instance()->value("Style").toString());
    if (styleIndex != -1) {
        ui.styleComboBox->setCurrentIndex(styleIndex);
    }
    // << styles

    // shortcuts >>
    QList<NAction *> actionList;
    for (NAction *action : m_player->findChildren<NAction *>()) {
        if (action->objectName().isEmpty() || !action->isCustomizable()) {
            continue;
        }
        actionList << action;
    }
    ui.shortcutEditorWidget->init(actionList);
    // << shortcuts
}

void NPreferencesDialog::saveSettings()
{
    for (const _ComboOption &option : _comboOptions) {
        NSettings::instance()->setValue(option.setting,
                                        findChild<QComboBox *>(option.combo)->currentData());
    }

    // general >>
    QList<QWidget *> widgets = findChildren<QWidget *>();
    foreach (QWidget *widget, widgets) {
        if (!widget->inherits("QCheckBox") && !widget->inherits("QLineEdit") &&
            !widget->inherits("NMultiLineEdit") && !widget->inherits("QAbstractSpinBox")) {
            continue;
        }
        QString className =
            QString(widget->metaObject()->className()).mid(1); // remove leading 'Q' or 'N'
        QString objectName = widget->objectName();
        if (objectName == "qt_spinbox_lineedit") { // QSpinBox inner widget
            continue;
        }
        if (objectName.startsWith("tooltipOffset")) {
            continue;
        }
        QString settingsName = objectName;
        settingsName.remove(className);
        settingsName[0] = settingsName.at(0).toUpper();
        if (widget->inherits("QCheckBox")) {
            NSettings::instance()->setValue(settingsName,
                                            qobject_cast<QCheckBox *>(widget)->isChecked());
        } else if (widget->inherits("QLineEdit")) {
            NSettings::instance()->setValue(settingsName, qobject_cast<QLineEdit *>(widget)->text());
        } else if (widget->inherits("NMultiLineEdit")) {
            NSettings::instance()->setValue(settingsName,
                                            qobject_cast<NMultiLineEdit *>(widget)->text());
        } else if (widget->inherits("QDoubleSpinBox")) {
            NSettings::instance()->setValue(settingsName,
                                            qobject_cast<QDoubleSpinBox *>(widget)->value());
        }
    }

#ifdef Q_OS_WIN
    NW7TaskBar::instance()->setEnabled(NSettings::instance()->value("TaskbarProgress").toBool());
#endif
    // << general

    // track info overlay >>
    for (int i = 0; i < ui.waveformTrackInfoTable->rowCount(); ++i) {
        for (int j = 0; j < ui.waveformTrackInfoTable->columnCount(); ++j) {
            QString objecName = ui.waveformTrackInfoTable->verticalHeaderItem(i)->text() +
                                ui.waveformTrackInfoTable->horizontalHeaderItem(j)->text();
            NSettings::instance()->setValue("TrackInfo/" + vNames.at(i) + hNames.at(j),
                                            ui.waveformTrackInfoTable->item(i, j)->text());
        }
    }
    // << track info overlay

    // waveform colours & accent >>
    {
        QString accent = ui.accentColorComboBox->currentData().toString();
        if (QColor(accent).isValid()) {
            NSettings::instance()->setValue("AccentColor", accent);
        }
    }
    // << waveform colours & accent

    // visualiser >>
    NSettings::instance()->setValue("VisualiserGradientFrom",
                                    ui.visualiserGradientFromButton->property("color"));
    NSettings::instance()->setValue("VisualiserGradientTo",
                                    ui.visualiserGradientToButton->property("color"));
    // << visualiser

    // playlist columns >>
    {
        QStringList specs;
        for (int i = 0; i < ui.playlistColumnsList->count(); ++i) {
            QListWidgetItem *item = ui.playlistColumnsList->item(i);
            if (item->checkState() == Qt::Checked) {
                specs << item->data(Qt::UserRole).toString();
            }
        }
        if (!specs.isEmpty()) { // keep the previous columns rather than showing none
            NSettings::instance()->setValue("PlaylistColumnsFormat", specs);
        }
    }
    // << playlist columns

    // track info encoding >>
    NSettings::instance()->setValue("EncodingTrackInfo",
                                    ui.encodingTrackInfoComboBox->currentData().toString());
    // << track info encoding

    // tooltip offset >>
    int tooltipOffsetX = ui.tooltipOffsetXSpinBox->value();
    int tooltipOffsetY = ui.tooltipOffsetYSpinBox->value();
    NSettings::instance()->setValue("TooltipOffset", QStringList()
                                                         << QString::number(tooltipOffsetX)
                                                         << QString::number(tooltipOffsetY));
    // << tooltip offset

    // plugins >>
    NFlagIterator<N::PluginType> iter(N::MaxPlugin);
    while (iter.hasNext()) {
        iter.next();
        N::PluginType type = iter.value();
        QString typeString = ENUM_TO_STR(N, PluginType, type);
        QString containerName = selectedContainer(type);
        if (!containerName.isEmpty()) {
            NSettings::instance()->setValue(QString() + "Plugins/" + typeString, containerName);
        }
    }
    // << plugins

    // skins >>
#ifndef _N_NO_SKINS_
    NSettings::instance()->setValue("Skin",
                                    ui.skinComboBox->itemData(ui.skinComboBox->currentIndex()));
#endif
    // << skins

    // translations >>
    NSettings::instance()->setValue("Language", ui.languageComboBox
                                                    ->itemData(ui.languageComboBox->currentIndex())
                                                    .toLocale()
                                                    .bcp47Name()
                                                    .split('-')
                                                    .first());
    // << translations

    // styles >>
    NSettings::instance()->setValue("Style", ui.styleComboBox->currentText());
    // << styles

    // shortcuts >>
    ui.shortcutEditorWidget->applyShortcuts();
    // << shortcuts

    emit settingsChanged();

    // systray check >>
    if (ui.trayIconCheckBox->isChecked() && !QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::warning(this, tr("Systray Error"),
                             tr("System Tray (Notification Area) is "
                                "not available on your system."));
    }
    // << systray check
}
