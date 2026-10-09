#include "main.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RC522_CS_PORT GPIOA
#define RC522_CS_PIN GPIO_PIN_4
#define RC522_RST_PORT GPIOB
#define RC522_RST_PIN GPIO_PIN_0
#define BUZZER_PORT GPIOB
#define BUZZER_PIN GPIO_PIN_1

static SPI_HandleTypeDef hspi1;
static TIM_HandleTypeDef htim3;
static void print_line(const char *s);
static const char *uid_scan_error = "unknown scan error";

static void buzzer_beep(uint16_t duration_ms)
{
    (void)HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);
    HAL_Delay(duration_ms);
    (void)HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_4);
}

static void buzzer_success(void)
{
    buzzer_beep(70);
    HAL_Delay(60);
    buzzer_beep(70);
}

static void buzzer_erase_success(void)
{
    buzzer_beep(70);
    HAL_Delay(50);
    buzzer_beep(70);
    HAL_Delay(50);
    buzzer_beep(180);
}

static void buzzer_error(void)
{
    buzzer_beep(250);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.HSIState = RCC_HSI_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) Error_Handler();
}

static void GPIO_SPI_UART_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = RC522_CS_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RC522_CS_PORT, &gpio);
    HAL_GPIO_WritePin(RC522_CS_PORT, RC522_CS_PIN, GPIO_PIN_SET);
    gpio.Pin = RC522_RST_PIN;
    HAL_GPIO_Init(RC522_RST_PORT, &gpio);
    HAL_GPIO_WritePin(RC522_RST_PORT, RC522_RST_PIN, GPIO_PIN_SET);
    gpio.Pin = BUZZER_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BUZZER_PORT, &gpio);

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 71;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 499;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK) Error_Handler();
    TIM_OC_InitTypeDef pwm = {0};
    pwm.OCMode = TIM_OCMODE_PWM1;
    pwm.Pulse = 250;
    pwm.OCPolarity = TIM_OCPOLARITY_HIGH;
    pwm.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim3, &pwm, TIM_CHANNEL_4) != HAL_OK) Error_Handler();

    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_6;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    if (HAL_SPI_Init(&hspi1) != HAL_OK) Error_Handler();

}

static void rc_write(uint8_t reg, uint8_t value)
{
    uint8_t bytes[2] = {(uint8_t)((reg << 1) & 0x7E), value};
    HAL_GPIO_WritePin(RC522_CS_PORT, RC522_CS_PIN, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, bytes, 2, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(RC522_CS_PORT, RC522_CS_PIN, GPIO_PIN_SET);
}

static uint8_t rc_read(uint8_t reg)
{
    uint8_t address = (uint8_t)(((reg << 1) & 0x7E) | 0x80);
    uint8_t value = 0;
    HAL_GPIO_WritePin(RC522_CS_PORT, RC522_CS_PIN, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &address, 1, HAL_MAX_DELAY);
    HAL_SPI_Receive(&hspi1, &value, 1, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(RC522_CS_PORT, RC522_CS_PIN, GPIO_PIN_SET);
    return value;
}

static void rc_crc_a(uint8_t *data, uint8_t len, uint8_t *crc)
{
    rc_write(0x01, 0x00);
    rc_write(0x05, 0x04); // Clear CRCIRq
    rc_write(0x0A, 0x80);
    for (uint8_t i = 0; i < len; ++i) rc_write(0x09, data[i]);
    rc_write(0x01, 0x03);
    uint32_t started = HAL_GetTick();
    while ((rc_read(0x05) & 0x04) == 0U && (uint32_t)(HAL_GetTick() - started) < 20U) {}
    crc[0] = rc_read(0x22);
    crc[1] = rc_read(0x21);
    rc_write(0x01, 0x00);
}

static void rc_init(void)
{
    HAL_GPIO_WritePin(RC522_RST_PORT, RC522_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(5);
    HAL_GPIO_WritePin(RC522_RST_PORT, RC522_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(50);
    rc_write(0x01, 0x0F); // Soft reset
    HAL_Delay(50);
    rc_write(0x2A, 0x8D); // TModeReg
    rc_write(0x2B, 0x3E); // TPrescalerReg
    rc_write(0x2D, 30);   // TReloadRegL
    rc_write(0x2C, 0);    // TReloadRegH
    rc_write(0x15, 0x40); // TxASKReg
    rc_write(0x11, 0x3D); // ModeReg
    rc_write(0x26, 0x70); // RFCfgReg: maximum receiver gain
    uint8_t antenna = rc_read(0x14);
    if ((antenna & 0x03) != 0x03) rc_write(0x14, antenna | 0x03);
}

static int rc_transceive(const uint8_t *send, uint8_t send_len,
                         uint8_t *recv, uint8_t *recv_len, uint8_t *last_bits)
{
    uint8_t framing = (send_len == 1U && (send[0] == 0x26U || send[0] == 0x52U)) ? 0x07U : 0x00U;
    rc_write(0x0D, framing); // REQA/WUPA are 7-bit short frames
    rc_write(0x04, 0x7F); // Clear IRQ flags
    rc_write(0x0A, 0x80); // Flush FIFO
    for (uint8_t i = 0; i < send_len; ++i) rc_write(0x09, send[i]);
    rc_write(0x01, 0x0C); // Transceive command
    rc_write(0x0D, (uint8_t)(framing | 0x80U)); // StartSend; preserve 7-bit REQA/WUPA framing
    uint32_t timeout = HAL_GetTick() + 30;
    uint8_t irq = 0;
    do {
        irq = rc_read(0x04);
        if ((int32_t)(HAL_GetTick() - timeout) >= 0) return 0;
    } while ((irq & 0x30) == 0);
    rc_write(0x0D, 0x00);
    if (irq & 0x01) return 0;
    uint8_t count = rc_read(0x0A) & 0x7F;
    if (count > *recv_len) return 0;
    *recv_len = count;
    for (uint8_t i = 0; i < count; ++i) recv[i] = rc_read(0x09);
    *last_bits = rc_read(0x0C) & 0x07;
    return 1;
}

static int read_uid(uint8_t *uid, uint8_t *uid_len, uint8_t *sak)
{
    uint8_t answer[10], count = sizeof(answer), last_bits = 0;
    uint8_t activated = 0;
    uid_scan_error = "no ATQA response to REQA/WUPA";
    /* Retry activation and try both wake-up and idle-card requests. */
    for (uint8_t attempt = 0; attempt < 3U && !activated; ++attempt) {
        const uint8_t requests[2] = {0x52, 0x26}; // WUPA, then REQA
        for (uint8_t i = 0; i < 2U && !activated; ++i) {
            count = sizeof(answer);
            if (rc_transceive(&requests[i], 1, answer, &count, &last_bits) && count == 2U) activated = 1;
        }
    }
    if (!activated) return 0;
    static const uint8_t cascade_cmd[3] = {0x93, 0x95, 0x97};
    *uid_len = 0;
    for (uint8_t level = 0; level < 3U; ++level) {
        uint8_t anticoll[2] = {cascade_cmd[level], 0x20};
        count = sizeof(answer);
        uid_scan_error = level == 0U ? "UID anticollision failed at cascade level 1" :
                         level == 1U ? "UID anticollision failed at cascade level 2" :
                                       "UID anticollision failed at cascade level 3";
        if (!rc_transceive(anticoll, sizeof(anticoll), answer, &count, &last_bits) || count != 5U) return 0;
        if ((uint8_t)(answer[0] ^ answer[1] ^ answer[2] ^ answer[3]) != answer[4]) {
            uid_scan_error = "UID BCC checksum mismatch";
            return 0;
        }

        uint8_t uid_part[4];
        uint8_t has_ct = (answer[0] == 0x88U);
        if (has_ct) memcpy(uid_part, &answer[1], 3);
        else memcpy(uid_part, answer, 4);
        uint8_t select[9] = {cascade_cmd[level], 0x70, answer[0], answer[1], answer[2], answer[3], answer[4], 0, 0};
        rc_crc_a(select, 7, &select[7]);
        count = sizeof(answer);
        uid_scan_error = "card SELECT failed";
        if (!rc_transceive(select, sizeof(select), answer, &count, &last_bits) || count != 3U) return 0;
        uint8_t sak_crc[2];
        rc_crc_a(answer, 1, sak_crc);
        if (answer[1] != sak_crc[0] || answer[2] != sak_crc[1]) {
            uid_scan_error = "SAK CRC checksum mismatch";
            return 0;
        }
        *sak = answer[0];
        uint8_t cascade = (uint8_t)(*sak & 0x04U);
        if ((cascade && !has_ct) || (!cascade && has_ct) || (cascade && level == 2U)) {
            uid_scan_error = "inconsistent UID cascade response";
            return 0;
        }
        uint8_t append_len = has_ct ? 3U : 4U;
        if ((uint8_t)(*uid_len + append_len) > 10U) {
            uid_scan_error = "UID length exceeds ISO14443A limit";
            return 0;
        }
        memcpy(&uid[*uid_len], uid_part, append_len);
        *uid_len = (uint8_t)(*uid_len + append_len);
        if (!cascade) return (*uid_len == 4U || *uid_len == 7U || *uid_len == 10U);
    }
    uid_scan_error = "UID cascade did not complete";
    return 0;
}

static int classic_card(uint8_t *uid, uint8_t *uid_len, uint8_t *sak, uint16_t *max_blocks)
{
    if (!read_uid(uid, uid_len, sak)) return 0;
    if (*sak == 0x08U) *max_blocks = 64U;
    else if (*sak == 0x18U) *max_blocks = 256U;
    else return 0;
    return 1;
}

static int block_is_data(uint8_t block, uint16_t max_blocks)
{
    if (block == 0U || block >= max_blocks) return 0;
    if (block < 128U) return (block % 4U) != 3U;
    return ((block - 128U) % 16U) != 15U;
}

static uint16_t block_sector(uint8_t block)
{
    if (block < 128U) return (uint16_t)(block / 4U);
    return (uint16_t)(32U + (block - 128U) / 16U);
}

static void stop_crypto(void)
{
    rc_write(0x08, (uint8_t)(rc_read(0x08) & (uint8_t)~0x08U));
}

static void picc_halt(void)
{
    uint8_t frame[4] = {0x50, 0x00, 0, 0};
    uint8_t response[4], count = sizeof(response), last_bits = 0;
    rc_crc_a(frame, 2, &frame[2]);
    /* A halted PICC intentionally sends no reply, so timeout is expected. */
    (void)rc_transceive(frame, sizeof(frame), response, &count, &last_bits);
}

static int authenticate(uint8_t block, const uint8_t *key, const uint8_t *uid)
{
    uint8_t frame[12] = {0x60, block, key[0], key[1], key[2], key[3], key[4], key[5], uid[0], uid[1], uid[2], uid[3]};
    rc_write(0x01, 0x00);
    rc_write(0x0A, 0x80);
    rc_write(0x04, 0x7F); // Clear stale authentication/transceive IRQ flags
    for (uint8_t i = 0; i < sizeof(frame); ++i) rc_write(0x09, frame[i]);
    rc_write(0x01, 0x0E); // MFAuthent
    uint32_t started = HAL_GetTick();
    while ((rc_read(0x04) & 0x10U) == 0U && (uint32_t)(HAL_GetTick() - started) < 30U) {}
    uint8_t ok = ((rc_read(0x08) & 0x08U) != 0U) && ((rc_read(0x06) & 0x1FU) == 0U);
    rc_write(0x01, 0x00);
    return ok;
}

static int mifare_read_block(uint8_t block, uint8_t *data)
{
    uint8_t frame[4] = {0x30, block, 0, 0};
    uint8_t response[18];
    rc_crc_a(frame, 2, &frame[2]);
    uint8_t count = 18, last_bits = 0;
    if (!rc_transceive(frame, 4, response, &count, &last_bits) || count != 18U) return 0;
    uint8_t crc[2];
    rc_crc_a(response, 16, crc);
    if (response[16] != crc[0] || response[17] != crc[1]) return 0;
    memcpy(data, response, 16);
    return 1;
}

static int mifare_write_block(uint8_t block, const uint8_t *data)
{
    uint8_t frame[4] = {0xA0, block, 0, 0};
    uint8_t response[2], count = sizeof(response), last_bits = 0;
    rc_crc_a(frame, 2, &frame[2]);
    if (!rc_transceive(frame, sizeof(frame), response, &count, &last_bits) ||
        count != 1U || last_bits != 4U || (response[0] & 0x0FU) != 0x0AU) return 0;
    uint8_t payload[18];
    memcpy(payload, data, 16);
    rc_crc_a(payload, 16, &payload[16]);
    count = sizeof(response);
    return rc_transceive(payload, sizeof(payload), response, &count, &last_bits) &&
           count == 1U && last_bits == 4U && (response[0] & 0x0FU) == 0x0AU;
}

static const char *card_name(uint8_t sak)
{
    if (sak == 0x08U) return "MIFARE Classic 1K";
    if (sak == 0x18U) return "MIFARE Classic 4K";
    if (sak == 0x09U) return "MIFARE Classic Mini";
    if ((sak & 0x20U) != 0U) return "ISO14443A (SAK indicates ISO14443-4)";
    if (sak == 0x00U) return "MIFARE Ultralight/NTAG family (SAK only)";
    return "ISO14443A card (unknown type from SAK)";
}

static int parse_block(const char *s, uint8_t *block)
{
    char *end;
    unsigned long n = strtoul(s, &end, 10);
    if (s[0] == '\0' || *end != '\0' || n > 255UL) return 0;
    *block = (uint8_t)n;
    return 1;
}

static int parse_data(const char *s, uint8_t *data)
{
    if (strlen(s) != 32U) return 0;
    for (uint8_t i = 0; i < 16U; ++i) {
        char pair[3] = {s[i * 2U], s[i * 2U + 1U], 0};
        char *end;
        unsigned long n = strtoul(pair, &end, 16);
        if (*end != '\0' || n > 255UL) return 0;
        data[i] = (uint8_t)n;
    }
    return 1;
}

static void command_card(char **argv, int argc)
{
    uint8_t uid[10], uid_len = 0, sak;
    uint16_t max_blocks;
    char line[128];
    if (argc == 1 && strcmp(argv[0], "scan") == 0) {
        if (!read_uid(uid, &uid_len, &sak)) {
            buzzer_error();
            (void)snprintf(line, sizeof(line), "Scan failed: %s. Check placement/type; RC522 supports ISO14443A at 13.56 MHz only.\r\n", uid_scan_error);
            print_line(line);
            return;
        }
        buzzer_beep(70);
        uint16_t used = (uint16_t)snprintf(line, sizeof(line), "UID:");
        for (uint8_t i = 0; i < uid_len && used < sizeof(line); ++i) {
            used += (uint16_t)snprintf(&line[used], sizeof(line) - used, " %02X", uid[i]);
        }
        (void)snprintf(&line[used], sizeof(line) - used, " | UID length: %u | SAK: 0x%02X | %s\r\n",
                       uid_len, sak, card_name(sak));
        print_line(line);
        picc_halt();
        return;
    }
    if (argc < 2 || (strcmp(argv[0], "read") != 0 && strcmp(argv[0], "write") != 0 &&
                     strcmp(argv[0], "erase") != 0 && strcmp(argv[0], "copy") != 0)) {
        print_line("Usage: read <block> | write <block> <32 hex> | erase <block> YES | copy <src> <dst>\r\n");
        return;
    }
    if (!classic_card(uid, &uid_len, &sak, &max_blocks)) {
        buzzer_error();
        print_line("Present a MIFARE Classic 1K/4K card first; no data changed.\r\n");
        picc_halt();
        return;
    }
    uint8_t block;
    if (!parse_block(argv[1], &block) || !block_is_data(block, max_blocks)) {
        buzzer_error();
        print_line("Invalid block. UID block and sector trailer blocks are protected.\r\n");
        picc_halt();
        return;
    }
    uint8_t key[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (uid_len < 4U || !authenticate(block, key, &uid[uid_len - 4U])) {
        buzzer_error();
        print_line("Key A authentication failed (default key is FF FF FF FF FF FF).\r\n");
        picc_halt();
        stop_crypto();
        return;
    }
    uint8_t data[16];
    if (strcmp(argv[0], "read") == 0 && argc == 2) {
        if (!mifare_read_block(block, data)) {
            buzzer_error();
            print_line("Block read failed.\r\n");
        }
        else {
            buzzer_beep(70);
            for (uint8_t i = 0; i < 16U; ++i) {
                (void)snprintf(line, sizeof(line), "%02X%s", data[i], (i == 15U) ? "\r\n" : " ");
                print_line(line);
            }
        }
    } else if (strcmp(argv[0], "write") == 0 && argc == 3) {
        if (!parse_data(argv[2], data)) {
            buzzer_error();
            print_line("Data must be exactly 32 hexadecimal digits.\r\n");
        } else if (!mifare_write_block(block, data)) {
            buzzer_error();
            print_line("Block write failed.\r\n");
        } else {
            buzzer_success();
            print_line("Block written.\r\n");
        }
    } else if (strcmp(argv[0], "erase") == 0 && argc == 3 && strcmp(argv[2], "YES") == 0) {
        memset(data, 0, sizeof(data));
        if (!mifare_write_block(block, data)) {
            buzzer_error();
            print_line("Erase failed.\r\n");
        } else {
            buzzer_erase_success();
            print_line("Block filled with zero bytes.\r\n");
        }
    } else if (strcmp(argv[0], "copy") == 0 && argc == 3) {
        uint8_t destination;
        if (!parse_block(argv[2], &destination) || !block_is_data(destination, max_blocks)) {
            buzzer_error();
            print_line("Invalid destination block; UID/trailer blocks are protected.\r\n");
        } else if (block_sector(destination) != block_sector(block)) {
            buzzer_error();
            print_line("Copy currently requires source and destination in the same sector.\r\n");
        } else if (!mifare_read_block(block, data)) {
            buzzer_error();
            print_line("Source block read failed.\r\n");
        } else {
            /* Authentication covers the whole sector: retain Crypto1 state. */
            if (mifare_write_block(destination, data)) {
                buzzer_success();
                print_line("Data block copied on the presented card.\r\n");
            } else {
                buzzer_error();
                print_line("Destination block write failed.\r\n");
            }
        }
    } else {
        print_line("Usage: read <block> | write <block> <32 hex> | erase <block> YES | copy <src> <dst>\r\n");
    }
    picc_halt();
    stop_crypto();
}

static void print_line(const char *s)
{
    const uint8_t *p = (const uint8_t *)s;
    uint16_t left = (uint16_t)strlen(s);
    while (left != 0U && CDC_HostConnected()) {
        uint16_t n = left > APP_TX_DATA_SIZE ? APP_TX_DATA_SIZE : left;
        uint32_t started = HAL_GetTick();
        uint8_t status;
        do {
            status = CDC_Transmit_FS((uint8_t *)p, n);
            if (status == USBD_OK) break;
            HAL_Delay(1);
        } while ((uint32_t)(HAL_GetTick() - started) < 250U);
        if (status != USBD_OK) return;
        p += n;
        left -= n;
        while ((uint32_t)(HAL_GetTick() - started) < 2U) { }
    }
}

static void print_help(void)
{
    print_line("\r\nRC522 commands:\r\n");
    print_line("  help         show this menu\r\n");
    print_line("  status       show RC522 VersionReg\r\n");
    print_line("  scan         read card UID and SAK\r\n");
    print_line("  read <block> read a MIFARE Classic data block\r\n");
    print_line("  write <block> <32 hex chars> write a data block\r\n");
    print_line("  erase <block> YES fill a data block with zeros\r\n");
    print_line("  copy <src> <dst> copy a data block\r\n");
    print_line("Only default Key A is enabled; UID and trailer blocks are protected.\r\n> ");
}

static char command_line[96];
static uint8_t command_length;
static void shell_poll(void)
{
    uint8_t ch;
    while (CDC_ReadByte(&ch)) {
        if (ch == '\r' || ch == '\n') {
            if (command_length == 0U) continue;
            command_line[command_length] = '\0';
            print_line("\r\n");
            char *argv[4];
            int argc = 0;
            char *token = strtok(command_line, " \t");
            while (token != NULL && argc < 4) {
                argv[argc++] = token;
                token = strtok(NULL, " \t");
            }
            if (argc == 0) {
                print_line("> ");
            } else if (argc == 1 && strcmp(argv[0], "help") == 0) print_help();
            else if (argc == 1 && strcmp(argv[0], "status") == 0) {
                char response[48];
                (void)snprintf(response, sizeof(response), "RC522 VersionReg=0x%02X\r\n> ", rc_read(0x37));
                print_line(response);
            } else if (strcmp(argv[0], "scan") == 0 || strcmp(argv[0], "read") == 0 ||
                       strcmp(argv[0], "write") == 0 || strcmp(argv[0], "erase") == 0 ||
                       strcmp(argv[0], "copy") == 0) {
                command_card(argv, argc);
                print_line("> ");
            } else {
                buzzer_error();
                print_line("Unknown command. Type help.\r\n> ");
            }
            command_length = 0;
        } else if (ch == 8U || ch == 127U) {
            if (command_length != 0U) {
                command_length--;
                print_line("\b \b");
            }
        } else if (ch >= 0x20U && ch < 0x7FU && command_length < sizeof(command_line) - 1U) {
            command_line[command_length++] = (char)ch;
            char echo[2] = {(char)ch, '\0'};
            print_line(echo);
        }
    }
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    GPIO_SPI_UART_Init();
    rc_init();
    MX_USB_DEVICE_Init();
    uint8_t was_connected = 0;
    while (1) {
        if (CDC_HostConnected() && !was_connected) {
            char line[96];
            (void)snprintf(line, sizeof(line), "\r\nRC522 ready: VersionReg=0x%02X\r\nType help for commands.\r\n> ", rc_read(0x37));
            print_line(line);
            was_connected = 1;
        } else if (!CDC_HostConnected()) {
            was_connected = 0;
        }
        shell_poll();
        HAL_Delay(1);
    }
}

void USB_LP_CAN1_RX0_IRQHandler(void)
{
    extern PCD_HandleTypeDef hpcd_USB_FS;
    HAL_PCD_IRQHandler(&hpcd_USB_FS);
}

void SysTick_Handler(void)
{
    HAL_IncTick();
    HAL_SYSTICK_IRQHandler();
}
