#pragma once
#include <algorithm>
#include <chrono>
#include <deque>
#include <vector>
namespace example
{
    class FrameMeter
    {
      public:
        using Clock               = std::chrono::steady_clock;
        double   lastMilliseconds = 0;
        unsigned frames           = 0;
        void     Record(Clock::time_point start)
        {
            const auto now   = Clock::now();
            lastMilliseconds = std::chrono::duration<double, std::milli>(now - start).count();
            frames++;
            if (frames > 1)
                durations.push_back(lastMilliseconds);
            if (durations.size() > 60)
                durations.pop_front();
            completed.push_back(now);
            Prune(now);
        }
        double FPS(bool active)
        {
            Prune(Clock::now());
            if (!active)
            {
                completed.clear();
                return 0;
            }
            if (completed.size() < 2)
                return 0;
            const auto span = std::chrono::duration<double>(completed.back() - completed.front()).count();
            return span > 0 ? double(completed.size() - 1) / span : 0;
        }
        double P95() const
        {
            if (durations.empty())
                return 0;
            auto values = std::vector<double>(durations.begin(), durations.end());
            std::sort(values.begin(), values.end());
            return values[(values.size() * 95 + 99) / 100 - 1];
        }

      private:
        std::deque<double>            durations;
        std::deque<Clock::time_point> completed;
        void                          Prune(Clock::time_point now)
        {
            while (completed.size() > 2 && now - completed[1] > std::chrono::seconds(1))
                completed.pop_front();
        }
    };
} // namespace example
