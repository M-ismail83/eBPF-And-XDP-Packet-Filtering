#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-declarations"
#include "vmlinux.h"
#pragma clang diagnostic pop

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define ETH_P_IP 0x0800

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} proto_cnt SEC(".maps");

SEC("xdp")
int my_prog(struct xdp_md* ctx)
{
    struct ethhdr* eth = (struct ethhdr*)(long)ctx->data;

    if ((void*)(eth + 1) > (void*)(long)ctx->data_end)
        return XDP_PASS;

    __u16 eth_type = eth->h_proto;

    if (eth_type == bpf_htons(ETH_P_IP)) {
        struct iphdr* ip = (struct iphdr*)(eth + 1);

        if ((void*)(ip + 1) > (void*)(long)ctx->data_end)
            return XDP_PASS;

        __u8 protocol = ip->protocol;

        if (protocol == IPPROTO_ICMP)
        {
            __u32 key = 0;
            __u64* value = bpf_map_lookup_elem(&proto_cnt, &key);
            if (value) {
                *value += 1;
            }
        }

    }
    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";