/* A minimized implementation of the full Edge Impulse firmware
 * for the CC1352P LaunchPad. This minimal example shows minimal code required to run
 * inferencing in both bare metal and RTOS projects.
 *
 * This example is provided for documentation purposes, has minimal testing, and
 * may not include the latest optimizations utilized by the fully supported Edge
 * Impulse firmware. For reference, see `ei_run_impulse.cpp` on github:
 * https://github.com/edgeimpulse/firmware-ti-launchxl/blob/main/
 *
 * and view the full Edge Impulse SDK documentation:
 * https://docs.edgeimpulse.com/docs/deployment/running-your-impulse-locally/deploy-your-model-as-a-c-library
 *
 * Copyright (c) 2022 EdgeImpulse Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* Include ----------------------------------------------------------------- */
#include <errno.h>
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

/// TI Drivers used for inferencing: timing and serial output
#include <ti/drivers/UART2.h>
#include "ti/drivers/Timer.h"
#include "ti_drivers_config.h"
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Clock.h>
#include <unistd.h>


/// state for timing and serial output
static UART2_Handle uart = NULL;
static Timer_Handle timer_handle = NULL;
static uint64_t timer_count = 0;

/// private function prototypes
void timer_Callback(Timer_Handle _myHandle, int_fast16_t _status);

/*
 * @brief Initialize peripherals and SDK routines needed to run inference. Run this exactly once
 */
extern "C" void ei_init(void) {
    // Setup up UART2 as target for ei_print functions
    UART2_Params uartParams;
    UART2_Params_init(&uartParams);
    uartParams.baudRate = 115200;
    uartParams.readMode = UART2_Mode_NONBLOCKING;
    uart = UART2_open(CONFIG_UART2_0, &uartParams);

    // Setup TIMER0 for measuring edge-impulse-sdk performance
    Timer_Params timer_params;
    Timer_init();
    Timer_Params_init(&timer_params);
    timer_params.period = 1000;
    timer_params.periodUnits = Timer_PERIOD_US;
    timer_params.timerMode = Timer_CONTINUOUS_CALLBACK;
    timer_params.timerCallback = timer_Callback;
    timer_handle = Timer_open(CONFIG_TIMER_0, &timer_params);
    Timer_start(timer_handle);

    // Setup the edge impulse SDK internals
    run_classifier_init();
}

/*
 * @brief Minimal example function for running inference on a buffer of time series data
 *
 * To use this function, collect a set of time series samples matching the raw data window
 * configured in your edge impulse project. For details on the data format expectations,
 * methods for collecting sensor data, and general information on using the edge impulse SDK
 * see our SDK documentation:
 * https://docs.edgeimpulse.com/docs/deployment/running-your-impulse-locally/deploy-your-model-as-a-c-library
 *
 *
 * NOTE: This method of inference on a pre-collected buffer is not well suited for
 * high frequency data such as audio and will incur added memory and compute overhead.
 *
 * For such cases, see the voice_recognition example in the ei_code_composer_examples repository.
 *
 * @param data A pointer to the data buffer of sensor samples. User
 *              must ensure this buffer matches the expected size and format of
 *              the Impulse, otherwise this function will return an error
 *
 * @param len The length of the data buffer
 *
 * @param debug Enables logging internally in the Edge Impulse SDK
 *              displayed using `Serial_Out`
 */
extern "C" ei_impulse_result_t ei_infer(float *data, size_t len, bool debug)
{
    signal_t signal;
    numpy::signal_from_buffer(data, len, &signal);
    ei_impulse_result_t result = { 0 };

    EI_IMPULSE_ERROR r = run_classifier(&signal, &result, debug);
    if (r != EI_IMPULSE_OK) {
        ei_printf("ERR: Failed to run classifier (%d)\r\n", r);
        while(1);
    }

    // print the predictions, but only if valid labels are present
    if (result.label_detected) {
        ei_printf("\r\nPredictions (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.): \r\n",
            result.timing.dsp, result.timing.classification, result.timing.anomaly);
        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            ei_printf("    %s: \t", result.classification[ix].label);
            // printing floating point
            ei_printf("%d%%", (int32_t) (result.classification[ix].value * 100.0));
            ei_printf("\r\n");
        }
    }
    return result;
}

/*
 * @brief Workaround if `usleep` is missing from some TIRTOS build (posix may not be enabled)
 */
__attribute__((weak)) extern "C" int usleep(useconds_t us) {
    Task_sleep(1 / Clock_tickPeriod);
    return 0;
}

/**
 * @brief Get current time in ms.
 *
 * This method is referenced in the TI porting layer
 * of the edge impulse SDK, and provides a simple interface to give hardware timing
 * resources to the SDK for benchmarking purposes.
 *
 * If you do not need benchmarking information, you can simply not initialize any timer
 * resources that populate the timer_count static variable.
 */
extern "C" uint64_t Timer_getMs(void)
{
    return timer_count;
}

/**
 * @brief Called by CONFIG_TIMER_0 interrupt to support `Timer_getMs`. Usage defined
 * in `ei_init`
 */
void timer_Callback(Timer_Handle _myHandle, int_fast16_t _status)
{
    timer_count++;
}

/**
 * @brief Allow edge impulse to write serial output, configured here to output over UART2.
 *
 * This method is referenced in the TI porting layer
 * of the edge impulse SDK, and provides a simple cross-platform interface for debug
 * prints and logging information from both the Edge Impulse SDK, and Edge Impulse example code.
 *
 * You can stub out this function definition, or redirect to your logging/trace interface of choice.
 *
 * @param string
 * @param length
 */
extern "C" void Serial_Out(char *string, int length)
{
    size_t bytes_written;
    UART2_write(uart, string, length, &bytes_written);
}

