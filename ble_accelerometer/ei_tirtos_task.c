/* A minimized implementation based from the full Edge Impulse firmware
 * for the CC1352P LaunchPad. This minimal example shows only code required to run
 * inferencing in both bare metal and RTOS projects.
 *
 * This example is provided for documentation purposes, has minimal testing, and
 * may not include the latest optimizations utilized by the fully supported Edge
 * Impulse firmware. For reference, see ei_run_impulse.cpp on github:
 * https://github.com/edgeimpulse/firmware-ti-launchxl/blob/main/
 *
 * and view the full Edge Impulse SDK documentation:
 * https://docs.edgeimpulse.com/docs/deployment/running-your-impulse-locally/deploy-your-model-as-a-c-library
 *
 * Copyright (c) 2021 EdgeImpulse Inc.
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

#include "ei_imu_minimal.h"
#include "ei_infer_minimal.h"
#include "ei_classifier_types.h"
#include "model-parameters/model_metadata.h"

#include <ti/sysbios/knl/Task.h>

// stack size may need to be modified depending on the Impulse used
#define EI_TASK_STACK_SIZE 4096

static uint8_t eiTaskStack[EI_TASK_STACK_SIZE];
static Task_Struct eiTask;

static float data[EI_CLASSIFIER_NN_INPUT_FRAME_SIZE];

/*
 *  ======== thread example: inferencing loop ========
 */
void inferThread(UArg a0, UArg a1)
{
    imu_init();
    ei_init();

    while(1) {
        imu_fill_window(data, EI_CLASSIFIER_RAW_SAMPLE_COUNT, EI_CLASSIFIER_INTERVAL_MS);
        ei_impulse_result_t result = ei_infer(data, EI_CLASSIFIER_NN_INPUT_FRAME_SIZE, false);
        if (result.label_detected) {
            /*
             * add custom post-processing logic here.
             * Handle the inference result and drive application
             * logic. For more information see:
             *
             * https://docs.edgeimpulse.com/docs/deployment/running-your-impulse-locally/deploy-your-model-as-a-c-library
             *
             */

            // Determine which label scored highest. There's more advanced post-processing options
            // possible, but to start just run a comparison
            float max_val = 0.0;
            uint16_t result_idx = 0;
            for (uint16_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
                if (max_val < result.classification[ix].value) {
                    max_val = result.classification[ix].value;
                    result_idx = ix;
                }
            }
        }
    }
}

/*
 * Create a task to run the example thread in a TI application
 */
void ei_create_task(void) {
    // Configure task

    Task_Params taskParams;
    Task_Params_init(&taskParams);
    taskParams.stack = eiTaskStack;
    taskParams.stackSize = EI_TASK_STACK_SIZE;
    taskParams.priority = 1;

    Task_construct(&eiTask, inferThread, &taskParams, NULL);
}
