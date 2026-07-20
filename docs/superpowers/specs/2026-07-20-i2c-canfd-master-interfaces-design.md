# H743 I2C and CANFD Master Interfaces Design

Date: 2026-07-20

## Goal

Add master-side communication interfaces for a slave device over I2C and CANFD while keeping the existing host/screen protocol unchanged.

This change only brings up the interfaces and protocol helpers. It does not add automatic periodic PING, register polling, or new screen commands.

## Hardware Mapping

- I2C uses PB8/PB9 as the master bus pins. The intended peripheral is I2C1.
- CANFD uses PD0/PD1 as the CANFD bus pins. The intended peripheral is FDCAN1.
- Existing RS422/RS485 UART test modes remain independent of these peripherals.

## Slave Protocol

I2C:

- Slave 7-bit address: `0x43`.
- I2C2 and I2C4 on the slave share one register table.
- Write format: first byte is register address, following bytes write consecutive registers.
- Read format: read consecutive bytes starting from the current register address. The master helper will set the start register before reading.

CANFD:

- Command extended ID: `0x18EF4301`.
- Response extended ID: `0x18EF0143`.
- Data format: byte 0 is sequence, byte 1 is command, byte 2 and later are command parameters.
- Supported commands:
  - `0x01` PING
  - `0x02` READ_REG
  - `0x03` WRITE_REG
  - `0x04` SET_CAN_MODE
  - `0x05` CLEAR_FLAGS

## Architecture

Add CubeMX-style peripheral files:

- `Core/Inc/i2c.h`
- `Core/Src/i2c.c`
- `Core/Inc/fdcan.h`
- `Core/Src/fdcan.c`

Add one application module:

- `Core/Inc/slave_comm.h`
- `Core/Src/slave_comm.c`

`slave_comm` exposes small master APIs for I2C register access and CANFD command exchange. It owns protocol constants, sequence handling, response capture, counters, and last-status variables for Keil Watch.

`main.c` initializes I2C, FDCAN, and `SlaveComm`, then calls `SlaveComm_Poll()` in the main loop. `SlaveComm_Poll()` only drains CANFD responses and updates status. It does not generate traffic on its own.

`host_comm` remains unchanged for the screen protocol. Future work can call `slave_comm` from `host_comm` without changing this interface bring-up.

## Error Handling

The APIs return `HAL_StatusTypeDef`.

- I2C rejects null buffers with non-zero lengths.
- I2C read helpers first transmit the register address, then receive the requested bytes.
- CANFD validates payload length before send.
- CANFD stores the last received response ID, sequence, command, and payload length.
- Counters track I2C TX/RX/error, CAN TX/RX/error, and ignored CAN frames.

## Tests and Verification

Add a static check script that verifies:

- I2C and FDCAN module files exist.
- PB8/PB9 and PD0/PD1 are configured in MSP/peripheral code.
- FDCAN HAL is enabled and included in the Keil project.
- `slave_comm` defines the required I2C address, CAN IDs, command values, and public APIs.
- `main.c` initializes and polls the new module.

After implementation, run the new static check, existing relevant checks, and Keil build.

## Non-Goals

- No screen protocol changes.
- No automatic periodic PING or register polling.
- No register table interpretation on the master side beyond byte read/write helpers.
- No changes to RS422 or RS485 mode selection.
