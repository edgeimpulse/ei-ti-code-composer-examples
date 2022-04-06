/* Bare minimum Edge Impulse inferencing implementation example
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

/* Include ----------------------------------------------------------------- */
#include "ei_run_classifier.h"
#include "ei_classifier_porting.h"
#include "model-parameters/model_metadata.h"
#include "numpy.hpp"

// Sentinel variable for quickly debugging if an error occur in the Edge Impulse model execution.
// This variable may be overwritten after the initial error occurs
static size_t r = 0;


/// Initialize the edge impulse classifier engine. Run this exactly once
extern "C" void ei_init() {
    run_classifier_init();
}

/// Minimal example function for running inference.
///
/// Returns the index of the highest scoring label, which can be used to index into the labels
/// array defined in `model-parameters/model_variables.h`
extern "C" uint16_t ei_infer(float *data, size_t data_len) {
    ei_impulse_result_t result = { 0 };

    signal_t signal;
    numpy::signal_from_buffer(data, data_len, &signal);

    // Run classifier takes a buffer of input data and executes the Impulse, outputting a result
    r |= run_classifier(&signal, &result, false);

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

    // return the index of the highest scoring label. To get the label name, index into
    // ei_classifier_inferencing_categories in model-variables.h
    return result_idx;
}

/// Minimal example function for running inference, optimized for large buffers of audio data
///
/// Returns the index of the highest scoring label, which can be used to index into the labels
/// array defined in `model-parameters/model_variables.h`
extern "C" uint16_t ei_infer_audio(int16_t *data, size_t data_len) {
    ei_impulse_result_t result = { 0 };

    signal_t signal;
    signal.total_length = data_len;
    signal.get_data = [data](size_t offset, size_t length, float *out_ptr) {
        return numpy::int16_to_float(&data[offset], out_ptr, length);
    };

    // Continuous inference is used in situations where the supplied signal is smaller than the
    // input tensor. Here any amount of data could be passed in,  and we want to handle that properly

    // Note that continuous inferencing is generally only supported for audio models
    r |= run_classifier_continuous(&signal, &result, false);

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

    // return the index of the highest scoring label. To get the label name, index into
    // ei_classifier_inferencing_categories in model-variables.h
    return result_idx;
}

/**
 * @brief Write data to the serial output. This is left empty for now, populate with any UART
 * or other serial protocol calls to enable debug prints and ei_printf from the edge impulse SDK
 *
 * @param string
 * @param length
 */
extern "C" void Serial_Out(char *string, int length)
{
}

/**
 * @brief Get current time in ms. This is left empty for now, populate with any timer implementation to
 * enable model performance benchmarking at runtime in the Edge Impulse SDK.
 *
 * @return uint64_t
 */
extern "C" uint64_t Timer_getMs(void)
{
    return 0;
}

