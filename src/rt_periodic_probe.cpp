#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

#include <sched.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

namespace {

constexpr std::size_t kStackPrefaultBytes = 512 * 1024;
std::atomic<bool> g_running{true};

struct Options {
    std::int64_t period_us = 10000;
    double duration_s = 10.0;
    int cpu = -1;
    int priority = 0;
    std::int64_t deadline_us = 1000;
    std::string output_path;
    bool no_mlock = false;
};

struct Sample {
    std::uint64_t seq = 0;
    std::int64_t expected_us = 0;
    std::int64_t actual_us = 0;
    std::int64_t latency_us = 0;
    std::int64_t actual_period_us = 0;
    int cpu = -1;
    long minor_faults = 0;
    long major_faults = 0;
    bool deadline_miss = false;
};

void on_signal(int) {
    g_running.store(false);
}

void usage() {
    std::cout
        << "Usage: rt_periodic_probe [options]\n"
        << "  --period-us N       Period in microseconds (default: 10000)\n"
        << "  --duration S        Run duration in seconds (default: 10)\n"
        << "  --cpu N             Pin process to CPU N\n"
        << "  --priority N        Set SCHED_FIFO priority N (requires privilege)\n"
        << "  --deadline-us N     Deadline miss threshold (default: 1000)\n"
        << "  --output PATH       Write CSV to PATH instead of stdout\n"
        << "  --no-mlock          Skip mlockall(MCL_CURRENT | MCL_FUTURE)\n"
        << "  --help              Show this help\n";
}

bool parse_int64(const std::string& value, std::int64_t& out) {
    try {
        std::size_t pos = 0;
        const long long parsed = std::stoll(value, &pos, 10);
        if (pos != value.size()) {
            return false;
        }
        out = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_double(const std::string& value, double& out) {
    try {
        std::size_t pos = 0;
        const double parsed = std::stod(value, &pos);
        if (pos != value.size()) {
            return false;
        }
        out = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_options(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") {
            usage();
            std::exit(0);
        } else if (arg == "--period-us" && i + 1 < argc) {
            if (!parse_int64(argv[++i], opts.period_us)) {
                return false;
            }
        } else if (arg == "--duration" && i + 1 < argc) {
            if (!parse_double(argv[++i], opts.duration_s)) {
                return false;
            }
        } else if (arg == "--cpu" && i + 1 < argc) {
            std::int64_t parsed = 0;
            if (!parse_int64(argv[++i], parsed)) {
                return false;
            }
            opts.cpu = static_cast<int>(parsed);
        } else if (arg == "--priority" && i + 1 < argc) {
            std::int64_t parsed = 0;
            if (!parse_int64(argv[++i], parsed)) {
                return false;
            }
            opts.priority = static_cast<int>(parsed);
        } else if (arg == "--deadline-us" && i + 1 < argc) {
            if (!parse_int64(argv[++i], opts.deadline_us)) {
                return false;
            }
        } else if (arg == "--output" && i + 1 < argc) {
            opts.output_path = argv[++i];
        } else if (arg == "--no-mlock") {
            opts.no_mlock = true;
        } else {
            std::cerr << "Unknown or incomplete option: " << arg << "\n";
            return false;
        }
    }

    return opts.period_us > 0 && opts.duration_s > 0.0 && opts.deadline_us >= 0;
}

std::int64_t timespec_to_us(const timespec& ts) {
    return static_cast<std::int64_t>(ts.tv_sec) * 1000000LL + ts.tv_nsec / 1000LL;
}

timespec us_to_timespec(std::int64_t us) {
    timespec ts{};
    ts.tv_sec = us / 1000000LL;
    ts.tv_nsec = (us % 1000000LL) * 1000LL;
    return ts;
}

std::int64_t monotonic_us() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return timespec_to_us(ts);
}

void prefault_stack() {
    volatile char stack[kStackPrefaultBytes];
    for (std::size_t i = 0; i < sizeof(stack); i += static_cast<std::size_t>(sysconf(_SC_PAGESIZE))) {
        stack[i] = 0;
    }
}

void configure_runtime(const Options& opts) {
    if (!opts.no_mlock) {
        if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
            std::cerr << "warning: mlockall failed: " << std::strerror(errno) << "\n";
        }
    }

    prefault_stack();

    if (opts.cpu >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(opts.cpu, &set);
        if (sched_setaffinity(0, sizeof(set), &set) != 0) {
            std::cerr << "warning: sched_setaffinity(cpu=" << opts.cpu
                      << ") failed: " << std::strerror(errno) << "\n";
        }
    }

    if (opts.priority > 0) {
        sched_param param{};
        param.sched_priority = opts.priority;
        if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
            std::cerr << "warning: sched_setscheduler(SCHED_FIFO,"
                      << opts.priority << ") failed: " << std::strerror(errno) << "\n";
        }
    }
}

void write_header(std::ostream& os) {
    os << "seq,expected_us,actual_us,latency_us,actual_period_us,cpu,"
       << "minor_faults,major_faults,deadline_miss\n";
}

void write_sample(std::ostream& os, const Sample& sample) {
    os << sample.seq << ','
       << sample.expected_us << ','
       << sample.actual_us << ','
       << sample.latency_us << ','
       << sample.actual_period_us << ','
       << sample.cpu << ','
       << sample.minor_faults << ','
       << sample.major_faults << ','
       << (sample.deadline_miss ? 1 : 0) << '\n';
}

double percentile(std::vector<std::int64_t> values, double p) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double rank = (p / 100.0) * static_cast<double>(values.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(rank));
    const auto hi = static_cast<std::size_t>(std::ceil(rank));
    if (lo == hi) {
        return static_cast<double>(values[lo]);
    }
    const double w = rank - static_cast<double>(lo);
    return static_cast<double>(values[lo]) * (1.0 - w) + static_cast<double>(values[hi]) * w;
}

void print_summary(const std::vector<Sample>& samples) {
    std::vector<std::int64_t> latencies;
    latencies.reserve(samples.size());
    std::uint64_t misses = 0;
    std::int64_t max_latency = std::numeric_limits<std::int64_t>::min();
    std::int64_t max_period = 0;

    for (const auto& sample : samples) {
        latencies.push_back(sample.latency_us);
        max_latency = std::max(max_latency, sample.latency_us);
        max_period = std::max(max_period, sample.actual_period_us);
        if (sample.deadline_miss) {
            ++misses;
        }
    }

    const double avg = latencies.empty()
                           ? 0.0
                           : static_cast<double>(std::accumulate(latencies.begin(), latencies.end(), 0LL)) /
                                 static_cast<double>(latencies.size());

    std::cerr << "summary:"
              << " samples=" << samples.size()
              << " deadline_misses=" << misses
              << " latency_avg_us=" << avg
              << " latency_p50_us=" << percentile(latencies, 50.0)
              << " latency_p99_us=" << percentile(latencies, 99.0)
              << " latency_max_us=" << (samples.empty() ? 0 : max_latency)
              << " actual_period_max_us=" << max_period
              << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    Options opts{};
    if (!parse_options(argc, argv, opts)) {
        usage();
        return 2;
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    std::ofstream file;
    std::ostream* out = &std::cout;
    if (!opts.output_path.empty()) {
        file.open(opts.output_path);
        if (!file) {
            std::cerr << "failed to open output: " << opts.output_path << "\n";
            return 1;
        }
        out = &file;
    }

    configure_runtime(opts);
    write_header(*out);

    std::vector<Sample> samples;
    samples.reserve(static_cast<std::size_t>((opts.duration_s * 1000000.0) /
                                             static_cast<double>(opts.period_us)) + 2);

    const std::int64_t start_us = monotonic_us();
    const std::int64_t stop_us = start_us + static_cast<std::int64_t>(opts.duration_s * 1000000.0);
    std::int64_t expected_us = start_us;
    std::int64_t previous_actual_us = start_us;
    std::uint64_t seq = 0;

    while (g_running.load()) {
        expected_us += opts.period_us;
        if (expected_us > stop_us) {
            break;
        }

        timespec next = us_to_timespec(expected_us);
        int rc = 0;
        do {
            rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);
        } while (rc == EINTR && g_running.load());

        if (rc != 0 && rc != EINTR) {
            std::cerr << "clock_nanosleep failed: " << std::strerror(rc) << "\n";
            return 1;
        }

        const std::int64_t actual_us = monotonic_us();
        rusage usage{};
        getrusage(RUSAGE_SELF, &usage);

        Sample sample{};
        sample.seq = seq++;
        sample.expected_us = expected_us;
        sample.actual_us = actual_us;
        sample.latency_us = actual_us - expected_us;
        sample.actual_period_us = actual_us - previous_actual_us;
        sample.cpu = sched_getcpu();
        sample.minor_faults = usage.ru_minflt;
        sample.major_faults = usage.ru_majflt;
        sample.deadline_miss =
            sample.latency_us > opts.deadline_us || sample.actual_period_us > opts.period_us + opts.deadline_us;

        previous_actual_us = actual_us;
        samples.push_back(sample);
        write_sample(*out, sample);
    }

    print_summary(samples);
    return 0;
}
