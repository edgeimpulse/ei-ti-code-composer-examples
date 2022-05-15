#include "bmi160.h"
#include "bmi160_config.h"
#include "ei_imu_minimal.h"

#include "ti_drivers_config.h"
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Clock.h>

#define CONVERT_G_TO_MS2    9.80665f

I2C_Handle i2cHandle; // externed to bmi160_config.c

/**
 * @brief Setup I2C
 *
 * @return int, 0 => OK
 */
int imu_init(void)
{
    I2C_Params      i2cParams;
    // Initialize the I2C pins
    I2C_Params_init(&i2cParams);
    i2cParams.bitRate = I2C_400kHz;
    i2cParams.transferMode = I2C_MODE_BLOCKING;
    i2cParams.transferCallbackFxn = NULL;
    i2cHandle = I2C_open(CONFIG_I2C_0, &i2cParams);
    if (i2cHandle == NULL) {
        return -1;
    }
    init_bmi160(i2cHandle);

    return 0;
}

/**
 * @brief Sample accelerometer (x, y, z axis) and store in provided buffer
 * Stores three floats.
 *
 * @return int, 0 => OK
 */
int imu_sample(float *buf)
{
    float acc_data[3];
        volatile uint32_t div_sample_count;

        if(bmi160_getData(acc_data)) {
            return -1;
        }

        buf[0] = acc_data[0] * CONVERT_G_TO_MS2;
        buf[1] = acc_data[1] * CONVERT_G_TO_MS2;
        buf[2] = acc_data[2] * CONVERT_G_TO_MS2;

    return 0;
}

/**
 * @brief Method to create a window with accelerometer data at a given sample rate
 * This method blocks and sleeps the thread while waiting.
 *
 * This method modifies the passed buf and len pointers to point to the newly updated data buffer and provide the buffer length
 *
 * @param buf buffer to store data in
 *
 * @param len size of the data buffer
 *
 * @param interval sample period in milliseconds
 *
 * @return int, 0 => OK
 */
int imu_fill_window(float *buf, size_t len, size_t interval) {

    for(int i = 0; i < len; i += 3) {
        if (imu_sample(&buf[i])) {
            return -1;
        }

        Task_sleep(interval / Clock_tickPeriod);
    }

    return 0;
}
