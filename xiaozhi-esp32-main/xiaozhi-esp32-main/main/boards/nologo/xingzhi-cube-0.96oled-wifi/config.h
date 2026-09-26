#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/i2c_types.h>

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_15
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_2
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_16
/* No speaker is fitted in the current OLED/IMU build. */
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_NC
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_NC
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_NC

#define BUILTIN_LED_GPIO        GPIO_NUM_48
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_40
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_39

#define DISPLAY_SDA_PIN GPIO_NUM_8
#define DISPLAY_SCL_PIN GPIO_NUM_9
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  64
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true

/* MPU6050 uses the second I2C controller in the original project. */
#define IMU_I2C_PORT I2C_NUM_1
#define IMU_SDA_PIN GPIO_NUM_4
#define IMU_SCL_PIN GPIO_NUM_5
#define MPU6050_ADDRESS_LOW 0x68
#define MPU6050_ADDRESS_HIGH 0x69

#endif // _BOARD_CONFIG_H_
