#pragma once

#include "comp1/Settings.h"
#include "comp1/TimingResponse.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

namespace comp1
{

// Works out the Timing Response (comp1/TimingResponse.h) on a thread of its own, for the Timing Response inset, so the
// message thread never runs the simulation and never waits for it: a drag stays as smooth as any other.
//
// The inset asks for the response at a request and later takes the newest finished one. Newest wins: a request made
// while the thread works replaces any still waiting, and the thread takes the newest when it finishes, so a drag's
// curve follows its latest settings, never a queue of old ones. ask() and take() come from one thread, the message
// thread; neither waits on the worker. When the worker happens to hold the hand-off at that moment, which it does only
// to copy a request in or a result out, they return false at once and the caller tries again on its next tick.
class TimingResponseWorker
{
public:
    // What a response is worked out from: the settings that shape it. It is worked out at drawnSampleRate, whatever the
    // host plays at.
    struct Request
    {
        Settings settings;

        bool operator== (const Request&) const = default;
    };

    // A finished response, the request it answers, and the thread it was worked out on.
    struct Result
    {
        TimingResponse response;
        Request request;
        std::thread::id workedOn;
    };

    TimingResponseWorker();
    // Stops the thread, waiting for the response it is working out, if any, to finish.
    ~TimingResponseWorker();

    // Asks for the response at request; true once the worker has it. Never waits.
    bool ask (const Request& request);
    // The newest finished response, if one finished since the last take(); none otherwise, or when the worker is busy
    // handing over. Never waits.
    std::optional<Result> take();

private:
    void run();

    std::mutex handOff;
    std::condition_variable asked;
    std::optional<Request> waiting;
    std::optional<Result> finished;
    bool stopping = false;
    std::thread thread;
};

} // namespace comp1
