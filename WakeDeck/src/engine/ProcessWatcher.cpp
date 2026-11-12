#include "ProcessWatcher.h"

#include <QSocketNotifier>
#include <QTimer>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <sys/event.h>
#include <sys/types.h>
#include <unistd.h>

namespace wakedeck {

ProcessWatcher::ProcessWatcher(QObject* parent) : QObject(parent) {}

ProcessWatcher::~ProcessWatcher() {
    stop();
}

bool ProcessWatcher::watch(qint64 pid) {
    stop();
    if (pid <= 0 || pid > std::numeric_limits<pid_t>::max()) return false;

    const int queue = ::kqueue();
    if (queue < 0) return false;
    ::fcntl(queue, F_SETFD, FD_CLOEXEC);

    struct kevent change;
    EV_SET(&change, static_cast<uintptr_t>(pid), EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
    if (::kevent(queue, &change, 1, nullptr, 0, nullptr) < 0) {
        const int error = errno;
        ::close(queue);
        if (error != ESRCH) return false;
        // The process is already gone: report it, but on a later turn of the
        // event loop, so the caller never sees the signal from inside watch().
        pid_ = pid;
        const quint64 generation = generation_;
        QTimer::singleShot(0, this, [this, generation] {
            if (generation == generation_) report();
        });
        return true;
    }

    queue_ = queue;
    pid_ = pid;
    notifier_ = new QSocketNotifier(static_cast<qintptr>(queue_), QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, &ProcessWatcher::onReadable);
    return true;
}

void ProcessWatcher::stop() {
    ++generation_;
    if (notifier_ != nullptr) {
        // Disabled before the descriptor closes; deleted later because stop()
        // may run inside the notifier's own signal.
        notifier_->setEnabled(false);
        notifier_->deleteLater();
        notifier_ = nullptr;
    }
    if (queue_ >= 0) {
        ::close(queue_);
        queue_ = -1;
    }
    pid_ = 0;
}

void ProcessWatcher::onReadable() {
    if (queue_ < 0) return;
    struct kevent event;
    const struct timespec noWait = {0, 0};
    const int count = ::kevent(queue_, nullptr, 0, &event, 1, &noWait);
    if (count <= 0) return;  // nothing pending after all, or interrupted: wait for the next wake-up
    if (event.filter != EVFILT_PROC) return;
    if ((event.flags & EV_ERROR) != 0) {
        if (event.data == ESRCH) {
            report();  // gone before the kernel could attach
        } else {
            stop();  // the watch broke; never claim an exit that was not seen
        }
        return;
    }
    if ((event.fflags & NOTE_EXIT) != 0) report();
}

void ProcessWatcher::report() {
    const qint64 pid = pid_;
    stop();
    emit exited(pid);  // last: a handler may delete this watcher
}

}  // namespace wakedeck
