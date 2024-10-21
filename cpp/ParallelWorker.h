#pragma once
#include "CommonMacros.h"

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#if defined DEBUG
#define DEFAULT_THREADS 1
#else
#define DEFAULT_THREADS 0
#endif
using time_point = std::chrono::time_point<std::chrono::system_clock>;

template <typename T> struct ParallelWorker
{
    private:
    std::vector<T>      _items;
    size_t              _total_count{0};
    std::atomic<size_t> _completed{0};
    // moodycamel::BlockingConcurrentQueue<M3UItem> _results{1024};
    std::mutex               _mutex;
    unsigned                 max_threads = DEFAULT_THREADS;
    std::vector<std::thread> _threads;
    std::atomic<unsigned>    _active_threads{0};
    bool                     _stopRequested{false};

    public:
    bool debug   = false;
    bool verbose = false;

    ParallelWorker() = default;
    virtual ~ParallelWorker() { WaitForFinish(); }
    CLASS_DELETE_COPY_AND_MOVE(ParallelWorker);

    [[nodiscard]] virtual std::string_view WorkName() const = 0;
    virtual void             Process(T& item)  = 0;

    virtual std::string WorkDesc(T const& item) const { return fmt::format("{}", item); }

    virtual void on_queue_finished() {}
    virtual void OnQueueProgress(size_t /* total */, size_t /* completed */) {}
    [[nodiscard]] bool         Working() const { return _active_threads > 0; }

    auto LockScope() { return std::scoped_lock<std::mutex>(_mutex); }
    void AddItem(T&& item)
    {
        auto lock = std::scoped_lock<std::mutex>(_mutex);
        _items.push_back(item);
    }

    void AddItem(T const& item)
    {
        auto lock = std::scoped_lock<std::mutex>(_mutex);
        _items.push_back(item);
    }

    void Start()
    {

        _total_count = _items.size();
        _completed   = 0;
        if (_total_count == 0) { return;
}
        if (max_threads == 0) { max_threads = std::thread::hardware_concurrency() + 1; }
        if (max_threads == 1)
        {
            _worker();
            return;
        }
        
                    for (size_t i = _active_threads; i < max_threads; i++)
            {
                _active_threads++;
                _threads.push_back(std::thread([this]() {
                    _worker();
                    _active_threads--;
                    if (_active_threads == 0) { on_queue_finished(); }
                }));
            }
       
    }

    void WaitForFinish()
    {
        for (auto& thread : _threads)
        {
            if (thread.joinable()) { thread.join();
}
        }
    }
    void Stop()
    {
        {
            auto lock      = LockScope();
            _stopRequested = true;
        }
        WaitForFinish();
        _items.clear();
        _stopRequested = false;
    }

    void _worker()
    {
        while (!_stopRequested)
        {
            T item;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (_items.empty()) { break; }
                std::swap(item, _items.back());
                _items.pop_back();
            }

            if (verbose) { fmt::print("{}: {} : starting\n", WorkName(), WorkDesc(item));
}
            try
            {
                Process(item);
            } catch (std::exception const& e)
            {
                auto message = fmt::format("{} : Item = {}\n\tError => {}\n", WorkName(), WorkDesc(item), e.what());
                fmt::print(stderr, "{}", message);
                if (debug) { throw;
}
            }
            auto completed = _completed.fetch_add(1, std::memory_order_relaxed);
            if (verbose) { fmt::print("{}: {} : finished\n", WorkName(), WorkDesc(item));
}
            OnQueueProgress(_total_count, completed);
        }
    }
};
