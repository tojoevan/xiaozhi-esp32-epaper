#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_GPIO_MCLK  GPIO_NUM_14
#define AUDIO_I2S_GPIO_WS    GPIO_NUM_38
#define AUDIO_I2S_GPIO_BCLK  GPIO_NUM_15
#define AUDIO_I2S_GPIO_DIN   GPIO_NUM_16
#define AUDIO_I2S_GPIO_DOUT  GPIO_NUM_45

#define AUDIO_CODEC_PA_PIN       GPIO_NUM_46
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_47
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_48
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR

#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VBAT_PWR_GPIO           GPIO_NUM_18

/*EPD port Init*/
#define EPD_SPI_NUM        SPI3_HOST

#define EPD_DC_PIN    GPIO_NUM_10
#define EPD_CS_PIN    GPIO_NUM_11
#define EPD_SCK_PIN   GPIO_NUM_12
#define EPD_MOSI_PIN  GPIO_NUM_13
#define EPD_RST_PIN   GPIO_NUM_9
#define EPD_BUSY_PIN  GPIO_NUM_8

#define EXAMPLE_LCD_WIDTH   200
#define EXAMPLE_LCD_HEIGHT  200

/*DEV POWER init*/
#define EPD_PWR_PIN     GPIO_NUM_6
#define Audio_PWR_PIN   GPIO_NUM_42
#define VBAT_PWR_PIN    GPIO_NUM_17
 
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY  false

#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0

#define SDMMC_D0_PIN    GPIO_NUM_40  
#define SDMMC_CLK_PIN   GPIO_NUM_39
#define SDMMC_CMD_PIN   GPIO_NUM_41
#define SDlist "/sdcard" //Directory, similar to a standard

/*i2c dev*/
#define I2C_RTC_DEV_Address        0x51
#define I2C_SHTC3_DEV_Address      0x70

#define SAFE_STR(str) ((str) ? (str) : "<null>")

#endif // _BOARD_CONFIG_H_
