/*
    Jakub Kurzak
    AMD Research
    2020
*/
#include "Report.h"
#include "HostStream.h"
#include "DeviceStream.h"

#include <thread>

#include <unistd.h>
#include <numa.h>

//------------------------------------------------------------------------------
int main(int argc, char** argv)
{
    assert(numa_available() != -1);
    int num_cpus = numa_num_configured_cpus();
    fprintf(stderr, "%3d CPUs\n", num_cpus);

    int numa_nodes = numa_num_configured_nodes();
    fprintf(stderr, "%3d NUMA nodes\n", numa_nodes);

    struct bitmask* bitmask;
    bitmask = numa_get_mems_allowed();
    for (int i = 0; i < numa_num_possible_nodes(); ++i) {
        if (numa_bitmask_isbitset(bitmask, i)) {
            long free_size;
            long node_size = numa_node_size(i, &free_size);
            fprintf(stderr, "\t%2d: %ld, %ld\n", i, node_size, free_size);
        }
    }

    int num_gpus;
    CALL_HIP(hipGetDeviceCount(&num_gpus));
    fprintf(stderr, "%3d GPUs\n", num_gpus);

    assert(argc > 3);
    // size in MB;
    std::size_t array_size = std::atol(argv[1])*1024*1024;
    std::size_t array_length = array_size/sizeof(double);
    // duration in seconds
    double test_duration = std::atof(argv[2]);

    double alpha = 1.0f;
    std::vector<Stream<double>*> streams(argc-3);
    for (int i = 3; i < argc; ++i)
        streams[i-3] = Stream<double>::make(argv[i], array_length,
                                            test_duration, alpha);

    fprintf(stderr, "%3ld streams\n", streams.size());
    for (auto const& stream : streams) {
        stream->printInfo();
        fprintf(stderr, "\n");
    }

    std::vector<std::thread> threads(streams.size());
    for (int i = 0; i < streams.size(); ++i)
        threads[i] = std::thread([&, i] {
            streams[i]->run();
            streams[i]->test();
        });

    for (auto& thread : threads)
        thread.join();

    double min_interval = std::numeric_limits<double>::infinity();
    for (auto const& stream : streams) {
        double interval;
        interval = stream->minInterval();
        interval = std::log10(interval);
        interval = std::floor(interval);
        interval = std::pow(10.0, interval);
        if (interval < min_interval)
            min_interval = interval;
    }
    fprintf(stderr, "%lf min interval\n", min_interval);

    double max_time = 0.0;
    for (auto const& stream : streams) {
        double end_time = stream->maxTime();
        if (end_time > max_time) {
            max_time = end_time;
        }
    }
    fprintf(stderr, "%lf max time\n", max_time);

    // for (auto const& stream : streams)
    //     stream->printStats();

    fflush(stderr);
    usleep(100);

    Report report(max_time, min_interval);
    for (auto const& stream : streams)
        report.addTimeline(*stream);
    report.print();

    return (EXIT_SUCCESS);
}
