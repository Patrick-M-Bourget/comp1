#include "TimingResponseWorker.h"

namespace comp1
{

TimingResponseWorker::TimingResponseWorker() : thread ([this] { run(); }) {}

TimingResponseWorker::~TimingResponseWorker()
{
    {
        const std::lock_guard lock (handOff);
        stopping = true;
    }
    asked.notify_one();
    thread.join();
}

bool TimingResponseWorker::ask (const Request& request)
{
    std::unique_lock lock (handOff, std::try_to_lock);
    if (! lock.owns_lock())
        return false;
    waiting = request;
    lock.unlock();
    asked.notify_one();
    return true;
}

std::optional<TimingResponseWorker::Result> TimingResponseWorker::take()
{
    std::unique_lock lock (handOff, std::try_to_lock);
    if (! lock.owns_lock() || ! finished)
        return std::nullopt;
    return std::exchange (finished, std::nullopt);
}

void TimingResponseWorker::run()
{
    std::unique_lock lock (handOff);
    for (;;)
    {
        asked.wait (lock, [this] { return stopping || waiting.has_value(); });
        if (stopping)
            return;
        const auto request = *std::exchange (waiting, std::nullopt);
        lock.unlock();
        Result result { .response = timingResponse (request.settings, drawnSampleRate), .request = request, .workedOn = std::this_thread::get_id() };
        lock.lock();
        finished = std::move (result);
    }
}

} // namespace comp1
