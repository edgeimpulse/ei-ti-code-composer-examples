# Voice Recognition on CC1352P7

This example starts from the [i2secho](https://dev.ti.com/tirex/explore/node?node=AIVcEZp2JO4Hp1OYffB3lw__pTTHBmu__LATEST) example in the `SimpleLink CC13x2_26x2 SDK` and adds voice recognition capability via Edge Impulse Transfer Learning models.

# Requirements
All hardware requirements are listed in the [i2secho documentation](https://dev.ti.com/tirex/explore/node?node=AIVcEZp2JO4Hp1OYffB3lw__pTTHBmu__LATEST) from TI.

## Integration Steps

1. Follow the TI guide in the [i2secho documentation](https://dev.ti.com/tirex/explore/node?node=AIVcEZp2JO4Hp1OYffB3lw__pTTHBmu__LATEST) to connect your hardware. Alternatively Edge Impulse provides similar instructions for the CC1352P Launchpad [here](https://docs.edgeimpulse.com/docs/development-platforms/officially-supported-mcu-targets/ti-launchxl#connecting-to-edge-impulse)

2. Either using the `Resource Explorer` or `New Project Wizard` in Code Composer, import the example project under [i2secho -> TI-RTOS -> TI Clang Compiler](https://dev.ti.com/tirex/explore/node?node=ADijIPyz5TJxnoAewlsd3w__pTTHBmu__LATEST)

3. Navigate to [www.edgeimpulse.com/evaluate](www.edgeimpulse.com/evaluate). This will walk you through creating a custom voice recognition algorithm in Edge Impulse using a technique known as 'few shot keyword spotting'. Using the platform, an accurate voice recognition model can be trained with as little as 30 samples of a given keyword. 

An example project is shown [here](https://studio.edgeimpulse.com/public/105569/latest) where a model is pre-trained to detect calls for help, intended for home safety applications. Requests to 'call 911' are detected against background and unknown audio. The idea is that such a detection can then be used to trigger application logic for signals or alarms.

4. Follow the base [instructions](../README.md) in this repository to integrate your new voice recognition library with i2secho.

5. Remove the `i2secho.c` file from your project, and paste the `ei_*` prefixed source files from this folder into the root of your project.

6. Set the stack and heap size to have enough capacity for your edge impulse project.

First, open the `<target_name>.cmd` file in the root of your project and chance the `HEAPSIZE` variable to at least 0x1A000:

```
HEAPSIZE = 0x1A000;  /* Size of heap buffer used by HeapMem */
```

Then, open `main_tirtos.c` and change the `THREADSTACKSIZE` define to at least 2048:

```
/* Stack size in bytes */
#define THREADSTACKSIZE    2048
```

7. Now connect your dev kit and debug the project. You can view the USB serial output in the ccstudio terminal via `Window -> Show View -> Other ... -> Terminal -> Terminal`

If you see error messages intermittently printed that read:

```
"Error sample buffer overrun. Decrease the number of slices per model window "
```

your device is unable to keep up with the requested inferencing interval - i.e. the project defines a buffer size of 500ms of data, enough to run inference every 250ms, but it takes > 250ms to classify the audio signal.

You can solve this by redefining how often inferencing is run relative to the window size. Open `Project -> Properties -> Build -> ARM Compiler -> Predefined Symbols` and paste in:

```
EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW=2
```
