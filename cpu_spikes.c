#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>
#include <locale.h>

#include <pci/pci.h>
#include <pci/header.h>
#include <gsl/gsl_statistics_ulong.h>

#include "utils.h"

struct cmd_line
{
    uint64_t iteration_count;
    uint64_t limit_ns;
};

static void
print_usage(const char * name)
{
    fprintf(stderr, "usage: %s [options] LIMIT_NS\n"
                    "where:\n"
                    "\t -h                 Display this usage message\n"
                    "\t -i ITERATION       Define the itration count\n",
                    name);
}

static int
parse_cmd_line(int argc, char * argv[], struct cmd_line * cmd_line)
{
    int err = -1;

    cmd_line->iteration_count = 10 * 1000 * 1000;

    int i;
    while ((i = getopt(argc, argv, "hi:")) != -1)
        switch(i)
        {
            default:
            case 'h':
                print_usage(argv[0]);
                goto arg_check_failed;
            case 'i':
                cmd_line->iteration_count = atoll(optarg);
                break;
        }

    if (! argv[optind])
    {
        print_usage(argv[0]);
        goto arg_check_failed;
    }

    cmd_line->limit_ns = atoll(argv[optind]);

    err = 0;

arg_check_failed:
    return err;
}

#define MAX_SPIKES 1000

struct spike
{
    uint64_t cycles_delta;
    struct timestamp timestamp;
};

static void
print_spikes(struct spike * spikes, size_t spikes_count,
             double cycles_per_ns, struct timestamp * initial_timestamp)
{
    size_t i;
    for (i = 0; i < spikes_count; ++i)
    {
        uint64_t cycles_delta = diff_timestamps(initial_timestamp,
                                                &spikes[i].timestamp);
        fprintf(stdout, "Spike: %'7.0lfns @ %'12.0lfns\n",
                spikes[i].cycles_delta / cycles_per_ns,
                cycles_delta / cycles_per_ns);
    }
}

int
main(int argc, char* argv[])
{
    int err = EXIT_FAILURE;

    struct cmd_line cmd_line;

    setlocale(LC_ALL, "");

    if (parse_cmd_line(argc, argv, &cmd_line) < 0)
        goto arg_check_failed;

    const double cpu_mhz = get_cpu_mhz();
    if (cpu_mhz < 0)
        goto get_cpu_mhz_failed;

    const double cycles_per_ns = cpu_mhz / 1e3;
    const double cycles_limit = cycles_per_ns * cmd_line.limit_ns;

    size_t spikes_count = 0;

    struct spike * spikes = calloc(1000, sizeof(struct spike));
    if (! spikes) {
        fprintf(stderr, "Failed to allocate spikes %s\n",
                strerror(errno));
        goto calloc_failed;
    }

    struct timestamp initial_timestamp;
    read_timestamp_counter(&initial_timestamp);

    struct timestamp t[2];
    read_timestamp_counter(&t[0]);

    size_t i;
    for (i = 1; i < cmd_line.iteration_count; ++ i)
    {
        read_timestamp_counter(&t[i % 2]);
        uint64_t diff = diff_timestamps(&t[(i - 1) % 2], &t[i % 2]);
        if (diff > cycles_limit)
        {
            spikes[spikes_count].cycles_delta = diff;
            memcpy(&spikes[spikes_count].timestamp,
                   &t[i % 2], sizeof(struct timestamp));
            ++ spikes_count;

            if (spikes_count == MAX_SPIKES)
            {
                print_spikes(spikes, spikes_count,
                             cycles_per_ns, &initial_timestamp);
                spikes_count = 0;
            }

            read_timestamp_counter(&t[ i % 2]);
        }
    }

    print_spikes(spikes, spikes_count,
                 cycles_per_ns, &initial_timestamp);

    const double cycles_per_ms = cpu_mhz * 1e3;

    fprintf(stdout, "Iterations count: %'zu\n"
                    "Sampling duration: %'.0lf ms\n"
                    "Detected frequency: %.0lf Mhz\n",
                    cmd_line.iteration_count,
                    cycle_since_timestamp(&initial_timestamp) / cycles_per_ms,
                    cpu_mhz);

    err = EXIT_SUCCESS;

    free(spikes);
calloc_failed:
get_cpu_mhz_failed:
arg_check_failed:
    return err;
}

