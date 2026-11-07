# Roadmap: XDP + DevSecOps

The XDP packet filter is treated as a real product: built, shipped, secured and monitored. Every phase has two tracks:

- **XDP:** the *what*. The kernel-side filter in Restricted C plus its C/libbpf userspace controller.
- **DevOps:** the *how*. How it gets built, shipped, run and protected.

Each phase adds one piece of each, so neither is learned in isolation.

**Pace:** about 10 hours a week, so roughly 12 weeks. If your time differs, scale the phases but keep the order.

```
                ┌──────────────── Git (rules + manifests as code) ────────────────┐
                ▼                                                                  │
   GitHub Actions: build → verifier load → scapy tests → lint/scan → sign image    │
                │                                                                  │
                ▼                                                                  │
   compose lab / k3s DaemonSet ──▶ [controller (C/libbpf)] ──▶ Prometheus ─▶ Grafana
                                       │ maps ▲ ring buffer
                                       ▼      │
   packets ──────────────────────▶ [XDP program] ──▶ PASS / DROP
```

---

## Ground rules

- **Lab only.** XDP attaches to container/netns veth interfaces, never to your host's real NIC. If you ever test a real NIC, first run a dead-man switch: `sleep 30 && ip link set dev <nic> xdp off &`.
- **Alert before drop.** New rules ship in alert-only mode first, and get promoted to DROP once tests show no false positives.
- **Distro-agnostic.** All builds run inside the Docker build stage, so moving from CachyOS to Nobara changes nothing. Thanks to CO-RE, one committed `vmlinux.h` works across kernels; `make vmlinux` regenerates it from the running kernel when needed.
- **Least privilege from day one.** Only the loader needs `CAP_BPF` + `CAP_NET_ADMIN`. Attacker and victim containers run unprivileged.

---

## Phase 1: Foundation and a reproducible lab (weeks 1–2)

**Outcome:** anyone can run `docker compose up` and get a working XDP lab.

**XDP**
- [ ] C refresher: pointers, structs, `stdint` types, byte order (`ntohs`/`htons`)
- [ ] Packet header basics: Ethernet, IPv4, TCP/UDP layouts and their fields
- [ ] Hello-world XDP: count packets in a `PERCPU_ARRAY` map and return `XDP_PASS`; the loader prints the count every second
- [ ] Build pipeline: `clang -O2 -g -target bpf` → `.bpf.o` → `bpftool gen skeleton` → `.skel.h`

**DevOps**
- [ ] Repo structure:
  ```
  bpf/          kernel-side programs (*.bpf.c) + vmlinux.h
  user/         C/libbpf loader / controller
  include/      structs shared by kernel and user space
  rules/        filter rules as code (Phase 3+)
  tests/        scapy traffic tests, pcaps, BPF_PROG_TEST_RUN tests
  deploy/       compose lab, Grafana/Prometheus config, k8s manifests (Phase 5)
  .github/      CI workflows (Phase 2)
  ```
- [ ] Multi-stage `Dockerfile`:
  - *build stage:* clang, llvm, libbpf-dev, bpftool → compiles the BPF object and the loader
  - *runtime stage:* slim image with only the loader binary, the `.bpf.o` and libbpf/libelf runtime libraries
- [ ] `compose.yaml` lab:
  - `victim`: a simple service (e.g. nginx) on a lab network
  - `attacker`: a tools image (scapy, nmap, hping3, curl)
  - `loader`: shares the victim's network namespace (`network_mode: "service:victim"`), so it attaches XDP to the victim's `eth0`; gets `CAP_BPF`, `CAP_NET_ADMIN` and a `/sys/fs/bpf` mount, and nothing else
- [ ] `Makefile` targets: `build` (images), `run` (`compose up`), `test` (placeholder for now), `clean`, `vmlinux`

**Deliverable:** a reproducible lab that starts with one command, with the packet counter going up when the attacker pings the victim.

---

## Phase 2: Parsing and CI (weeks 3–4)

**Outcome:** every push proves the filter compiles, loads, and drops what it should.

**XDP**
- [ ] `bpf/parse.h`: cursor-based, bounds-checked parsing of Ethernet → IPv4 (with variable IHL) → TCP/UDP/ICMP
- [ ] Drop by source IP, protocol and destination port (still hardcoded in this phase)
- [ ] Debug with `bpf_printk` and `/sys/kernel/tracing/trace_pipe`; learn to read verifier errors
- [ ] Cheap header sanity checks: truncated headers, IHL < 5, TCP data offset < 5, NULL/XMAS/SYN+FIN flag combinations

**DevOps**
- [ ] GitHub Actions pipeline, modeled on your existing security-scan workflow:
  1. **Build:** the Docker build stage compiles the BPF object and the loader
  2. **Verifier check:** create a veth pair on the runner and load + attach the program. Verifier rejections fail the job.
  3. **Traffic tests:** scapy sends crafted packets through the veth; assert what passes and what drops (counter values)
  4. **Unit tests:** `BPF_PROG_TEST_RUN` feeds packets straight to the program with no network needed
  5. **Lint:** `clang-format --dry-run --Werror` and `clang-tidy` on the C code
  6. **Scan:** SonarCloud on the source, Trivy on the loader image
- [ ] Pre-commit hooks: clang-format, trailing whitespace, a secrets scanner (e.g. gitleaks)
- [ ] `make test` runs the same checks locally

**Deliverable:** a pipeline that fails when the filter breaks. Prove it by pushing a commit with a broken bounds check and watching CI catch it.

---

## Phase 3: Maps, control plane and observability (weeks 5–6)

**Outcome:** you can watch attack traffic being dropped on a live dashboard.

**XDP**
- [ ] Move the rules into maps:
  - `ip_rules`: `LPM_TRIE` for CIDR rules
  - `port_rules`: `HASH` keyed by `{proto, port}`
  - `stats`: `PERCPU_ARRAY` / `PERCPU_HASH` counters (seen, passed, dropped, per rule)
- [ ] Rule value `{action, rule_id}` with actions ALERT / DROP / PASS
- [ ] Pin maps under `/sys/fs/bpf/xdpfilter/`
- [ ] The C/libbpf controller: one language across kernel and user space

**DevOps**
- [ ] The controller serves **Prometheus metrics** on `/metrics` (small HTTP endpoint in C, e.g. libmicrohttpd or a minimal hand-written server): `xdp_packets_total{action}`, `xdp_rule_hits_total{rule_id}`
- [ ] Add Prometheus + **Grafana** to the compose lab, with the dashboard provisioned from a JSON file in `deploy/` (dashboards as code)
- [ ] Structured **JSON logs** from the controller (one event per line)
- [ ] Rules loaded from a **config file** (`rules/*.yaml` or INI) and reloaded on change or SIGHUP. Nothing hardcoded.

**Deliverable:** a Grafana dashboard fed by real kernel counters. Run an `nmap`/`hping3` attack from the attacker container and watch the drop graphs move.

---

## Phase 4: Payload inspection and a two-stage pipeline (weeks 7–8)

**Outcome:** a rule change goes from pull request to tested to deployed with no manual steps.

**XDP**
- [ ] Fixed-offset signatures against plaintext traffic. The victim's nginx on port 80 is the target: HTTP method/path patterns such as `../`, `/etc/passwd`, `/bin/sh`, matched with bounded loops over the first N payload bytes
- [ ] Signatures stored in a map, so they're updated from the rule files
- [ ] **Two-stage pipeline:** XDP flags suspicious packets and sends metadata + the first ~128 payload bytes to user space through a `RINGBUF`
- [ ] User-space analyzer in the controller: multi-pattern matching (Aho-Corasick) over the snippets, emits alerts as JSON logs and metrics

**DevOps**
- [ ] **Rules as code:** rules live in `rules/` in Git and change only through PRs with review
- [ ] Rule tests against pcaps: `tests/pcaps/good/` must produce zero alerts, `tests/pcaps/bad/` must produce the expected alerts. CI replays them through the filter.
- [ ] Rule deployment by the pipeline: on merge, CI builds and publishes a new image or rule bundle; the lab picks it up automatically (e.g. the controller pulls rules, or compose redeploys)
- [ ] **Alerting** on the metrics: Prometheus alert rules (e.g. drop rate spike, signature hit) routed through Alertmanager

**Deliverable:** a tested rule-deployment flow. Open a PR adding a signature, watch CI test it against the pcaps, merge it, and see it live in the lab.

---

## Phase 5: Kubernetes and GitOps (weeks 9–10)

**Outcome:** pushing to Git updates the cluster by itself.

**DevOps**
- [ ] A throwaway **k3s/k3d** cluster
- [ ] Deploy the filter as a **DaemonSet** (one loader per node), manifests in a Git repo such as your `k3s-gitops-test`
- [ ] **Argo CD or Flux** so Git is the source of truth for the deployment
- [ ] **Helm or Kustomize** for configuration (interfaces, rule bundle, mode, metrics port)
- [ ] A Prometheus `ServiceMonitor` (or scrape annotations) for the DaemonSet's metrics

**Security**
- [ ] The DaemonSet needs elevated rights (`CAP_BPF`, `CAP_NET_ADMIN`, host networking, the bpffs mount). Use it to practice least privilege:
  - `capabilities: drop: ["ALL"]`, then add back only what's required
  - `readOnlyRootFilesystem: true`
  - non-root where possible; no `privileged: true`
  - a seccomp profile (`RuntimeDefault` at minimum)
- [ ] `docs/privileges.md`: why each privilege is needed

**Caution:** use a separate lab cluster, not one you depend on. Attaching XDP to node interfaces can conflict with the cluster's networking plugin (flannel, Cilium). Start with a dedicated interface or the generic XDP mode, and expect to break the cluster at least once.

**Deliverable:** a GitOps-managed deployment of the filter. Change a rule or a Helm value in Git and the cluster follows on its own.

---

## Phase 6: DevSecOps hardening (week 11)

**Outcome:** the filter's whole delivery chain is signed, scanned and threat-modeled.

**Supply chain**
- [ ] Generate an **SBOM** with Syft for every image in CI
- [ ] Sign images with **Cosign** (keyless with GitHub OIDC, or a key pair); attach the SBOM as an attestation
- [ ] Pin base images by **digest**, not by tag
- [ ] Enforce signed images only with a **Kyverno** admission policy (`verifyImages`) in the cluster

**Threat model**
- [ ] `docs/threat-model.md` covering your own tool's risks (STRIDE is a good frame), for example:
  - Someone tampers with the rules (a malicious PR, a compromised rule bundle) → blind spots or self-DoS
  - The loader is compromised → it holds `CAP_BPF`/`CAP_NET_ADMIN`, so it can load arbitrary BPF or rewrite maps
  - Map tampering through the pinned bpffs paths
  - Alert floods hiding real attacks; a ring buffer fills up and drops events
- [ ] Mitigations for each, linked to what's already in place (review, signing, least privilege, rate limits)

**Runtime security**
- [ ] Try **Falco** or **Tetragon** (both eBPF-based) to see runtime security from the other side. For example, detect a shell started in the loader container or unexpected `bpf()` syscalls.

**Deliverable:** a short threat model and a hardened pipeline.

---

## Phase 7: Measurement and write-up (week 12)

**Outcome:** a portfolio piece you can show in interviews.

- [ ] **Benchmarks** in a VM or on a dedicated interface (`pktgen`/`trafgen`):
  - no filter vs. XDP filter (generic and native mode)
  - XDP vs. `iptables`/`nftables` doing the same drops
  - cost of each feature: parsing only → + maps → + payload inspection
- [ ] **Honest limits** in the docs: no TCP stream reassembly, no inspection of encrypted payloads, ingress only, bounded payload window
- [ ] Polish the **README**: what it is, quick start (`docker compose up`), an architecture diagram, benchmark table
- [ ] Record a short **demo**: attack → dashboard → rule PR → deployed → attack dropped

**Deliverable:** a finished, documented project.

---

## After week 12: extensions

From the earlier IDS plan; pick these up once the core product is done.

- **Behavioral (traffic-pattern) detection:** per-source state in `LRU_HASH` maps with time windows for SYN floods, port scans, host sweeps and brute force; a blocklist with expiry
- **TC egress program** sharing maps with XDP, for two-way flow stats (SYN vs. SYN-ACK ratio, amplification)
- **Anomaly daemon:** EWMA baselines, z-scores and beaconing detection in the controller, writing timed blocks back to the blocklist map
- **IPv6 and VLAN** parsing; fragment evasion checks
- **AF_XDP** for full-packet deep inspection in user space
- A small subset of Suricata/Snort rule import

---

## Skills map

| Phase | XDP / kernel | DevSecOps |
|---|---|---|
| 1 | BTF, CO-RE, skeletons, first map | Multi-stage Docker, compose, capabilities, Makefile |
| 2 | Parsing, bounds checks, verifier | GitHub Actions, scapy tests, linting, SonarCloud, Trivy, pre-commit |
| 3 | Hash / LPM / per-CPU maps, pinning | Prometheus, Grafana, JSON logs, config as code |
| 4 | Payload access, ring buffer | Rules as code, pcap testing, CD, alerting |
| 5 | Running XDP on cluster nodes | k3s, DaemonSets, GitOps (Argo CD / Flux), Helm/Kustomize, security contexts |
| 6 | Threats to BPF tooling | SBOM, Cosign, digest pinning, Kyverno, threat modeling, Falco/Tetragon |
| 7 | Performance, XDP limits | Benchmarking, documentation, presentation |

**First step:** Phase 1: repo structure, the Dockerfile and the compose lab.
