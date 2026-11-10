#pragma once
#include <iiSDKCore/Runtime/HostLoop.h>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace iiWebServer::detail {
// Owner-thread bridge for the existing network loop. Native socket readiness
// is delivered by iiNetwork; no separate Core I/O monitor is needed here.
class RuntimePump final : public QObject, public std::enable_shared_from_this<RuntimePump> {
public:
    explicit RuntimePump(std::size_t capacity = 8192);
    ~RuntimePump() override;
    ii::runtime::EventLoop& events() noexcept { return events_; }
    std::uint64_t dispatchCount() const noexcept { return dispatches_; }
private:
    void dispatch();
    void armDeadline();
    ii::runtime::EventLoop events_;
    QTimer deadline_;
    std::unique_ptr<ii::runtime::HostLoop> host_;
    std::uint64_t dispatches_ = 0;
};
}
