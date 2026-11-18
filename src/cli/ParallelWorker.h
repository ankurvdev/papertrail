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
    struct Listener
    {
        Listener()          = default;
        virtual ~Listener() = default;

        CLASS_DELETE_COPY_AND_MOVE(Listener);

        virtual void OnQueueIdle() {}
        virtual void OnQueueWorking() {}
        virtual void OnQueueProgress(size_t /* total */, size_t /* completed */) {}
        virtual void OnQueueItemCompleted(T& /* item  */) {}
    };
    protected:
    
    unsigned                 maxThreads = DEFAULT_THREADS;

    private:
    std::vector<T>      _items;
    size_t              _total_count{0};
    std::atomic<size_t> _completed{0};
    // moodycamel::BlockingConcurrentQueue<M3UItem> _results{1024};
    std::mutex               _mutex;
    std::vector<std::thread> _threads;
    std::atomic<unsigned>    _active_threads{0};
    bool                     _stopRequested{false};
    bool                     _autoStart{false};

    std::mutex _listenerMutex;
    Listener   _defaultListener{};
    Listener*  _listener{&_defaultListener};

    public:
    bool debug   = false;
    bool verbose = false;

    ParallelWorker() = default;
    virtual ~ParallelWorker() { WaitForFinish(); }
    CLASS_DELETE_COPY_AND_MOVE(ParallelWorker);

    [[nodiscard]] virtual std::string_view WorkName() const = 0;
    virtual void                           Process(T& item) = 0;

    virtual std::string WorkDesc(T const& item) const = 0;    //{ return fmt::format("{}", item); }

    [[nodiscard]] bool Working() const { return _active_threads > 0; }

    void AttachListener(Listener& listener)
    {
        auto lock = std::scoped_lock<std::mutex>(_listenerMutex);
        _listener = &listener;
    }
    auto LockScope() { return std::scoped_lock<std::mutex>(_mutex); }
    void AddItem(T&& item)
    {
        auto lock = std::scoped_lock<std::mutex>(_mutex);
        if (_stopRequested) return;
        _items.push_back(item);
        if (_autoStart) { _Start(); }
    }

    void AddItem(T const& item)
    {
        auto lock = std::scoped_lock<std::mutex>(_mutex);
        if (_stopRequested) return;
        _items.push_back(item);
        if (_autoStart) { _Start(); }
    }

    void _Start()
    {
        _total_count = _items.size();
        _completed   = 0;
        if (_total_count == 0) { return; }
        if (maxThreads == 0) { maxThreads = std::min(4u, static_cast<unsigned>(std::thread::hardware_concurrency()) + 1); }
        //if (_max_threads == 1)
        //{
        //    {
        //        std::lock_guard<std::mutex> lock(_listenerMutex);
        //        _listener->OnQueueWorking();
        //    }
        //    Worker_();
        //    return;
        //}
        {
            std::lock_guard<std::mutex> lock(_listenerMutex);
            _listener->OnQueueWorking();
        }
        for (size_t i = _active_threads; i < maxThreads; i++)
        {
            _active_threads++;
            _threads.push_back(std::thread([this]() {
                Worker_();
                _active_threads--;
                if (_active_threads == 0)
                {

                    std::lock_guard<std::mutex> lock(_listenerMutex);
                    _listener->OnQueueIdle();
                }
            }));
        }
    }

    void Start()
    {
        _autoStart = true;
        _Start();
    }

    void WaitForFinish()
    {
        _autoStart = false;
        for (auto& thread : _threads)
        {
            if (thread.joinable()) { thread.join(); }
        }
    }

    void Stop()
    {
        {
            auto lock      = LockScope();
            _stopRequested = true;
            _autoStart     = false;
        }
        WaitForFinish();
        _items.clear();
        _stopRequested = false;
    }

    private:
    void Worker_()
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

            if (verbose) { fmt::print("{}: {} : starting\n", WorkName(), WorkDesc(item)); }
            try
            {
                Process(item);
            } catch (std::exception const& e)
            {
                auto message = fmt::format("{} : Item = {}\n\tError => {}\n", WorkName(), WorkDesc(item), e.what());
                fmt::print(stderr, "{}", message);
                if (debug) { throw; }
            }
            auto completed = _completed.fetch_add(1, std::memory_order_relaxed);
            if (verbose) { fmt::print("{}: {} : finished\n", WorkName(), WorkDesc(item)); }
            {
                std::lock_guard<std::mutex> lock(_listenerMutex);
                _listener->OnQueueProgress(_total_count, completed);
                _listener->OnQueueItemCompleted(item);
            }
        }
    }
};
