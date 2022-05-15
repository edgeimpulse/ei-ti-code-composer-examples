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

#ifndef EI_INFER_MINIMAL_H
#define EI_INFER_MINIMAL_H

/* Include ----------------------------------------------------------------- */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "ei_classifier_types.h"

/* Function prototypes ----------------------------------------------------- */

/*
 * @brief Initialize peripherals and SDK routines needed to run inference. Run this exactly once
 */
extern int ei_init(void);

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
extern ei_impulse_result_t ei_infer(float *data, size_t len, bool debug);

#endif
