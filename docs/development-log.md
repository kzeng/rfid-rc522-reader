# 开发记录

## 目标

使用 STM32F103C8T6 的 SPI1 驱动 RC522，并通过 Blue Pill 的 USB 口提供 CDC 虚拟串口命令行，逐步学习识卡和受控读写。参考 USB CDC 与 shell 结构：`/home/zengkai/Codes/stm32f103-demo`。

## 开发步骤

1. 检查仓库：最初只有 `AGENTS.md`，没有固件、构建配置或测试。
2. 确认调试器：OpenOCD 识别到 ST-Link V2，目标电压约 3.27 V，STM32F1 SWD 可访问。
3. 建立 Blue Pill / STM32Cube 固件工程，先实现 SPI1 和 RC522 版本寄存器、UID/SAK 读取。
4. 按参考工程接入 USB CDC 与简单命令行，用中断接收、主循环解析命令。
5. 加入 MIFARE Classic 1K/4K 的 UID/SAK 扫描、默认 Key A 认证，以及普通数据块读、写、清零和块间复制命令。
6. 用 PB1 驱动有源蜂鸣器模块，以不同节奏提示识卡、操作成功和失败。
7. 编译并通过 ST-Link 刷写/校验。连接板载 USB 口验证枚举和命令交互，再逐步验证测试卡操作。

## 当前接线假设

使用先前约定的 SPI1 接线：RC522 `SDA/SS→PA4`、`SCK→PA5`、`MISO→PA6`、`MOSI→PA7`、`RST→PB0`、`3.3V→3.3V`、`GND→GND`；`IRQ` 不接。RC522 供电和信号均使用 3.3 V。USB CDC 使用 Blue Pill 的 USB D-/D+（PA11/PA12）连接电脑，终端设置 115200 8N1（USB CDC 的波特率仅为终端兼容设置）。

三针无源蜂鸣器模块的 `S/SIG` 接 PB1（TIM3_CH4）、`-` 接 GND、`+` 按模块额定电压供电；模块输入需兼容 3.3 V 逻辑。TIM3 输出约 2 kHz、50% 占空比。两针裸蜂鸣器可能需要三极管驱动。

## 问题与处理

- PlatformIO 默认缓存位于只读的 `/home/zengkai/.platformio`，运行时无法创建平台锁文件；将 `PLATFORMIO_CORE_DIR` 设为 `/tmp/rfid-pio`，并在该目录链接本机已安装的平台与工具包，构建成功。
- Blue Pill 的 USB D+ 常见固定上拉电阻，ST-Link 下载后可能无法重新枚举。参考工程在 USB 初始化前短暂拉低 PA12，当前 USB CDC 初始化沿用这一断开/重连处理。
- PlatformIO 的 ST-Link 上传目标报 `open failed`；改用系统 OpenOCD 的 `interface/stlink-v2.cfg` 和 `target/stm32f1x.cfg`，固件成功写入并通过 Verify。
- 首次编译发现 `Error_Handler` 的静态定义与 USB 文件的外部声明冲突；改成全局定义后链接通过。4K 卡块数上限使用 16 位整数，避免 256 被截断到 0。
- 检查 RC522 帧后发现 WUPA 必须发送 7 位短帧，且触发发送时要保留 `TxLastBits`；同时在选择卡片、CRC 和认证前清除旧中断标志，避免复用上一次命令的状态。
- 实测发现连续执行 `scan` 后 `read` 会误报无卡：扫描命令结束时卡片仍处于 SELECTED 状态。增加 HLTA 收尾，让下一条命令可用 WUPA 重新激活卡片；修复版本已编译、烧录并通过 Verify，待读卡命令复测。
- 增加 PB1/TIM3_CH4 蜂鸣器状态提示：识卡/读卡短鸣，写入/块复制双鸣，清零为双短一长，失败长鸣。按 2 kHz、50% PWM 驱动无源模块。
- 仅凭 UID/SAK 不一定能准确判断卡片容量或可写能力。当前仅接受 MIFARE Classic 1K/4K 的普通数据块操作，并避开 UID 厂商块和扇区尾块。

## 验证记录

- OpenOCD 连接成功：识别 STLINK V2J46S7，目标电压约 3.27 V，STM32F103 目标核心响应正常。
- PlatformIO 构建成功：Flash 使用约 23.2 KB / 64 KB，RAM 使用约 4.0 KB / 20 KB。
- 蜂鸣器 PWM 版本由 OpenOCD 编程，Flash Verify 成功。
- HLTA 修复版本构建成功（Flash 约 24.7 KB，RAM 约 4.1 KB）。用户确认板上有标为 SWDIO/SWCLK 的专用调试引脚（对应 PA13/PA14）；之后 OpenOCD 重新连接成功，固件写入并通过 Verify。
- 首次实测 `copy 4 5` 失败。原因是两个块在同一扇区，扇区认证已覆盖目标块；原实现却先关闭 Crypto1 再重新认证，导致写目标块失败。现改为同扇区内保留认证状态直接写入，并明确拒绝跨扇区复制。修复版构建成功（Flash 24.7 KB，RAM 4.1 KB），已由 ST-Link 写入并通过 OpenOCD Verify；待用户实卡复测。
- 用户已连接 USB 线；主机识别到 `0483:5740` 虚拟串口，USB 树显示接口由 `cdc_acm` 驱动接管，`/sys/class/tty/ttyACM0` 已出现。当前执行环境未暴露可打开的 `/dev/ttyACM0` 节点，因此命令交互及刷卡读写需由用户终端确认。
- 针对老门禁扣扫描失败，扩展 ISO14443A UID 级联选择流程，支持 4/7/10 字节 UID；扫描显示完整 UID 长度、最终 SAK 和类型提示。MIFARE Classic 读写仍限制为已支持的 1K/4K 及默认 Key A。构建通过（Flash 25.0 KB，RAM 4.1 KB），已由 ST-Link 写入并通过 OpenOCD Verify；等待配套卡和老门禁扣实测。
- 实测发现 UID 扩展版连原有白卡也无法扫描：SELECT 阶段把包含 SAK+CRC_A 的 3 字节响应误判成无效。现按 3 字节长度接收并校验 SAK CRC，失败提示也改为不武断归因频率。修复版构建通过（Flash 25.1 KB，RAM 4.1 KB），再次由 ST-Link 写入并通过 OpenOCD Verify；需要用户用白卡先复测。
- 为尝试改善老扣读取，RC522 `RFCfgReg` 接收增益调至最高，并对 WUPA/REQA 进行三轮尝试；扫描失败消息增加 ATQA、级联、防冲突、BCC、SELECT/SAK CRC 阶段信息。构建通过（Flash 25.6 KB，RAM 4.1 KB），已刷入并 Verify。接收增益仅可能改善弱耦合接收，不会提高天线发射功率或扩展到其他频率/协议。
- 白卡在 UID 扩展后已恢复识别（4 字节 UID `6C C3 F8 03`，SAK `0x08`）；老门禁扣在增益最高、REQA/WUPA 多次尝试下仍无 ATQA 响应。此结果表示没有收到 ISO14443A 寻卡响应，不能单独判定卡片频率；可能为协议/频率不支持、耦合差或卡片损坏。
- 新增《RC522 与 RFID 卡入门》，整理射频/卡型概念、接线操作、命令说明、固件接口与安全边界；README 增加入口。同步修正 `AGENTS.md` 中与当前源码布局和 PlatformIO 构建不符的旧说明。
- 补充 STM32F103C8T6、RC522、三针无源蜂鸣器的逐脚接线表与示意图，并区分 USB CDC 串口和 ST-Link/SWD 调试连接；在 README 加入简明提示。
