# Edge Impulse + TI Code Composer Examples

This repository provides code snippets and walkthroughs for integrating Edge Impulse with Texas Instruments Code Composer projects.

## Introduction

[Edge Impulse](https://www.edgeimpulse.com/) deploys trained learning algoritms via an open source C++ library fully compatible with all Texas Instruments `SimpleLink` devices. 

Edge Impulse provides a comprehensive set of [tutorials](https://docs.edgeimpulse.com/docs/development-boards/ti-launchxl) and prebuilt [firmware examples](https://github.com/edgeimpulse/firmware-ti-launchxl) for the `CC1352 LaunchPad XL` development kits, but when designing for custom hardware user's generally need to integrate the Edge Impulse SDK with existing projects that target custom hardware.

This repository provides the minimal steps required to integrate an arbitrary Edge Impulse project into an existing Code Composer Studio project. 

## Requirements
* [TI Code Composer Studio IDE](https://www.ti.com/tool/CCSTUDIO)
* A trained Edge Impulse project

If you are new to Edge Impulse, you can train a custom voice recognition project [here](https://studio.edgeimpulse.com/evaluate) in just a few minutes

For simplicity's sake, the guide below is written for the `TI ARM Clang` Compiler, and has been tested with [SimpleLink™ CC13x2\_26x2 SDK 5.20.00.52](https://www.ti.com/tool/SIMPLELINK-CC13XX-CC26XX-SDK). 

Edge Impulse libraries should be compatible with all compilers supporting `c++11`, as well as future SimpleLink SDK versions and devices. However the integration steps may vary slightly based on toolchain and target. For known issues & workarounds with specific SDKs or toolchains, see the `Troubleshooting` section below.

## Build Integration Steps

1. [Deploy](https://docs.edgeimpulse.com/docs/tutorials/deploy-your-model-as-a-c-library#download-the-c++-library-from-edge-impulse) your Edge Impulse project as a C++ Library, and unzip the downloaded archive.

You should see the following directories:

```
edge-impulse-sdk
tflite-model
model-parameters
```

See our [documentation](https://docs.edgeimpulse.com/docs/tutorials/deploy-your-model-as-a-c-library#download-the-c++-library-from-edge-impulse) for details on the library contents.

2. Copy the three directories listed above into the root directory of your code composer project
![](doc/ccs-ei-lib.png)

3. Add the required include paths to your project. Copy the paths below and paste them into `Project -> Properties -> Build -> Arm Compiler -> Include Options`

```
${PROJECT_ROOT}/edge-impulse-sdk/porting
${PROJECT_ROOT}/edge-impulse-sdk/dsp
${PROJECT_ROOT}/edge-impulse-sdk/classifier
```
![](ccs-ei-include.png)

4. For devices using the [SimpleLink™ CC13x2\_26x2 SDK](https://www.ti.com/tool/SIMPLELINK-CC13XX-CC26XX-SDK), enable the TI porting functions built into the Edge Impulse SDK.

Copy the definition below and paste it into `Project -> Properties -> Build -> ARM Compiler -> Predefined Symbols`

```
EI_PORTING_TI=1
```

![](doc/ccs-ei-symbols.png)

These built in porting functions implement a set of platform specific methods that the Edge Impulse SDK needs for basic operation. 

These functions are described in `edge-impulse-sdk/porting/ti/ei_classifier_porting.cpp`, and If you are targetting a device not in the `CC13xx_26xx` family, you will likely need to modify the implementations to suit your target platform.

5. At this point, the Edge Impulse library is integrated, and the next step is to now make use of the library to run the trained learning algorithm.

A bare minimum example of the code required is provided for reference in [ei-infer.cpp](./ei-infer.cpp). Copy this file into the root of your ccs project, and then define the following extern statements in application `.c` or `.cpp` source files that need to run inferencing. 

```c
#include "model-parameters/model_variables.h"
extern "C" void ei_init(void);
extern "C" uint16_t ei_infer(float *buf, size_t len);
extern "C" uint16_t ei_infer_audio(int16_t *buf, size_t len);
```

6. Finally, in application code:
    * Call `ei_init` exactly once during initialization
    * Pass a buffer of samples to `ei_infer` or `ei_infer_audio` to run the learning algorithm. Note that `ei_infer_audio` is optimized to use less memory when handling large streams of input data compared to `ei_infer`, but is only intended to work with audio models. 
    * Index into the `ei_classifier_inferencing_categories` array in `model_variables.h` using the index returned by `ei_infer*`

This executes inference for most code composer projects, so long as a valid buffer of sample data is provided, but is an extremely minimal example intended mostly for reference.

For robust inferencing with useful metrics such as the execution time, memory debugging, and model accuracy - examples for `CC13xx_CC26xx` are available [here](https://github.com/edgeimpulse/firmware-ti-launchxl/blob/main/ei_run_impulse.cpp) and [here](https://github.com/edgeimpulse/example-standalone-inferencing-ti-launchxl/blob/main/ei_main.cpp#L39).

## Next Steps & Examples

Actual application code must determine how to obtain formatted sample buffers from sensor input, determine when inferencing run, and act on the inferencing result.

To provide examples of this in various contexts, this repository contains example projects derived from TI SDK examples. These projects utilize different sensors, Edge Impulse projects, and wireless stacks. 

If you are planning or developing an enterprise application or product with Edge Impulse and Texas Instruments, we also provide dedicated technical support & engineering services for developing production grade edge machine learning solutions. [Contact us](https://www.edgeimpulse.com/contact) to learn more.

## Troubleshooting

This section contains known issues and workarounds to specific Code Composer project types, SDKs, or toolchains

### * "error: use of undeclared identifier E\*" when using `SIMPLELINK-CC13XX-CC26XX-SDK 5.40.00.40`, `TIRTOS`, and `TICLANG 1.30 LTS`

This observed issue occurs due to a SimpleLink SDK conflict when including `<complex>` (required by the Edge Impulse SDK) from the c++ standard library. The error log will appear as:

```
In file included from /Applications/ti/ccs1110/ccs/tools/compiler/ti-cgt-armllvm_1.3.0.LTS/include/c++/v1/complex:242:
In file included from /Applications/ti/ccs1110/ccs/tools/compiler/ti-cgt-armllvm_1.3.0.LTS/include/c++/v1/sstream:184:
In file included from /Applications/ti/ccs1110/ccs/tools/compiler/ti-cgt-armllvm_1.3.0.LTS/include/c++/v1/ostream:137:
In file included from /Applications/ti/ccs1110/ccs/tools/compiler/ti-cgt-armllvm_1.3.0.LTS/include/c++/v1/ios:215:
In file included from /Applications/ti/ccs1110/ccs/tools/compiler/ti-cgt-armllvm_1.3.0.LTS/include/c++/v1/__locale:18:
In file included from /Applications/ti/ccs1110/ccs/tools/compiler/ti-cgt-armllvm_1.3.0.LTS/include/c++/v1/mutex:190:
In file included from /Applications/ti/ccs1110/ccs/tools/compiler/ti-cgt-armllvm_1.3.0.LTS/include/c++/v1/__mutex_base:15:
In file included from /Applications/ti/ccs1110/ccs/tools/compiler/ti-cgt-armllvm_1.3.0.LTS/include/c++/v1/system_error:145:
/Applications/ti/ccs1110/ccs/tools/compiler/ti-cgt-armllvm_1.3.0.LTS/include/c++/v1/__errc:142:43: error: use of undeclared identifier 'EIDRM'
    identifier_removed                  = EIDRM,
                                          ^
/Applications/ti/ccs1110/ccs/tools/compiler/ti-cgt-armllvm_1.3.0.LTS/include/c++/v1/__errc:156:43: error: use of undeclared identifier 'ENOLINK'
    no_link                             = ENOLINK,
                                          ^
/Applications/ti/ccs1110/ccs/tools/compiler/ti-cgt-armllvm_1.3.0.LTS/include/c++/v1/__errc:161:43: error: use of undeclared identifier 'ENOMSG'
    no_message_available                = ENOMSG,
                                          ^
/Applications/ti/ccs1110/ccs/tools/compiler/ti-cgt-armllvm_1.3.0.LTS/include/c++/v1/__errc:163:43: error: use of undeclared identifier 'ENOMSG'
    no_message                          = ENOMSG,

    . . . 
```

To workaround this issue, either revert to a different Simplelink SDK version, or modify the file located in `<SDK>\source\ti\posix\ticlang\errno.h`. 

If modifying the SDK, at the end of `errno.h`, find the line that reads:

```
#endif /* ti_posix_ticlang_errno__include */
```

and modify it to read:

```
#include_next <errno.h>
at the end of the file, at the line right before “#endif /* ti_posix_ticlang_errno__include */”, introduce “#include_next <errno.h>”
```

### "filename or extension is too long" on windows

The tensorflow directory structure inside the edge-impulse-sdk necessitates long object and include paths during linking. This can cause windows systems to hit the command character limit in the ccs console. 

The error will appear as:
```
makefile:951: recipe for target '<project_name>.out' failed

tiarmclang: error: unable to execute command: Couldn't execute program 'C:/ti/ccs1110/ccs/tools/compiler/ti-cgt-armllvm_1.3.0.LTS/bin\tiarmlnk.exe': The filename or extension is too long.  (0xCE)
```

A robust fix for the issue is in progress and will be added in a future release. In the short term, a temporary workaround may be to move your project to a new ccs workspace in `C:/` with a very short workspace and project name. 
