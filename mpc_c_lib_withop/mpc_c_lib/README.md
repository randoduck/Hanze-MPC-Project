# MPC C Library — Raspberry Pi Smart Meter Demo

A minimal Multi-Party Computation (MPC) library in C that lets two smart meters
compute the sum of their private energy consumption values without either meter
seeing the other's input. Only the grid operator sees the final aggregate.

Built for Raspberry Pi (ARM Cortex-A7, Raspbian bookworm).
Implements UC-1 (Private Consumption Aggregation) from the Hanze smart meter project.

---

## Architecture

```
  [Meter 0 / Pi 0]         [Meter 1 / Pi 1]
   value = 300 (private)    value = 700 (private)
        |                        |
        |--- exchange shares -----|
        |                        |
   partial sum (random)     partial sum (random)
        |                        |
        +----------+  +----------+
                   |  |
           [Operator / Pi 2]
            AGGREGATE = 1000   <-- only this device sees the result
```

Neither meter ever sends its raw value. Neither meter learns the aggregate.
Only the operator does.

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
│   ├── test_uc1.c        Smart meter program (run on meter Pi 0 or Pi 1)
│   └── test_operator.c   Grid operator program (run on operator Pi 2)
└── Makefile
```

---

## Step 1 — Install dependencies

On each machine (laptop or Raspberry Pi):

```bash
sudo apt update
sudo apt install gcc make
```

No external libraries needed.

---

## Step 2 — Build

```bash
cd mpc_c_lib
make
```

No errors or warnings expected. Binaries go into `build/`.

---

## Step 3 — Run standalone tests (no network needed)

Verify the math is correct before running on hardware:

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

## Step 4 — Simulate on one machine (3 terminals)

Use this when everything runs on your laptop. Use 127.0.0.1 for all IPs.

**Important: start the operator first, then the meters.**

```bash
# Terminal 1 — operator (start this first):
cd mpc_c_lib
./build/test_operator 127.0.0.1 127.0.0.1

# Terminal 2 — meter 0:
cd mpc_c_lib
./build/test_uc1 0 127.0.0.1 127.0.0.1 127.0.0.1 300

# Terminal 3 — meter 1:
cd mpc_c_lib
./build/test_uc1 1 127.0.0.1 127.0.0.1 127.0.0.1 700
```

The meters will print their private value and then "does NOT know the aggregate".
Only the operator terminal prints the aggregate (1000).

---

## Step 5 — Run on three Raspberry Pis

### Find the IP address of each Pi

On each Pi:
```bash
hostname -I
```

Example: Pi 0 = 192.168.1.10, Pi 1 = 192.168.1.11, Pi 2 (operator) = 192.168.1.12

### Copy the library to each Pi

From your laptop:
```bash
scp -r mpc_c_lib pi@192.168.1.10:~
scp -r mpc_c_lib pi@192.168.1.11:~
scp -r mpc_c_lib pi@192.168.1.12:~
```

### Build on each Pi

SSH into each Pi and run:
```bash
cd mpc_c_lib
make
```

### Run the protocol

Open three terminal windows (one SSH session per Pi).
**Start the operator first**, then the meters in any order.

```bash
# Pi 2 — operator (start first):
./build/test_operator 192.168.1.10 192.168.1.11

# Pi 0 — meter 0:
./build/test_uc1 0 192.168.1.10 192.168.1.11 192.168.1.12 300

# Pi 1 — meter 1:
./build/test_uc1 1 192.168.1.10 192.168.1.11 192.168.1.12 700
```

Replace IPs with your actual values.
Replace 300 and 700 with whatever consumption values you want to test.

### Expected output

Meter terminals print:
```
[meter 0] value=300  (private, never leaves this device)
...
[meter 0] ** This device does NOT know the aggregate. **
```

Operator terminal prints:
```
[operator] AGGREGATE CONSUMPTION = 1000
[operator] (Neither meter sent its raw value over the network)
```

---

## Common mistake: wrong IPs

If the meters get stuck at "connecting to other meter..." it means the IPs are wrong.

- Running everything on **one laptop**: use `127.0.0.1` for ALL IPs
- Running on **real Pis**: use each Pi's actual IP from `hostname -I`

Never use Pi IPs (192.168.x.x) when running on your laptop — your laptop
cannot connect to those addresses unless it IS that Pi.

---

## Argument reference

```
./build/test_uc1 <my_id> <meter0_ip> <meter1_ip> <operator_ip> <my_value>

  my_id        : 0 or 1 (which meter this is)
  meter0_ip    : IP address of meter 0
  meter1_ip    : IP address of meter 1
  operator_ip  : IP address of the grid operator
  my_value     : this meter's private consumption value (integer)

./build/test_operator <meter0_ip> <meter1_ip>

  meter0_ip    : IP address of meter 0 (informational only)
  meter1_ip    : IP address of meter 1 (informational only)
```

---

## Troubleshooting

**Meters stuck at "connecting to other meter..."**
Wrong IPs. See "Common mistake" above.

**Operator stuck at "waiting for partial sum from meter 0..."**
The meters haven't finished their share exchange yet, or they crashed.
Check the meter terminals for errors.

**"bind: Address already in use"**
A previous run didn't clean up. Wait 30 seconds and retry, or:
```bash
fuser -k 5000/tcp 5001/tcp 5010/tcp 5011/tcp
```

**Build fails on Pi**
```bash
sudo apt install gcc make
```
