#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include <net/if.h>

#include "xdp_count.skel.h"

static volatile sig_atomic_t running = 1;

void handle_signal(int sig)
{
    (void)sig; // Unused parameter
    running = 0;
}

int main(int argc, char** argv)
{
    if(argc != 2) {
        fprintf(stderr, "Usage: %s <interface>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int ifindex = if_nametoindex(argv[1]);

    if(ifindex == 0) {
        fprintf(stderr, "Failed to get interface index for %s: %s\n", argv[1], strerror(errno));
        return EXIT_FAILURE;
    }

    struct xdp_count_bpf* skel = xdp_count_bpf__open_and_load();

    if(!skel) {
        fprintf(stderr, "Failed to open and load BPF skeleton: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    struct bpf_program* program = skel->progs.my_prog;
    struct bpf_map* map = skel->maps.proto_cnt;



    struct bpf_link* link = bpf_program__attach_xdp(program, ifindex);

    if(!link) {
        fprintf(stderr, "Failed to attach BPF program to interface %s: %s\n", argv[1], strerror(errno));
        xdp_count_bpf__destroy(skel);
        return EXIT_FAILURE;
    }

    int ncpus = libbpf_num_possible_cpus();

    if(ncpus < 0) {
        fprintf(stderr, "Failed to get number of possible CPUs: %s\n", strerror(ncpus));
        bpf_link__destroy(link);
        xdp_count_bpf__destroy(skel);
        return EXIT_FAILURE;
    }

    __u32 key = 0;
    __u64* values = calloc(ncpus, sizeof(__u64));

    if(!values) {
        fprintf(stderr, "Failed to allocate memory for values: %s\n", strerror(errno));
        bpf_link__destroy(link);
        xdp_count_bpf__destroy(skel);
        return EXIT_FAILURE;
    }

    signal(SIGINT, handle_signal);
    while(running)
    {
        __u64 total = 0;

        bpf_map__lookup_elem(map, &key, sizeof(key), values, ncpus * sizeof(__u64), 0);

        for(int i = 0; i < ncpus; i++) {
            total += values[i];
        }

        printf("Total count: %llu\n", total);
        sleep(1);
    }

    printf("Detaching BPF program and cleaning up...\n");
    
    free(values);
    bpf_link__destroy(link);
    xdp_count_bpf__destroy(skel);

    printf("Exiting.\n");

}