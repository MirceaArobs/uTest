#include <QApplication>
#include <QAbstractItemView>
#include <QByteArray>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMetaObject>
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
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <thread>
#include <utility>

#ifdef __linux__
#include <fcntl.h>
#include <linux/gpio.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

class CatalogItemsList : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    QMimeData *mimeData(const QList<QListWidgetItem *> items) const override {
        auto *data = new QMimeData;
        QStringList itemTexts;
        for (const QListWidgetItem *item : items) {
            itemTexts.append(item->text());
        }
        data->setText(itemTexts.join('\n'));
        return data;
    }
};

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
        if (count() > 0) {
            setCurrentRow(count() - 1);
            scrollToItem(currentItem());
        }
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
        painter.drawText(8, gpioY - 5, "GPIO22");
        painter.drawText(8, gpioY + 13, "GPIO17");
        painter.drawText(8, gpioY + 28, "GPIO");
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

        drawGpioWaveform(painter, "GPIO22", now, firstVisible, graphWidth, gpioY,
                         QColor("#f4b942"));
        drawGpioWaveform(painter, "GPIO17", now, firstVisible, graphWidth, gpioY,
                         QColor("#78d6a5"));
        drawGpioWaveform(painter, "GPIO", now, firstVisible, graphWidth, gpioY,
                         QColor("#c6a0f6"));

        for (int i = 0; i < events.size(); ++i) {
            const TraceEvent &event = events.at(i);
            if (event.timestampMs < firstVisible || event.timestampMs > now) {
                continue;
            }

            const int x = left + static_cast<int>(
                static_cast<double>(event.timestampMs - firstVisible) / windowMs * graphWidth);
            const bool isUart = event.channel.compare("UART", Qt::CaseInsensitive) == 0;
            const bool isCollision =
                event.channel.compare("COLLISION", Qt::CaseInsensitive) == 0;
            if (isUart || isCollision) {
                const QColor color = isCollision ? QColor("#ff8c32") : QColor("#49c6e5");
                painter.setPen(Qt::NoPen);
                painter.setBrush(color);
                painter.drawEllipse(QPoint(x, uartY), i == selectedEvent ? 8 : 6,
                                    i == selectedEvent ? 8 : 6);
                painter.setPen(QColor("#101820"));
                painter.drawText(x - 4, uartY + 4, isCollision ? "!" : event.details.left(2));
            } else if (event.channel.startsWith("GPIO", Qt::CaseInsensitive)) {
                const int centerY = gpioTrackCenter(event.channel, gpioY);
                const bool high = event.details.contains("HIGH", Qt::CaseInsensitive);
                const int edgeY = centerY + (high ? -5 : 5);
                const QColor color =
                    event.channel.compare("GPIO22", Qt::CaseInsensitive) == 0
                        ? QColor("#f4b942")
                        : (event.channel.compare("GPIO17", Qt::CaseInsensitive) == 0
                               ? QColor("#78d6a5")
                               : QColor("#c6a0f6"));
                painter.setPen(Qt::NoPen);
                painter.setBrush(color);
                painter.drawEllipse(QPoint(x, edgeY), i == selectedEvent ? 5 : 3,
                                    i == selectedEvent ? 5 : 3);
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
    static int gpioTrackCenter(const QString &channel, int gpioY) {
        if (channel.compare("GPIO22", Qt::CaseInsensitive) == 0) {
            return gpioY - 8;
        }
        if (channel.compare("GPIO17", Qt::CaseInsensitive) == 0) {
            return gpioY + 8;
        }
        return gpioY;
    }

    void drawGpioWaveform(QPainter &painter, const QString &channel, qint64 now,
                          qint64 firstVisible, int graphWidth, int gpioY, const QColor &color) {
        constexpr int left = 74;
        const int centerY = gpioTrackCenter(channel, gpioY);
        const int highY = centerY - 5;
        const int lowY = centerY + 5;
        bool high = false;
        int previousX = left;

        painter.setPen(QPen(color, 2));
        for (const TraceEvent &traceEvent : events) {
            if (traceEvent.channel.compare(channel, Qt::CaseInsensitive) != 0) {
                continue;
            }
            if (traceEvent.timestampMs < firstVisible) {
                high = traceEvent.details.contains("HIGH", Qt::CaseInsensitive);
                continue;
            }
            if (traceEvent.timestampMs > now) {
                break;
            }
            const int x = left + static_cast<int>(
                static_cast<double>(traceEvent.timestampMs - firstVisible) / windowMs *
                graphWidth);
            painter.drawLine(previousX, high ? highY : lowY, x, high ? highY : lowY);
            high = traceEvent.details.contains("HIGH", Qt::CaseInsensitive);
            painter.drawLine(x, high ? lowY : highY, x, high ? highY : lowY);
            previousX = x;
        }

        const int endX = std::clamp(
            left + static_cast<int>(static_cast<double>(now - firstVisible) / windowMs *
                                    graphWidth),
            left, left + graphWidth);
        painter.drawLine(previousX, high ? highY : lowY, endX, high ? highY : lowY);
    }

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
            const bool isUart = traceEvent.channel.compare("UART", Qt::CaseInsensitive) == 0 ||
                                traceEvent.channel.compare("COLLISION", Qt::CaseInsensitive) == 0;
            const int y = isUart ? uartY : gpioTrackCenter(traceEvent.channel, gpioY);
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

class HardwareController {
public:
    HardwareController(EventTimeline *timeline, std::function<void(const QString &)> statusHandler)
        : timeline(timeline), statusHandler(std::move(statusHandler)) {
#ifdef __linux__
        openHardware();
#else
        reportStatus("UART/GPIO hardware access is only available on Linux.");
#endif
    }

    ~HardwareController() {
#ifdef __linux__
        stopRequested.store(true);
        if (monitorThread.joinable()) {
            monitorThread.join();
        }
        closeIfOpen(gpioEventFd);
        closeIfOpen(gpioOutputFd);
        closeIfOpen(gpioChipFd);
        closeIfOpen(uartFd);
#endif
    }

    bool sendDummyTelegram(QString *error) {
#ifdef __linux__
        if (uartFd < 0) {
            *error = uartError;
            return false;
        }

        ++sequence;
        const int payloadLength = 3 + sequence % 10;
        QByteArray telegram;
        telegram.append(static_cast<char>(0xa5));
        telegram.append(static_cast<char>(payloadLength));
        telegram.append(static_cast<char>(sequence & 0xff));
        unsigned char checksum = static_cast<unsigned char>(payloadLength ^ (sequence & 0xff));
        for (int i = 0; i < payloadLength; ++i) {
            const auto byte = static_cast<unsigned char>((sequence * 31 + i * 17) & 0xff);
            telegram.append(static_cast<char>(byte));
            checksum ^= byte;
        }
        telegram.append(static_cast<char>(checksum));

        const auto txDuration = std::chrono::nanoseconds(
            static_cast<qint64>(telegram.size()) * 10 * 1000000000LL / 115200 + 10000000LL);
        const qint64 deadline = monotonicNanoseconds() + txDuration.count();
        transmitting.store(true);
        transmitDeadlineNs.store(deadline);
        int sent = 0;
        while (sent < telegram.size()) {
            const ssize_t count = ::write(uartFd, telegram.constData() + sent,
                                          static_cast<size_t>(telegram.size() - sent));
            if (count > 0) {
                sent += static_cast<int>(count);
                continue;
            }
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                pollfd descriptor{uartFd, POLLOUT, 0};
                const int pollResult = ::poll(&descriptor, 1, 500);
                if (pollResult > 0 && (descriptor.revents & POLLOUT)) {
                    continue;
                }
                const int writeError = pollResult == 0 ? ETIMEDOUT : errno;
                transmitting.store(false);
                transmitDeadlineNs.store(
                    monotonicNanoseconds() +
                    static_cast<qint64>(sent) * 10 * 1000000000LL / 115200 + 10000000LL);
                if (sent > 0) {
                    postEvent("UART", "TX partial [" + hexBytes(telegram.left(sent)) + "]");
                }
                *error = QString("UART write timed out or failed after %1/%2 bytes: %3")
                             .arg(sent)
                             .arg(telegram.size())
                             .arg(QString::fromLocal8Bit(std::strerror(writeError)));
                return false;
            }
            const int errorNumber = count == 0 ? EIO : errno;
            transmitting.store(false);
            transmitDeadlineNs.store(monotonicNanoseconds() +
                                     static_cast<qint64>(sent) * 10 * 1000000000LL / 115200 +
                                     10000000LL);
            if (sent > 0) {
                postEvent("UART", "TX partial [" + hexBytes(telegram.left(sent)) + "]");
            }
            *error = QString("UART write failed after %1/%2 bytes: %3")
                         .arg(sent)
                         .arg(telegram.size())
                         .arg(QString::fromLocal8Bit(std::strerror(errorNumber)));
            return false;
        }
        transmitting.store(false);
        postEvent("UART", "TX [" + hexBytes(telegram) + "]");
        return true;
#else
        *error = "UART hardware access is only available on Linux.";
        return false;
#endif
    }

    bool toggleGpio17(QString *error) {
#ifdef __linux__
        if (gpioOutputFd < 0) {
            *error = gpioOutputError;
            return false;
        }
        const bool nextHigh = !gpio17High;
        gpiohandle_data value{};
        value.values[0] = nextHigh ? 1 : 0;
        if (::ioctl(gpioOutputFd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &value) < 0) {
            *error = QString("Unable to set GPIO17: %1")
                         .arg(QString::fromLocal8Bit(std::strerror(errno)));
            return false;
        }
        gpio17High = nextHigh;
        postEvent("GPIO17", nextHigh ? "GPIO17 HIGH" : "GPIO17 LOW");
        return true;
#else
        *error = "GPIO hardware access is only available on Linux.";
        return false;
#endif
    }

private:
#ifdef __linux__
    static void closeIfOpen(int &fd) {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

    static qint64 monotonicNanoseconds() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    static QString hexBytes(const QByteArray &bytes) {
        return QString::fromLatin1(bytes.toHex(' ').toUpper());
    }

    void openHardware() {
        const QString uartPath = qEnvironmentVariable("UTEST_UART_DEVICE", "/dev/serial0");
        uartFd = ::open(uartPath.toLocal8Bit().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (uartFd >= 0) {
            termios settings{};
            if (::tcgetattr(uartFd, &settings) == 0) {
                ::cfmakeraw(&settings);
                if (::cfsetispeed(&settings, B115200) < 0 ||
                    ::cfsetospeed(&settings, B115200) < 0) {
                    uartError = QString("Cannot set baud rate for %1: %2")
                                    .arg(uartPath, QString::fromLocal8Bit(std::strerror(errno)));
                    closeIfOpen(uartFd);
                }
                settings.c_cflag |= CLOCAL | CREAD;
                settings.c_cflag &= static_cast<tcflag_t>(~(PARENB | CSTOPB | CSIZE));
                settings.c_cflag |= CS8;
                settings.c_cc[VMIN] = 0;
                settings.c_cc[VTIME] = 0;
                if (uartFd >= 0 && ::tcsetattr(uartFd, TCSANOW, &settings) < 0) {
                    uartError = QString("Cannot configure %1: %2")
                                    .arg(uartPath, QString::fromLocal8Bit(std::strerror(errno)));
                    closeIfOpen(uartFd);
                }
            } else {
                uartError = QString("Cannot read settings for %1: %2")
                                .arg(uartPath, QString::fromLocal8Bit(std::strerror(errno)));
                closeIfOpen(uartFd);
            }
        } else {
            uartError = QString("Cannot open %1: %2")
                            .arg(uartPath, QString::fromLocal8Bit(std::strerror(errno)));
        }
        if (uartFd >= 0) {
            uartError = QString("%1 is ready at 115200 8N1").arg(uartPath);
        }

        const QString gpioPath = qEnvironmentVariable("UTEST_GPIO_CHIP", "/dev/gpiochip0");
        gpioChipFd = ::open(gpioPath.toLocal8Bit().constData(), O_RDONLY);
        if (gpioChipFd < 0) {
            const QString error = QString("Cannot open %1: %2")
                                      .arg(gpioPath, QString::fromLocal8Bit(std::strerror(errno)));
            gpioOutputError = error;
            gpioInputError = error;
        } else {
            gpiohandle_request outputRequest{};
            outputRequest.lineoffsets[0] = 17;
            outputRequest.flags = GPIOHANDLE_REQUEST_OUTPUT;
            outputRequest.default_values[0] = 0;
            outputRequest.lines = 1;
            std::strncpy(outputRequest.consumer_label, "uTest GPIO17 output",
                         sizeof(outputRequest.consumer_label) - 1);
            if (::ioctl(gpioChipFd, GPIO_GET_LINEHANDLE_IOCTL, &outputRequest) == 0) {
                gpioOutputFd = outputRequest.fd;
                gpioOutputError = "GPIO17 output is ready";
            } else {
                gpioOutputError = QString("Cannot request GPIO17 output: %1")
                                      .arg(QString::fromLocal8Bit(std::strerror(errno)));
            }

            gpioevent_request inputRequest{};
            inputRequest.lineoffset = 22;
            inputRequest.eventflags = GPIOEVENT_REQUEST_BOTH_EDGES;
            std::strncpy(inputRequest.consumer_label, "uTest GPIO22 monitor",
                         sizeof(inputRequest.consumer_label) - 1);
            if (::ioctl(gpioChipFd, GPIO_GET_LINEEVENT_IOCTL, &inputRequest) == 0) {
                gpioEventFd = inputRequest.fd;
                gpioInputError = "GPIO22 input monitoring is ready";
            } else {
                gpioInputError = QString("Cannot monitor GPIO22: %1")
                                     .arg(QString::fromLocal8Bit(std::strerror(errno)));
            }
        }

        reportStatus(QString("%1; %2; %3")
                         .arg(uartError, gpioOutputError, gpioInputError));
        if (uartFd >= 0 || gpioEventFd >= 0) {
            monitorThread = std::thread([this]() { monitorHardware(); });
        }
    }

    void configureMonitorThread() {
        const long cpuCount = ::sysconf(_SC_NPROCESSORS_ONLN);
        const int dedicatedCore = cpuCount > 1 ? 1 : 0;
        cpu_set_t cpuSet;
        CPU_ZERO(&cpuSet);
        CPU_SET(dedicatedCore, &cpuSet);
        const int affinityResult = ::pthread_setaffinity_np(
            ::pthread_self(), sizeof(cpuSet), &cpuSet);

        sched_param priority{};
        priority.sched_priority = ::sched_get_priority_max(SCHED_FIFO);
        const int priorityResult = priority.sched_priority > 0
                                       ? ::pthread_setschedparam(::pthread_self(), SCHED_FIFO,
                                                                 &priority)
                                       : ENOTSUP;

        QString status = affinityResult == 0
                             ? QString("Hardware monitor pinned to CPU %1").arg(dedicatedCore)
                             : QString("Could not pin hardware monitor to CPU %1: %2")
                                   .arg(dedicatedCore)
                                   .arg(QString::fromLocal8Bit(std::strerror(affinityResult)));
        status += priorityResult == 0
                      ? "; SCHED_FIFO high priority enabled"
                      : QString("; SCHED_FIFO unavailable: %1")
                            .arg(QString::fromLocal8Bit(std::strerror(priorityResult)));
        reportStatus(status);
    }

    void monitorHardware() {
        configureMonitorThread();
        while (!stopRequested.load()) {
            pollfd descriptors[2]{};
            nfds_t descriptorCount = 0;
            int uartIndex = -1;
            int gpioIndex = -1;
            if (uartFd >= 0) {
                uartIndex = static_cast<int>(descriptorCount);
                descriptors[descriptorCount++] = {uartFd, POLLIN, 0};
            }
            if (gpioEventFd >= 0) {
                gpioIndex = static_cast<int>(descriptorCount);
                descriptors[descriptorCount++] = {gpioEventFd, POLLIN, 0};
            }
            if (::poll(descriptors, descriptorCount, 100) < 0) {
                if (errno == EINTR) {
                    continue;
                }
                reportStatus(QString("Hardware monitor poll failed: %1")
                                 .arg(QString::fromLocal8Bit(std::strerror(errno))));
                break;
            }
            if ((uartIndex >= 0 &&
                 (descriptors[uartIndex].revents & (POLLERR | POLLHUP | POLLNVAL))) ||
                (gpioIndex >= 0 &&
                 (descriptors[gpioIndex].revents & (POLLERR | POLLHUP | POLLNVAL)))) {
                reportStatus("UART or GPIO monitor device closed or reported an I/O error.");
                break;
            }

            if (uartIndex >= 0 && (descriptors[uartIndex].revents & POLLIN)) {
                QByteArray received;
                char buffer[256];
                for (;;) {
                    const ssize_t count = ::read(uartFd, buffer, sizeof(buffer));
                    if (count > 0) {
                        received.append(buffer, static_cast<int>(count));
                    } else if (count < 0 && errno == EINTR) {
                        continue;
                    } else {
                        break;
                    }
                }
                if (!received.isEmpty()) {
                    const QString details = "RX [" + hexBytes(received) + "]";
                    postEvent("UART", details);
                    if (transmitting.load() || monotonicNanoseconds() <= transmitDeadlineNs.load()) {
                        postEvent("COLLISION", "UART collision: received " + hexBytes(received) +
                                                   " during transmit");
                    }
                }
            }

            if (gpioIndex >= 0 && (descriptors[gpioIndex].revents & POLLIN)) {
                gpioevent_data edge{};
                const ssize_t count = ::read(gpioEventFd, &edge, sizeof(edge));
                if (count == static_cast<ssize_t>(sizeof(edge))) {
                    const bool high = edge.id == GPIOEVENT_EVENT_RISING_EDGE;
                    postEvent("GPIO22", high ? "GPIO22 HIGH" : "GPIO22 LOW");
                } else if (count < 0 && errno != EINTR && errno != EAGAIN) {
                    reportStatus(QString("GPIO22 monitor read failed: %1")
                                     .arg(QString::fromLocal8Bit(std::strerror(errno))));
                }
            }
        }
    }
#endif

    void postEvent(const QString &channel, const QString &details) {
        QMetaObject::invokeMethod(
            timeline, [timeline = timeline, channel, details]() {
                timeline->addEvent(channel, details);
            },
            Qt::QueuedConnection);
    }

    void reportStatus(const QString &message) {
        QMetaObject::invokeMethod(
            timeline, [handler = statusHandler, message]() { handler(message); },
            Qt::QueuedConnection);
    }

    EventTimeline *timeline;
    std::function<void(const QString &)> statusHandler;
#ifdef __linux__
    int uartFd = -1;
    int gpioChipFd = -1;
    int gpioOutputFd = -1;
    int gpioEventFd = -1;
    int sequence = 0;
    bool gpio17High = false;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> transmitting{false};
    std::atomic<qint64> transmitDeadlineNs{0};
    std::thread monitorThread;
    QString uartError = "UART is unavailable";
    QString gpioOutputError = "GPIO17 output is unavailable";
    QString gpioInputError = "GPIO22 input is unavailable";
#endif
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
    auto *listWidget = new CatalogItemsList(catalogPanel);
    listWidget->addItems({"Item 1: Initialize System",
                          "Item 2: Calibrate Sensors",
                          "Item 3: Run Diagnostic",
                          "Item 4: Update Firmware",
                          "Item 5: Shut Down"});
    listWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    listWidget->setDragEnabled(true);
    listWidget->setDragDropMode(QAbstractItemView::DragOnly);
    listWidget->setDefaultDropAction(Qt::CopyAction);
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
    auto *uartButton = new QPushButton("Send UART", &window);
    auto *gpioButton = new QPushButton("Toggle GPIO17", &window);
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

    auto *statusLine = new QLabel("Opening UART and GPIO hardware...", &window);
    statusLine->setMinimumHeight(28);
    statusLine->setWordWrap(true);
    statusLine->setStyleSheet("QLabel { background: #e8edf0; padding: 5px; }");
    timeline->setSelectionHandler([statusLine](const QString &details) {
        statusLine->setText(details);
    });
    HardwareController hardware(timeline, [statusLine](const QString &message) {
        statusLine->setText(message);
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
    QObject::connect(uartButton, &QPushButton::clicked, [&hardware, statusLine]() {
        QString error;
        if (!hardware.sendDummyTelegram(&error)) {
            statusLine->setText(error);
        }
    });
    QObject::connect(gpioButton, &QPushButton::clicked, [&hardware, statusLine]() {
        QString error;
        if (!hardware.toggleGpio17(&error)) {
            statusLine->setText(error);
        }
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
