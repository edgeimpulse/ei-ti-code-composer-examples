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
#include <stdint.h>
#include <stdlib.h>

#include "ei_microphone_minimal_audio.h"
#include "edge-impulse-sdk/porting/ei_classifier_porting.h"
#include "arm_math.h"

/* POSIX Header files */
#include <mqueue.h>

#include "ti_drivers_config.h"
#include "AudioCodec.h"
#include <ti/drivers/I2S.h>
#include "model-parameters/model_metadata.h"

/* Audio sampling config */
#define AUDIO_SAMPLING_FREQUENCY            16000
#define AUDIO_SAMPLES_PER_MS                (AUDIO_SAMPLING_FREQUENCY / 1000)
#define AUDIO_DSP_SAMPLE_LENGTH_MS          8
#define AUDIO_DSP_SAMPLE_RESOLUTION         (sizeof(short))
#define AUDIO_DSP_SAMPLE_BUFFER_SIZE        EI_CLASSIFIER_SLICE_SIZE * (sizeof(short))

/* The higher the sampling frequency, the less time we have to process the data, but the higher the sound quality. */
#define SAMPLE_RATE                     AUDIO_SAMPLING_FREQUENCY   /* Supported values: 8kHz, 16kHz, 32kHz and 44.1kHz */
#define INPUT_OPTION                    AudioCodec_MIC_ONBOARD
#define OUTPUT_OPTION                   AudioCodec_SPEAKER_NONE

/* The more storage space we have, the more delay we have, but the more time we have to process the data. */
#define NUMBUFS         2      /* Total number of buffers to loop through */
#define BUFSIZE         AUDIO_DSP_SAMPLE_BUFFER_SIZE     /* I2S buffer size */

#define MSG_SIZE sizeof(struct frameEvarg)
#define MSG_NUM NUMBUFS

struct frameEvarg {
    int32_t flen;
    //int32_t num;
    int16_t *fbuf;
};

/** Status and control struct for inferencing struct */
typedef struct {
    int16_t *buffers[2];
    uint8_t buf_select;
    uint8_t buf_ready;
    uint32_t buf_count;
    uint32_t n_samples;
} inference_t;

/* Private variables ------------------------------------------------------- */
I2S_Handle i2sHandle;
static mqd_t mic_queue;
/* Lists containing transactions. Each transaction is in turn in these three lists */
List_List i2sReadList;
/* Buffers containing the data: written by read-interface, modified by treatment, and read by write-interface */
static uint8_t* i2sBufList[NUMBUFS] = {};
/* Transactions will successively be part of the i2sReadList and the treatmentList */
static I2S_Transaction *i2sTransactionList[NUMBUFS] = {};

static int max_msg_ready = 0;
static volatile bool record_ready = false;
static volatile bool skip = true; // used to skip the first (invalid) sample slice

/* Data structure managing safe buffer reads during inferencing */
static inference_t inference;

/* Private functions ------------------------------------------------------- */

/**
 * @brief      Inference audio callback, store samples in ram buffer
 *             Signal when buffer is full, and swap buffers
 * @param      buffer   Pointer to source buffer
 * @param[in]  n_bytes  Number of bytes to write
 */
static void audio_buffer_inference_callback(void *buffer, uint32_t n_bytes)
{
    inference.buf_count += n_bytes >> 1; // bytes to samples

    if(inference.buf_count >= inference.n_samples) {
        inference.buffers[inference.buf_select] = (int16_t*) buffer;
        inference.buf_select ^= 1;
        inference.buf_count = 0;
        inference.buf_ready = 1;
    } else {
        ei_printf("audio sampling buffer overflow: (%d:%d)\r\n", inference.buf_count,n_bytes) ;
    }
}

/**
 * Gets audio data passed by driver
 *
 * @param[in]  callback  Callback needs to handle the audio samples
 */
static void get_dsp_data(void (*callback)(void *buffer, uint32_t n_bytes))
{
    struct frameEvarg evArg;
    int n_msg_ready;
    struct mq_attr mqAttrs;
    do {
        mq_receive(mic_queue, (char *)&evArg, sizeof(evArg), NULL);

        callback((void *)evArg.fbuf, evArg.flen);

        mq_getattr(mic_queue, &mqAttrs);
        n_msg_ready = mqAttrs.mq_curmsgs;

        if(n_msg_ready > max_msg_ready) {
            max_msg_ready = n_msg_ready;
        }

    } while(n_msg_ready);
}

static void FrameCb(void *buf, uint16_t blen)
{
    if ((record_ready == true) && (skip == false)) {
        struct frameEvarg evArg;

        evArg.flen = blen;
        evArg.fbuf = (int16_t*) buf;

        mq_send(mic_queue , (char *)&evArg, sizeof(struct frameEvarg), 0);
    } else if ((record_ready == true) && (skip == true)) {
        skip = false;
    }
}

static void empty_queue()
{
    struct frameEvarg evArg;
    int n_msg_ready;
    struct mq_attr mqAttrs;
    do {
        mq_getattr(mic_queue, &mqAttrs);
        n_msg_ready = mqAttrs.mq_curmsgs;

        if(n_msg_ready > 0) {
            mq_receive(mic_queue, (char *)&evArg, sizeof(evArg), NULL);
            n_msg_ready--;
        }
    } while(n_msg_ready);
}

static void errCallbackFxn(I2S_Handle handle, int_fast16_t status, I2S_Transaction *transactionPtr) {
    /* The content of this callback is executed if an I2S error occurs */
    //sem_post(&semErrorCallback);
}

static void writeCallbackFxn(I2S_Handle handle, int_fast16_t status, I2S_Transaction *transactionPtr) {
    /*
     * The content of this callback is executed every time a write-transaction is started
     */
}

static void readCallbackFxn(I2S_Handle handle, int_fast16_t status, I2S_Transaction *transactionPtr) {
    /*
     * The content of this callback is executed every time a read-transaction
     * is started
     */

    /* We must consider the previous transaction (the current one is not over) */
    I2S_Transaction *transactionFinished = (I2S_Transaction*)List_prev(&transactionPtr->queueElement);

    if(transactionFinished != NULL) {
        /* The finished transaction contains data that must be treated */

        // no need for processing, already using right channel with ONBOARD_MIC and MONO_INV
        FrameCb(transactionFinished->bufPtr, transactionFinished->bufSize);
    }
}

static int audio_codec_open()
{
    /* Initialize TLV320AIC3254 Codec on Audio BP */
    uint8_t status = AudioCodec_open();
    if( AudioCodec_STATUS_SUCCESS != status)
    {
        /* Error Initializing codec */
        while(1);
    }

    /* Configure Codec */
    status =  AudioCodec_config(AudioCodec_TI_3254, AudioCodec_16_BIT,
                                SAMPLE_RATE, AudioCodec_MONO, OUTPUT_OPTION,
                                INPUT_OPTION);
    if( AudioCodec_STATUS_SUCCESS != status)
    {
        /* Error Initializing codec */
        while(1);
    }

    /* Volume control */
    AudioCodec_micVolCtrl(AudioCodec_TI_3254, AudioCodec_MIC_ONBOARD, 75);

    /*
     *  Initialize and Open the I2S driver
     */
    I2S_init();
    I2S_Params i2sParams;
    I2S_Params_init(&i2sParams);
    i2sParams.samplingFrequency =  SAMPLE_RATE;
    i2sParams.fixedBufferLength =  BUFSIZE;
    i2sParams.writeCallback     =  writeCallbackFxn ;
    i2sParams.readCallback      =  readCallbackFxn ;
    i2sParams.errorCallback     =  errCallbackFxn;
    i2sParams.SD1Channels       =  I2S_CHANNELS_MONO_INV;
    i2sParams.SD0Use            =  I2S_SD0_DISABLED;
    i2sParams.SD0Channels       =  I2S_CHANNELS_NONE;
    i2sHandle = I2S_open(CONFIG_I2S_0, &i2sParams);
    if (i2sHandle == NULL) {
        /* Error Opening the I2S driver */
        while(1);
    }

    return 0;
}


static void startStream()
{
    /* Initialize the queues and the I2S transactions */
    List_clearList(&i2sReadList);

    uint8_t k;
    for(k = 0; k < NUMBUFS; k++) {
        i2sBufList[k] = (uint8_t *)ei_malloc(BUFSIZE);
        if (i2sBufList[k] == NULL) {
            ei_printf("failed to create i2sBuf: %d\r\n", k);
            return;
        }

        i2sTransactionList[k] = (I2S_Transaction *)ei_malloc(sizeof(I2S_Transaction));
        if (i2sTransactionList[k] == NULL) {
            ei_printf("failed to create i2sTransaction: %d\r\n", k);
            return;
        }
        I2S_Transaction_init(i2sTransactionList[k]);
        i2sTransactionList[k]->bufPtr  = i2sBufList[k];
        i2sTransactionList[k]->bufSize = BUFSIZE;
        List_put(&i2sReadList, (List_Elem*)i2sTransactionList[k]);
    }

    List_tail(&i2sReadList)->next = List_head(&i2sReadList);
    List_head(&i2sReadList)->prev = List_tail(&i2sReadList);

    I2S_setReadQueueHead(i2sHandle,  (I2S_Transaction*) List_head(&i2sReadList));

    skip = true;
    empty_queue();

    I2S_startClocks(i2sHandle);
    I2S_startRead(i2sHandle);
}

static void stopStream()
{
    /* Stop I2S streaming */
    I2S_stopRead(i2sHandle);
    I2S_stopClocks(i2sHandle);

    uint8_t k;
    for(k = 0; k < NUMBUFS; k++) {
        ei_free(i2sBufList[k]);
        ei_free(i2sTransactionList[k]);
    }
}

/* Public functions -------------------------------------------------------- */
/**
 * @brief      Set the PDM mic to +34dB, returns 0 if OK
 */
extern "C" int ei_microphone_init(void)
{
    struct mq_attr mqAttrs;
    mqAttrs.mq_maxmsg = MSG_NUM;
    mqAttrs.mq_msgsize = MSG_SIZE;
    mqAttrs.mq_flags = 0;
    mic_queue = mq_open ("audio", O_RDWR | O_CREAT,
                   0664, &mqAttrs);
    if (mic_queue == (mqd_t)-1) {
        /* mq_open() failed */
        return -1;
    }

    return audio_codec_open();
}

extern "C" bool ei_microphone_inference_start(uint32_t n_samples)
{
    inference.buf_select = 0;
    inference.buf_count = 0;
    inference.n_samples = n_samples;
    inference.buf_ready = 0;

    startStream();
    record_ready = true;

    return true;
}

extern "C" bool ei_microphone_inference_record(void)
{
    bool ret = true;

   if(inference.buf_ready == 1) {
        ei_printf(
            "Error sample buffer overrun. Decrease the number of slices per model window "
            "(EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW)\n");
        ret = false;
    }

    while (inference.buf_ready == 0) {
        get_dsp_data(&audio_buffer_inference_callback);
    };

    if (max_msg_ready >= NUMBUFS-1) {
        ei_printf(
            "Error sample buffer overrun. Decrease the number of slices per model window "
            "(EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW): %d\n", max_msg_ready);
    }

    max_msg_ready = 0;
    inference.buf_ready = 0;

    return ret;
}

/*
 * Get raw audio signal data
 */
extern "C" int ei_microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr)
{
    arm_q15_to_float(&inference.buffers[inference.buf_select ^ 1][offset], out_ptr, length);
    return 0;
}

/*
 * Cleanly de-initialize audio resources
 */
extern "C" bool ei_microphone_inference_end(void)
{
    record_ready = false;
    stopStream();

    return true;
}
