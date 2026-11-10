#include "../src/Transport/RuntimePump.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void runFor(int milliseconds) {
    QEventLoop wait;
    QTimer::singleShot(milliseconds, &wait, &QEventLoop::quit);
    wait.exec();
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        auto pump = std::make_shared<iiWebServer::detail::RuntimePump>(64);
        runFor(30);
        const auto idle = pump->dispatchCount();
        runFor(80);
        check(pump->dispatchCount() == idle, "idle runtime continued to wake");

        QEventLoop completion;
        bool called = false;
        auto sender = pump->events().sender();
        bool admitted = false;
        std::thread producer([&] { admitted = bool(sender.post([&] { called = true; completion.quit(); })); });
        producer.join();
        check(admitted, "post rejected");
        QTimer::singleShot(2000, &completion, &QEventLoop::quit);
        completion.exec();
        check(called, "cross-thread wake was lost");

        QEventLoop deadline;
        bool expired = false;
        check(bool(pump->events().after(30ms, [&] { expired = true; deadline.quit(); })), "timer rejected");
        QTimer::singleShot(2000, &deadline, &QEventLoop::quit);
        deadline.exec();
        check(expired, "one-shot deadline was lost");

        QEventLoop teardown;
        bool destroyed = false;
        auto weak = std::weak_ptr(pump);
        check(bool(sender.post([&] { pump.reset(); destroyed = true; teardown.quit(); })), "teardown post rejected");
        QTimer::singleShot(2000, &teardown, &QEventLoop::quit);
        teardown.exec();
        check(destroyed && weak.expired(), "runtime did not retire after active dispatch");
        check(!sender.post([] {}), "retired runtime accepted work");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
