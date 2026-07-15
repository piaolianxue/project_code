# H743 RS485 Master Test Design

## Background

The H743 digital I/O test firmware currently uses UART7 for the host/screen protocol and uses the existing RS422 driver/protocol stack for U9 and U10. U9 maps to USART1 and U10 maps to USART3. The hardware can switch serial port 1 and serial port 3 between RS422 and RS485 externally. Software should keep one shared low-level serial driver and select the test behavior at the application layer.

The reference implementation is the F103 project at:

`D:\YC\Code\AA31A\STM32F103T8\AA31A_HG_F103`

The H743 RS485 test must interoperate with the F103 `rs485_speed_test` slave protocol. The F103 wire payload uses magic bytes `0x52 0x35`, version `2`, `DATA/ECHO` commands, a 128-byte payload, and the outer `0x0F 0xF0` 135-byte RS422 protocol frame.

## Goals

- Keep the existing host "start test" and "stop test" commands.
- Add a compile-time test mode macro: `0` means current RS422 behavior, `1` means RS485 master speed test behavior.
- Default the macro to `0`, so the firmware keeps RS422 behavior unless the site changes the macro and rebuilds.
- In RS485 mode, H743 acts as master.
- Use serial port 1 and serial port 3 enable switches to choose test ports at start time:
  - only serial port 1 enabled: test U9 / USART1
  - only serial port 3 enabled: test U10 / USART3
  - both enabled: test both ports concurrently
- Each tested H743 port connects to an independent F103-compatible slave/echo device.
- Run RS485 tests until the host sends stop test. Do not auto-stop after 60 seconds.
- Upload live TX bytes, RX bytes, and error rate once per normal host upload cycle while running, and keep uploading the final values after stop.

## Non-Goals

- Do not add a host-side mode selection command in this change.
- Do not infer RS422/RS485 mode from SW4/SW5 or other hardware state.
- Do not replace the existing RS422 protocol frame format.
- Do not make a one-off byte-stream test that is incompatible with the F103 payload protocol.
- Do not change the screen/host command framing beyond what is needed to report correct statistics.

## Host Control Behavior

The existing host protocol stays in use:

- Baud-rate controls remain per port.
- Serial port enable controls remain per port.
- Start test and stop test command IDs remain the existing IDs.

In RS485 mode:

- Serial port 1 uses the "serial port 1 baudrate" control.
- Serial port 3 uses the "serial port 3 baudrate" control.
- If a port has not received a baudrate from the host, its RS485 test baudrate defaults to `1000000`.
- Start test snapshots the serial port 1 and serial port 3 enable states. That snapshot selects the active RS485 instances for the current run.
- Stop test stops all active RS485 instances and leaves final counters available for upload.

## Upload Behavior

U10 results upload through the serial port 3 controls because U10 maps to USART3 in this codebase.

For serial port 1 and serial port 3, uploaded values use physical link byte semantics:

- TX bytes: total bytes successfully accepted for transmission into the selected U9/U10 protocol port.
- RX bytes: total bytes consumed by the RS422 protocol parser for that port.
- Error rate: protocol error bytes multiplied by 1,000,000 and divided by received total bytes.

Serial port 2 and serial port 4 remain zero unless later functionality adds real backing ports.

## Architecture

### RS422 Driver Layer

The existing RS422 driver remains the common low-level transport for both RS422 and RS485 behavior. It should be extended, not forked:

- Add explicit TX byte counters per port.
- Add RX and TX overflow counters per port.
- Add `RS422_ClearTx()`.
- Add dynamic baudrate APIs:
  - `RS422_SetBaudRate(port, baudrate)`
  - `RS422_GetRequestedBaudRate(port)`
  - `RS422_GetActualBaudRate(port)`
- Preserve direction control through the existing TX enable pins. In RS485 master mode, transmit mode is entered only while data is being sent, then the port returns to receive mode to wait for ECHO.

### RS422 Protocol Layer

The protocol layer keeps the existing frame format:

- header: `0x0F 0xF0`
- ID
- data type
- direction
- length
- 128-byte data payload
- checksum

Add a non-blocking send wrapper:

- `RS422_ProtocolSend_IT(port, id, data_type, data)`

This avoids blocking the main loop while two RS485 master instances run concurrently.

### RS485 Speed Test Layer

Add an H743-adapted `rs485_speed_test` module based on the F103 protocol:

- Keep `rs485_speed_test_wire.h` compatible with F103.
- Support one instance per physical port rather than one global singleton.
- Instances are needed for:
  - U9 / USART1 / serial port 1
  - U10 / USART3 / serial port 3
- Each active instance sends F103-compatible `DATA` frames and waits for matching `ECHO` frames.
- Sequence numbers are per instance.
- If an ECHO times out, clear the wait state, increment timeout/error counters, and continue sending future DATA frames until stop.
- The test has no fixed duration in RS485 mode.

### Host Integration

`host_comm.c` owns host commands and upload formatting. It should not own the RS485 wire protocol internals.

Add a compile-time macro near the host test configuration:

```c
#define HOST_COMM_TEST_MODE_RS422 0U
#define HOST_COMM_TEST_MODE_RS485 1U
#define HOST_COMM_TEST_MODE_DEFAULT HOST_COMM_TEST_MODE_RS422
```

When start test is received:

- If mode is RS422: keep current RS422 test behavior.
- If mode is RS485:
  - stop/clear any previous RS485 test state
  - use the current serial port 1 and serial port 3 enable states as the active port mask
  - apply each selected port's configured baudrate, defaulting to `1000000`
  - clear selected-port transport/protocol/stat counters
  - start selected RS485 master instances

When stop test is received:

- If mode is RS422: keep current RS422 stop behavior.
- If mode is RS485: stop all RS485 instances and preserve counters.

During `HostComm_Poll()`:

- Continue polling RS422 protocol receive paths.
- In RS485 mode, run active RS485 master instances.
- Continue the normal periodic upload cadence.

## Error Handling

- Invalid baudrate commands still record a host protocol error.
- If no serial port 1 or serial port 3 enable is active when RS485 start is received, do not start any instance; upload zeros/final counters and leave `test_running` false.
- If a port cannot apply its baudrate, mark that instance stopped/error and continue any other selected instance.
- Parser errors, checksum errors, invalid F103 payloads, timeouts, UART errors, and buffer overflows contribute to diagnostic counters.
- Error rate uploaded to the host is based on protocol error bytes over protocol received bytes.

## Testing

Static checks should cover:

- The test mode macro defaults to RS422.
- F103-compatible RS485 payload constants are present and unchanged.
- U9 and U10 instances are both available to the RS485 speed test module.
- Host start routes to RS422 or RS485 based on the compile-time macro.
- Serial port 1 and serial port 3 baudrates default to `1000000` for RS485 when not set by host.
- TX upload uses a byte counter, not UART transmit-complete interrupt count.
- U10 upload uses serial port 3 control IDs.

Build verification should use Keil after implementation.

Hardware verification should cover:

- Macro `0`: existing RS422 behavior still works.
- Macro `1`, only serial port 1 enabled: H743 talks to one F103 slave on U9.
- Macro `1`, only serial port 3 enabled: H743 talks to one F103 slave on U10.
- Macro `1`, both enabled: both independent F103 slaves run concurrently.
- Stop command freezes the active RS485 test and final values keep uploading.
