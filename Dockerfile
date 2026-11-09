FROM debian:trixie-slim AS build
WORKDIR /app

# Required packages
RUN apt-get update && apt-get install -y --no-install-recommends \
    clang \
    llvm \
    make \
    gcc \
    bpftool \
    libbpf-dev && \
    rm -rf /var/lib/apt/lists/*

COPY . .
RUN make


FROM debian:trixie-slim AS runtime
WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
    libbpf1 && \
    rm -rf /var/lib/apt/lists/*

COPY --from=build /app/build/xdp_count /app/
ENTRYPOINT ["/app/xdp_count"]
CMD ["eth0"]