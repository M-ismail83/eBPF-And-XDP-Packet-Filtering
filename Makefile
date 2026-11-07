CLANG := clang
CC := gcc
BPFTOOL := bpftool

BUILD := build
USER_DIR := user
BPF_CFLAGS := -O2 -g -Wall -target bpf
CFLAGS := -Wall -Wextra
LDLIBS := -lbpf

.DELETE_ON_ERROR:

.PHONY: all clean

all: $(BUILD)/xdp_count

$(BUILD)/xdp_count.bpf.o: bpf/xdp_count.bpf.c
	mkdir -p $(BUILD)
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@

$(BUILD)/xdp_count.skel.h: $(BUILD)/xdp_count.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

$(BUILD)/xdp_count: $(USER_DIR)/xdp_count.c $(BUILD)/xdp_count.skel.h
	$(CC) $(CFLAGS) -I $(BUILD) $< -o $@ $(LDLIBS)

clean:
	rm -rf $(BUILD)