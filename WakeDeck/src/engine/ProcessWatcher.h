// ProcessWatcher: tells the Qt event loop when one process exits. The process
// does not have to be a child of WakeDeck: on macOS the kernel's kqueue reports
// the exit of any process the user may see (EVFILT_PROC with NOTE_EXIT), and
// the kqueue's descriptor becomes readable at that moment, so a QSocketNotifier
// wakes the event loop. Nothing polls and nothing blocks.
#pragma once

#include <QObject>

class QSocketNotifier;

namespace wakedeck {

class ProcessWatcher final : public QObject {
    Q_OBJECT
public:
    explicit ProcessWatcher(QObject* parent = nullptr);
    ~ProcessWatcher() override;  // stops watching and closes the kqueue
    ProcessWatcher(const ProcessWatcher&) = delete;
    ProcessWatcher& operator=(const ProcessWatcher&) = delete;

    // Starts watching `pid`, replacing any earlier watch. A process that is
    // already gone is reported as exited too, on a later turn of the event loop,
    // never from inside this call. False if `pid` is not positive or the kernel
    // refused for another reason; then nothing will be reported.
    bool watch(qint64 pid);
    // Stops watching; no exited() follows for the earlier process.
    void stop();

    bool isWatching() const { return pid_ > 0; }
    qint64 pid() const { return pid_; }

signals:
    // Emitted once per watch, on the thread the watcher lives in. The watch has
    // ended by then, so a handler may start another one or delete the watcher.
    void exited(qint64 pid);

private:
    void onReadable();
    void report();

    int queue_ = -1;  // the kqueue descriptor, owned
    QSocketNotifier* notifier_ = nullptr;
    qint64 pid_ = 0;
    quint64 generation_ = 0;  // a report already queued for an earlier watch is dropped
};

}  // namespace wakedeck
