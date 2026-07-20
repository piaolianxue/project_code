# H743 I2C 与 CANFD 主机接口设计

日期：2026-07-20

## 目标

为主机新增与从机通讯的 I2C 和 CANFD 接口，同时保持现有主机与上位机屏幕的通讯协议不变。

本次只打通接口和协议封装，不增加自动周期 `PING`、寄存器轮询，也不新增屏幕命令。

## 硬件映射

- I2C 使用 PB8/PB9，主机侧计划使用 I2C1。
- CANFD 使用 PD0/PD1，主机侧计划使用 FDCAN1。
- 现有 RS422/RS485 串口测试模式与 I2C/CANFD 外设相互独立。

## 从机协议

I2C：

- 从机 7 位地址：`0x43`。
- 从机侧 I2C2 和 I2C4 共用同一套寄存器表。
- I2C 写：第 1 字节是寄存器地址，后续字节写入连续寄存器。
- I2C 读：从当前寄存器地址开始连续读。主机读接口会先写入起始寄存器地址，再读取指定长度。

CANFD：

- 命令扩展 ID：`0x18EF4301`。
- 响应扩展 ID：`0x18EF0143`。
- 数据格式：Byte0 是 `sequence`，Byte1 是 `command`，Byte2 及后续字节是命令参数。
- 支持命令：
  - `0x01` PING
  - `0x02` READ_REG
  - `0x03` WRITE_REG
  - `0x04` SET_CAN_MODE
  - `0x05` CLEAR_FLAGS

## 架构

新增 CubeMX 风格外设文件：

- `Core/Inc/i2c.h`
- `Core/Src/i2c.c`
- `Core/Inc/fdcan.h`
- `Core/Src/fdcan.c`

新增一个应用层模块：

- `Core/Inc/slave_comm.h`
- `Core/Src/slave_comm.c`

`slave_comm` 提供小而明确的主机接口，用于 I2C 寄存器读写和 CANFD 命令交互。它负责协议常量、CANFD sequence 递增、响应缓存、计数器和最后状态变量，方便 Keil Watch 观察。

`main.c` 初始化 I2C、FDCAN 和 `SlaveComm`，并在主循环调用 `SlaveComm_Poll()`。`SlaveComm_Poll()` 只接收和记录 CANFD 响应，不主动产生通讯流量。

`host_comm` 保持上位机屏幕协议不变。后续如果需要，可以在不改变本次接口层的前提下，从 `host_comm` 调用 `slave_comm`。

## 错误处理

接口返回 `HAL_StatusTypeDef`。

- I2C 对“长度非 0 但缓冲区为空”的调用返回错误。
- I2C 读接口先发送寄存器地址，再读取指定长度。
- CANFD 发送前检查载荷长度。
- CANFD 保存最后一次收到的响应 ID、sequence、command 和载荷长度。
- 计数器记录 I2C 发送、接收、错误，CANFD 发送、接收、错误，以及被忽略的 CANFD 帧。

## 测试与验证

新增静态检查脚本，验证：

- I2C 和 FDCAN 模块文件存在。
- PB8/PB9 和 PD0/PD1 已在 MSP/外设代码中配置。
- FDCAN HAL 已启用，Keil 工程已加入 FDCAN 驱动源。
- `slave_comm` 定义了要求的 I2C 地址、CANFD ID、命令值和公开 API。
- `main.c` 初始化并轮询新模块。

实现完成后，运行新增静态检查、现有相关检查和 Keil 构建。

## 非目标

- 不修改屏幕协议。
- 不增加自动周期 `PING` 或寄存器轮询。
- 不在主机侧解释寄存器表含义，只提供按字节读写接口。
- 不修改 RS422/RS485 模式选择。
