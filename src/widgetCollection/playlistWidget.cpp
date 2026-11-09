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

#include "playlistWidget.h"

#include <QApplication>
#include <QCollator>
#include <QContextMenuEvent>
#include <QDrag>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPointer>
#include <QRegExp>
#include <QHeaderView>
#include <QMap>
#include <QScrollBar>
#include <QRegularExpression>
#include <QStandardItemModel>

#include <algorithm>
#include <thread>
#ifdef Q_OS_MAC
#include <sys/stat.h>
#endif

#include "action.h"
#include "playbackEngineInterface.h"
#include "playlistDataItem.h"
#include "playlistStorage.h"
#include "playlistWidgetItem.h"
#include "pluginLoader.h"
#include "settings.h"
#include "trackInfoReader.h"
#include "utils.h"

NPlaylistWidget::NPlaylistWidget(QWidget *parent) : QListWidget(parent)
{
    m_fileDropBorderColor = QColor(Qt::transparent);
    m_fileDropBackground = QBrush(Qt::NoBrush);
    m_failedTextColor = QColor(Qt::red);
    m_playingTextColor = QColor(Qt::black);
    m_fileDropRadius = 0;

    m_trackInfoReader = NULL;

    m_playbackEngine = dynamic_cast<NPlaybackEngineInterface *>(
        NPluginLoader::getPlugin(N::PlaybackEngine));
    Q_ASSERT(m_playbackEngine);

    // triggered bu user input (double click or enter, depending on platform):
    connect(this, &QListWidget::itemActivated, [this](QListWidgetItem *item) {
        playItem(reinterpret_cast<NPlaylistWidgetItem *>(item));
    });
    connect(m_playbackEngine, SIGNAL(mediaFinished(const QString &, int)), this,
            SLOT(on_playbackEngine_mediaFinished(const QString &, int)));
    connect(m_playbackEngine, SIGNAL(mediaFailed(const QString &, int)), this,
            SLOT(on_playbackEngine_mediaFailed(const QString &, int)));
    connect(m_playbackEngine, SIGNAL(mediaChanged(const QString &, int)), this,
            SLOT(on_playbackEngine_mediaChanged(const QString &, int)));
    connect(m_playbackEngine, SIGNAL(nextMediaRequested()), this,
            SLOT(on_playbackEngine_prepareNextMediaRequested()), Qt::BlockingQueuedConnection);

    setItemDelegate(new NPlaylistWidgetItemDelegate(this));
    m_playingItem = NULL;

    m_itemDrag = NULL;
    m_fetchRunning = 0;
    m_sortWaitingForFetch = false;
    m_pressOnItem = false;
    m_fileDrop = false;
    m_dragStart = DragStartInside;
    m_dropEnd = DropEndInside;

    m_repeatMode = NSettings::instance()->value("Repeat").toBool();
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDefaultDropAction(Qt::MoveAction);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDragEnabled(true);

    m_processVisibleItemsTimer = new QTimer(this);
    m_processVisibleItemsTimer->setSingleShot(true);
    connect(m_processVisibleItemsTimer, &QTimer::timeout, [this]() {
        processVisibleItems();
        calculateDuration();
    });
    connect(verticalScrollBar(), SIGNAL(valueChanged(int)), this,
            SLOT(startProcessVisibleItemsTimer()));

    m_missingFilesTimer = new QTimer(this);
    m_missingFilesTimer->setInterval(3000);
    connect(m_missingFilesTimer, &QTimer::timeout, this, &NPlaylistWidget::checkMissingFiles);

    // columns mode: a standalone header above the viewport, rows are drawn
    // column by column by NPlaylistWidgetItemDelegate
    m_columnsMode = false;
    m_headerModel = new QStandardItemModel(this);
    m_header = new QHeaderView(Qt::Horizontal, this);
    m_header->setModel(m_headerModel);
    m_header->setSectionsMovable(false);
    m_header->setSectionsClickable(true);
    m_header->setHighlightSections(false);
    m_header->setStretchLastSection(true);
    m_header->setSectionResizeMode(QHeaderView::Interactive);
    m_header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_header->hide();
    m_columnsAutoWidth = true;
    m_applyingColumnWidths = false;
    m_headerPressed = false;
    m_sortColumn = -1;
    m_sortOrder = Qt::AscendingOrder;
    m_header->viewport()->installEventFilter(this);
    connect(m_header, &QHeaderView::sectionClicked, this, &NPlaylistWidget::sortByColumn);
    connect(m_header, &QHeaderView::sectionResized, [this]() {
        viewport()->update();
        // only dragging a divider on the header counts as the user resizing
        // (the last section also resizes itself during layout and window resizes)
        if (!m_applyingColumnWidths && m_headerPressed) {
            m_columnsAutoWidth = false;
            saveColumnWidths();
        }
    });
    setColumnsMode(NSettings::instance()->value("PlaylistColumns").toBool());
}

void NPlaylistWidget::loadColumns()
{
    m_columnFormats.clear();
    m_columnAlignments.clear();
    QStringList headers;
    for (const QString &spec : NSettings::instance()->value("PlaylistColumnsFormat").toStringList()) {
        // "Header|format[|r]"; the format itself may contain '|' (e.g. "{%t|%F}")
        QString header = spec.section('|', 0, 0);
        QString format = spec.section('|', 1);
        Qt::Alignment align = Qt::AlignLeft;
        if (format.endsWith("|r")) {
            format.chop(2);
            align = Qt::AlignRight;
        }
        headers << header;
        m_columnFormats << format;
        m_columnAlignments << align;
    }

    m_headerLabels = headers;
    clearSortState(); // column positions may have changed
    m_headerModel->clear();
    m_headerModel->setColumnCount(headers.size());
    m_headerModel->setHorizontalHeaderLabels(headers);
    for (int i = 0; i < headers.size(); ++i) {
        m_headerModel->setHeaderData(i, Qt::Horizontal,
                                     QVariant::fromValue(int(m_columnAlignments[i] | Qt::AlignVCenter)),
                                     Qt::TextAlignmentRole);
    }

    // restore the user's widths; otherwise size columns proportionally
    QStringList widths = NSettings::instance()->value("PlaylistColumnSizes").toStringList();
    bool haveWidths = widths.size() == headers.size();
    for (int i = 0; haveWidths && i < widths.size(); ++i) {
        haveWidths = widths.at(i).toInt() > 0;
    }
    m_columnsAutoWidth = !haveWidths;
    m_applyingColumnWidths = true;
    if (haveWidths) {
        for (int i = 0; i < headers.size(); ++i) {
            m_header->resizeSection(i, widths.at(i).toInt());
        }
    }
    m_applyingColumnWidths = false;
    if (m_columnsAutoWidth) {
        applyAutoColumnWidths();
    }
}

void NPlaylistWidget::applyAutoColumnWidths()
{
    int count = m_header->count();
    int available = viewport()->width();
    if (count == 0 || available <= 0) {
        return;
    }
    // narrow numeric columns, wider text columns
    QList<int> weights;
    int total = 0;
    for (int i = 0; i < count; ++i) {
        QString header = m_headerModel->headerData(i, Qt::Horizontal).toString().toLower();
        int w = 20;
        if (header == "title") {
            w = 35;
        } else if (header == "artist" || header == "album") {
            w = 25;
        } else if (header == "bpm" || header == "key" || header == "year" ||
                   m_columnAlignments.value(i) == Qt::AlignRight) {
            w = 8;
        }
        weights << w;
        total += w;
    }
    m_applyingColumnWidths = true;
    for (int i = 0; i < count; ++i) {
        m_header->resizeSection(i, qMax(40, available * weights.at(i) / total));
    }
    m_applyingColumnWidths = false;
    viewport()->update();
}

void NPlaylistWidget::saveColumnWidths()
{
    if (!m_columnsMode) {
        return;
    }
    QStringList widths;
    for (int i = 0; i < m_header->count(); ++i) {
        widths << QString::number(m_header->sectionSize(i));
    }
    NSettings::instance()->setValue("PlaylistColumnSizes", widths);
}

void NPlaylistWidget::setColumnsMode(bool enable)
{
    QStringList specs = NSettings::instance()->value("PlaylistColumnsFormat").toStringList();
    if (enable == m_columnsMode && (!enable || (!m_columnFormats.isEmpty() && specs == m_columnSpecs))) {
        return; // unchanged: keep cached column texts
    }
    m_columnSpecs = specs;
    m_columnsMode = enable;
    if (enable) {
        loadColumns();
        updateHeaderStyle();
        int h = m_header->sizeHint().height();
        setViewportMargins(0, h, 0, 0);
        m_header->show();
        updateHeaderGeometry();
    } else {
        m_header->hide();
        setViewportMargins(0, 0, 0, 0);
    }

    // drop cached column texts so visible rows recompute them
    for (int i = 0; i < count(); ++i) {
        itemAtRow(i)->setData(N::ColumnsRole, QVariant());
    }
    startProcessVisibleItemsTimer(); // fills rows in incrementally
    viewport()->update();
}

void NPlaylistWidget::updateHeaderGeometry()
{
    if (!m_columnsMode) {
        return;
    }
    QRect vg = viewport()->geometry();
    int h = m_header->sizeHint().height();
    m_header->setGeometry(vg.left(), vg.top() - h, vg.width(), h);
}

void NPlaylistWidget::updateHeaderStyle()
{
    // follow the skin: use the playlist's own text colour, no native chrome
    QColor text = palette().color(QPalette::Text);
    QColor line = text;
    line.setAlpha(60);
    m_header->setStyleSheet(QString("QHeaderView { background: transparent; }"
                                    "QHeaderView::section { background: transparent; color: %1;"
                                    " border: none; border-bottom: 1px solid %2;"
                                    " border-right: 1px solid %2; padding: 2px 4px; }")
                                .arg(text.name(QColor::HexArgb), line.name(QColor::HexArgb)));
}

void NPlaylistWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
        if (m_columnsMode) {
            updateHeaderStyle();
        }
    }
    QListWidget::changeEvent(event);
}

bool NPlaylistWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_header->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            m_headerPressed = true;
        } else if (event->type() == QEvent::MouseButtonRelease) {
            m_headerPressed = false;
        }
    }
    return QListWidget::eventFilter(watched, event);
}

int NPlaylistWidget::columnCount() const
{
    return m_columnFormats.size();
}

int NPlaylistWidget::columnX(int column) const
{
    return m_header->sectionViewportPosition(column);
}

int NPlaylistWidget::columnWidth(int column) const
{
    return m_header->sectionSize(column);
}

Qt::Alignment NPlaylistWidget::columnAlignment(int column) const
{
    return m_columnAlignments.value(column, Qt::AlignLeft);
}

void NPlaylistWidget::setTrackInfoReader(NTrackInfoReader *reader)
{
    m_trackInfoReader = reader;
}

void NPlaylistWidget::resizeEvent(QResizeEvent *event)
{
    startProcessVisibleItemsTimer();
    QListWidget::resizeEvent(event);
    updateHeaderGeometry();
    if (m_columnsMode && m_columnsAutoWidth) {
        applyAutoColumnWidths();
    }
}

void NPlaylistWidget::startProcessVisibleItemsTimer()
{
    if (m_processVisibleItemsTimer->isActive()) {
        m_processVisibleItemsTimer->stop();
    }

    m_processVisibleItemsTimer->start(30);
}

void NPlaylistWidget::processVisibleItems()
{
    bool emitItemsChanged = false;
    int minRow = row(itemAt(0, 0));
    QListWidgetItem *maxItem = itemAt(0, this->height());
    int maxRow = maxItem ? row(maxItem) : count() - 1;

    int totalRows = maxRow - minRow + 1;
    minRow = qMax(0, minRow - totalRows);
    maxRow = qMin(maxRow + totalRows, count() - 1);
    QString titleFormat = NSettings::instance()->value("PlaylistTrackInfo").toString();

    // Reading tags can block (large files, iCloud/network storage), so cap the
    // work per pass and continue on the next timer tick to keep the UI responsive.
    QElapsedTimer budget;
    budget.start();
    for (int i = minRow; i <= maxRow; ++i) {
        emitItemsChanged = refreshItemData(itemAtRow(i), titleFormat) || emitItemsChanged;
        if (i < maxRow && budget.elapsed() > 25) {
            startProcessVisibleItemsTimer();
            break;
        }
    }

    if (emitItemsChanged) {
        emit itemsChanged();
    }
}

QStringList NPlaylistWidget::selectedFiles() const
{
    QStringList files;
    for (QListWidgetItem *item : selectedItems()) {
        files << item->data(N::PathRole).toString();
    }
    return files;
}

void NPlaylistWidget::wheelEvent(QWheelEvent *event)
{
    QListWidget::wheelEvent(event);
    event->accept();
}

void NPlaylistWidget::contextMenuEvent(QContextMenuEvent *event)
{
    if (selectedItems().size() != 0 && itemAt(event->pos())) {
        emit contextMenuRequested(mapToGlobal(event->pos()));
    } else {
        QListWidget::contextMenuEvent(event);
    }
}

void NPlaylistWidget::removeFiles(const QStringList &files)
{
    clearSortState();
    QList<int> rowsToRemove;
    for (size_t row = 0; row < count(); ++row) {
        if (files.contains(item(row)->data(N::PathRole).toString())) {
            rowsToRemove << row;
        }
    }

    std::reverse(rowsToRemove.begin(), rowsToRemove.end());
    for (int row : rowsToRemove) {
        delete takeItem(row);
    }

    viewport()->update();
}

void NPlaylistWidget::removeSelected()
{
    clearSortState();
    int firstSelectedRow = count() - 1; // set to last row
    bool playingItemRemoved = false;
    foreach (QListWidgetItem *item, selectedItems()) {
        int rowToDelete = row(item);
        if (firstSelectedRow > rowToDelete) {
            firstSelectedRow = rowToDelete;
        }
        NPlaylistWidgetItem *itemToDelete = reinterpret_cast<NPlaylistWidgetItem *>(
            takeItem(rowToDelete));

        if (m_playingItem == itemToDelete) {
            playingItemRemoved = true;
            m_playingItem = NULL;
        }

        m_itemMap.remove(itemToDelete->data(N::IdRole).toInt());
        delete itemToDelete;
    }
    viewport()->update();

    updateTrackIndexes();
    processVisibleItems();
    calculateDuration();
    emit itemsChanged();

    int newCount = count();
    if (newCount == 0) {
        playItem(NULL);
        return;
    }

    int newCurrentRow = firstSelectedRow;
    if (firstSelectedRow >= newCount) { // last row was within removed
        if (playingItemRemoved && NSettings::instance()->value("LoopPlaylist").toBool()) {
            newCurrentRow = 0;
        } else {
            newCurrentRow = newCount - 1;
        }
    }
    NPlaylistWidgetItem *newCurrentItem = itemAtRow(newCurrentRow);

    if (playingItemRemoved && m_playbackEngine->state() != N::PlaybackStopped) {
        playItem(newCurrentItem);
    }
    // sets keyboard focus:
    if (newCurrentItem) {
        QListWidget::setCurrentItem(newCurrentItem);
    }
}

void NPlaylistWidget::calculateDuration()
{
    int secondsTotal = 0;
    for (int i = 0; i < count(); ++i) {
        NPlaylistWidgetItem *item = itemAtRow(i);
        int seconds = item->data(N::DurationRole).toInt();
        if (seconds > 0) {
            secondsTotal += seconds;
        }
    }

    emit durationChanged(secondsTotal);
}

void NPlaylistWidget::updateTrackIndexes()
{
    for (int i = 0; i < count(); ++i) {
        itemAtRow(i)->setData(N::TrackIndexRole, i);
    }
}

NPlaylistWidget::~NPlaylistWidget() {}

void NPlaylistWidget::resetPlayingItem()
{
    if (m_playingItem) {
        m_playingItem->setData(N::PlayingRole, false);
    }
    m_playingItem = NULL;
}

void NPlaylistWidget::setPlaceholderData(NPlaylistWidgetItem *item, const QString &file)
{
    if (item->text().isEmpty()) {
        item->setText(QFileInfo(file).fileName());
    }
    if (m_columnsMode && item->data(N::ColumnsRole).toStringList().isEmpty()) {
        QStringList columns;
        for (int i = 0; i < m_columnFormats.size(); ++i) {
            columns << QString();
        }
        int titleColumn = qMax(0, m_columnFormats.indexOf(QRegExp(".*%t.*")));
        if (!columns.isEmpty()) {
            columns[titleColumn] = QFileInfo(file).fileName();
        }
        item->setData(N::ColumnsRole, columns);
    }
}

void NPlaylistWidget::checkMissingFiles()
{
    // check each folder once; while a drive is unplugged its folders don't
    // exist, so this is a handful of quick checks however long the playlist is
    QMap<QString, QStringList> byDir;
    for (const QString &file : m_missingFiles) {
        byDir[QFileInfo(file).path()] << file;
    }
    QStringList found;
    for (auto it = byDir.constBegin(); it != byDir.constEnd(); ++it) {
#ifdef Q_OS_WIN
        if (it.key().startsWith("//") || it.key().startsWith("\\\\")) {
            continue; // an offline network share can take seconds to answer
        }
#endif
        if (!QFileInfo::exists(it.key())) {
            continue;
        }
        for (const QString &file : it.value()) {
            if (QFileInfo::exists(file)) {
                found << file;
            }
        }
    }
    if (found.isEmpty()) {
        return;
    }

    for (const QString &file : found) {
        m_missingFiles.remove(file);
    }
    if (m_missingFiles.isEmpty()) {
        m_missingFilesTimer->stop();
    }
    refreshItemsForFiles(found);
    calculateDuration();
    emit itemsChanged();
}

bool NPlaylistWidget::refreshItemData(NPlaylistWidgetItem *item, QString titleFormat, bool force)
{
    if (!force && m_missingFiles.contains(item->data(N::PathRole).toString())) {
        return false; // re-read by checkMissingFiles() once the file is back
    }

    // column texts are not persisted; refresh rows that don't have them yet
    bool columnsMissing = m_columnsMode && item->data(N::ColumnsRole).toStringList().size() !=
                                               m_columnFormats.size();
    if (!force && !columnsMissing && titleFormat == item->data(N::TitleFormatRole).toString() &&
        !item->text().isEmpty() && item->data(N::DurationRole).toInt() != -1) {
        return false;
    }

    QString file = item->data(N::PathRole).toString();
    if (needsFetch(file)) {
        // show the file name until the file is available; the row is
        // refreshed when the background fetch finishes
        setPlaceholderData(item, file);
        return false;
    }

    if (!file.isEmpty() && !QFileInfo::exists(file)) {
        // not there right now (unplugged drive, renamed file): show the file
        // name and read the tags once the file turns up
        setPlaceholderData(item, file);
        m_missingFiles.insert(file);
        if (!m_missingFilesTimer->isActive()) {
            m_missingFilesTimer->start();
        }
        return false;
    }
    m_missingFiles.remove(file);

    QString title;
    if (m_trackInfoReader) {
        m_trackInfoReader->setSource(file);
        title = m_trackInfoReader->toString(titleFormat);
        item->setData(N::DurationRole, m_trackInfoReader->toString("%D").toInt());
        if (m_columnsMode) {
            QStringList columns;
            for (const QString &format : m_columnFormats) {
                columns << m_trackInfoReader->toString(format);
            }
            item->setData(N::ColumnsRole, columns);
        }
    }

    item->setText(title);
    item->setData(N::TitleFormatRole, titleFormat);
    return true;
}

void NPlaylistWidget::setPlayingItem(NPlaylistWidgetItem *item)
{
    item->setData(N::PlayingRole, true);
    item->setData(N::FailedRole, false); // reset failed role
    refreshItemData(item, NSettings::instance()->value("PlaylistTrackInfo").toString(),
                    true); // with force

    if (NSettings::instance()->value("ScrollToItem").toBool()) {
        scrollToItem(item);
    }
    m_playingItem = item;
    emit playingItemChanged();
    QListWidget::viewport()->update();
}

void NPlaylistWidget::on_playbackEngine_mediaChanged(const QString &file, int id)
{
    if (m_playingItem) {
        m_playingItem->setData(N::PlayingRole, false);
        m_playingItem->setData(N::PositionRole, m_playbackEngine->position());
        m_playingItem->setData(N::CountRole, m_playingItem->data(N::CountRole).toInt() + 1);
    }

    resetPlayingItem();

    if (!m_itemMap.contains(id)) {
        emit playingItemChanged();
        QListWidget::viewport()->update();
        return;
    }

    NPlaylistWidgetItem *item = m_itemMap[id];
    setPlayingItem(item);
}

void NPlaylistWidget::on_playbackEngine_prepareNextMediaRequested()
{
    NPlaylistWidgetItem *item = NULL;
    if (m_repeatMode) {
        item = playingItem();
    } else {
        item = nextItem(m_playingItem);
    }
    if (!item) {
        return;
    }
    m_playbackEngine->nextMediaRespond(item->data(N::PathRole).toString(),
                                       item->data(N::IdRole).toInt());
}

void NPlaylistWidget::on_playbackEngine_mediaFinished(const QString &, int id)
{
    if (!m_itemMap.contains(id)) {
        return;
    }

    NPlaylistWidgetItem *item = NULL;
    if (m_repeatMode) {
        item = m_itemMap[id];
    } else {
        item = nextItem(item);
    }

    if (!item) {
        emit playlistFinished();
        return;
    }

    playItem(nextItem(item));
}

void NPlaylistWidget::on_playbackEngine_mediaFailed(const QString &file, int id)
{
    if (!m_itemMap.contains(id)) {
        return;
    }

    resetPlayingItem();

    NPlaylistWidgetItem *item = m_itemMap[id];
    item->setData(N::PlayingRole, true);
    item->setData(N::FailedRole, true);

    if (NSettings::instance()->value("ScrollToItem").toBool()) {
        scrollToItem(item);
    }
    m_playingItem = item;
    emit playingItemChanged();
    QListWidget::viewport()->update();
}

void NPlaylistWidget::playItem(NPlaylistWidgetItem *item)
{
    if (item) {
        m_playbackEngine->setMedia(item->data(N::PathRole).toString(),
                                   item->data(N::IdRole).toInt());
        m_playbackEngine->play();
    } else {
        resetPlayingItem();
        m_playbackEngine->setMedia("", 0);
    }
}

void NPlaylistWidget::playRow(int row)
{
    playItem(itemAtRow(row));
}

int NPlaylistWidget::playingRow() const
{
    if (m_playingItem) {
        return row(m_playingItem);
    } else {
        return -1;
    }
}

NPlaylistWidgetItem *NPlaylistWidget::playingItem() const
{
    return m_playingItem;
}

bool NPlaylistWidget::hasPlayingItem() const
{
    return playingRow() != -1;
}

void NPlaylistWidget::addItem(NPlaylistWidgetItem *item)
{
    QListWidget::addItem(item);
    m_itemMap[item->data(N::IdRole).toInt()] = item;
}

void NPlaylistWidget::addFiles(const QStringList &files)
{
    clearSortState();
    foreach (QString path, files)
        addItem(new NPlaylistWidgetItem(QFileInfo(path)));

    processVisibleItems();
    calculateDuration();
    updateTrackIndexes();
    emit itemsChanged();
}

void NPlaylistWidget::addItems(const QList<NPlaylistDataItem> &dataItems)
{
    clearSortState();
    foreach (NPlaylistDataItem dataItem, dataItems)
        addItem(new NPlaylistWidgetItem(dataItem));

    processVisibleItems();
    calculateDuration();
    updateTrackIndexes();
    emit itemsChanged();
}

void NPlaylistWidget::setFiles(const QStringList &files)
{
    clearSortState();
    clear();
    m_playingItem = NULL;
    foreach (QString path, files)
        addItem(new NPlaylistWidgetItem(QFileInfo(path)));

    processVisibleItems();
    calculateDuration();
    updateTrackIndexes();
    emit itemsChanged();
}

void NPlaylistWidget::setItems(const QList<NPlaylistDataItem> &dataItems)
{
    clearSortState();
    clear();
    m_playingItem = NULL;

    foreach (NPlaylistDataItem dataItem, dataItems)
        addItem(new NPlaylistWidgetItem(dataItem));

    processVisibleItems();
    calculateDuration();
    updateTrackIndexes();
    emit itemsChanged();
}

bool NPlaylistWidget::setPlaylist(const QString &file)
{
    clearSortState();
    clear();
    m_playingItem = NULL;

    QList<NPlaylistDataItem> dataItemsList = NPlaylistStorage::readM3u(file);

    if (dataItemsList.isEmpty()) {
        calculateDuration();
        emit itemsChanged();
        return false;
    }

    for (int i = 0; i < dataItemsList.count(); ++i) {
        addItem(new NPlaylistWidgetItem(dataItemsList.at(i)));
    }

    processVisibleItems();
    calculateDuration();
    updateTrackIndexes();
    emit itemsChanged();

    return true;
}

void NPlaylistWidget::playNextItem()
{
    NPlaylistWidgetItem *item = nextItem(m_playingItem);
    if (!item) {
        emit addMoreRequested();
        item = nextItem(m_playingItem);
    }

    if (item) {
        playItem(item);
    }
}

void NPlaylistWidget::playPrevItem()
{
    NPlaylistWidgetItem *item = prevItem(m_playingItem);

    if (item) {
        playItem(item);
    }
}

void NPlaylistWidget::rowsInserted(const QModelIndex &parent, int start, int end)
{
    QListWidget::rowsInserted(parent, start, end);
}

void NPlaylistWidget::shufflePlaylist()
{
    clearSortState();
    QList<QListWidgetItem *> items;
    for (int i = 0; i < count(); ++i) {
        items.append(takeItem(0));
    }
    qsrand(QDateTime::currentMSecsSinceEpoch() % UINT_MAX);
    for (int i = items.count() - 1; i > 0; --i) {
        items.swap(i, qrand() % (i + 1));
    }
    for (int i = 0; i < items.count(); ++i) {
        QListWidget::addItem(items[i]);
    }
    processVisibleItems();
    updateTrackIndexes();
    emit itemsChanged();
}

bool NPlaylistWidget::repeatMode() const
{
    return m_repeatMode;
}

void NPlaylistWidget::setRepeatMode(bool enable)
{
    if (m_repeatMode != enable) {
        emit repeatModeChanged(enable);
    }
    m_repeatMode = enable;
    NSettings::instance()->setValue("Repeat", enable);
}

NPlaylistWidgetItem *NPlaylistWidget::itemAtRow(int row) const
{
    if (count() == 0) {
        return NULL;
    }

    if (qBound(0, row, count() - 1) != row) {
        return NULL;
    }

    return reinterpret_cast<NPlaylistWidgetItem *>(QListWidget::item(row));
}

NPlaylistWidgetItem *NPlaylistWidget::nextItem(NPlaylistWidgetItem *item) const
{
    if (!item) {
        return NULL;
    }

    int nextRow = QListWidget::row(item) + 1;
    if (nextRow >= count() && NSettings::instance()->value("LoopPlaylist").toBool()) {
        nextRow = 0;
    }

    return NPlaylistWidget::itemAtRow(nextRow);
}

NPlaylistWidgetItem *NPlaylistWidget::prevItem(NPlaylistWidgetItem *item) const
{
    if (!item) {
        return NULL;
    }

    int prevRow = QListWidget::row(item) - 1;
    if (prevRow < 0 && NSettings::instance()->value("LoopPlaylist").toBool()) {
        prevRow = count() - 1;
    }

    return NPlaylistWidget::itemAtRow(prevRow);
}

void NPlaylistWidget::paintEvent(QPaintEvent *e)
{
    QListWidget::paintEvent(e);

    if (count() == 0) {
        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing);

        int iconSide = 120;
        painter.translate(0, (iconSide + fontMetrics().height()) / 2);
        QRect iconRect = QRect(0, 0, iconSide, iconSide);
        QRect rect = viewport()->rect();
        iconRect.moveCenter(rect.center());
        iconRect.moveBottom(rect.center().y() - fontMetrics().height());

        QPen pen = painter.pen();
        pen.setWidth(4);
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashOffset(6);
        pen.setDashPattern(QVector<qreal>() << 3 << 4);
        painter.setPen(pen);
        painter.drawRoundedRect(iconRect, 20, 20);
        painter.drawText(viewport()->rect(), Qt::AlignHCenter | Qt::AlignVCenter,
                         tr("Drop media here"));

        painter.setPen(Qt::NoPen);
        QBrush brush = painter.brush();
        brush.setColor(pen.color());
        brush.setStyle(Qt::SolidPattern);
        painter.setBrush(brush);
        static const QPoint points[7] = {QPoint(33, 78), QPoint(66, 39), QPoint(51, 39),
                                         QPoint(51, 0),  QPoint(15, 0),  QPoint(15, 39),
                                         QPoint(0, 39)};
        painter.translate((rect.width() - iconSide) / 2 + 26, (rect.height() - iconSide) / 2 - 54);
        painter.drawPolygon(points, 7);
    }

    if (m_fileDrop) {
        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(m_fileDropBorderColor);
        painter.setBrush(m_fileDropBackground);
        painter.drawRoundedRect(QRectF(viewport()->rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                                m_fileDropRadius, m_fileDropRadius);
    }
}

// DRAG & DROP >>
bool NPlaylistWidget::dropMimeData(int index, const QMimeData *data, Qt::DropAction action)
{
    clearSortState();
    if (action == Qt::MoveAction && m_dragStart == DragStartInside) { // moving within playlist
        updateTrackIndexes();
        emit itemsChanged();

        return false;
    }

    bool wasEmpty = false;
    if (count() == 0) {
        wasEmpty = true;
    }

    QString titleFormat = NSettings::instance()->value("PlaylistTrackInfo").toString();
    foreach (QUrl url, data->urls()) {
        foreach (NPlaylistDataItem dataItem, NUtils::dirListRecursive(url.toLocalFile())) {
            NPlaylistWidgetItem *item = new NPlaylistWidgetItem(dataItem);
            QListWidget::insertItem(index, item);
            m_itemMap[item->data(N::IdRole).toInt()] = item;
            ++index;
        }
    }

    if (wasEmpty) {
        playRow(0);
    }

    m_itemDrag = NULL;

    processVisibleItems();
    calculateDuration();
    updateTrackIndexes();
    emit itemsChanged();

    return true;
}

QStringList NPlaylistWidget::mimeTypes() const
{
    QStringList qstrList;
    qstrList.append("text/uri-list");
    return qstrList;
}

QMimeData *NPlaylistWidget::mimeData(const QList<QListWidgetItem *> items) const
{
    QList<QUrl> urls;
    foreach (QListWidgetItem *item, items)
        urls << QUrl::fromLocalFile(item->data(N::PathRole).toString());
    QPointer<QMimeData> data = new QMimeData();

    data->setUrls(urls);

    return data;
}

void NPlaylistWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->pos();
        m_pressOnItem = itemAt(event->pos()) != NULL;
    }
    QListWidget::mousePressEvent(event);
}

void NPlaylistWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton)) {
        return;
    }

    if (!itemAt(event->pos())) {
        selectionModel()->clearSelection();
        return;
    }

    // Start dragging only after a deliberate movement that began on a track.
    // Without the threshold every click with the slightest wobble became a
    // drag, which hands macOS a file URL per selected track; for tracks on a
    // drive that's no longer connected that can stall with a beach ball.
    if (!m_pressOnItem ||
        (event->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance()) {
        return;
    }

    if (selectedItems().isEmpty()) {
        return;
    }
    m_pressOnItem = false; // one drag per press

    // missing files (e.g. on an unplugged drive) aren't put on the drag;
    // moving rows within the playlist doesn't need the URLs
    QList<QUrl> urls;
    foreach (QListWidgetItem *item, selectedItems()) {
        QString path = item->data(N::PathRole).toString();
        if (QFileInfo::exists(path)) {
            urls << QUrl::fromLocalFile(path);
        }
    }
    QStringList files = selectedFiles();

    QMimeData *mimeData = new QMimeData;
    mimeData->setUrls(urls);
    m_mimeDataUrls.clear();

    m_itemDrag = new QDrag(this);
    m_itemDrag->setMimeData(mimeData);
    Qt::DropAction dropAction = m_itemDrag->exec(Qt::CopyAction | Qt::MoveAction,
                                                 Qt::MoveAction);      // blocking
    if (dropAction == Qt::MoveAction && m_dropEnd == DropEndOutside) { // dropping to file manager
        removeFiles(files);
    }
}

void NPlaylistWidget::dropEvent(QDropEvent *event)
{
    clearSortState();
    if (event->source()) { // moving within playlist
        m_dragStart = DragStartInside;
    } else { // dropping from file manager
        m_dragStart = DragStartOutside;
    }

    QListWidget::dropEvent(event);

    updateTrackIndexes();
    emit itemsChanged();

    m_fileDrop = false;
    viewport()->update();
}

void NPlaylistWidget::dragEnterEvent(QDragEnterEvent *event)
{
    m_dropEnd = DropEndInside;

    if (m_itemDrag && !m_mimeDataUrls.isEmpty()) {
        m_itemDrag->mimeData()->setUrls(m_mimeDataUrls); // recover old data
    }

    if (!m_itemDrag) {
        m_fileDrop = true;
        viewport()->update();
    }

    QListWidget::dragEnterEvent(event);
}

void NPlaylistWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (!m_itemDrag) {
        m_fileDrop = (!itemAt(event->pos())) ? true : false;
    }

    QListWidget::dragMoveEvent(event);
}

void NPlaylistWidget::dragLeaveEvent(QDragLeaveEvent *event)
{
    m_dropEnd = DropEndOutside;

    if (m_itemDrag) {
        m_mimeDataUrls = m_itemDrag->mimeData()->urls(); // backup
    }

    m_fileDrop = false;
    viewport()->update();

    QListWidget::dragLeaveEvent(event);
}
// << DRAG & DROP

// STYLESHEET PROPERTIES >>
void NPlaylistWidget::setPlayingTextColor(QColor color)
{
    m_playingTextColor = color;
}

QColor NPlaylistWidget::playingTextColor() const
{
    return m_playingTextColor;
}

void NPlaylistWidget::setFailedTextColor(QColor color)
{
    m_failedTextColor = color;
}

QColor NPlaylistWidget::failedTextColor() const
{
    return m_failedTextColor;
}

QColor NPlaylistWidget::fileDropBorderColor() const
{
    return m_fileDropBorderColor;
}

void NPlaylistWidget::setFileDropBorderColor(QColor color)
{
    m_fileDropBorderColor = color;
}

QBrush NPlaylistWidget::fileDropBackground() const
{
    return m_fileDropBackground;
}

void NPlaylistWidget::setFileDropBackground(QBrush brush)
{
    m_fileDropBackground = brush;
}

int NPlaylistWidget::fileDropRadius() const
{
    return m_fileDropRadius;
}

void NPlaylistWidget::setFileDropRadius(int radius)
{
    m_fileDropRadius = radius;
}
// << STYLESHEET PROPERTIES

void NPlaylistWidget::refreshItemsForFiles(const QStringList &files)
{
    QSet<QString> wanted(files.begin(), files.end());
    QString titleFormat = NSettings::instance()->value("PlaylistTrackInfo").toString();
    for (int row = 0; row < count(); ++row) {
        NPlaylistWidgetItem *item = itemAtRow(row);
        if (item && wanted.contains(item->data(N::PathRole).toString())) {
            refreshItemData(item, titleFormat, true);
        }
    }
    viewport()->update();
}

// Marks every row's texts as out of date (e.g. the key notation changed);
// visible rows are re-read now, the rest when they scroll into view
void NPlaylistWidget::invalidateItemData()
{
    for (int row = 0; row < count(); ++row) {
        if (NPlaylistWidgetItem *item = itemAtRow(row)) {
            item->setData(N::TitleFormatRole, QString());
        }
    }
    processVisibleItems();
    viewport()->update();
}

void NPlaylistWidget::refreshItemsForFile(const QString &file)
{
    QString titleFormat = NSettings::instance()->value("PlaylistTrackInfo").toString();
    for (int row = 0; row < count(); ++row) {
        NPlaylistWidgetItem *item = itemAtRow(row);
        if (item && item->data(N::PathRole).toString() == file) {
            refreshItemData(item, titleFormat, true);
        }
    }
    viewport()->update();
}

// Sorting by column >>

namespace
{
enum SortKind { SortText, SortNumber, SortDuration, SortKey };

// what a column holds, judged from its format (e.g. "%M" is BPM)
SortKind sortKindForFormat(const QString &format)
{
    if (format.contains("%d") || format.contains("%D")) {
        return SortDuration;
    }
    if (format.contains("%k")) {
        return SortKey;
    }
    for (const char *tag : {"%M", "%y", "%n", "%B", "%s", "%b", "%C"}) {
        if (format.contains(tag)) {
            return SortNumber;
        }
    }
    return SortText;
}

// "6:25" or "1:02:03" -> seconds; -1 if not a time
double parseDuration(const QString &text)
{
    QStringList parts = text.trimmed().split(':');
    if (parts.size() < 2 || parts.size() > 3) {
        return -1;
    }
    double seconds = 0;
    for (const QString &part : parts) {
        bool ok = false;
        int value = part.toInt(&ok);
        if (!ok) {
            return -1;
        }
        seconds = seconds * 60 + value;
    }
    return seconds;
}

// first number in the text ("320 kbps" -> 320); NaN-free: -1 if none
double parseNumber(const QString &text)
{
    static const QRegularExpression number("[-+]?\\d+(?:[.,]\\d+)?");
    QRegularExpressionMatch m = number.match(text);
    if (!m.hasMatch()) {
        return -1;
    }
    return m.captured(0).replace(',', '.').toDouble();
}

} // namespace

void NPlaylistWidget::updateSortIndicator()
{
    for (int i = 0; i < m_headerLabels.size(); ++i) {
        QString label = m_headerLabels.at(i);
        if (i == m_sortColumn) {
            QString arrow = m_sortOrder == Qt::AscendingOrder ? QString::fromUtf8("\u25B2")
                                                              : QString::fromUtf8("\u25BC");
            // keep the arrow on the inner side of right-aligned columns
            label = m_columnAlignments.value(i) & Qt::AlignRight ? arrow + " " + label
                                                                  : label + " " + arrow;
        }
        m_headerModel->setHeaderData(i, Qt::Horizontal, label, Qt::DisplayRole);
    }
}

void NPlaylistWidget::clearSortState()
{
    m_sortWaitingForFetch = false;
    m_presortOrder.clear();
    if (m_sortColumn != -1) {
        m_sortColumn = -1;
        updateSortIndicator();
    }
}

void NPlaylistWidget::applyOrder(const QList<QListWidgetItem *> &order)
{
    QListWidgetItem *current = currentItem();
    QList<QListWidgetItem *> selected = selectedItems();
    while (count() > 0) {
        takeItem(0);
    }
    for (QListWidgetItem *item : order) {
        QListWidget::addItem(item);
    }
    for (QListWidgetItem *item : selected) {
        item->setSelected(true);
    }
    if (current) {
        setCurrentItem(current, QItemSelectionModel::NoUpdate);
    }
    updateTrackIndexes();
    processVisibleItems();
    if (m_playingItem) {
        scrollToItem(m_playingItem);
    } else if (current) {
        scrollToItem(current);
    }
    viewport()->update();
    emit itemsChanged(); // the new order is saved with the playlist
}

void NPlaylistWidget::sortByColumn(int column)
{
    if (!m_columnsMode || column < 0 || column >= m_columnFormats.size() || count() < 2) {
        return;
    }

    // clicks cycle: ascending -> descending -> back to the order before sorting
    if (column == m_sortColumn && m_sortOrder == Qt::DescendingOrder) {
        m_sortWaitingForFetch = false;
        QList<QListWidgetItem *> original = m_presortOrder;
        m_presortOrder.clear();
        m_sortColumn = -1;
        updateSortIndicator();
        if (original.size() == count()) {
            applyOrder(original);
        }
        return;
    }
    Qt::SortOrder order = column == m_sortColumn ? Qt::DescendingOrder : Qt::AscendingOrder;

    if (m_sortColumn == -1) {
        m_presortOrder.clear();
        for (int i = 0; i < count(); ++i) {
            m_presortOrder << item(i);
        }
    }
    m_sortColumn = column;
    m_sortOrder = order;
    updateSortIndicator();

    // every row needs its column values, not only the visible ones; files
    // still being fetched from the cloud are sorted once they arrive
    QString titleFormat = NSettings::instance()->value("PlaylistTrackInfo").toString();
    for (int i = 0; i < count(); ++i) {
        refreshItemData(itemAtRow(i), titleFormat);
    }
    if (!m_fetchPending.isEmpty()) {
        m_sortWaitingForFetch = true;
        return;
    }
    sortRows(column, order);
}

void NPlaylistWidget::sortRows(int column, Qt::SortOrder order)
{
    m_sortWaitingForFetch = false;
    if (column < 0 || column >= m_columnFormats.size()) {
        return;
    }
    QList<QListWidgetItem *> items;
    for (int i = 0; i < count(); ++i) {
        items << item(i);
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);

    SortKind kind = sortKindForFormat(m_columnFormats.at(column));
    struct Entry
    {
        QListWidgetItem *item;
        QString text;
        double value; // numbers, durations, keys; -1 = no usable value
    };
    QVector<Entry> entries;
    entries.reserve(items.size());
    for (QListWidgetItem *item : items) {
        QString text = item->data(N::ColumnsRole).toStringList().value(column).trimmed();
        double value = -1;
        switch (kind) {
            case SortNumber:
                value = parseNumber(text);
                break;
            case SortDuration:
                value = parseDuration(text);
                break;
            case SortKey:
                value = NUtils::camelotIndex(text);
                break;
            case SortText:
                break;
        }
        entries.append({item, text, value});
    }

    QCollator collator;
    collator.setNumericMode(true); // "Track 2" before "Track 10"
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    bool ascending = order == Qt::AscendingOrder;

    std::stable_sort(entries.begin(), entries.end(), [&](const Entry &a, const Entry &b) {
        // empty values go last in both directions
        bool aEmpty = a.text.isEmpty(), bEmpty = b.text.isEmpty();
        if (aEmpty != bEmpty) {
            return bEmpty;
        }
        if (aEmpty) {
            return false;
        }
        int cmp;
        if (kind != SortText && a.value >= 0 && b.value >= 0) {
            cmp = a.value < b.value ? -1 : (a.value > b.value ? 1 : 0);
        } else if (kind != SortText && (a.value >= 0) != (b.value >= 0)) {
            return a.value >= 0; // unparseable values after the real ones
        } else {
            cmp = collator.compare(a.text, b.text);
        }
        return ascending ? cmp < 0 : cmp > 0;
    });
    QApplication::restoreOverrideCursor();

    QList<QListWidgetItem *> sorted;
    for (const Entry &e : entries) {
        sorted << e.item;
    }
    applyOrder(sorted);
}
// << Sorting by column

// Background fetching of cloud files >>

bool NPlaylistWidget::needsFetch(const QString &file)
{
    if (file.isEmpty() || m_readyFiles.contains(file)) {
        return false;
    }
    bool cloud = file.contains("/Library/Mobile Documents/") || file.contains("/Library/CloudStorage/");
#ifdef Q_OS_MAC
    struct stat st;
    if (::stat(QFile::encodeName(file).constData(), &st) == 0 && (st.st_flags & SF_DATALESS)) {
        cloud = true; // offloaded by macOS: reading it downloads it
    }
#endif
    if (!cloud) {
        m_readyFiles.insert(file);
        return false;
    }
    if (!m_fetchPending.contains(file)) {
        m_fetchPending.insert(file);
        m_fetchQueue.append(file);
        startFetches();
    }
    return true;
}

void NPlaylistWidget::startFetches()
{
    const int maxRunning = 2;
    while (m_fetchRunning < maxRunning && !m_fetchQueue.isEmpty()) {
        QString file = m_fetchQueue.takeFirst();
        ++m_fetchRunning;
        QPointer<NPlaylistWidget> self(this);
        // a plain detached thread: quitting never waits for a download
        std::thread([file, self]() {
            // reading from a cloud file makes the system bring it down;
            // read the start and the end, where the tags live
            QFile f(file);
            if (f.open(QIODevice::ReadOnly)) {
                f.read(64 * 1024);
                if (f.size() > 64 * 1024) {
                    f.seek(f.size() - 64 * 1024);
                    f.read(64 * 1024);
                }
            }
            QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [file, self]() {
                    if (self) {
                        self->fileFetched(file);
                    }
                },
                Qt::QueuedConnection);
        }).detach();
    }
}

void NPlaylistWidget::fileFetched(const QString &file)
{
    --m_fetchRunning;
    m_fetchPending.remove(file);
    m_readyFiles.insert(file);
    refreshItemsForFile(file);
    startFetches();
    if (m_sortWaitingForFetch && m_fetchPending.isEmpty()) {
        sortRows(m_sortColumn, m_sortOrder);
    }
    emit itemsChanged();
}
// << Background fetching of cloud files
QList<NPlaylistDataItem> NPlaylistWidget::selectedDataItems() const
{
    QList<NPlaylistDataItem> items;
    for (int i = 0; i < count(); ++i) {
        NPlaylistWidgetItem *it = itemAtRow(i);
        if (it->isSelected()) {
            items << it->dataItem();
        }
    }
    return items;
}
