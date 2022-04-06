# Fall Detection with the BLE5-Stack

## :construction: This example is under construction :construction:
This example is a work in progress.


This example starts from the [simple_peripheral](https://dev.ti.com/tirex/explore/node?node=AHkS7uUGcCCoQPnM1CZygg__BSEc4rl__LATEST) example in the `SimpleLink CC13xx_26xx SDK` and adds fall detection capability via Edge Impulse 

# Requirements
* All hardware requirements listed in the [simple_peripheral documentation](https://dev.ti.com/tirex/explore/node?node=AHkS7uUGcCCoQPnM1CZygg__BSEc4rl__LATEST)
* A [BOOSTXL-SENSORS](https://www.ti.com/tool/BOOSTXL-SENSORS) board

## Integration Steps

1. Either using the `Resource Explorer` or `New Project Wizard` in Code Composer, import the example project under [simple_peripheral -> TI-RTOS -> TI Clang Compiler](https://dev.ti.com/tirex/explore/node?node=AMBd8emOu4nstM8hYtKWTg__BSEc4rl__LATEST)

2. Navigate to the [Fall Detector v2](www.edgeimpulse.com/evaluate) Edge Impulse Project. This is a trained model designed to detect falls using an accelerometer. Clone this project (top right), and then navigate to the `Deployment` tab.

2. Follow the [instructions](../README.md) in this repository to integrate your new voice recognition project with i2secho

3. Make minor modifications to `Application/simple_peripheral` to obtain accelerometer data, run inference, and transmit the result.

A work in progress example (running inference only) is provided in this directory.

