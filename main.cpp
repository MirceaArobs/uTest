#include <QApplication>
#include <QAbstractItemView>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QList>
#include <QStringList>
#include <QTimer>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

class ActiveItemsList : public QListWidget {
public:
    explicit ActiveItemsList(QWidget *parent = nullptr) : QListWidget(parent) {
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::DropOnly);
    }

protected:
    void dragEnterEvent(QDragEnterEvent *event) override {
        if (event->mimeData()->hasText()) {
            event->acceptProposedAction();
        } else {
            event->ignore();
        }
    }

    void dragMoveEvent(QDragMoveEvent *event) override {
        if (event->mimeData()->hasText()) {
            event->acceptProposedAction();
        } else {
            event->ignore();
        }
    }

    void dropEvent(QDropEvent *event) override {
        if (!event->mimeData()->hasText()) {
            event->ignore();
            return;
        }
        const QStringList droppedItems = event->mimeData()->text().split('\n');
        for (const QString &item : droppedItems) {
            if (!item.trimmed().isEmpty()) {
                addItem(item);
            }
        }
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }
};

struct TraceEvent {
    qint64 timestampMs;
    QString channel;
    QString details;
};

class EventTimeline : public QWidget {
public:
    explicit EventTimeline(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(190);
        setMouseTracking(true);
        clock.start();
        viewEndMs = 0;

        repaintTimer.setInterval(50);
        connect(&repaintTimer, &QTimer::timeout, this, QOverload<>::of(&EventTimeline::update));
        repaintTimer.start();
    }

    void addEvent(const QString &channel, const QString &details) {
        if (!running) {
            return;
        }
        events.append({clock.elapsed(), channel, details});
        if (events.size() > 2000) {
            events.removeFirst();
            if (selectedEvent >= 0) {
                --selectedEvent;
            }
        }
        if (!paused) {
            selectedEvent = events.size() - 1;
        }
        update();
    }

    void setRunning(bool shouldRun) {
        if (running == shouldRun) {
            return;
        }
        running = shouldRun;
        if (running) {
            paused = false;
            pausedAtMs = clock.elapsed();
            viewEndMs = pausedAtMs;
            repaintTimer.start();
        } else {
            paused = true;
            pausedAtMs = clock.elapsed();
            viewEndMs = pausedAtMs;
            repaintTimer.stop();
        }
        update();
    }

    void setPaused(bool shouldPause) {
        if (!running) {
            return;
        }
        if (paused == shouldPause) {
            return;
        }
        paused = shouldPause;
        if (paused) {
            pausedAtMs = clock.elapsed();
            viewEndMs = pausedAtMs;
            repaintTimer.stop();
        } else {
            repaintTimer.start();
        }
        update();
    }

    void scrollBy(qint64 deltaMs) {
        if (running) {
            return;
        }
        viewEndMs = std::clamp<qint64>(viewEndMs + deltaMs, 0, pausedAtMs);
        update();
    }

    qint64 visibleWindowMs() const {
        return windowMs;
    }

    void zoomBy(bool zoomIn, double anchorRatio = 0.5) {
        if (running) {
            return;
        }
        constexpr qint64 minimumWindowMs = 1000;
        constexpr qint64 maximumWindowMs = 300000;
        const qint64 newWindowMs = std::clamp<qint64>(
            zoomIn ? windowMs / 2 : windowMs * 2, minimumWindowMs, maximumWindowMs);
        const qint64 anchorMs = viewEndMs - windowMs +
                                static_cast<qint64>(windowMs * anchorRatio);
        const qint64 newStartMs = anchorMs - static_cast<qint64>(newWindowMs * anchorRatio);
        windowMs = newWindowMs;
        viewEndMs = std::clamp<qint64>(newStartMs + windowMs, 0, pausedAtMs);
        update();
    }

    void setSelectionHandler(std::function<void(const QString &)> handler) {
        selectionHandler = std::move(handler);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#101820"));

        constexpr int left = 74;
        constexpr int right = 18;
        constexpr int top = 24;
        constexpr int bottom = 34;
        const int graphWidth = width() - left - right;
        const int graphHeight = height() - top - bottom;
        if (graphWidth <= 0 || graphHeight <= 0) {
            return;
        }

        const qint64 now = running && !paused ? clock.elapsed() : viewEndMs;
        const qint64 firstVisible = now - windowMs;
        const int uartY = top + graphHeight / 3;
        const int gpioY = top + (graphHeight * 2) / 3;

        painter.setPen(QColor("#81909b"));
        painter.drawText(8, uartY + 5, "UART");
        painter.drawText(8, gpioY + 5, "GPIO");
        painter.setPen(QColor("#34434d"));
        painter.drawLine(left, uartY, width() - right, uartY);
        painter.drawLine(left, gpioY, width() - right, gpioY);

        painter.setPen(QColor("#71808a"));
        const QString graphStatus = !running
                                        ? QString("Stopped - %1s").arg(windowMs / 1000.0, 0, 'g', 3)
                                        : (paused ? "Paused" : "Live");
        painter.drawText(left, 16, graphStatus);
        for (int tick = 0; tick <= 6; ++tick) {
            const int x = left + graphWidth * tick / 6;
            painter.setPen(QColor("#34434d"));
            painter.drawLine(x, top, x, height() - bottom);
            painter.setPen(QColor("#aab5bc"));
            const double secondsAgo = windowMs * (6 - tick) / 6000.0;
            painter.drawText(x - 14, height() - 10,
                             QString::number(secondsAgo, 'g', 2) + "s");
        }

        for (int i = 0; i < events.size(); ++i) {
            const TraceEvent &event = events.at(i);
            if (event.timestampMs < firstVisible || event.timestampMs > now) {
                continue;
            }

            const int x = left + static_cast<int>(
                static_cast<double>(event.timestampMs - firstVisible) / windowMs * graphWidth);
            const bool isUart = event.channel.compare("UART", Qt::CaseInsensitive) == 0;
            const int y = isUart ? uartY : gpioY;
            const QColor color = isUart ? QColor("#49c6e5") : QColor("#f4b942");

            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawEllipse(QPoint(x, y), i == selectedEvent ? 8 : 6,
                                i == selectedEvent ? 8 : 6);
            painter.setPen(QColor("#101820"));
            painter.drawText(x - 9, y + 4, event.details.left(2));
            if (i == selectedEvent) {
                painter.setPen(color);
                painter.drawLine(x, y + 9, x, y + 21);
            }
        }

        if (cursorVisible) {
            painter.setPen(QPen(QColor("#dce6eb"), 1, Qt::DashLine));
            painter.drawLine(cursorX, top, cursorX, height() - bottom);
            painter.drawLine(left, cursorY, width() - right, cursorY);
        }
    }

    void leaveEvent(QEvent *) override {
        if (cursorHidden) {
            unsetCursor();
            cursorHidden = false;
        }
        cursorVisible = false;
        update();
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        constexpr int left = 74;
        constexpr int right = 18;
        constexpr int top = 24;
        constexpr int bottom = 34;
        cursorVisible = event->pos().x() >= left && event->pos().x() <= width() - right &&
                        event->pos().y() >= top && event->pos().y() <= height() - bottom;
        if (cursorVisible != cursorHidden) {
            if (cursorVisible) {
                setCursor(Qt::BlankCursor);
            } else {
                unsetCursor();
            }
            cursorHidden = cursorVisible;
        }
        cursorX = event->pos().x();
        cursorY = event->pos().y();
        if (dragging && !running && (event->buttons() & Qt::LeftButton)) {
            const int graphWidth = width() - 74 - 18;
            if (graphWidth > 0) {
                const int deltaX = event->pos().x() - dragStartX;
                dragMoved = dragMoved || std::abs(deltaX) > 3;
                if (dragMoved) {
                    viewEndMs = std::clamp<qint64>(
                        dragStartEndMs - static_cast<qint64>(deltaX) * windowMs / graphWidth,
                        0, pausedAtMs);
                }
            }
        }
        update();
    }

    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton) {
            return;
        }
        if (!running && event->pos().x() >= 74 && event->pos().x() <= width() - 18 &&
            event->pos().y() >= 24 && event->pos().y() <= height() - 34) {
            dragging = true;
            dragMoved = false;
            dragStartX = event->pos().x();
            dragStartEndMs = viewEndMs;
            return;
        }
        selectEventAt(event->pos());
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && dragging) {
            if (!dragMoved) {
                selectEventAt(event->pos());
            }
            dragging = false;
        }
    }

    void wheelEvent(QWheelEvent *event) override {
        if (running || event->angleDelta().y() == 0) {
            event->ignore();
            return;
        }
        constexpr int left = 74;
        constexpr int right = 18;
        const int graphWidth = width() - left - right;
        const double anchorRatio = graphWidth > 0
                                       ? std::clamp(static_cast<double>(event->pos().x() - left) /
                                                        graphWidth,
                                                    0.0, 1.0)
                                       : 0.5;
        zoomBy(event->angleDelta().y() > 0, anchorRatio);
        event->accept();
    }

private:
    void selectEventAt(const QPoint &position) {
        constexpr int left = 74;
        constexpr int right = 18;
        const int graphWidth = width() - left - right;
        if (graphWidth <= 0) {
            return;
        }

        const qint64 now = running && !paused ? clock.elapsed() : viewEndMs;
        const qint64 firstVisible = now - windowMs;
        const int graphHeight = height() - 24 - 34;
        const int uartY = 24 + graphHeight / 3;
        const int gpioY = 24 + (graphHeight * 2) / 3;
        int nearestIndex = -1;
        double nearestDistance = 11.0;

        for (int i = 0; i < events.size(); ++i) {
            const TraceEvent &traceEvent = events.at(i);
            if (traceEvent.timestampMs < firstVisible || traceEvent.timestampMs > now) {
                continue;
            }
            const int x = left + static_cast<int>(
                static_cast<double>(traceEvent.timestampMs - firstVisible) / windowMs * graphWidth);
            const bool isUart = traceEvent.channel.compare("UART", Qt::CaseInsensitive) == 0;
            const int y = isUart ? uartY : gpioY;
            const double distance = std::hypot(position.x() - x, position.y() - y);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestIndex = i;
            }
        }

        if (nearestIndex >= 0) {
            selectedEvent = nearestIndex;
            update();
            if (selectionHandler) {
                const TraceEvent &selected = events.at(selectedEvent);
                selectionHandler(QString("%1 ms | %2 | %3")
                                     .arg(selected.timestampMs)
                                     .arg(selected.channel, selected.details));
            }
        }
    }

    QElapsedTimer clock;
    QTimer repaintTimer;
    QList<TraceEvent> events;
    int selectedEvent = -1;
    qint64 pausedAtMs = 0;
    qint64 viewEndMs = 0;
    qint64 windowMs = 30000;
    qint64 dragStartEndMs = 0;
    int dragStartX = 0;
    int cursorX = 0;
    int cursorY = 0;
    bool dragging = false;
    bool dragMoved = false;
    bool paused = false;
    bool running = true;
    bool cursorVisible = false;
    bool cursorHidden = false;
    std::function<void(const QString &)> selectionHandler;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle("Framebuffer Qt Demo");
    window.resize(800, 480);

    auto *mainLayout = new QVBoxLayout(&window);
    auto *itemListsLayout = new QHBoxLayout();
    auto *catalogPanel = new QWidget(&window);
    auto *catalogLayout = new QVBoxLayout(catalogPanel);
    catalogLayout->setContentsMargins(0, 0, 0, 0);
    auto *catalogLabel = new QLabel("Test catalog (drag items to active list)", catalogPanel);
    auto *listWidget = new QListWidget(catalogPanel);
    listWidget->addItems({"Item 1: Initialize System",
                          "Item 2: Calibrate Sensors",
                          "Item 3: Run Diagnostic",
                          "Item 4: Update Firmware",
                          "Item 5: Shut Down"});
    listWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    listWidget->setDragEnabled(true);
    listWidget->setDragDropMode(QAbstractItemView::DragOnly);
    catalogLayout->addWidget(catalogLabel);
    catalogLayout->addWidget(listWidget);

    auto *activePanel = new QWidget(&window);
    auto *activeLayout = new QVBoxLayout(activePanel);
    activeLayout->setContentsMargins(0, 0, 0, 0);
    auto *activeLabel = new QLabel("Active items", activePanel);
    auto *activeList = new ActiveItemsList(activePanel);
    activeList->setSelectionMode(QAbstractItemView::SingleSelection);
    activeList->setToolTip("Drop test catalog items here to add them to the execution list.");
    activeLayout->addWidget(activeLabel);
    activeLayout->addWidget(activeList);
    itemListsLayout->addWidget(catalogPanel, 1);
    itemListsLayout->addWidget(activePanel, 1);
    mainLayout->addLayout(itemListsLayout, 1);

    auto *eventControls = new QHBoxLayout();
    eventControls->setSpacing(4);
    auto *btnAction = new QPushButton("Process active", &window);
    auto *btnExit = new QPushButton("Exit", &window);
    auto *uartButton = new QPushButton("UART demo", &window);
    auto *gpioButton = new QPushButton("GPIO demo", &window);
    auto *captureButton = new QPushButton("Stop capture", &window);
    captureButton->setCheckable(true);
    captureButton->setChecked(true);
    auto *pauseButton = new QPushButton("Pause scroll", &window);
    pauseButton->setCheckable(true);
    auto *olderButton = new QPushButton("Older", &window);
    auto *newerButton = new QPushButton("Newer", &window);
    auto *zoomOutButton = new QPushButton("Zoom -", &window);
    auto *zoomInButton = new QPushButton("Zoom +", &window);
    olderButton->setEnabled(false);
    newerButton->setEnabled(false);
    zoomOutButton->setEnabled(false);
    zoomInButton->setEnabled(false);
    const QList<QPushButton *> buttons = {btnAction, btnExit, uartButton, gpioButton,
                                          captureButton, pauseButton, olderButton, newerButton,
                                          zoomOutButton, zoomInButton};
    for (QPushButton *button : buttons) {
        button->setMinimumHeight(40);
        eventControls->addWidget(button);
    }
    mainLayout->addLayout(eventControls);

    auto *timeline = new EventTimeline(&window);
    mainLayout->addWidget(timeline, 2);

    auto *statusLine = new QLabel("Select an event marker to view its full details.", &window);
    statusLine->setMinimumHeight(28);
    statusLine->setWordWrap(true);
    statusLine->setStyleSheet("QLabel { background: #e8edf0; padding: 5px; }");
    timeline->setSelectionHandler([statusLine](const QString &details) {
        statusLine->setText(details);
    });

    mainLayout->addWidget(statusLine);

    QObject::connect(btnAction, &QPushButton::clicked, [&]() {
        QListWidgetItem *selected = activeList->currentItem();
        if (selected) {
            QMessageBox::information(&window, "Action Triggered",
                                     "Processing: " + selected->text());
        } else {
            QMessageBox::warning(&window, "Selection Missing",
                                 "Please add and select an active item first.");
        }
    });
    QObject::connect(btnExit, &QPushButton::clicked, &app, &QApplication::quit);
    QObject::connect(uartButton, &QPushButton::clicked, [timeline]() {
        timeline->addEvent("UART", "RX: sensor response received");
    });
    QObject::connect(gpioButton, &QPushButton::clicked, [timeline]() {
        static bool pinHigh = false;
        pinHigh = !pinHigh;
        timeline->addEvent("GPIO", pinHigh ? "GPIO 17 HIGH" : "GPIO 17 LOW");
    });
    QObject::connect(pauseButton, &QPushButton::clicked, [timeline, pauseButton](bool paused) {
        timeline->setPaused(paused);
        pauseButton->setText(paused ? "Resume scrolling" : "Pause scrolling");
    });
    QObject::connect(captureButton, &QPushButton::clicked,
                     [timeline, captureButton, pauseButton, olderButton, newerButton,
                      zoomOutButton, zoomInButton](bool running) {
                         timeline->setRunning(running);
                         captureButton->setText(running ? "Stop capture" : "Start capture");
                         pauseButton->setChecked(false);
                         pauseButton->setText("Pause scroll");
                         pauseButton->setEnabled(running);
                         olderButton->setEnabled(!running);
                         newerButton->setEnabled(!running);
                         zoomOutButton->setEnabled(!running);
                         zoomInButton->setEnabled(!running);
                     });
    QObject::connect(olderButton, &QPushButton::clicked, [timeline]() {
        timeline->scrollBy(-timeline->visibleWindowMs() / 4);
    });
    QObject::connect(newerButton, &QPushButton::clicked, [timeline]() {
        timeline->scrollBy(timeline->visibleWindowMs() / 4);
    });
    QObject::connect(zoomOutButton, &QPushButton::clicked, [timeline]() {
        timeline->zoomBy(false);
    });
    QObject::connect(zoomInButton, &QPushButton::clicked, [timeline]() {
        timeline->zoomBy(true);
    });

    window.show();
    return app.exec();
}
