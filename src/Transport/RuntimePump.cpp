#include "RuntimePump.h"
#include <QtCore/QMetaObject>
#include <algorithm>
#include <chrono>
#include <limits>

namespace iiWebServer::detail {
RuntimePump::RuntimePump(std::size_t capacity) : events_(capacity) {
    deadline_.setSingleShot(true);
    deadline_.setTimerType(Qt::PreciseTimer);
    QObject::connect(&deadline_, &QTimer::timeout, this, [this] { dispatch(); });
    host_ = std::make_unique<ii::runtime::HostLoop>(events_, [this]() noexcept {
        QMetaObject::invokeMethod(this, [this] { dispatch(); }, Qt::QueuedConnection);
    }, ii::runtime::HostLoopOptions{.monitor_io = false});
}
RuntimePump::~RuntimePump() = default;

void RuntimePump::dispatch() {
    // A callback can release the last external owner while poll is running.
    const auto keep = shared_from_this();
    ++dispatches_;
    events_.poll();
    armDeadline();
}
void RuntimePump::armDeadline() {
    const auto next = events_.next_deadline();
    if (!next) { deadline_.stop(); return; }
    const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(*next - ii::runtime::Clock::now()).count();
    const auto delay = std::clamp(remaining, decltype(remaining){0},
                                  static_cast<decltype(remaining)>((std::numeric_limits<int>::max)()));
    deadline_.start(static_cast<int>(delay));
}
}
