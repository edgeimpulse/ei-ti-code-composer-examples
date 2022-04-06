/******************************************************************************

 @file  simple_peripheral.c

 @brief This file contains the Simple Peripheral sample application for use
        with the CC2650 Bluetooth Low Energy Protocol Stack.

 Group: WCS, BTS
 Target Device: cc13x2_26x2

 ******************************************************************************
 
 Copyright (c) 2013-2021, Texas Instruments Incorporated
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:

 *  Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

 *  Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

 *  Neither the name of Texas Instruments Incorporated nor the names of
    its contributors may be used to endorse or promote products derived
    from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 ******************************************************************************
 
 
 *****************************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <string.h>

#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Event.h>
#include <ti/sysbios/knl/Queue.h>

#include <ti/display/Display.h>

#if (!(defined __TI_COMPILER_VERSION__) && !(defined __GNUC__))
#include <intrinsics.h>
#endif

#include <ti/drivers/utils/List.h>

#include <icall.h>
#include "util.h"
#include <bcomdef.h>
/* This Header file contains all BLE API and icall structure definition */
#include <icall_ble_api.h>

#include <devinfoservice.h>
#include <simple_gatt_profile.h>

#ifdef USE_RCOSC
#include <rcosc_calibration.h>
#endif //USE_RCOSC

#include <ti_drivers_config.h>
#include <board_key.h>

#include <menu/two_btn_menu.h>

#include "simple_peripheral_menu.h"
#include "simple_peripheral.h"
#include "ti_ble_config.h"

#ifdef PTM_MODE
#include "npi_task.h"               // To allow RX event registration
#include "npi_ble.h"                // To enable transmission of messages to UART
#include "icall_hci_tl.h"   // To allow ICall HCI Transport Layer
#endif // PTM_MODE


/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */
// How often to perform periodic event (in ms)
#define SP_PERIODIC_EVT_PERIOD               5000

// Task configuration
#define SP_TASK_PRIORITY                     1

#ifndef SP_TASK_STACK_SIZE
#define SP_TASK_STACK_SIZE                   1024
#endif

// Application events
#define SP_STATE_CHANGE_EVT                  0
#define SP_CHAR_CHANGE_EVT                   1
#define SP_KEY_CHANGE_EVT                    2
#define SP_ADV_EVT                           3
#define SP_PAIR_STATE_EVT                    4
#define SP_PASSCODE_EVT                      5
#define SP_PERIODIC_EVT                      6
#define SP_READ_RPA_EVT                      7
#define SP_SEND_PARAM_UPDATE_EVT             8
#define SP_CONN_EVT                          9

// Internal Events for RTOS application
#define SP_ICALL_EVT                         ICALL_MSG_EVENT_ID // Event_Id_31
#define SP_QUEUE_EVT                         UTIL_QUEUE_EVENT_ID // Event_Id_30

// Bitwise OR of all RTOS events to pend on
#define SP_ALL_EVENTS                        (SP_ICALL_EVT             | \
                                              SP_QUEUE_EVT)

// Size of string-converted device address ("0xXXXXXXXXXXXX")
#define SP_ADDR_STR_SIZE     15

// Row numbers for two-button menu
#define SP_ROW_SEPARATOR_1   (TBM_ROW_APP + 0)
#define SP_ROW_STATUS_1      (TBM_ROW_APP + 1)
#define SP_ROW_STATUS_2      (TBM_ROW_APP + 2)
#define SP_ROW_CONNECTION    (TBM_ROW_APP + 3)
#define SP_ROW_ADVSTATE      (TBM_ROW_APP + 4)
#define SP_ROW_RSSI          (TBM_ROW_APP + 5)
#define SP_ROW_IDA           (TBM_ROW_APP + 6)
#define SP_ROW_RPA           (TBM_ROW_APP + 7)
#define SP_ROW_DEBUG         (TBM_ROW_APP + 8)
#define SP_ROW_AC            (TBM_ROW_APP + 9)

// For storing the active connections
#define SP_RSSI_TRACK_CHNLS        1            // Max possible channels can be GAP_BONDINGS_MAX
#define SP_MAX_RSSI_STORE_DEPTH    5
#define SP_INVALID_HANDLE          0xFFFF
#define RSSI_2M_THRSHLD           -30           
#define RSSI_1M_THRSHLD           -40           
#define RSSI_S2_THRSHLD           -50           
#define RSSI_S8_THRSHLD           -60           
#define SP_PHY_NONE                LL_PHY_NONE  // No PHY set
#define AUTO_PHY_UPDATE            0xFF

// Spin if the expression is not true
#define SIMPLEPERIPHERAL_ASSERT(expr) if (!(expr)) simple_peripheral_spin();

/*********************************************************************
 * TYPEDEFS
 */

// Auto connect availble groups
enum
{
  AUTOCONNECT_DISABLE = 0,              // Disable
  AUTOCONNECT_GROUP_A = 1,              // Group A
  AUTOCONNECT_GROUP_B = 2               // Group B
};


// App event passed from stack modules. This type is defined by the application
// since it can queue events to itself however it wants.
typedef struct
{
  uint8_t event;                // event type
  void    *pData;               // pointer to message
} spEvt_t;

// Container to store passcode data when passing from gapbondmgr callback
// to app event. See the pfnPairStateCB_t documentation from the gapbondmgr.h
// header file for more information on each parameter.
typedef struct
{
  uint8_t state;
  uint16_t connHandle;
  uint8_t status;
} spPairStateData_t;

// Container to store passcode data when passing from gapbondmgr callback
// to app event. See the pfnPasscodeCB_t documentation from the gapbondmgr.h
// header file for more information on each parameter.
typedef struct
{
  uint8_t deviceAddr[B_ADDR_LEN];
  uint16_t connHandle;
  uint8_t uiInputs;
  uint8_t uiOutputs;
  uint32_t numComparison;
} spPasscodeData_t;

// Container to store advertising event data when passing from advertising
// callback to app event. See the respective event in GapAdvScan_Event_IDs
// in gap_advertiser.h for the type that pBuf should be cast to.
typedef struct
{
  uint32_t event;
  void *pBuf;
} spGapAdvEventData_t;

// Container to store information from clock expiration using a flexible array
// since data is not always needed
typedef struct
{
  uint8_t event;                //
  uint8_t data[];
} spClockEventData_t;

// List element for parameter update and PHY command status lists
typedef struct
{
  List_Elem elem;
  uint16_t  connHandle;
} spConnHandleEntry_t;

// Connected device information
typedef struct
{
  uint16_t         	    connHandle;                        // Connection Handle
  spClockEventData_t*   pParamUpdateEventData;
  Clock_Struct*    	    pUpdateClock;                      // pointer to clock struct
  int8_t           	    rssiArr[SP_MAX_RSSI_STORE_DEPTH];
  uint8_t          	    rssiCntr;
  int8_t           	    rssiAvg;
  bool             	    phyCngRq;                          // Set to true if PHY change request is in progress
  uint8_t          	    currPhy;
  uint8_t          	    rqPhy;
  uint8_t          	    phyRqFailCnt;                      // PHY change request count
  bool             	    isAutoPHYEnable;                   // Flag to indicate auto phy change
} spConnRec_t;

/*********************************************************************
 * GLOBAL VARIABLES
 */

// Display Interface
Display_Handle dispHandle = NULL;

// Task configuration
Task_Struct spTask;
#if defined __TI_COMPILER_VERSION__
#pragma DATA_ALIGN(spTaskStack, 8)
#else
#pragma data_alignment=8
#endif
uint8_t spTaskStack[SP_TASK_STACK_SIZE];

#define APP_EVT_EVENT_MAX 0x9
char *appEventStrings[] = {
  "APP_STATE_CHANGE_EVT     ",
  "APP_CHAR_CHANGE_EVT      ",
  "APP_KEY_CHANGE_EVT       ",
  "APP_ADV_EVT              ",
  "APP_PAIR_STATE_EVT       ",
  "APP_PASSCODE_EVT         ",
  "APP_READ_RPA_EVT         ",
  "APP_PERIODIC_EVT         ",
  "APP_SEND_PARAM_UPDATE_EVT",
  "APP_CONN_EVT             ",
};

/*********************************************************************
 * LOCAL VARIABLES
 */

// Entity ID globally used to check for source and/or destination of messages
static ICall_EntityID selfEntity;

// Event globally used to post local events and pend on system and
// local events.
static ICall_SyncHandle syncEvent;

// Queue object used for app messages
static Queue_Struct appMsgQueue;
static Queue_Handle appMsgQueueHandle;

// Clock instance for internal periodic events. Only one is needed since
// GattServApp will handle notifying all connected GATT clients
static Clock_Struct clkPeriodic;
// Clock instance for RPA read events.
static Clock_Struct clkRpaRead;

// Memory to pass periodic event ID to clock handler
spClockEventData_t argPeriodic =
{ .event = SP_PERIODIC_EVT };

// Memory to pass RPA read event ID to clock handler
spClockEventData_t argRpaRead =
{ .event = SP_READ_RPA_EVT };

// Per-handle connection info
static spConnRec_t connList[MAX_NUM_BLE_CONNS];

// Current connection handle as chosen by menu
static uint16_t menuConnHandle = LINKDB_CONNHANDLE_INVALID;

// List to store connection handles for set phy command status's
static List_List setPhyCommStatList;

// List to store connection handles for queued param updates
static List_List paramUpdateList;

// Auto connect Disabled/Enabled {0 - Disabled, 1- Group A , 2-Group B, ...}
uint8_t autoConnect = AUTOCONNECT_DISABLE;

// Advertising handles
static uint8 advHandleLegacy;
static uint8 advHandleLongRange;

// Address mode
static GAP_Addr_Modes_t addrMode = DEFAULT_ADDRESS_MODE;

// Current Random Private Address
static uint8 rpa[B_ADDR_LEN] = {0};

// Test data used with Edge Impulse inferencing
static float test_fall_buf[400] = {
                          -0.5435, 9.0734, 2.6450, -0.5112, 9.0321, 2.7168,
 -0.5249, 8.9950, 2.5450, -0.3962, 9.0734, 2.4876, -0.3962, 9.1249, 2.3158, -0.2340,
 9.1692, 2.2272, -0.3118, 9.0369, 2.2296, -0.3220, 9.1800, 2.1189, -0.3639, 9.2165,
 2.1859, -0.4956, 9.2470, 2.1566, -0.5698, 9.2177, 2.3140, -0.5549, 9.2446, 2.2679,
 -0.4854, 9.1051, 2.2841, -0.3837, 9.1572, 2.2948, -0.4896, 9.1273, 2.1847, -0.5016,
 8.9848, 2.1691, -0.4076, 9.0627, 2.1422, -0.3968, 9.0351, 2.2470, -0.5531, 8.9699,
 2.2027, -0.7159, 9.0357, 2.2248, -0.5698, 9.1566, 2.0710, -0.4310, 9.0561, 2.3385,
 -0.3723, 8.9729, 2.5947, -0.4567, 8.8813, 2.6348, -0.4567, 8.9537, 2.5468, -0.6093,
 9.1063, 2.3918, -0.6303, 9.2302, 2.1302, -0.5405, 9.2907, 2.1901, -0.5142, 9.2632,
 2.2745, -0.5052, 9.3158, 2.2236, -0.5195, 9.1997, 2.3589, -0.5608, 9.4020, 2.4738,
 -0.5980, 9.3709, 2.4840, -0.5303, 9.3937, 2.3224, -0.4741, 9.3482, 2.3948, -0.4902,
 9.2554, 2.2505, -0.6668, 9.2805, 2.2553, -0.6692, 9.1919, 2.1590, -0.6895, 9.1692,
 2.3200, -0.5710, 9.1788, 2.3014, -0.6207, 9.0818, 2.3266, -0.6835, 9.0513, 2.4187,
 -0.5842, 9.0764, 2.4092, -0.6015, 9.0154, 2.4008, -0.6165, 9.0202, 2.3703, -0.5854,
 9.1195, 2.3583, -0.6668, 9.1710, 2.4379, -0.7326, 9.2213, 2.1787, -0.5848, 9.1967,
 2.0686, -0.5399, 9.2087, 2.2571, -0.6279, 9.1698, 2.2589, -0.6740, 9.1554, 2.2936,
 -0.5429, 9.0884, 2.1889, -0.5453, 9.0830, 2.2793, -0.5333, 9.2117, 2.3750, -0.6865,
 9.1309, 2.4900, -0.7374, 9.1710, 2.3248, -0.7446, 9.2147, 2.2230, -0.6399, 9.2099,
 2.2715, -0.5644, 9.1794, 2.1069, -0.6189, 9.2093, 2.1925, -0.5944, 9.1614, 2.0913,
 -0.5638, 9.2344, 2.2661, -0.5213, 9.2464, 2.2697, -0.4705, 9.2398, 2.3056, -0.4741,
 9.2410, 2.3152, -0.5632, 9.2781, 2.3320, -0.5830, 9.2973, 2.3343, -0.6015, 9.3428,
 2.1620, -0.5932, 9.4314, 2.2517, -0.6512, 9.3637, 2.2434, -0.7602, 9.2506, 2.1614,
 -0.6680, 9.2973, 2.0111, -0.7506, 9.3877, 1.9567, -0.6770, 9.4122, 2.1506, -0.7111,
 9.2979, 2.2817, -0.7841, 9.1243, 2.1913, -0.6901, 8.9956, 2.2721, -0.6746, 9.0208,
 2.2081, -0.7398, 9.1351, 2.0931, -0.6931, 9.1111, 1.9088, -0.5932, 9.1764, 1.9309,
 -0.8128, 9.2883, 1.8142, -0.8505, 9.3919, 1.9363, -0.8015, 9.2865, 1.9782, -0.8116,
 9.3865, 2.0979, -0.6979, 9.1955, 2.2104, -0.6518, 9.1554, 2.2715, -0.4932, 9.2362,
 2.2015, -0.6387, 9.2051, 2.1548, -0.7183, 9.2590, 2.0824, -0.7823, 9.2638, 2.0536,
 -0.8110, 9.2853, 1.9740, -0.6895, 9.2686, 2.0919, -0.6931, 9.0806, 2.1392, -0.6590,
 9.0644, 2.1907, -0.5704, 8.9501, 2.2517, -0.4094, 8.9274, 2.2823, -0.4657, 8.8651,
 2.3212, -0.5567, 8.8160, 2.1344, -0.4980, 8.8041, 2.0369, -0.4543, 8.8184, 2.0578,
 -0.4202, 8.7400, 1.9196, -0.3274, 8.6197, 2.0327, -0.1808, 8.4851, 2.1638, -0.0365,
 8.3797, 2.3565, 0.0108, 8.4336, 2.3397, 0.0599, 8.5575, 2.2763, 0.1107, 8.5108,
 2.2248, 0.1395, 8.4653, 2.2290, 0.3615, 8.4845, 2.1685, 0.3801, 8.5174, 2.1183,
 0.5303, 8.5611, 2.1434, 0.7679, 8.6084, 2.1428, 1.0349, 8.6856, 2.2362, 1.3707,
 8.7508, 2.3733, 1.3635, 8.7376, 2.5115, 1.2917, 8.7197, 2.4086, 1.2007, 8.9274,
 2.3134, 1.0738, 8.8238, 2.0327, 0.6794, 8.9615, 1.9567, 0.6381, 8.9992, 1.8920,
 0.4220, 8.8837, 1.8112, 0.3238, 8.7113, 1.5508, 0.6171, 8.7747, 1.6897, 0.7482,
 8.5880, 1.7986, 0.9206, 8.3683, 1.9118, 0.9170, 8.2708, 2.1261, 0.9403, 8.1995,
 2.1745, 0.9511, 8.0882, 2.0488, 0.7691, 8.2031, 2.0339, 0.5339, 8.2313, 1.9070,
 0.2269, 8.2636, 1.6562, 0.0036, 8.2175, 1.6149, -0.1742, 8.2055, 1.6682, -0.4154,
 7.9697, 1.5628, -0.4681, 7.8135, 1.7520, -0.3160, 7.5627, 1.7406, -0.0682, 7.4059,
 1.8783, 0.1437, 7.3412, 1.9896, 0.2269, 7.0030, 1.9231, 0.3681, 6.8905, 1.9944,
 0.2544, 7.1204, 1.8777, 0.0742, 7.0342, 1.7328, -0.0569, 6.9462, 1.6215, -0.4782,
 6.9713, 1.4299, -0.7302, 6.8337, 1.2246, -1.1085, 6.8815, 1.0852, -1.2713, 6.7098,
 0.9667, -1.3174, 6.6086, 0.9068, -1.3641, 6.1699, 0.7943, -1.1031, 6.0591, 0.6985,
 -1.0241, 5.4887, 0.8009, -0.9002, 5.1002, 0.7751, -1.0936, 4.3251, 0.6925, -0.7943,
 4.3610, 0.8613, -0.8140, 3.8277, 0.7925, -0.9301, 3.5979, 0.6069, -1.0714, 3.4291,
 0.0132, -1.2701, 3.6087, -0.6991, -1.2306, 3.6901, -1.2707, -1.2653, 3.9612, -1.7490,
 -1.2929, 4.3946, -2.1308, -1.0523, 4.8064, -2.2104, -0.8260, 4.8117, -2.1135, -1.5556,
 4.7836, -2.1255, -2.4313, 5.9047, -1.8154, -3.1406, 6.6116, -1.2785, -3.5099, 7.0767,
 -0.8697, -3.6679, 7.2556, -0.7757, -3.5979, 7.1635, -1.2408, -3.4961, 7.5447, -2.1907,
 -3.3848, 8.0571, -2.8144, -3.3674, 8.6999, -3.2004, -3.4081, 8.9944, -3.1777, -2.8898,
 9.3739, -2.6618, -2.7031, 9.3601, -2.2829, -2.9149, 9.6127, -1.7131, -3.1035, 9.9294,
 -1.8328, -2.6294, 9.8677, -2.4648, -3.5338, 10.5518, -3.0089, -3.8535, 10.9517,
 -3.5949, -4.2455, 10.9547, -3.7942, -4.5370, 11.1803, -2.9562, -4.1976, 10.8433,
 -1.8783, -3.8337, 10.2017, -1.1408, -3.2334, 9.9509, -0.8817, -2.7354, 9.9557, -1.4916,
 -2.2895, 9.8468, -2.5923, -2.6899, 10.2089, -3.3417, -2.2972, 10.0730, -3.8205,
 -2.5702, 10.2777, -3.6320, -2.2559, 9.8168, -3.4147, -1.9944, 9.4092, -3.0263, -1.5377,
 8.8579, -2.2440, -0.8224, 8.2995, -1.4832, 0.3807, 8.0325, -0.7512, 1.3485, 7.7004,
 -0.4046, 1.9716, 7.7327, -0.4136, 1.9429, 7.8973, -1.0283, 0.7889, 8.5144, -2.1422,
 -1.5975, 8.9872, -4.1905, -3.2178, 9.1997, -7.4771, -2.1793, 9.7749, -10.4232, -0.0658,
 9.5289, -10.9313, 2.8042, 8.1373, -8.9477, 5.2182, 7.3101, -4.7734, 6.1292, 6.2914,
 -0.4136, 4.4903, 5.6916, 2.1859, 0.6853, 6.9390, 1.0020, -1.7968, 9.0327, -2.9000,
 -3.1029, 9.6606, -7.4400, -5.3157, 12.5690, -10.7290, -9.0028, 13.3806, -11.4072,
 -11.1558, 14.3850, -8.7538, -12.6743, 14.5795, -5.0853, -14.1982, 13.1053, -2.2266,
 -14.2563, 14.3018, -1.5856, -13.6374, 13.3944, -3.6787, -11.9908, 13.0083, -7.3095,
 -10.7781, 11.1318, -7.8446, -10.8350, 7.5759, -8.3294, -8.6580, 5.6216, -7.2616,
 -7.8033, 2.9724, -7.8506, -3.2651, 0.5160, -6.8486, 0.4046, 0.6015, -5.4654, 0.2005,
 0.7021, -5.4265, 2.1763, 3.3914, -5.2912, 1.7172, 3.1196, -5.6348, 0.2143, 3.4255,
 -4.3209, -2.3924, 3.5644, -3.4482, -8.9280, 5.3044, -2.3577, -14.6519, 8.8053, 0.2011,
 -17.9110, 10.6578, 1.5071, -19.1369, 11.4539, 1.6113, -19.5720, 11.0989, 1.9148,
 -16.9623, 9.4595, 3.3447, -12.9508, 5.5803, 1.4437, -7.8907, 2.8808, 0.2741, -6.4979,
 0.6692, 0.2801, -7.6166, 1.0600, 0.5022, -8.6700, 1.9124, 1.2623, -11.0738, 2.9155,
 4.0965, -12.4103, 4.3628, 5.9268, -13.7828, 5.2798, 7.8865, -13.3680, 5.9256, 7.5519,
 -14.4394, 6.7068, 7.0719, -13.5338, 6.5781, 5.2247, -15.6240, 7.5878, 4.0384, -14.7399,
 7.4232, 1.9184, -15.8765, 7.5346, 1.8645, -15.1487, 6.7169, 1.4281, -14.8943, 5.0105,
 2.1650, -13.7559, 3.0544, 2.0590, -12.6372, 1.2288, 2.5917, -11.9914, -0.5614, 2.6163,
 -10.9954, -1.4150, 2.4906, -10.8128, -1.9836, 2.5331, -10.1053, -2.1781, 2.4822,
 -9.8898, -1.9243, 1.9309, -9.1075, -1.6472, 1.6682, -8.6664, -1.0307, 1.3791, -8.1768,
 -0.7506, 1.1313, -8.0397, -0.5776, 1.1019, -8.5886, 0.0239, 1.3096, -8.8125, 0.5519,
 0.8266, -9.0896, 1.4581, 0.7829, -9.5864, 1.7274, 0.6506, -9.9419, 2.2924, 0.6829,
 -10.1987, 2.4672, 1.0157, -10.0670, 2.3212, 1.3174, -9.8360, 2.2278, 0.9924, -9.7241,
 1.9650, 0.8200, -9.4936, 2.1602, 0.4860, -9.3087, 1.9483, 0.2376, -8.9944, 1.8561,
 0.0850, -8.6808, 1.5562, 0.2532, -8.6359, 1.5927, 0.6829, -8.6485, 1.4156, 1.0989,
 -8.3677, 1.2432, 1.0792, -8.3049, 1.2857, 0.7236, -8.6508, 1.4234, 0.3633, -8.7532,
 1.6568, 0.1640, -8.7915, 1.9674, 0.1712, -8.6550, 2.1578, 0.0748, -8.7987, 2.5486,
 0.2245, -9.6235, 3.1460, 0.3071, -9.7067, 3.0556, 0.2981, -10.0281, 3.1520, 0.2729,
 -10.1807, 4.0211, 0.1790, -10.3986, 4.0977, 0.0443, -10.3382, 4.2329, -0.3400, -10.8876,
 4.7908, -0.5237, -10.8505, 4.6214, -0.9020, -10.3005, 4.0330, -1.0044, -9.7642,
 3.5608, -0.9343, -9.4703, 3.0143, -0.4136, -9.1955, 2.4427, 0.2239, -8.8202, 1.8932,
 0.8404, -8.4192, 1.6592, 0.9643, -8.2337, 1.6376, 0.6357, -8.1301, 1.8346, -0.1227,
 -8.3683, 2.0728, -0.6291, -8.5359, 2.4547, -0.5812, -8.7628, 2.6899, -0.4998, -8.8484,
 2.8964, -0.2376, -8.8615, 2.9503, -0.1478, -8.7658, 3.0424, -0.1909, -8.6359, 3.1071,
 -0.0784, -8.7514, 3.1603, -0.1472, -8.6874, 3.2675, -0.2053, -8.5880, 3.2040, -0.0976,
 -8.6820, 3.1957, -0.3513, -8.6526, 3.1143, -0.3208, -8.6012, 3.0670, -0.3992, -8.7035,
 2.9066, -0.4818, -8.7424, 2.8754, -0.5207, -8.6389, 2.8036, -0.5321, -8.7376, 2.7432,
 -0.2508, -8.9034, 2.8180, -0.1431, -9.0543, 2.6911, -0.1383, -9.0920, 2.9251, -0.0611,
 -9.2985, 2.8443, -0.1389, -9.1207, 2.9754, -0.2915, -9.2602, 2.8946, -0.2897, -9.1866,
 2.8317, -0.2149, -9.2799, 2.7336, -0.0754, -9.5229, 2.9113, -0.0557, -9.6798, 2.9323,
 -0.1431, -9.7127, 2.8916, -0.2496, -9.3296, 2.8228, -0.4118, -9.0794, 2.6342, -0.3549,
 -9.0261, 2.6294, -0.1670, -9.0220, 2.5408, 0.0820, -9.1117, 2.7940, 0.1371, -9.3128,
 2.9359, 0.2137, -9.6540, 3.0442, 0.1856, -9.6552, 3.1412, 0.1562, -9.3733, 3.0556,
 0.1203, -9.0974, 2.9431, 0.1454, -8.6646, 2.8335, 0.0204, -8.5401, 2.6671, -0.0090,
 -8.4581, 2.6019, 0.0449, -8.3234, 2.5361, 0.1065, -8.1924, 2.4618, 0.3466, -8.2067,
 2.6438, 0.3831, -8.3115, 2.7216, 0.4429, -8.5168, 2.7599, 0.3747, -8.7011, 2.7785,
 0.3717, -8.9118, 3.0143, 0.4100, -8.8873, 2.9910, 0.4046, -8.8466, 2.9832, 0.1951,
 -8.6814, 2.9939, 0.2987, -8.7825, 2.9742, 0.3400, -8.7742, 2.8114, 0.3765, -8.9304,
 2.8371, 0.3597, -8.9495, 2.9383, 0.4148, -9.0315, 2.9562, 0.5890, -8.8633, 2.9957,
 0.6141, -8.9178, 3.1029, 0.4304, -8.7514, 3.0939, 0.3998, -8.7891, 3.1220, 0.2149,
 -8.9370, 3.0712, 0.2358, -8.9896, 2.9724, 0.3226, -8.9890, 3.0502, 0.4633, -8.7939,
 3.0317, 0.5920, -8.7939, 2.8868, 0.7739, -8.8394, 3.0544, 0.8278, -8.7317, 3.0508,
 0.6381, -8.8340, 3.1406, 0.6440, -8.6856, 2.9820, 0.5465, -8.6712, 2.9862, 0.5393,
 -8.4994, 2.9951, 0.6452, -8.3486, 2.9945, 0.5124, -8.3588, 2.9550, 0.6345, -8.5874,
 2.9269, 0.7129, -8.7508, 2.7874, 0.8631, -8.7161, 2.9592, 0.8164, -8.4222, 2.9467,
 0.8523, -8.3540, 2.9718, 0.6045, -8.2588, 2.8910, 0.6470, -8.3845, 2.9125, 0.4429,
 -8.5275, 3.1113, 0.5357, -8.5796, 3.1705, 0.6937, -8.6257, 3.2238, 0.7213, -8.6796,
 3.2591, 0.8511, -8.7436, 3.1903, 0.9445, -8.5790, 3.1861, 0.9691, -8.4851, 3.2968,
 0.8841, -8.2444, 3.3196, 0.8428, -8.2313, 3.2286, 0.6979, -8.2432, 3.2974, 0.6488,
 -8.2863, 3.2555, 0.6644, -8.3420, 3.3531, 0.5902, -8.4102, 3.3411, 0.7278, -8.4988,
 3.3866, 0.7991, -8.5228, 3.4931, 0.9134, -8.6095, 3.4411, 1.0900, -8.7969, 3.5075,
 1.0882, -8.9352, 3.4991, 1.0983
};

/*********************************************************************
 * LOCAL FUNCTIONS
 */

static void SimplePeripheral_init( void );
static void SimplePeripheral_taskFxn(UArg a0, UArg a1);

static uint8_t SimplePeripheral_processStackMsg(ICall_Hdr *pMsg);
static uint8_t SimplePeripheral_processGATTMsg(gattMsgEvent_t *pMsg);
static void SimplePeripheral_processGapMessage(gapEventHdr_t *pMsg);
static void SimplePeripheral_advCallback(uint32_t event, void *pBuf, uintptr_t arg);
static void SimplePeripheral_processAdvEvent(spGapAdvEventData_t *pEventData);
static void SimplePeripheral_processAppMsg(spEvt_t *pMsg);
static void SimplePeripheral_processCharValueChangeEvt(uint8_t paramId);
static void SimplePeripheral_performPeriodicTask(void);
static void SimplePeripheral_updateRPA(void);
static void SimplePeripheral_clockHandler(UArg arg);
static void SimplePeripheral_passcodeCb(uint8_t *pDeviceAddr, uint16_t connHandle,
                                        uint8_t uiInputs, uint8_t uiOutputs,
                                        uint32_t numComparison);
static void SimplePeripheral_pairStateCb(uint16_t connHandle, uint8_t state,
                                         uint8_t status);
static void SimplePeripheral_processPairState(spPairStateData_t *pPairState);
static void SimplePeripheral_processPasscode(spPasscodeData_t *pPasscodeData);
static void SimplePeripheral_charValueChangeCB(uint8_t paramId);
static status_t SimplePeripheral_enqueueMsg(uint8_t event, void *pData);
static void SimplePeripheral_keyChangeHandler(uint8 keys);
static void SimplePeripheral_handleKeys(uint8_t keys);
static void SimplePeripheral_processCmdCompleteEvt(hciEvt_CmdComplete_t *pMsg);
static void SimplePeripheral_initPHYRSSIArray(void);
static void SimplePeripheral_updatePHYStat(uint16_t eventCode, uint8_t *pMsg);
static uint8_t SimplePeripheral_addConn(uint16_t connHandle);
static uint8_t SimplePeripheral_getConnIndex(uint16_t connHandle);
static uint8_t SimplePeripheral_removeConn(uint16_t connHandle);
static void SimplePeripheral_processParamUpdate(uint16_t connHandle);
static status_t SimplePeripheral_startAutoPhyChange(uint16_t connHandle);
static status_t SimplePeripheral_stopAutoPhyChange(uint16_t connHandle);
static status_t SimplePeripheral_setPhy(uint16_t connHandle, uint8_t allPhys,
                                        uint8_t txPhy, uint8_t rxPhy,
                                        uint16_t phyOpts);
static uint8_t SimplePeripheral_clearConnListEntry(uint16_t connHandle);
static void SimplePeripheral_menuSwitchCb(tbmMenuObj_t* pMenuObjCurr,
                                          tbmMenuObj_t* pMenuObjNext);
static void SimplePeripheral_connEvtCB(Gap_ConnEventRpt_t *pReport);
static void SimplePeripheral_processConnEvt(Gap_ConnEventRpt_t *pReport);
#ifdef PTM_MODE
void simple_peripheral_handleNPIRxInterceptEvent(uint8_t *pMsg);  // Declaration
static void simple_peripheral_sendToNPI(uint8_t *buf, uint16_t len);  // Declaration
#endif // PTM_MODE

/*********************************************************************
 * EXTERN FUNCTIONS
 */
extern void AssertHandler(uint8 assertCause, uint8 assertSubcause);

/// Note: (Edge Impulse): Extern EI CPP function call
extern void ei_init(void);
extern uint16_t ei_infer(int16_t *data, size_t data_len);

/*********************************************************************
 * PROFILE CALLBACKS
 */

// GAP Bond Manager Callbacks
static gapBondCBs_t SimplePeripheral_BondMgrCBs =
{
  SimplePeripheral_passcodeCb,       // Passcode callback
  SimplePeripheral_pairStateCb       // Pairing/Bonding state Callback
};

// Simple GATT Profile Callbacks
static simpleProfileCBs_t SimplePeripheral_simpleProfileCBs =
{
  SimplePeripheral_charValueChangeCB // Simple GATT Characteristic value change callback
};

/*********************************************************************
 * PUBLIC FUNCTIONS
 */

/*********************************************************************
 * @fn      simple_peripheral_spin
 *
 * @brief   Spin forever
 *
 * @param   none
 */
static void simple_peripheral_spin(void)
{
  volatile uint8_t x = 0;

  while(1)
  {
    x++;
  }
}

#ifdef PTM_MODE
/*********************************************************************
* @fn      simple_peripheral_handleNPIRxInterceptEvent
*
* @brief   Intercept an NPI RX serial message and queue for this application.
*
* @param   pMsg - a NPIMSG_msg_t containing the intercepted message.
*
* @return  none.
*/
void simple_peripheral_handleNPIRxInterceptEvent(uint8_t *pMsg)
{
 // Send Command via HCI TL
 HCI_TL_SendToStack(((NPIMSG_msg_t *)pMsg)->pBuf);

 // The data is stored as a message, free this first.
 ICall_freeMsg(((NPIMSG_msg_t *)pMsg)->pBuf);

 // Free container.
 ICall_free(pMsg);
}

/*********************************************************************
* @fn      simple_peripheral_sendToNPI
*
* @brief   Create an NPI packet and send to NPI to transmit.
*
* @param   buf - pointer HCI event or data.
*
* @param   len - length of buf in bytes.
*
* @return  none
*/
static void simple_peripheral_sendToNPI(uint8_t *buf, uint16_t len)
{
 npiPkt_t *pNpiPkt = (npiPkt_t *)ICall_allocMsg(sizeof(npiPkt_t) + len);

 if (pNpiPkt)
 {
   pNpiPkt->hdr.event = buf[0]; //Has the event status code in first byte of payload
   pNpiPkt->hdr.status = 0xFF;
   pNpiPkt->pktLen = len;
   pNpiPkt->pData  = (uint8 *)(pNpiPkt + 1);

   memcpy(pNpiPkt->pData, buf, len);

   // Send to NPI
   // Note: there is no need to free this packet.  NPI will do that itself.
   NPITask_sendToHost((uint8_t *)pNpiPkt);
 }
}
#endif // PTM_MODE

/*********************************************************************
 * @fn      SimplePeripheral_createTask
 *
 * @brief   Task creation function for the Simple Peripheral.
 */
void SimplePeripheral_createTask(void)
{
  Task_Params taskParams;

  // Configure task
  Task_Params_init(&taskParams);
  taskParams.stack = spTaskStack;
  taskParams.stackSize = SP_TASK_STACK_SIZE;
  taskParams.priority = SP_TASK_PRIORITY;

  Task_construct(&spTask, SimplePeripheral_taskFxn, &taskParams, NULL);
}

/*********************************************************************
 * @fn      SimplePeripheral_init
 *
 * @brief   Called during initialization and contains application
 *          specific initialization (ie. hardware initialization/setup,
 *          table initialization, power up notification, etc), and
 *          profile initialization/setup.
 */
static void SimplePeripheral_init(void)
{
  BLE_LOG_INT_TIME(0, BLE_LOG_MODULE_APP, "APP : ---- init ", SP_TASK_PRIORITY);
  // Create the menu
  SimplePeripheral_buildMenu();

  // ******************************************************************
  // N0 STACK API CALLS CAN OCCUR BEFORE THIS CALL TO ICall_registerApp
  // ******************************************************************
  // Register the current thread as an ICall dispatcher application
  // so that the application can send and receive messages.
  ICall_registerApp(&selfEntity, &syncEvent);

#ifdef USE_RCOSC
  // Set device's Sleep Clock Accuracy
#if ( HOST_CONFIG & ( CENTRAL_CFG | PERIPHERAL_CFG ) )
  HCI_EXT_SetSCACmd(500);
#endif // (CENTRAL_CFG | PERIPHERAL_CFG)
  RCOSC_enableCalibration();
#endif // USE_RCOSC

  // Create an RTOS queue for message from profile to be sent to app.
  appMsgQueueHandle = Util_constructQueue(&appMsgQueue);

  // Create one-shot clock for internal periodic events.

  Util_constructClock(&clkPeriodic, SimplePeripheral_clockHandler,
                      SP_PERIODIC_EVT_PERIOD, 0, true, (UArg)&argPeriodic);

  // Set the Device Name characteristic in the GAP GATT Service
  // For more information, see the section in the User's Guide:
  // http://software-dl.ti.com/lprf/ble5stack-latest/
  GGS_SetParameter(GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN, attDeviceName);

  // Configure GAP
  {
    uint16_t paramUpdateDecision = DEFAULT_PARAM_UPDATE_REQ_DECISION;

    // Pass all parameter update requests to the app for it to decide
    GAP_SetParamValue(GAP_PARAM_LINK_UPDATE_DECISION, paramUpdateDecision);
  }

  // Setup the GAP Bond Manager. For more information see the GAP Bond Manager
  // section in the User's Guide
  setBondManagerParameters();

  // Initialize GATT attributes
  GGS_AddService(GATT_ALL_SERVICES);           // GAP GATT Service
  GATTServApp_AddService(GATT_ALL_SERVICES);   // GATT Service
  DevInfo_AddService();                        // Device Information Service
  SimpleProfile_AddService(GATT_ALL_SERVICES); // Simple GATT Profile

  // Setup the SimpleProfile Characteristic Values
  // For more information, see the GATT and GATTServApp sections in the User's Guide:
  // http://software-dl.ti.com/lprf/ble5stack-latest/
  {
    uint8_t charValue1 = 1;
    uint8_t charValue2 = 2;
    uint8_t charValue3 = 3;
    uint8_t charValue4 = 4;
    uint8_t charValue5[SIMPLEPROFILE_CHAR5_LEN] = { 1, 2, 3, 4, 5 };

    SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR1, sizeof(uint8_t),
                               &charValue1);
    SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR2, sizeof(uint8_t),
                               &charValue2);
    SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR3, sizeof(uint8_t),
                               &charValue3);
    SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR4, sizeof(uint8_t),
                               &charValue4);
    SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR5, SIMPLEPROFILE_CHAR5_LEN,
                               charValue5);
  }

  // Register callback with SimpleGATTprofile
  SimpleProfile_RegisterAppCBs(&SimplePeripheral_simpleProfileCBs);

  // Start Bond Manager and register callback
  VOID GAPBondMgr_Register(&SimplePeripheral_BondMgrCBs);

  // Register with GAP for HCI/Host messages. This is needed to receive HCI
  // events. For more information, see the HCI section in the User's Guide:
  // http://software-dl.ti.com/lprf/ble5stack-latest/
  GAP_RegisterForMsgs(selfEntity);

  // Register for GATT local events and ATT Responses pending for transmission
  GATT_RegisterForMsgs(selfEntity);

  // Set default values for Data Length Extension
  // Extended Data Length Feature is already enabled by default
  {
    // Set initial values to maximum, RX is set to max. by default(251 octets, 2120us)
    // Some brand smartphone is essentially needing 251/2120, so we set them here.
    #define APP_SUGGESTED_PDU_SIZE 251 //default is 27 octets(TX)
    #define APP_SUGGESTED_TX_TIME 2120 //default is 328us(TX)

    // This API is documented in hci.h
    // See the LE Data Length Extension section in the BLE5-Stack User's Guide for information on using this command:
    // http://software-dl.ti.com/lprf/ble5stack-latest/
    HCI_LE_WriteSuggestedDefaultDataLenCmd(APP_SUGGESTED_PDU_SIZE, APP_SUGGESTED_TX_TIME);
  }

  // Initialize GATT Client
  GATT_InitClient("");

  // Init key debouncer
  Board_initKeys(SimplePeripheral_keyChangeHandler);

  // Initialize Connection List
  SimplePeripheral_clearConnListEntry(LINKDB_CONNHANDLE_ALL);

  BLE_LOG_INT_TIME(0, BLE_LOG_MODULE_APP, "APP : ---- call GAP_DeviceInit", GAP_PROFILE_PERIPHERAL);
  //Initialize GAP layer for Peripheral role and register to receive GAP events
  GAP_DeviceInit(GAP_PROFILE_PERIPHERAL, selfEntity, addrMode, &pRandomAddress);

  // Initialize array to store connection handle and RSSI values
  SimplePeripheral_initPHYRSSIArray();

  // The type of display is configured based on the BOARD_DISPLAY_USE...
  // preprocessor definitions
  dispHandle = Display_open(Display_Type_ANY, NULL);

  // Initialize Two-Button Menu module
  TBM_SET_TITLE(&spMenuMain, "Simple Peripheral");
  tbm_setItemStatus(&spMenuMain, TBM_ITEM_NONE, TBM_ITEM_ALL);

  tbm_initTwoBtnMenu(dispHandle, &spMenuMain, 5, SimplePeripheral_menuSwitchCb);
  Display_printf(dispHandle, SP_ROW_SEPARATOR_1, 0, "====================");

#ifdef PTM_MODE
  // Intercept NPI RX events.
  NPITask_registerIncomingRXEventAppCB(simple_peripheral_handleNPIRxInterceptEvent, INTERCEPT);

  // Register for Command Status information
  HCI_TL_Init(NULL, (HCI_TL_CommandStatusCB_t) simple_peripheral_sendToNPI, NULL, selfEntity);

  // Register for Events
  HCI_TL_getCmdResponderID(ICall_getLocalMsgEntityId(ICALL_SERVICE_CLASS_BLE_MSG, selfEntity));

  // Inform Stack to Initialize PTM
  HCI_EXT_EnablePTMCmd();
#endif // PTM_MODE

  // NOTE(Edge Impulse): initialize fall detection resources
  ei_init();
}

/*********************************************************************
 * @fn      SimplePeripheral_taskFxn
 *
 * @brief   Application task entry point for the Simple Peripheral.
 *
 * @param   a0, a1 - not used.
 */
static void SimplePeripheral_taskFxn(UArg a0, UArg a1)
{
  // Initialize application
  SimplePeripheral_init();

  // Application main loop
  for (;;)
  {
    uint32_t events;

    // Waits for an event to be posted associated with the calling thread.
    // Note that an event associated with a thread is posted when a
    // message is queued to the message receive queue of the thread
    events = Event_pend(syncEvent, Event_Id_NONE, SP_ALL_EVENTS,
                        ICALL_TIMEOUT_FOREVER);

    if (events)
    {
      ICall_EntityID dest;
      ICall_ServiceEnum src;
      ICall_HciExtEvt *pMsg = NULL;

      // Fetch any available messages that might have been sent from the stack
      if (ICall_fetchServiceMsg(&src, &dest,
                                (void **)&pMsg) == ICALL_ERRNO_SUCCESS)
      {
        uint8 safeToDealloc = TRUE;

        if ((src == ICALL_SERVICE_CLASS_BLE) && (dest == selfEntity))
        {
          ICall_Stack_Event *pEvt = (ICall_Stack_Event *)pMsg;

          // Check for BLE stack events first
          if (pEvt->signature != 0xffff)
          {
            // Process inter-task message
            safeToDealloc = SimplePeripheral_processStackMsg((ICall_Hdr *)pMsg);
          }
        }

        if (pMsg && safeToDealloc)
        {
          ICall_freeMsg(pMsg);
        }
      }

      // If RTOS queue is not empty, process app message.
      if (events & SP_QUEUE_EVT)
      {
        while (!Queue_empty(appMsgQueueHandle))
        {
          spEvt_t *pMsg = (spEvt_t *)Util_dequeueMsg(appMsgQueueHandle);
          if (pMsg)
          {
            // Process message.
            SimplePeripheral_processAppMsg(pMsg);

            // Free the space from the message.
            ICall_free(pMsg);
          }
        }
      }
    }
  }
}

/*********************************************************************
 * @fn      SimplePeripheral_processStackMsg
 *
 * @brief   Process an incoming stack message.
 *
 * @param   pMsg - message to process
 *
 * @return  TRUE if safe to deallocate incoming message, FALSE otherwise.
 */
static uint8_t SimplePeripheral_processStackMsg(ICall_Hdr *pMsg)
{
  // Always dealloc pMsg unless set otherwise
  uint8_t safeToDealloc = TRUE;

  BLE_LOG_INT_INT(0, BLE_LOG_MODULE_APP, "APP : Stack msg status=%d, event=0x%x\n", pMsg->status, pMsg->event);

  switch (pMsg->event)
  {
    case GAP_MSG_EVENT:
      SimplePeripheral_processGapMessage((gapEventHdr_t*) pMsg);
      break;

    case GATT_MSG_EVENT:
      // Process GATT message
      safeToDealloc = SimplePeripheral_processGATTMsg((gattMsgEvent_t *)pMsg);
      break;

    case HCI_GAP_EVENT_EVENT:
    {
      // Process HCI message
      switch(pMsg->status)
      {
        case HCI_COMMAND_COMPLETE_EVENT_CODE:
        // Process HCI Command Complete Events here
        {
          SimplePeripheral_processCmdCompleteEvt((hciEvt_CmdComplete_t *) pMsg);
          break;
        }

        case HCI_BLE_HARDWARE_ERROR_EVENT_CODE:
          AssertHandler(HAL_ASSERT_CAUSE_HARDWARE_ERROR,0);
          break;

        // HCI Commands Events
        case HCI_COMMAND_STATUS_EVENT_CODE:
        {
          hciEvt_CommandStatus_t *pMyMsg = (hciEvt_CommandStatus_t *)pMsg;
          switch ( pMyMsg->cmdOpcode )
          {
            case HCI_LE_SET_PHY:
            {
              if (pMyMsg->cmdStatus == HCI_ERROR_CODE_UNSUPPORTED_REMOTE_FEATURE)
              {
                Display_printf(dispHandle, SP_ROW_STATUS_1, 0,
                        "PHY Change failure, peer does not support this");
              }
              else
              {
                Display_printf(dispHandle, SP_ROW_STATUS_1, 0,
                               "PHY Update Status Event: 0x%x",
                               pMyMsg->cmdStatus);
              }

              SimplePeripheral_updatePHYStat(HCI_LE_SET_PHY, (uint8_t *)pMsg);
              break;
            }

            default:
              break;
          }
          break;
        }

        // LE Events
        case HCI_LE_EVENT_CODE:
        {
          hciEvt_BLEPhyUpdateComplete_t *pPUC =
            (hciEvt_BLEPhyUpdateComplete_t*) pMsg;

          // A Phy Update Has Completed or Failed
          if (pPUC->BLEEventCode == HCI_BLE_PHY_UPDATE_COMPLETE_EVENT)
          {
            if (pPUC->status != SUCCESS)
            {
              Display_printf(dispHandle, SP_ROW_STATUS_1, 0,
                             "PHY Change failure");
            }
            else
            {
              // Only symmetrical PHY is supported.
              // rxPhy should be equal to txPhy.
              Display_printf(dispHandle, SP_ROW_STATUS_2, 0,
                             "PHY Updated to %s",
                             (pPUC->rxPhy == PHY_UPDATE_COMPLETE_EVENT_1M) ? "1M" :
                             (pPUC->rxPhy == PHY_UPDATE_COMPLETE_EVENT_2M) ? "2M" :
                             (pPUC->rxPhy == PHY_UPDATE_COMPLETE_EVENT_CODED) ? "CODED" : "Unexpected PHY Value");
            }

            SimplePeripheral_updatePHYStat(HCI_BLE_PHY_UPDATE_COMPLETE_EVENT, (uint8_t *)pMsg);
          }
          break;
        }

        default:
          break;
      }

      break;
    }

    default:
      // do nothing
      break;
  }

#ifdef PTM_MODE
  // Check for NPI Messages
  hciPacket_t *pBuf = (hciPacket_t *)pMsg;

  // Serialized HCI Event
  if (pBuf->hdr.event == HCI_CTRL_TO_HOST_EVENT)
  {
    uint16_t len = 0;

    // Determine the packet length
    switch(pBuf->pData[0])
    {
      case HCI_EVENT_PACKET:
        len = HCI_EVENT_MIN_LENGTH + pBuf->pData[2];
        break;

      case HCI_ACL_DATA_PACKET:
        len = HCI_DATA_MIN_LENGTH + BUILD_UINT16(pBuf->pData[3], pBuf->pData[4]);
        break;

      default:
        break;
    }

    // Send to Remote Host.
    simple_peripheral_sendToNPI(pBuf->pData, len);

    // Free buffers if needed.
    switch (pBuf->pData[0])
    {
      case HCI_ACL_DATA_PACKET:
      case HCI_SCO_DATA_PACKET:
        BM_free(pBuf->pData);
      default:
        break;
    }
  }
#endif // PTM_MODE

  return (safeToDealloc);
}

/*********************************************************************
 * @fn      SimplePeripheral_processGATTMsg
 *
 * @brief   Process GATT messages and events.
 *
 * @return  TRUE if safe to deallocate incoming message, FALSE otherwise.
 */
static uint8_t SimplePeripheral_processGATTMsg(gattMsgEvent_t *pMsg)
{
  if (pMsg->method == ATT_FLOW_CTRL_VIOLATED_EVENT)
  {
    // ATT request-response or indication-confirmation flow control is
    // violated. All subsequent ATT requests or indications will be dropped.
    // The app is informed in case it wants to drop the connection.

    // Display the opcode of the message that caused the violation.
    Display_printf(dispHandle, SP_ROW_STATUS_1, 0, "FC Violated: %d", pMsg->msg.flowCtrlEvt.opcode);
  }
  else if (pMsg->method == ATT_MTU_UPDATED_EVENT)
  {
    // MTU size updated
    Display_printf(dispHandle, SP_ROW_STATUS_1, 0, "MTU Size: %d", pMsg->msg.mtuEvt.MTU);
  }

  // Free message payload. Needed only for ATT Protocol messages
  GATT_bm_free(&pMsg->msg, pMsg->method);

  // It's safe to free the incoming message
  return (TRUE);
}

/*********************************************************************
 * @fn      SimplePeripheral_processAppMsg
 *
 * @brief   Process an incoming callback from a profile.
 *
 * @param   pMsg - message to process
 *
 * @return  None.
 */
static void SimplePeripheral_processAppMsg(spEvt_t *pMsg)
{
  bool dealloc = TRUE;

  if (pMsg->event <= APP_EVT_EVENT_MAX)
  {
    BLE_LOG_INT_STR(0, BLE_LOG_MODULE_APP, "APP : App msg status=%d, event=%s\n", 0, appEventStrings[pMsg->event]);
  }
  else
  {
    BLE_LOG_INT_INT(0, BLE_LOG_MODULE_APP, "APP : App msg status=%d, event=0x%x\n", 0, pMsg->event);
  }

  switch (pMsg->event)
  {
    case SP_CHAR_CHANGE_EVT:
      SimplePeripheral_processCharValueChangeEvt(*(uint8_t*)(pMsg->pData));
      break;

    case SP_KEY_CHANGE_EVT:
      SimplePeripheral_handleKeys(*(uint8_t*)(pMsg->pData));
      break;

    case SP_ADV_EVT:
      SimplePeripheral_processAdvEvent((spGapAdvEventData_t*)(pMsg->pData));
      break;

    case SP_PAIR_STATE_EVT:
      SimplePeripheral_processPairState((spPairStateData_t*)(pMsg->pData));
      break;

    case SP_PASSCODE_EVT:
      SimplePeripheral_processPasscode((spPasscodeData_t*)(pMsg->pData));
      break;

    case SP_PERIODIC_EVT:
      SimplePeripheral_performPeriodicTask();
      break;

    case SP_READ_RPA_EVT:
      SimplePeripheral_updateRPA();
      break;

    case SP_SEND_PARAM_UPDATE_EVT:
    {
      // Extract connection handle from data
      uint16_t connHandle = *(uint16_t *)(((spClockEventData_t *)pMsg->pData)->data);

      SimplePeripheral_processParamUpdate(connHandle);

      // This data is not dynamically allocated
      dealloc = FALSE;
      break;
    }

    case SP_CONN_EVT:
      SimplePeripheral_processConnEvt((Gap_ConnEventRpt_t *)(pMsg->pData));
      break;

    default:
      // Do nothing.
      break;
  }

  // Free message data if it exists and we are to dealloc
  if ((dealloc == TRUE) && (pMsg->pData != NULL))
  {
    ICall_free(pMsg->pData);
  }
}

/*********************************************************************
 * @fn      SimplePeripheral_processGapMessage
 *
 * @brief   Process an incoming GAP event.
 *
 * @param   pMsg - message to process
 */
static void SimplePeripheral_processGapMessage(gapEventHdr_t *pMsg)
{
  switch(pMsg->opcode)
  {
    case GAP_DEVICE_INIT_DONE_EVENT:
    {
      bStatus_t status = FAILURE;

      gapDeviceInitDoneEvent_t *pPkt = (gapDeviceInitDoneEvent_t *)pMsg;

      if(pPkt->hdr.status == SUCCESS)
      {
        // Store the system ID
        uint8_t systemId[DEVINFO_SYSTEM_ID_LEN];

        // use 6 bytes of device address for 8 bytes of system ID value
        systemId[0] = pPkt->devAddr[0];
        systemId[1] = pPkt->devAddr[1];
        systemId[2] = pPkt->devAddr[2];

        // set middle bytes to zero
        systemId[4] = 0x00;
        systemId[3] = 0x00;

        // shift three bytes up
        systemId[7] = pPkt->devAddr[5];
        systemId[6] = pPkt->devAddr[4];
        systemId[5] = pPkt->devAddr[3];

        // Set Device Info Service Parameter
        DevInfo_SetParameter(DEVINFO_SYSTEM_ID, DEVINFO_SYSTEM_ID_LEN, systemId);

        Display_printf(dispHandle, SP_ROW_STATUS_1, 0, "Initialized");

        BLE_LOG_INT_TIME(0, BLE_LOG_MODULE_APP, "APP : ---- got GAP_DEVICE_INIT_DONE_EVENT", 0);
        // Setup and start Advertising
        // For more information, see the GAP section in the User's Guide:
        // http://software-dl.ti.com/lprf/ble5stack-latest/

        BLE_LOG_INT_INT(0, BLE_LOG_MODULE_APP, "APP : ---- call GapAdv_create set=%d,%d\n", 0, 0);
        // Create Advertisement set #1 and assign handle
        status = GapAdv_create(&SimplePeripheral_advCallback, &advParams1,
                               &advHandleLegacy);
        SIMPLEPERIPHERAL_ASSERT(status == SUCCESS);

        // Load advertising data for set #1 that is statically allocated by the app
        status = GapAdv_loadByHandle(advHandleLegacy, GAP_ADV_DATA_TYPE_ADV,
                                     sizeof(advData1), advData1);
        SIMPLEPERIPHERAL_ASSERT(status == SUCCESS);

        // Load scan response data for set #1 that is statically allocated by the app
        status = GapAdv_loadByHandle(advHandleLegacy, GAP_ADV_DATA_TYPE_SCAN_RSP,
                                     sizeof(scanResData1), scanResData1);
        SIMPLEPERIPHERAL_ASSERT(status == SUCCESS);

        // Set event mask for set #1
        status = GapAdv_setEventMask(advHandleLegacy,
                                     GAP_ADV_EVT_MASK_START_AFTER_ENABLE |
                                     GAP_ADV_EVT_MASK_END_AFTER_DISABLE |
                                     GAP_ADV_EVT_MASK_SET_TERMINATED);

        // Enable legacy advertising for set #1
        status = GapAdv_enable(advHandleLegacy, GAP_ADV_ENABLE_OPTIONS_USE_MAX , 0);
        SIMPLEPERIPHERAL_ASSERT(status == SUCCESS);

        BLE_LOG_INT_INT(0, BLE_LOG_MODULE_APP, "APP : ---- call GapAdv_create set=%d,%d\n", 1, 0);
        // Create Advertisement set #2 and assign handle
        status = GapAdv_create(&SimplePeripheral_advCallback, &advParams2,
                               &advHandleLongRange);
        SIMPLEPERIPHERAL_ASSERT(status == SUCCESS);

        // Load advertising data for set #2 that is statically allocated by the app
        status = GapAdv_loadByHandle(advHandleLongRange, GAP_ADV_DATA_TYPE_ADV,
                                     sizeof(advData2), advData2);
        SIMPLEPERIPHERAL_ASSERT(status == SUCCESS);

        // Set event mask for set #2
        status = GapAdv_setEventMask(advHandleLongRange,
                                     GAP_ADV_EVT_MASK_START_AFTER_ENABLE |
                                     GAP_ADV_EVT_MASK_END_AFTER_DISABLE |
                                     GAP_ADV_EVT_MASK_SET_TERMINATED);

        BLE_LOG_INT_TIME(0, BLE_LOG_MODULE_APP, "APP : ---- GapAdv_enable", 0);
        // Enable long range advertising for set #2
        status = GapAdv_enable(advHandleLongRange, GAP_ADV_ENABLE_OPTIONS_USE_MAX , 0);
        SIMPLEPERIPHERAL_ASSERT(status == SUCCESS);

        // Display device address
        Display_printf(dispHandle, SP_ROW_IDA, 0, "%s Addr: %s",
                       (addrMode <= ADDRMODE_RANDOM) ? "Dev" : "ID",
                       Util_convertBdAddr2Str(pPkt->devAddr));

        if (addrMode > ADDRMODE_RANDOM)
        {
          SimplePeripheral_updateRPA();

          // Create one-shot clock for RPA check event.
          Util_constructClock(&clkRpaRead, SimplePeripheral_clockHandler,
                              READ_RPA_PERIOD, 0, true,
                              (UArg) &argRpaRead);
        }
        tbm_setItemStatus(&spMenuMain, SP_ITEM_AUTOCONNECT, TBM_ITEM_NONE);
      }

      break;
    }

    case GAP_LINK_ESTABLISHED_EVENT:
    {
      gapEstLinkReqEvent_t *pPkt = (gapEstLinkReqEvent_t *)pMsg;

      BLE_LOG_INT_TIME(0, BLE_LOG_MODULE_APP, "APP : ---- got GAP_LINK_ESTABLISHED_EVENT", 0);
      // Display the amount of current connections
      uint8_t numActive = linkDB_NumActive("");
      Display_printf(dispHandle, SP_ROW_STATUS_2, 0, "Num Conns: %d",
                     (uint16_t)numActive);

      if (pPkt->hdr.status == SUCCESS)
      {
        // Add connection to list and start RSSI
        SimplePeripheral_addConn(pPkt->connectionHandle);

        // Display the address of this connection
        Display_printf(dispHandle, SP_ROW_STATUS_1, 0, "Connected to %s",
                       Util_convertBdAddr2Str(pPkt->devAddr));

        // Enable connection selection option
        tbm_setItemStatus(&spMenuMain, SP_ITEM_SELECT_CONN,SP_ITEM_AUTOCONNECT);

        // Start Periodic Clock.
        Util_startClock(&clkPeriodic);
      }
      if ((numActive < MAX_NUM_BLE_CONNS) && (autoConnect == AUTOCONNECT_DISABLE))
      {
        // Start advertising since there is room for more connections
        GapAdv_enable(advHandleLegacy, GAP_ADV_ENABLE_OPTIONS_USE_MAX , 0);
        GapAdv_enable(advHandleLongRange, GAP_ADV_ENABLE_OPTIONS_USE_MAX , 0);
      }
      else
      {
        // Stop advertising since there is no room for more connections
        GapAdv_disable(advHandleLongRange);
        GapAdv_disable(advHandleLegacy);
      }
      break;
    }

    case GAP_LINK_TERMINATED_EVENT:
    {
      gapTerminateLinkEvent_t *pPkt = (gapTerminateLinkEvent_t *)pMsg;

      // Display the amount of current connections
      uint8_t numActive = linkDB_NumActive("");
      Display_printf(dispHandle, SP_ROW_STATUS_1, 0, "Device Disconnected!");
      Display_printf(dispHandle, SP_ROW_STATUS_2, 0, "Num Conns: %d",
                     (uint16_t)numActive);

      // Remove the connection from the list and disable RSSI if needed
      SimplePeripheral_removeConn(pPkt->connectionHandle);

      // If no active connections
      if (numActive == 0)
      {
        // Stop periodic clock
        Util_stopClock(&clkPeriodic);

        // Disable Connection Selection option
        tbm_setItemStatus(&spMenuMain, SP_ITEM_AUTOCONNECT, SP_ITEM_SELECT_CONN);
      }

      BLE_LOG_INT_STR(0, BLE_LOG_MODULE_APP, "APP : GAP msg: status=%d, opcode=%s\n", 0, "GAP_LINK_TERMINATED_EVENT");
      // Start advertising since there is room for more connections
      GapAdv_enable(advHandleLegacy, GAP_ADV_ENABLE_OPTIONS_USE_MAX , 0);
      GapAdv_enable(advHandleLongRange, GAP_ADV_ENABLE_OPTIONS_USE_MAX , 0);

      // Clear remaining lines
      Display_clearLine(dispHandle, SP_ROW_CONNECTION);

      break;
    }

    case GAP_UPDATE_LINK_PARAM_REQ_EVENT:
    {
      gapUpdateLinkParamReqReply_t rsp;

      gapUpdateLinkParamReqEvent_t *pReq = (gapUpdateLinkParamReqEvent_t *)pMsg;

      rsp.connectionHandle = pReq->req.connectionHandle;
      rsp.signalIdentifier = pReq->req.signalIdentifier;

      // Only accept connection intervals with slave latency of 0
      // This is just an example of how the application can send a response
      if(pReq->req.connLatency == 0)
      {
        rsp.intervalMin = pReq->req.intervalMin;
        rsp.intervalMax = pReq->req.intervalMax;
        rsp.connLatency = pReq->req.connLatency;
        rsp.connTimeout = pReq->req.connTimeout;
        rsp.accepted = TRUE;
      }
      else
      {
        rsp.accepted = FALSE;
      }

      // Send Reply
      VOID GAP_UpdateLinkParamReqReply(&rsp);

      break;
    }

    case GAP_LINK_PARAM_UPDATE_EVENT:
    {
      gapLinkUpdateEvent_t *pPkt = (gapLinkUpdateEvent_t *)pMsg;

      // Get the address from the connection handle
      linkDBInfo_t linkInfo;
      linkDB_GetInfo(pPkt->connectionHandle, &linkInfo);

      if(pPkt->status == SUCCESS)
      {
        // Display the address of the connection update
        Display_printf(dispHandle, SP_ROW_STATUS_2, 0, "Link Param Updated: %s",
                       Util_convertBdAddr2Str(linkInfo.addr));
      }
      else
      {
        // Display the address of the connection update failure
        Display_printf(dispHandle, SP_ROW_STATUS_2, 0,
                       "Link Param Update Failed 0x%x: %s", pPkt->opcode,
                       Util_convertBdAddr2Str(linkInfo.addr));
      }

      // Check if there are any queued parameter updates
      spConnHandleEntry_t *connHandleEntry = (spConnHandleEntry_t *)List_get(&paramUpdateList);
      if (connHandleEntry != NULL)
      {
        // Attempt to send queued update now
        SimplePeripheral_processParamUpdate(connHandleEntry->connHandle);

        // Free list element
        ICall_free(connHandleEntry);
      }

      break;
    }

#if defined ( NOTIFY_PARAM_UPDATE_RJCT )
    case GAP_LINK_PARAM_UPDATE_REJECT_EVENT:
    {
      linkDBInfo_t linkInfo;
      gapLinkUpdateEvent_t *pPkt = (gapLinkUpdateEvent_t *)pMsg;

      // Get the address from the connection handle
      linkDB_GetInfo(pPkt->connectionHandle, &linkInfo);

      // Display the address of the connection update failure
      Display_printf(dispHandle, SP_ROW_STATUS_2, 0,
                     "Peer Device's Update Request Rejected 0x%x: %s", pPkt->opcode,
                     Util_convertBdAddr2Str(linkInfo.addr));

      break;
    }
#endif

    default:
      Display_clearLines(dispHandle, SP_ROW_STATUS_1, SP_ROW_STATUS_2);
      break;
  }
}

/*********************************************************************
 * @fn      SimplePeripheral_charValueChangeCB
 *
 * @brief   Callback from Simple Profile indicating a characteristic
 *          value change.
 *
 * @param   paramId - parameter Id of the value that was changed.
 *
 * @return  None.
 */
static void SimplePeripheral_charValueChangeCB(uint8_t paramId)
{
  uint8_t *pValue = ICall_malloc(sizeof(uint8_t));

  if (pValue)
  {
    *pValue = paramId;

    if (SimplePeripheral_enqueueMsg(SP_CHAR_CHANGE_EVT, pValue) != SUCCESS)
    {
      ICall_free(pValue);
    }
  }
}

/*********************************************************************
 * @fn      SimplePeripheral_processCharValueChangeEvt
 *
 * @brief   Process a pending Simple Profile characteristic value change
 *          event.
 *
 * @param   paramID - parameter ID of the value that was changed.
 */
static void SimplePeripheral_processCharValueChangeEvt(uint8_t paramId)
{
  uint8_t newValue;

  switch(paramId)
  {
    case SIMPLEPROFILE_CHAR1:
      SimpleProfile_GetParameter(SIMPLEPROFILE_CHAR1, &newValue);

      Display_printf(dispHandle, SP_ROW_STATUS_1, 0, "Char 1: %d", (uint16_t)newValue);
      break;

    case SIMPLEPROFILE_CHAR3:
      SimpleProfile_GetParameter(SIMPLEPROFILE_CHAR3, &newValue);

      Display_printf(dispHandle, SP_ROW_STATUS_1, 0, "Char 3: %d", (uint16_t)newValue);
      break;

    default:
      // should not reach here!
      break;
  }
}

/*********************************************************************
 * @fn      SimplePeripheral_performPeriodicTask
 *
 * @brief   Perform a periodic application task. This function gets called
 *          every five seconds (SP_PERIODIC_EVT_PERIOD). In this example,
 *          the value of the third characteristic in the SimpleGATTProfile
 *          service is retrieved from the profile, and then copied into the
 *          value of the the fourth characteristic.
 *
 * @param   None.
 *
 * @return  None.
 */
static void SimplePeripheral_performPeriodicTask(void)
{
  // NOTE(Edge Impulse): Test inferencing on the simple example characteristic
  //    The output from ei_infer() is the integer index of the output label determined
  //    by the inferencing algorithm
  uint8_t valueToCopy = ei_infer(test_fall_buf, 400);

  SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR4, sizeof(uint8_t),
                             &valueToCopy);
}

/*********************************************************************
 * @fn      SimplePeripheral_updateRPA
 *
 * @brief   Read the current RPA from the stack and update display
 *          if the RPA has changed.
 *
 * @param   None.
 *
 * @return  None.
 */
static void SimplePeripheral_updateRPA(void)
{
  uint8_t* pRpaNew;

  // Read the current RPA.
  pRpaNew = GAP_GetDevAddress(FALSE);

  if (memcmp(pRpaNew, rpa, B_ADDR_LEN))
  {
    // If the RPA has changed, update the display
    Display_printf(dispHandle, SP_ROW_RPA, 0, "RP Addr: %s",
                   Util_convertBdAddr2Str(pRpaNew));
    memcpy(rpa, pRpaNew, B_ADDR_LEN);
  }
}

/*********************************************************************
 * @fn      SimplePeripheral_clockHandler
 *
 * @brief   Handler function for clock timeouts.
 *
 * @param   arg - event type
 *
 * @return  None.
 */
static void SimplePeripheral_clockHandler(UArg arg)
{
  spClockEventData_t *pData = (spClockEventData_t *)arg;

 if (pData->event == SP_PERIODIC_EVT)
 {
   // Start the next period
   Util_startClock(&clkPeriodic);

   // Post event to wake up the application
   SimplePeripheral_enqueueMsg(SP_PERIODIC_EVT, NULL);
 }
 else if (pData->event == SP_READ_RPA_EVT)
 {
   // Start the next period
   Util_startClock(&clkRpaRead);

   // Post event to read the current RPA
   SimplePeripheral_enqueueMsg(SP_READ_RPA_EVT, NULL);
 }
 else if (pData->event == SP_SEND_PARAM_UPDATE_EVT)
 {
    // Send message to app
    SimplePeripheral_enqueueMsg(SP_SEND_PARAM_UPDATE_EVT, pData);
 }
}

/*********************************************************************
 * @fn      SimplePeripheral_keyChangeHandler
 *
 * @brief   Key event handler function
 *
 * @param   keys - bitmap of pressed keys
 *
 * @return  none
 */
static void SimplePeripheral_keyChangeHandler(uint8_t keys)
{
  uint8_t *pValue = ICall_malloc(sizeof(uint8_t));

  if (pValue)
  {
    *pValue = keys;

    if(SimplePeripheral_enqueueMsg(SP_KEY_CHANGE_EVT, pValue) != SUCCESS)
    {
      ICall_free(pValue);
    }
  }
}

/*********************************************************************
 * @fn      SimplePeripheral_handleKeys
 *
 * @brief   Handles all key events for this device.
 *
 * @param   keys - bit field for key events. Valid entries:
 *                 KEY_LEFT
 *                 KEY_RIGHT
 */
static void SimplePeripheral_handleKeys(uint8_t keys)
{
  if (keys & KEY_LEFT)
  {
    // Check if the key is still pressed. Workaround for possible bouncing.
    if (PIN_getInputValue(CONFIG_PIN_BTN1) == 0)
    {
      tbm_buttonLeft();
    }
  }
  else if (keys & KEY_RIGHT)
  {
    // Check if the key is still pressed. Workaround for possible bouncing.
    if (PIN_getInputValue(CONFIG_PIN_BTN2) == 0)
    {
      tbm_buttonRight();
    }
  }
}

/*********************************************************************
 * @fn      SimplePeripheral_doSetConnPhy
 *
 * @brief   Set PHY preference.
 *
 * @param   index - 0: 1M PHY
 *                  1: 2M PHY
 *                  2: 1M + 2M PHY
 *                  3: CODED PHY (Long range)
 *                  4: 1M + 2M + CODED PHY
 *
 * @return  always true
 */
bool SimplePeripheral_doSetConnPhy(uint8 index)
{
  bool status = TRUE;

  static uint8_t phy[] = {
    HCI_PHY_1_MBPS, HCI_PHY_2_MBPS, HCI_PHY_1_MBPS | HCI_PHY_2_MBPS,
    HCI_PHY_CODED, HCI_PHY_1_MBPS | HCI_PHY_2_MBPS | HCI_PHY_CODED,
    AUTO_PHY_UPDATE
  };

  uint8_t connIndex = SimplePeripheral_getConnIndex(menuConnHandle);
  if (connIndex >= MAX_NUM_BLE_CONNS)
  {
    Display_printf(dispHandle, SP_ROW_STATUS_1, 0, "Connection handle is not in the connList !!!");
    return FALSE;
  }

  // Set Phy Preference on the current connection. Apply the same value
  // for RX and TX.
  // If auto PHY update is not selected and if auto PHY update is enabled, then
  // stop auto PHY update
  // Note PHYs are already enabled by default in build_config.opt in stack project.
  if(phy[index] != AUTO_PHY_UPDATE)
  {
    // Cancel RSSI reading  and auto phy changing
    SimplePeripheral_stopAutoPhyChange(connList[connIndex].connHandle);

    SimplePeripheral_setPhy(menuConnHandle, 0, phy[index], phy[index], 0);

    Display_printf(dispHandle, SP_ROW_STATUS_1, 0, "PHY preference: %s",
                   TBM_GET_ACTION_DESC(&spMenuConnPhy, index));
  }
  else
  {
    // Start RSSI read for auto PHY update (if it is disabled)
    SimplePeripheral_startAutoPhyChange(menuConnHandle);
  }

  return status;
}
/*********************************************************************
 * @fn      SimplePeripheral_advCallback
 *
 * @brief   GapAdv module callback
 *
 * @param   pMsg - message to process
 */
static void SimplePeripheral_advCallback(uint32_t event, void *pBuf, uintptr_t arg)
{
  spGapAdvEventData_t *pData = ICall_malloc(sizeof(spGapAdvEventData_t));

  if (pData)
  {
    pData->event = event;
    pData->pBuf = pBuf;

    if(SimplePeripheral_enqueueMsg(SP_ADV_EVT, pData) != SUCCESS)
    {
      ICall_free(pData);
    }
  }
}

/*********************************************************************
 * @fn      SimplePeripheral_processAdvEvent
 *
 * @brief   Process advertising event in app context
 *
 * @param   pEventData
 */
static void SimplePeripheral_processAdvEvent(spGapAdvEventData_t *pEventData)
{
  switch (pEventData->event)
  {
    case GAP_EVT_ADV_START_AFTER_ENABLE:
      BLE_LOG_INT_TIME(0, BLE_LOG_MODULE_APP, "APP : ---- GAP_EVT_ADV_START_AFTER_ENABLE", 0);
      Display_printf(dispHandle, SP_ROW_ADVSTATE, 0, "Adv Set %d Enabled",
                     *(uint8_t *)(pEventData->pBuf));
      break;

    case GAP_EVT_ADV_END_AFTER_DISABLE:
      Display_printf(dispHandle, SP_ROW_ADVSTATE, 0, "Adv Set %d Disabled",
                     *(uint8_t *)(pEventData->pBuf));
      break;

    case GAP_EVT_ADV_START:
      break;

    case GAP_EVT_ADV_END:
      break;

    case GAP_EVT_ADV_SET_TERMINATED:
    {
#ifndef Display_DISABLE_ALL
      GapAdv_setTerm_t *advSetTerm = (GapAdv_setTerm_t *)(pEventData->pBuf);

      Display_printf(dispHandle, SP_ROW_ADVSTATE, 0, "Adv Set %d disabled after conn %d",
                     advSetTerm->handle, advSetTerm->connHandle );
#endif
    }
    break;

    case GAP_EVT_SCAN_REQ_RECEIVED:
      break;

    case GAP_EVT_INSUFFICIENT_MEMORY:
      break;

    default:
      break;
  }

  // All events have associated memory to free except the insufficient memory
  // event
  if (pEventData->event != GAP_EVT_INSUFFICIENT_MEMORY)
  {
    ICall_free(pEventData->pBuf);
  }
}


/*********************************************************************
 * @fn      SimplePeripheral_pairStateCb
 *
 * @brief   Pairing state callback.
 *
 * @return  none
 */
static void SimplePeripheral_pairStateCb(uint16_t connHandle, uint8_t state,
                                         uint8_t status)
{
  spPairStateData_t *pData = ICall_malloc(sizeof(spPairStateData_t));

  // Allocate space for the event data.
  if (pData)
  {
    pData->state = state;
    pData->connHandle = connHandle;
    pData->status = status;

    // Queue the event.
    if(SimplePeripheral_enqueueMsg(SP_PAIR_STATE_EVT, pData) != SUCCESS)
    {
      ICall_free(pData);
    }
  }
}

/*********************************************************************
 * @fn      SimplePeripheral_passcodeCb
 *
 * @brief   Passcode callback.
 *
 * @return  none
 */
static void SimplePeripheral_passcodeCb(uint8_t *pDeviceAddr,
                                        uint16_t connHandle,
                                        uint8_t uiInputs,
                                        uint8_t uiOutputs,
                                        uint32_t numComparison)
{
  spPasscodeData_t *pData = ICall_malloc(sizeof(spPasscodeData_t));

  // Allocate space for the passcode event.
  if (pData )
  {
    pData->connHandle = connHandle;
    memcpy(pData->deviceAddr, pDeviceAddr, B_ADDR_LEN);
    pData->uiInputs = uiInputs;
    pData->uiOutputs = uiOutputs;
    pData->numComparison = numComparison;

    // Enqueue the event.
    if(SimplePeripheral_enqueueMsg(SP_PASSCODE_EVT, pData) != SUCCESS)
    {
      ICall_free(pData);
    }
  }
}

/*********************************************************************
 * @fn      SimplePeripheral_processPairState
 *
 * @brief   Process the new paring state.
 *
 * @return  none
 */
static void SimplePeripheral_processPairState(spPairStateData_t *pPairData)
{
  uint8_t state = pPairData->state;
  uint8_t status = pPairData->status;

  switch (state)
  {
    case GAPBOND_PAIRING_STATE_STARTED:
      Display_printf(dispHandle, SP_ROW_CONNECTION, 0, "Pairing started");
      break;

    case GAPBOND_PAIRING_STATE_COMPLETE:
      if (status == SUCCESS)
      {
        Display_printf(dispHandle, SP_ROW_CONNECTION, 0, "Pairing success");
      }
      else
      {
        Display_printf(dispHandle, SP_ROW_CONNECTION, 0, "Pairing fail: %d", status);
      }
      break;

    case GAPBOND_PAIRING_STATE_ENCRYPTED:
      if (status == SUCCESS)
      {
        Display_printf(dispHandle, SP_ROW_CONNECTION, 0, "Encryption success");
      }
      else
      {
        Display_printf(dispHandle, SP_ROW_CONNECTION, 0, "Encryption failed: %d", status);
      }
      break;

    case GAPBOND_PAIRING_STATE_BOND_SAVED:
      if (status == SUCCESS)
      {
        Display_printf(dispHandle, SP_ROW_CONNECTION, 0, "Bond save success");
      }
      else
      {
        Display_printf(dispHandle, SP_ROW_CONNECTION, 0, "Bond save failed: %d", status);
      }
      break;

    default:
      break;
  }
}

/*********************************************************************
 * @fn      SimplePeripheral_processPasscode
 *
 * @brief   Process the Passcode request.
 *
 * @return  none
 */
static void SimplePeripheral_processPasscode(spPasscodeData_t *pPasscodeData)
{
  // Display passcode to user
  if (pPasscodeData->uiOutputs != 0)
  {
    Display_printf(dispHandle, SP_ROW_CONNECTION, 0, "Passcode: %d",
                   B_APP_DEFAULT_PASSCODE);
  }

  // Send passcode response
  GAPBondMgr_PasscodeRsp(pPasscodeData->connHandle , SUCCESS,
                         B_APP_DEFAULT_PASSCODE);
}

/*********************************************************************
 * @fn      SimplePeripheral_connEvtCB
 *
 * @brief   Connection event callback.
 *
 * @param pReport pointer to connection event report
 */
static void SimplePeripheral_connEvtCB(Gap_ConnEventRpt_t *pReport)
{
  // Enqueue the event for processing in the app context.
  if(SimplePeripheral_enqueueMsg(SP_CONN_EVT, pReport) != SUCCESS)
  {
    ICall_free(pReport);
  }
}

/*********************************************************************
 * @fn      SimplePeripheral_processConnEvt
 *
 * @brief   Process connection event.
 *
 * @param pReport pointer to connection event report
 */
static void SimplePeripheral_processConnEvt(Gap_ConnEventRpt_t *pReport)
{
  // Get index from handle
  uint8_t connIndex = SimplePeripheral_getConnIndex(pReport->handle);

  if (connIndex >= MAX_NUM_BLE_CONNS)
  {
    Display_printf(dispHandle, SP_ROW_STATUS_1, 0, "Connection handle is not in the connList !!!");
    return;
  }

  // If auto phy change is enabled
  if (connList[connIndex].isAutoPHYEnable == TRUE)
  {
    // Read the RSSI
    HCI_ReadRssiCmd(pReport->handle);
  }
}


/*********************************************************************
 * @fn      SimplePeripheral_enqueueMsg
 *
 * @brief   Creates a message and puts the message in RTOS queue.
 *
 * @param   event - message event.
 * @param   state - message state.
 */
static status_t SimplePeripheral_enqueueMsg(uint8_t event, void *pData)
{
  uint8_t success;
  spEvt_t *pMsg = ICall_malloc(sizeof(spEvt_t));

  // Create dynamic pointer to message.
  if(pMsg)
  {
    pMsg->event = event;
    pMsg->pData = pData;

    // Enqueue the message.
    success = Util_enqueueMsg(appMsgQueueHandle, syncEvent, (uint8_t *)pMsg);
    return (success) ? SUCCESS : FAILURE;
  }

  return(bleMemAllocError);
}

/*********************************************************************
 * @fn      SimplePeripheral_doSelectConn
 *
 * @brief   Select a connection to communicate with
 *
 * @param   index - item index from the menu
 *
 * @return  always true
 */
bool SimplePeripheral_doSelectConn(uint8_t index)
{
  menuConnHandle = connList[index].connHandle;

  // Set the menu title and go to this connection's context
  TBM_SET_TITLE(&spMenuPerConn, TBM_GET_ACTION_DESC(&spMenuSelectConn, index));

  // Clear non-connection-related message
  Display_clearLine(dispHandle, SP_ROW_CONNECTION);

  tbm_goTo(&spMenuPerConn);

  return (true);
}
/*********************************************************************
 * @fn      SimplePeripheral_doAutoConnect
 *
 * @brief   Enable/Disable peripheral as AutoConnect node.
 *
 * @param   index - 0 : Disable AutoConnect
 *                  1 : Enable Group A
 *                  2 : Enable Group B
 *
 * @return  always true
 */
bool SimplePeripheral_doAutoConnect(uint8_t index)
{
    if (index == 1)
    {
      if (autoConnect != AUTOCONNECT_GROUP_A)
      {
        GapAdv_disable(advHandleLongRange);
        GapAdv_disable(advHandleLegacy);
        advData1[2] = 'G';
        advData1[3] = 'A';
        advData2[2] = 'G';
        advData2[3] = 'A';
        GapAdv_enable(advHandleLegacy, GAP_ADV_ENABLE_OPTIONS_USE_MAX , 0);
        GapAdv_enable(advHandleLongRange, GAP_ADV_ENABLE_OPTIONS_USE_MAX , 0);
        autoConnect = AUTOCONNECT_GROUP_A;
      }	
	  Display_printf(dispHandle, SP_ROW_AC, 0, "AutoConnect enabled: Group A");
    }
    else if (index == 2)
    {
      if (autoConnect != AUTOCONNECT_GROUP_B)
      {
        GapAdv_disable(advHandleLongRange);
        GapAdv_disable(advHandleLegacy);
        advData1[2] = 'G';
        advData1[3] = 'B';
        advData2[2] = 'G';
        advData2[3] = 'B';
        GapAdv_enable(advHandleLegacy, GAP_ADV_ENABLE_OPTIONS_USE_MAX , 0);
        GapAdv_enable(advHandleLongRange, GAP_ADV_ENABLE_OPTIONS_USE_MAX , 0);
        autoConnect = AUTOCONNECT_GROUP_B;
      } 
      Display_printf(dispHandle, SP_ROW_AC, 0, "AutoConnect enabled: Group B");
    }
    else
    {
      if (autoConnect)
      {
        GapAdv_disable(advHandleLongRange);
        GapAdv_disable(advHandleLegacy);
        advData1[2] = 'S';
        advData1[3] = 'P';
        advData2[2] = 'S';
        advData2[3] = 'P';
        GapAdv_enable(advHandleLegacy, GAP_ADV_ENABLE_OPTIONS_USE_MAX , 0);
        GapAdv_enable(advHandleLongRange, GAP_ADV_ENABLE_OPTIONS_USE_MAX , 0);
        autoConnect = AUTOCONNECT_DISABLE;
      } 
      Display_printf(dispHandle, SP_ROW_AC, 0, "AutoConnect disabled");
    }
    tbm_goTo(&spMenuMain);
    
    return (true);
}


/*********************************************************************
 * @fn      SimplePeripheral_addConn
 *
 * @brief   Add a device to the connected device list
 *
 * @return  index of the connected device list entry where the new connection
 *          info is put in.
 *          if there is no room, MAX_NUM_BLE_CONNS will be returned.
 */
static uint8_t SimplePeripheral_addConn(uint16_t connHandle)
{
  uint8_t i;
  uint8_t status = bleNoResources;

  // Try to find an available entry
  for (i = 0; i < MAX_NUM_BLE_CONNS; i++)
  {
    if (connList[i].connHandle == LINKDB_CONNHANDLE_INVALID)
    {
      // Found available entry to put a new connection info in
      connList[i].connHandle = connHandle;

#ifdef DEFAULT_SEND_PARAM_UPDATE_REQ
      // Allocate data to send through clock handler
      connList[i].pParamUpdateEventData = ICall_malloc(sizeof(spClockEventData_t) +
                                                       sizeof (uint16_t));
      if(connList[i].pParamUpdateEventData)
      {
        connList[i].pParamUpdateEventData->event = SP_SEND_PARAM_UPDATE_EVT;
        *((uint16_t *)connList[i].pParamUpdateEventData->data) = connHandle;

        // Create a clock object and start
        connList[i].pUpdateClock
          = (Clock_Struct*) ICall_malloc(sizeof(Clock_Struct));

        if (connList[i].pUpdateClock)
        {
          Util_constructClock(connList[i].pUpdateClock,
                              SimplePeripheral_clockHandler,
                              SEND_PARAM_UPDATE_DELAY, 0, true,
                              (UArg) (connList[i].pParamUpdateEventData));
        }
        else
        {
            ICall_free(connList[i].pParamUpdateEventData);
        }
      }
      else
      {
        status = bleMemAllocError;
      }
#endif

      // Set default PHY to 1M
      connList[i].currPhy = HCI_PHY_1_MBPS;

      break;
    }
  }

  return status;
}

/*********************************************************************
 * @fn      SimplePeripheral_getConnIndex
 *
 * @brief   Find index in the connected device list by connHandle
 *
 * @return  the index of the entry that has the given connection handle.
 *          if there is no match, MAX_NUM_BLE_CONNS will be returned.
 */
static uint8_t SimplePeripheral_getConnIndex(uint16_t connHandle)
{
  uint8_t i;

  for (i = 0; i < MAX_NUM_BLE_CONNS; i++)
  {
    if (connList[i].connHandle == connHandle)
    {
      return i;
    }
  }

  return(MAX_NUM_BLE_CONNS);
}

/*********************************************************************
 * @fn      SimplePeripheral_getConnIndex
 *
 * @brief   Find index in the connected device list by connHandle
 *
 * @return  SUCCESS if connHandle found valid index or bleInvalidRange
 *          if index wasn't found. LINKDB_CONNHANDLE_ALL will always succeed.
 */
static uint8_t SimplePeripheral_clearConnListEntry(uint16_t connHandle)
{
  uint8_t i;
  // Set to invalid connection index initially
  uint8_t connIndex = MAX_NUM_BLE_CONNS;

  if(connHandle != LINKDB_CONNHANDLE_ALL)
  {
    // Get connection index from handle
    connIndex = SimplePeripheral_getConnIndex(connHandle);
    if(connIndex >= MAX_NUM_BLE_CONNS)
	{
	  return(bleInvalidRange);
	}
  }

  // Clear specific handle or all handles
  for(i = 0; i < MAX_NUM_BLE_CONNS; i++)
  {
    if((connIndex == i) || (connHandle == LINKDB_CONNHANDLE_ALL))
    {
      connList[i].connHandle = LINKDB_CONNHANDLE_INVALID;
      connList[i].currPhy = 0;
      connList[i].phyCngRq = 0;
      connList[i].phyRqFailCnt = 0;
      connList[i].rqPhy = 0;
      memset(connList[i].rssiArr, 0, SP_MAX_RSSI_STORE_DEPTH);
      connList[i].rssiAvg = 0;
      connList[i].rssiCntr = 0;
      connList[i].isAutoPHYEnable = FALSE;
    }
  }

  return(SUCCESS);
}

/*********************************************************************
 * @fn      SimplePeripheral_clearPendingParamUpdate
 *
 * @brief   clean pending param update request in the paramUpdateList list
 *
 * @param   connHandle - connection handle to clean
 *
 * @return  none
 */
void SimplePeripheral_clearPendingParamUpdate(uint16_t connHandle)
{
  List_Elem *curr;

  for (curr = List_head(&paramUpdateList); curr != NULL; curr = List_next(curr)) 
  {
    if (((spConnHandleEntry_t *)curr)->connHandle == connHandle)
    {
      List_remove(&paramUpdateList, curr);
    }
  }
}

/*********************************************************************
 * @fn      SimplePeripheral_removeConn
 *
 * @brief   Remove a device from the connected device list
 *
 * @return  index of the connected device list entry where the new connection
 *          info is removed from.
 *          if connHandle is not found, MAX_NUM_BLE_CONNS will be returned.
 */
static uint8_t SimplePeripheral_removeConn(uint16_t connHandle)
{
  uint8_t connIndex = SimplePeripheral_getConnIndex(connHandle);

  if(connIndex != MAX_NUM_BLE_CONNS)
  {
    Clock_Struct* pUpdateClock = connList[connIndex].pUpdateClock;

    if (pUpdateClock != NULL)
    {
      // Stop and destruct the RTOS clock if it's still alive
      if (Util_isActive(pUpdateClock))
      {
        Util_stopClock(pUpdateClock);
      }

      // Destruct the clock object
      Clock_destruct(pUpdateClock);
      // Free clock struct
      ICall_free(pUpdateClock);
      // Free ParamUpdateEventData
      ICall_free(connList[connIndex].pParamUpdateEventData);
    }
    // Clear pending update requests from paramUpdateList
    SimplePeripheral_clearPendingParamUpdate(connHandle);
    // Stop Auto PHY Change
    SimplePeripheral_stopAutoPhyChange(connHandle);
    // Clear Connection List Entry
    SimplePeripheral_clearConnListEntry(connHandle);
  }

  return connIndex;
}

/*********************************************************************
 * @fn      SimplePeripheral_processParamUpdate
 *
 * @brief   Process a parameters update request
 *
 * @return  None
 */
static void SimplePeripheral_processParamUpdate(uint16_t connHandle)
{
  gapUpdateLinkParamReq_t req;
  uint8_t connIndex;

  req.connectionHandle = connHandle;
#ifdef DEFAULT_SEND_PARAM_UPDATE_REQ
  req.connLatency = DEFAULT_DESIRED_SLAVE_LATENCY;
  req.connTimeout = DEFAULT_DESIRED_CONN_TIMEOUT;
  req.intervalMin = DEFAULT_DESIRED_MIN_CONN_INTERVAL;
  req.intervalMax = DEFAULT_DESIRED_MAX_CONN_INTERVAL;
#endif

  connIndex = SimplePeripheral_getConnIndex(connHandle);
  if (connIndex >= MAX_NUM_BLE_CONNS)
  {
    Display_printf(dispHandle, SP_ROW_STATUS_1, 0, "Connection handle is not in the connList !!!");
    return;
  }

  // Deconstruct the clock object
  Clock_destruct(connList[connIndex].pUpdateClock);
  // Free clock struct, only in case it is not NULL
  if (connList[connIndex].pUpdateClock != NULL)
  {
      ICall_free(connList[connIndex].pUpdateClock);
      connList[connIndex].pUpdateClock = NULL;
  }
  // Free ParamUpdateEventData, only in case it is not NULL
  if (connList[connIndex].pParamUpdateEventData != NULL)
      ICall_free(connList[connIndex].pParamUpdateEventData);

  // Send parameter update
  bStatus_t status = GAP_UpdateLinkParamReq(&req);

  // If there is an ongoing update, queue this for when the udpate completes
  if (status == bleAlreadyInRequestedMode)
  {
    spConnHandleEntry_t *connHandleEntry = ICall_malloc(sizeof(spConnHandleEntry_t));
    if (connHandleEntry)
    {
      connHandleEntry->connHandle = connHandle;

      List_put(&paramUpdateList, (List_Elem *)connHandleEntry);
    }
  }
}

/*********************************************************************
 * @fn      SimpleCentral_processCmdCompleteEvt
 *
 * @brief   Process an incoming OSAL HCI Command Complete Event.
 *
 * @param   pMsg - message to process
 *
 * @return  none
 */
static void SimplePeripheral_processCmdCompleteEvt(hciEvt_CmdComplete_t *pMsg)
{
  uint8_t status = pMsg->pReturnParam[0];

  //Find which command this command complete is for
  switch (pMsg->cmdOpcode)
  {
    case HCI_READ_RSSI:
    {
      int8 rssi = (int8)pMsg->pReturnParam[3];  

      // Display RSSI value, if RSSI is higher than threshold, change to faster PHY
      if (status == SUCCESS)
      {
        uint16_t handle = BUILD_UINT16(pMsg->pReturnParam[1], pMsg->pReturnParam[2]);

        uint8_t index = SimplePeripheral_getConnIndex(handle);
        if (index >= MAX_NUM_BLE_CONNS)
        {
          Display_printf(dispHandle, SP_ROW_STATUS_1, 0, "Connection handle is not in the connList !!!");
          return;
        }

        if (rssi != LL_RSSI_NOT_AVAILABLE)
        {
          connList[index].rssiArr[connList[index].rssiCntr++] = rssi;
          connList[index].rssiCntr %= SP_MAX_RSSI_STORE_DEPTH;

          int16_t sum_rssi = 0;
          for(uint8_t cnt=0; cnt<SP_MAX_RSSI_STORE_DEPTH; cnt++)
          {
            sum_rssi += connList[index].rssiArr[cnt];
          }
          connList[index].rssiAvg = (uint32_t)(sum_rssi/SP_MAX_RSSI_STORE_DEPTH);

          uint8_t phyRq = SP_PHY_NONE;
          uint8_t phyRqS = SP_PHY_NONE;
          uint8_t phyOpt = LL_PHY_OPT_NONE;

          if(connList[index].phyCngRq == FALSE)
          {
            if((connList[index].rssiAvg >= RSSI_2M_THRSHLD) &&
            (connList[index].currPhy != HCI_PHY_2_MBPS) &&
                 (connList[index].currPhy != SP_PHY_NONE))
            {
              // try to go to higher data rate
              phyRqS = phyRq = HCI_PHY_2_MBPS;
            }
            else if((connList[index].rssiAvg < RSSI_2M_THRSHLD) &&
                    (connList[index].rssiAvg >= RSSI_1M_THRSHLD) &&
                    (connList[index].currPhy != HCI_PHY_1_MBPS) &&
                    (connList[index].currPhy != SP_PHY_NONE))
            {
              // try to go to legacy regular data rate
              phyRqS = phyRq = HCI_PHY_1_MBPS;
            }
            else if((connList[index].rssiAvg >= RSSI_S2_THRSHLD) &&
                    (connList[index].rssiAvg < RSSI_1M_THRSHLD) &&
                    (connList[index].currPhy != SP_PHY_NONE))
            {
              // try to go to lower data rate S=2(500kb/s)
              phyRqS = HCI_PHY_CODED;
              phyOpt = LL_PHY_OPT_S2;
              phyRq = BLE5_CODED_S2_PHY;
            }
            else if(connList[index].rssiAvg < RSSI_S2_THRSHLD )
            {
              // try to go to lowest data rate S=8(125kb/s)
              phyRqS = HCI_PHY_CODED;
              phyOpt = LL_PHY_OPT_S8;
              phyRq = BLE5_CODED_S8_PHY;
            }
            if((phyRq != SP_PHY_NONE) &&
               // First check if the request for this phy change is already not honored then don't request for change
               (((connList[index].rqPhy == phyRq) &&
                 (connList[index].phyRqFailCnt < 2)) ||
                 (connList[index].rqPhy != phyRq)))
            {
              //Initiate PHY change based on RSSI
              SimplePeripheral_setPhy(connList[index].connHandle, 0,
                                      phyRqS, phyRqS, phyOpt);
              connList[index].phyCngRq = TRUE;

              // If it a request for different phy than failed request, reset the count
              if(connList[index].rqPhy != phyRq)
              {
                // then reset the request phy counter and requested phy
                connList[index].phyRqFailCnt = 0;
              }

              if(phyOpt == LL_PHY_OPT_NONE)
              {
                connList[index].rqPhy = phyRq;
              }
              else if(phyOpt == LL_PHY_OPT_S2)
              {
                connList[index].rqPhy = BLE5_CODED_S2_PHY;
              }
              else
              {
                connList[index].rqPhy = BLE5_CODED_S8_PHY;
              }

            } // end of if ((phyRq != SP_PHY_NONE) && ...
          } // end of if (connList[index].phyCngRq == FALSE)
        } // end of if (rssi != LL_RSSI_NOT_AVAILABLE)

        Display_printf(dispHandle, SP_ROW_RSSI, 0,
                       "RSSI:%d dBm, AVG RSSI:%d dBm",
                       (uint32_t)(rssi),
                       connList[index].rssiAvg);

	  } // end of if (status == SUCCESS)
      break;
    }

    case HCI_LE_READ_PHY:
    {
      if (status == SUCCESS)
      {
        Display_printf(dispHandle, SP_ROW_RSSI + 2, 0, "RXPh: %d, TXPh: %d",
                       pMsg->pReturnParam[3], pMsg->pReturnParam[4]);
      }
      break;
    }

    default:
      break;
  } // end of switch (pMsg->cmdOpcode)
}

/*********************************************************************
* @fn      SimplePeripheral_initPHYRSSIArray
*
* @brief   Initializes the array of structure/s to store data related
*          RSSI based auto PHy change
*
* @param   connHandle - the connection handle
*
* @param   addr - pointer to device address
*
* @return  index of connection handle
*/
static void SimplePeripheral_initPHYRSSIArray(void)
{
  //Initialize array to store connection handle and RSSI values
  memset(connList, 0, sizeof(connList));
  for (uint8_t index = 0; index < MAX_NUM_BLE_CONNS; index++)
  {
    connList[index].connHandle = SP_INVALID_HANDLE;
  }
}
/*********************************************************************
      // Set default PHY to 1M
 * @fn      SimplePeripheral_startAutoPhyChange
 *
 * @brief   Start periodic RSSI reads on a link.
 *
 * @param   connHandle - connection handle of link
 * @param   devAddr - device address
 *
 * @return  SUCCESS: Terminate started
 *          bleIncorrectMode: No link
 *          bleNoResources: No resources
 */
static status_t SimplePeripheral_startAutoPhyChange(uint16_t connHandle)
{
  status_t status = FAILURE;

  // Get connection index from handle
  uint8_t connIndex = SimplePeripheral_getConnIndex(connHandle);
  SIMPLEPERIPHERAL_ASSERT(connIndex < MAX_NUM_BLE_CONNS);

  // Start Connection Event notice for RSSI calculation
  status = Gap_RegisterConnEventCb(SimplePeripheral_connEvtCB, GAP_CB_REGISTER, connHandle);

  // Flag in connection info if successful
  if (status == SUCCESS)
  {
    connList[connIndex].isAutoPHYEnable = TRUE;
  }

  return status;
}

/*********************************************************************
 * @fn      SimplePeripheral_stopAutoPhyChange
 *
 * @brief   Cancel periodic RSSI reads on a link.
 *
 * @param   connHandle - connection handle of link
 *
 * @return  SUCCESS: Operation successful
 *          bleIncorrectMode: No link
 */
static status_t SimplePeripheral_stopAutoPhyChange(uint16_t connHandle)
{
  // Get connection index from handle
  uint8_t connIndex = SimplePeripheral_getConnIndex(connHandle);
  SIMPLEPERIPHERAL_ASSERT(connIndex < MAX_NUM_BLE_CONNS);

  // Stop connection event notice
  Gap_RegisterConnEventCb(NULL, GAP_CB_UNREGISTER, connHandle);

  // Also update the phychange request status for active RSSI tracking connection
  connList[connIndex].phyCngRq = FALSE;
  connList[connIndex].isAutoPHYEnable = FALSE;

  return SUCCESS;
}

/*********************************************************************
 * @fn      SimplePeripheral_setPhy
 *
 * @brief   Call the HCI set phy API and and add the handle to a
 *          list to match it to an incoming command status event
 */
static status_t SimplePeripheral_setPhy(uint16_t connHandle, uint8_t allPhys,
                                        uint8_t txPhy, uint8_t rxPhy,
                                        uint16_t phyOpts)
{
  // Allocate list entry to store handle for command status
  spConnHandleEntry_t *connHandleEntry = ICall_malloc(sizeof(spConnHandleEntry_t));

  if (connHandleEntry)
  {
    connHandleEntry->connHandle = connHandle;

    // Add entry to the phy command status list
    List_put(&setPhyCommStatList, (List_Elem *)connHandleEntry);

    // Send PHY Update
    HCI_LE_SetPhyCmd(connHandle, allPhys, txPhy, rxPhy, phyOpts);
  }

  return SUCCESS;
}

/*********************************************************************
* @fn      SimplePeripheral_updatePHYStat
*
* @brief   Update the auto phy update state machine
*
* @param   connHandle - the connection handle
*
* @return  None
*/
static void SimplePeripheral_updatePHYStat(uint16_t eventCode, uint8_t *pMsg)
{
  uint8_t connIndex;

  switch (eventCode)
  {
    case HCI_LE_SET_PHY:
    {
      // Get connection handle from list
      spConnHandleEntry_t *connHandleEntry =
                           (spConnHandleEntry_t *)List_get(&setPhyCommStatList);

      if (connHandleEntry)
      {
        // Get index from connection handle
        connIndex = SimplePeripheral_getConnIndex(connHandleEntry->connHandle);

        ICall_free(connHandleEntry);

        // Is this connection still valid?
        if (connIndex < MAX_NUM_BLE_CONNS)
        {
          hciEvt_CommandStatus_t *pMyMsg = (hciEvt_CommandStatus_t *)pMsg;

          if (pMyMsg->cmdStatus == HCI_ERROR_CODE_UNSUPPORTED_REMOTE_FEATURE)
          {
            // Update the phychange request status for active RSSI tracking connection
            connList[connIndex].phyCngRq = FALSE;
            connList[connIndex].phyRqFailCnt++;
          }
        }
      }
      break;
    }

    // LE Event - a Phy update has completed or failed
    case HCI_BLE_PHY_UPDATE_COMPLETE_EVENT:
    {
      hciEvt_BLEPhyUpdateComplete_t *pPUC =
                                     (hciEvt_BLEPhyUpdateComplete_t*) pMsg;

      if(pPUC)
      {
        // Get index from connection handle
        connIndex = SimplePeripheral_getConnIndex(pPUC->connHandle);

        // Is this connection still valid?
        if (connIndex < MAX_NUM_BLE_CONNS)
        {
          // Update the phychange request status for active RSSI tracking connection
          connList[connIndex].phyCngRq = FALSE;

          if (pPUC->status == SUCCESS)
          {
            connList[connIndex].currPhy = pPUC->rxPhy;
          }
          if(pPUC->rxPhy != connList[connIndex].rqPhy)
          {
            connList[connIndex].phyRqFailCnt++;
          }
          else
          {
            // Reset the request phy counter and requested phy
            connList[connIndex].phyRqFailCnt = 0;
            connList[connIndex].rqPhy = 0;
          }
        }
      }

      break;
    }

    default:
      break;
  } // end of switch (eventCode)
}

/*********************************************************************
 * @fn      SimplePeripheral_menuSwitchCb
 *
 * @brief   Detect menu context switching
 *
 * @param   pMenuObjCurr - the current menu object
 * @param   pMenuObjNext - the menu object the context is about to switch to
 *
 * @return  none
 */
static void SimplePeripheral_menuSwitchCb(tbmMenuObj_t* pMenuObjCurr,
                                       tbmMenuObj_t* pMenuObjNext)
{
  uint8_t NUMB_ACTIVE_CONNS = linkDB_NumActive("");

  // interested in only the events of
  // entering scMenuConnect, spMenuSelectConn, and scMenuMain for now
  if (pMenuObjNext == &spMenuSelectConn)
  {
    static uint8_t* pAddrs;
    uint8_t* pAddrTemp;

    if (pAddrs != NULL)
    {
      ICall_free(pAddrs);
    }

    // Allocate buffer to display addresses
    pAddrs = ICall_malloc(NUMB_ACTIVE_CONNS * SP_ADDR_STR_SIZE);

    if (pAddrs == NULL)
    {
      TBM_SET_NUM_ITEM(&spMenuSelectConn, 0);
    }
    else
    {
      uint8_t i;

      TBM_SET_NUM_ITEM(&spMenuSelectConn, MAX_NUM_BLE_CONNS);

      pAddrTemp = pAddrs;

      // Add active connection info to the menu object
      for (i = 0; i < MAX_NUM_BLE_CONNS; i++)
      {
        if (connList[i].connHandle != LINKDB_CONNHANDLE_INVALID)
        {
          // Get the address from the connection handle
          linkDBInfo_t linkInfo;
          linkDB_GetInfo(connList[i].connHandle, &linkInfo);
          // This connection is active. Set the corresponding menu item with
          // the address of this connection and enable the item.
          memcpy(pAddrTemp, Util_convertBdAddr2Str(linkInfo.addr),
                 SP_ADDR_STR_SIZE);
          TBM_SET_ACTION_DESC(&spMenuSelectConn, i, pAddrTemp);
          tbm_setItemStatus(&spMenuSelectConn, (1 << i), SP_ITEM_NONE);
          pAddrTemp += SP_ADDR_STR_SIZE;
        }
        else
        {
          // This connection is not active. Disable the corresponding menu item.
          tbm_setItemStatus(&spMenuSelectConn, SP_ITEM_NONE, (1 << i));
        }
      }
    }
  }
  else if (pMenuObjNext == &spMenuMain)
  {
    // Now we are not in a specific connection's context

    // Clear connection-related message
    Display_clearLine(dispHandle, SP_ROW_CONNECTION);
  }
}
/*********************************************************************
*********************************************************************/
