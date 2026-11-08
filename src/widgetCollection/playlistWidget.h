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

#ifndef N_PLAYLIST_WIDGET_H
#define N_PLAYLIST_WIDGET_H

#include <QList>
#include <QListWidget>
#include <QSet>
#include <QPointer>

#include "global.h"

class NPlaylistDataItem;
class NPlaylistWidgetItem;
class NTrackInfoReader;
class NPlaybackEngineInterface;
class QContextMenuEvent;
class QHeaderView;
class QStandardItemModel;
class QDropEvent;
class QMenu;
class QString;
class QStringList;

class NPlaylistWidget : public QListWidget
{
    Q_OBJECT
    Q_PROPERTY(QColor failed_text_color READ failedTextColor WRITE setFailedTextColor)
    Q_PROPERTY(QColor playing_text_color READ playingTextColor WRITE setPlayingTextColor)
    Q_PROPERTY(QColor file_drop_border_color READ fileDropBorderColor WRITE setFileDropBorderColor)
    Q_PROPERTY(QBrush file_drop_background READ fileDropBackground WRITE setFileDropBackground)
    Q_PROPERTY(int file_drop_radius READ fileDropRadius WRITE setFileDropRadius)

private:
    NPlaylistWidgetItem *m_playingItem;
    QMap<int, NPlaylistWidgetItem *> m_itemMap;
    NTrackInfoReader *m_trackInfoReader;
    NPlaybackEngineInterface *m_playbackEngine;
    QTimer *m_processVisibleItemsTimer;
    bool m_repeatMode;

    // columns mode >>
    bool m_columnsMode;
    QHeaderView *m_header;
    QStandardItemModel *m_headerModel;
    QStringList m_columnFormats;
    QStringList m_columnSpecs; // PlaylistColumnsFormat the columns were built from
    QList<Qt::Alignment> m_columnAlignments;
    void loadColumns();
    void updateHeaderGeometry();
    void updateHeaderStyle();
    void saveColumnWidths();
    // until the user resizes a column, widths follow the playlist width
    bool m_columnsAutoWidth;
    bool m_applyingColumnWidths;
    bool m_headerPressed; // mouse is down on the header (user dragging a divider)
    void applyAutoColumnWidths();

    // sorting by a column reorders the playlist itself; clicking the column a
    // third time puts back the order from before the first sort, as long as
    // nothing else (drag, shuffle, add, remove) has changed the order since
    QStringList m_headerLabels;
    int m_sortColumn;
    Qt::SortOrder m_sortOrder;
    QList<QListWidgetItem *> m_presortOrder;
    void sortByColumn(int column);
    void sortRows(int column, Qt::SortOrder order);
    bool m_sortWaitingForFetch; // a sort is waiting for cloud files to arrive
    void applyOrder(const QList<QListWidgetItem *> &order);
    void clearSortState();
    void updateSortIndicator();
    // << columns mode

    void addItem(NPlaylistWidgetItem *item);
    void paintEvent(QPaintEvent *event);
    void contextMenuEvent(QContextMenuEvent *event);
    void resizeEvent(QResizeEvent *event);
    void changeEvent(QEvent *event);
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool refreshItemData(NPlaylistWidgetItem *item, QString titleFormat, bool force = false);
    NPlaylistWidgetItem *nextItem(NPlaylistWidgetItem *) const;
    NPlaylistWidgetItem *prevItem(NPlaylistWidgetItem *item) const;
    void resetPlayingItem();
    bool revealInFileManager(const QString &file, QString *error) const;

protected:
    void wheelEvent(QWheelEvent *event);
    void mousePressEvent(QMouseEvent *event);

protected slots:
    void rowsInserted(const QModelIndex &parent, int start, int end);

private slots:
    void startProcessVisibleItemsTimer();

    void on_playbackEngine_mediaChanged(const QString &file, int id);
    void on_playbackEngine_prepareNextMediaRequested();
    void on_playbackEngine_mediaFinished(const QString &file, int id);
    void on_playbackEngine_mediaFailed(const QString &file, int id);

public:
    NPlaylistWidget(QWidget *parent = 0);
    ~NPlaylistWidget();

    QStringList selectedFiles() const;
    QList<NPlaylistDataItem> selectedDataItems() const; // in playlist order
    NPlaylistWidgetItem *itemAtRow(int row) const;
    void invalidateItemData();
    void refreshItemsForFile(const QString &file);
    void refreshItemsForFiles(const QStringList &files);
    NPlaylistWidgetItem *playingItem() const;
    int playingRow() const;
    Q_INVOKABLE bool hasPlayingItem() const;
    Q_INVOKABLE bool repeatMode() const;

    void setTrackInfoReader(NTrackInfoReader *reader);

    bool columnsMode() const { return m_columnsMode; }
    int columnCount() const;
    int columnX(int column) const;
    int columnWidth(int column) const;
    Qt::Alignment columnAlignment(int column) const;

public slots:
    void setColumnsMode(bool enable);
    void playRow(int row);
    void playItem(NPlaylistWidgetItem *item);
    void playNextItem();
    void playPrevItem();

    void setPlayingItem(NPlaylistWidgetItem *item); // does not start playback

    void addFiles(const QStringList &files);
    void addItems(const QList<NPlaylistDataItem> &dataItems);
    void setFiles(const QStringList &files);
    void setItems(const QList<NPlaylistDataItem> &dataItems);
    bool setPlaylist(const QString &file);
    void shufflePlaylist();
    void processVisibleItems();
    void updateTrackIndexes();
    void calculateDuration();
    void setRepeatMode(bool enable);
    void removeSelected();
    void removeFiles(const QStringList &files);

signals:
    void addMoreRequested();
    void contextMenuRequested(const QPoint &pos);

    void repeatModeChanged(bool enable);

    void itemsChanged();
    void playingItemChanged();
    void durationChanged(int seconds);
    void playlistFinished();

    // DRAG & DROP >>
public:
    enum DragStart
    {
        DragStartInside,
        DragStartOutside
    };
    Q_ENUM(DragStart)
    enum DropEnd
    {
        DropEndInside,
        DropEndOutside
    };
    Q_ENUM(DropEnd)
private:
    DragStart m_dragStart;
    DropEnd m_dropEnd;
    QPointer<QDrag> m_itemDrag;
    // Files that may have to be downloaded before they can be read (iCloud
    // Drive, other cloud folders, files macOS has offloaded) are fetched on a
    // background thread first, so reading their tags never blocks the UI.
    QSet<QString> m_readyFiles;   // safe to read on the main thread
    QSet<QString> m_fetchPending; // queued or being fetched
    QStringList m_fetchQueue;
    int m_fetchRunning;
    bool needsFetch(const QString &file);
    void startFetches();
    void fileFetched(const QString &file);

    // Files that weren't there when their row was read (e.g. on an unplugged
    // USB drive). They're checked again every few seconds and the rows are
    // re-read once the files appear.
    QSet<QString> m_missingFiles;
    QTimer *m_missingFilesTimer;
    void checkMissingFiles();
    void setPlaceholderData(NPlaylistWidgetItem *item, const QString &file);

    QPoint m_pressPos;     // where the left button went down
    bool m_pressOnItem;    // ...and whether that was on a track
    bool m_fileDrop;
    QList<QUrl> m_mimeDataUrls;
    QStringList mimeTypes() const;
    QMimeData *mimeData(const QList<QListWidgetItem *> items) const;
    bool dropMimeData(int index, const QMimeData *data, Qt::DropAction action);

protected:
    void dropEvent(QDropEvent *event);
    void dragEnterEvent(QDragEnterEvent *event);
    void dragMoveEvent(QDragMoveEvent *event);
    void dragLeaveEvent(QDragLeaveEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    // << DRAG & DROP

    // STYLESHEET PROPERTIES >>
private:
    QColor m_failedTextColor;
    QColor m_playingTextColor;
    QColor m_fileDropBorderColor;
    QBrush m_fileDropBackground;
    int m_fileDropRadius;

public:
    QColor failedTextColor() const;
    void setFailedTextColor(QColor color);

    QColor playingTextColor() const;
    void setPlayingTextColor(QColor color);

    QColor fileDropBorderColor() const;
    void setFileDropBorderColor(QColor color);

    QBrush fileDropBackground() const;
    void setFileDropBackground(QBrush brush);

    int fileDropRadius() const;
    void setFileDropRadius(int radius);
    // << STYLESHEET PROPERTIES
};

#endif
