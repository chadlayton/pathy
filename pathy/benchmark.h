#include <chrono>
#include <cassert>
#include <map>
#include <vector>
#include <iostream>
#include <algorithm>
#include <numeric>

#define NOMINMAX
#include <Windows.h>

namespace benchmark
{
class timer 
{
public:
	void start()
	{
		assert(!_is_started);
		_is_started = true;
		_start_time = std::chrono::steady_clock::now();
	}

	double stop()
	{
		assert(_is_started);
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		_is_started = false;
		std::chrono::duration<double, std::milli> elapsed_ms = (now - _start_time);
		return elapsed_ms.count();
	}

private:
	bool _is_started = false;
	std::chrono::steady_clock::time_point _start_time;
};

class benchmark
{
public:
	explicit benchmark(const char* name) : 
		_name(name)
	{
		_timer.start();
	}

	~benchmark()
	{
		const double time_ms = _timer.stop();
		_samples[_name]._elapsed_times.push_back(time_ms);
	}

	static void report(std::ostream& os)
	{
		for (auto& sample : _samples)
		{
			os << sample.first << "(" << sample.second._elapsed_times.size() << ")" <<  std::endl;
			os << "\taverage (ms): " << 
				std::accumulate(
					sample.second._elapsed_times.begin(), 
					sample.second._elapsed_times.end(), 0.0) /
				sample.second._elapsed_times.size() << std::endl;
			os << "\t    min (ms): " << 
				*std::min_element(
					sample.second._elapsed_times.begin(), 
					sample.second._elapsed_times.end()) << std::endl;
			os << "\t    max (ms): " << 
				*std::max_element(
					sample.second._elapsed_times.begin(), 
					sample.second._elapsed_times.end()) << std::endl;
		}
	}

private:
	const char* _name;
	timer _timer;

	struct sample
	{
		std::vector<double> _elapsed_times;
	};

	static std::map<const char*, sample> _samples;
};

std::map<const char*, benchmark::sample> benchmark::_samples;

#pragma optimize("", off)

template <class T>
void escape(T&& datum)
{
	// see here: http://stackoverflow.com/questions/28287064/how-not-to-optimize-away-mechanics-of-a-folly-function
	datum = datum;
}

#pragma optimize("", on)

inline void clobber()
{
	// see here: http://stackoverflow.com/questions/14449141/the-difference-between-asm-asm-volatile-and-clobbering-memory
	_ReadWriteBarrier();
}

}