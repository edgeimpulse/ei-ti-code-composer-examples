# Voice Recognition on CC1352P7

This example starts from the [i2secho](https://dev.ti.com/tirex/explore/node?node=AIVcEZp2JO4Hp1OYffB3lw__pTTHBmu__LATEST) example in the `SimpleLink CC13x2_26x2 SDK` and adds voice recognition capability via Edge Impulse Transfer Learning models.

# Requirements
All hardware requirements are listed in the [i2secho documentation](https://dev.ti.com/tirex/explore/node?node=AIVcEZp2JO4Hp1OYffB3lw__pTTHBmu__LATEST) from TI

## Steps

1. Either using the `Resource Explorer` or `New Project Wizard` in Code Composer, import the example project under [i2secho -> TI-RTOS -> TI Clang Compiler](https://dev.ti.com/tirex/explore/node?node=ADijIPyz5TJxnoAewlsd3w__pTTHBmu__LATEST)

2. Navigate to [www.edgeimpulse.com/evaluate](www.edgeimpulse.com/evaluate). This will walk you through creating a custom voice recognition algorithm in Edge Impulse using a technique known as 'few shot keyword spotting'. Using the platform, an accurate voice recognition model can be trained with as little as 30 samples of a given keyword. 

Just follow along with the wizard, and it will lead you through collection, testing, and deployment steps.

As an example use case, in the demo example shown [here](TBD) a model is trained for safety applications. Requests to 'call 911' are used to detect an emergency in progress, which can then be used to trigger application logic for signals, alarms, or other systems.

2. Follow the [instructions](../README.md) in this repository to integrate your new voice recognition project with i2secho

3. Make minor modifications to i2secho to run inference. An example is provided here in [i2secho.c](./i2secho.c). The primary changes made are:

* externing the `ei-infer` cpp methods
* Setting the codec's audio SAMPLE_RATE to 16kHz
* Changing `BUFSIZE` to fit larger chunks of audio data
* modifying the `echoThread` to run inference instead of filtering:
 
```C
int16_t *buf = transactionToTreat->bufPtr;
/* bufSize is expressed in bytes but samples to consider are 16 bits long */
uint16_t numOfSamples = transactionToTreat->bufSize / sizeof(uint16_t);
label_idx = ei_infer_audio(buf, numOfSamples);
```

4. Add application logic to output when an activation is detected. An example of this will be added in a future release
