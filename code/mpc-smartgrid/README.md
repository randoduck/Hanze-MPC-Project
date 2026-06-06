# MPC C Library — Raspberry Pi Smart Meter Demo

A minimal Multi-Party Computation (MPC) library in C that lets two parties
compute the sum of their private values without either party seeing the other's input.

Built for Raspberry Pi (ARM Cortex-A7, Raspbian bookworm).
Simulates the UC-1 use case from the Hanze smart meter project.

---

## What it does

Two smart meters (Raspberry Pis) each hold a private energy consumption value.
They run a protocol that produces the total combined consumption — without
either meter ever sending its raw value over the network.

---

## File structure

```
mpc_c_lib/
├── include/
│   ├── mpc_field.h       Finite field arithmetic (declarations)
│   ├── mpc_share.h       Secret sharing (declarations)
│   └── mpc_comm.h        Network communication interface
├── src/
│   ├── mpc_field.c       field_add, field_sub, field_mul, field_inv, field_rand
│   ├── mpc_share.c       share_split, share_reconstruct
│   └── mpc_comm_tcp.c    TCP implementation of the comm interface
├── test/
│   ├── test_field.c      Unit tests for field arithmetic (no network needed)
│   ├── test_share.c      Unit tests for secret sharing (no network needed)
│   └── test_uc1.c        Full 2-party protocol over TCP
└── Makefile
```

---

## Step 1 — Install dependencies

On each machine (your laptop or Raspberry Pi):

```bash
sudo apt update
sudo apt install gcc make
```

That's all. No external libraries needed.

---

## Step 2 — Build

```bash
cd mpc_c_lib
make
```

You should see no errors or warnings. Binaries go into `build/`.

---

## Step 3 — Run standalone tests (no network needed)

Run this on one machine to verify the math is correct:

```bash
make test
```

Expected output ends with:
```
ALL FIELD TESTS PASSED
ALL SHARE TESTS PASSED
All standalone tests done.
```

---

## Step 4 — Simulate two parties on one machine

Open two terminal windows. Run one command in each:

```bash
# Terminal 1 (party 0, value = 300):
cd mpc_c_lib
./build/test_uc1 0 127.0.0.1 127.0.0.1 300

# Terminal 2 (party 1, value = 700):
cd mpc_c_lib
./build/test_uc1 1 127.0.0.1 127.0.0.1 700
```

Terminal 1 will wait after printing "Connecting..." — this is normal.
Run Terminal 2 and both will complete.

Expected output on both terminals:
```
[UC-1] AGGREGATE    = 1000
[UC-1] Protocol ms  = <some number>
```

---

## Step 5 — Run on two Raspberry Pis

### Find the IP addresses

On each Pi, run:
```bash
hostname -I
```

Note the IP of each Pi (e.g. Pi 0 = 192.168.1.10, Pi 1 = 192.168.1.11).

### Copy the library to each Pi

From your laptop:
```bash
scp -r mpc_c_lib pi@192.168.1.10:~
scp -r mpc_c_lib pi@192.168.1.11:~
```

### Build on each Pi

SSH into each Pi and run:
```bash
cd mpc_c_lib
make
```

### Run the protocol

SSH into both Pis (two terminal windows). Run simultaneously:

```bash
# Pi 0 (replace IPs with your actual values):
./build/test_uc1 0 192.168.1.10 192.168.1.11 300

# Pi 1:
./build/test_uc1 1 192.168.1.10 192.168.1.11 700
```

Party 0 listens first. Start party 1 within 5 seconds.
Both Pis will print the aggregate. Neither prints the other's value.

---

## Changing the values

You can use any two integers that add up to whatever you want:

```bash
# Pi 0 has 1500 Wh, Pi 1 has 2300 Wh, aggregate should be 3800
./build/test_uc1 0 192.168.1.10 192.168.1.11 1500
./build/test_uc1 1 192.168.1.10 192.168.1.11 2300
```

Maximum value: 2147483646 (= 2^31 - 2). Stays well above any realistic energy reading.

---

## How the protocol works (brief)

1. Each party splits its value into 2 random-looking shares that sum to the original value mod p.
2. Each party sends one share to the other (the share itself reveals nothing).
3. Each party adds their kept share + received share = a partial sum.
4. The two partial sums are exchanged and added together = the aggregate.

The security property: seeing one share is equivalent to seeing a uniformly random
number — it contains zero information about the original value.

---

## Troubleshooting

**"Connection refused" on party 1**
Party 0 is not running yet. Start party 0 first, then party 1 within ~5 seconds.

**"Timeout" during protocol**
The other party crashed or the network dropped. Check both Pis are on the same network.

**Port already in use**
Another process is using port 5000. Either kill it (`fuser -k 5000/tcp`) or
change `MPC_BASE_PORT` in `include/mpc_comm.h` and rebuild.

**Build fails on Pi**
Make sure gcc is installed: `sudo apt install gcc make`
