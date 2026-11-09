CLANG := clang
CC := gcc
BPFTOOL := bpftool

BUILD := build
USER_DIR := user
BPF_CFLAGS := -O2 -g -Wall -target bpf
CFLAGS := -Wall -Wextra
LDLIBS := -lbpf
VMLINUX_BTF := /sys/kernel/btf/vmlinux
VM_FLAGS := btf dump file $(VMLINUX_BTF) format c > bpf/vmlinux.h

.DELETE_ON_ERROR:

.PHONY: all clean vmlinux run build

all: bpf/vmlinux.h $(BUILD)/xdp_count 

$(BUILD)/xdp_count.bpf.o: bpf/xdp_count.bpf.c bpf/vmlinux.h 
	mkdir -p $(BUILD)
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@

$(BUILD)/xdp_count.skel.h: $(BUILD)/xdp_count.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

$(BUILD)/xdp_count: $(USER_DIR)/xdp_count.c $(BUILD)/xdp_count.skel.h
	$(CC) $(CFLAGS) -I $(BUILD) $< -o $@ $(LDLIBS)


clean:
	rm -rf $(BUILD)

vmlinux:
	$(BPFTOOL) $(VM_FLAGS)

build:
	docker build --target runtime -t xdp-loader .

run:
	docker compose up --build