// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "imu.h"
#include "math.h"
#include "system.h"
#include "logging.h"

#define imu_iom_isr       am_iom_isr1(IMU_SPI_NUMBER)
#define am_iom_isr1(n)    am_iom_isr(n)
#define am_iom_isr(n)     am_iomaster ## n ## _isr


// IMU Chip-Specific Definitions ---------------------------------------------------------------------------------------

// Channel numbers
enum Channels
{
   CHANNEL_COMMAND = 0,
   CHANNEL_EXECUTABLE = 1,
   CHANNEL_CONTROL = 2,
   CHANNEL_REPORTS = 3,
   CHANNEL_WAKE_REPORTS = 4,
   CHANNEL_GYRO = 5
};

// Report definitions
#define SHTP_REPORT_UNSOLICITED_RESPONSE                0x00
#define SHTP_REPORT_UNSOLICITED_RESPONSE1               0x01
#define SHTP_REPORT_SENSOR_FLUSH_RESPONSE               0xEF
#define SHTP_REPORT_SENSOR_FLUSH_REQUEST                0xF0
#define SHTP_REPORT_COMMAND_RESPONSE                    0xF1
#define SHTP_REPORT_COMMAND_REQUEST                     0xF2
#define SHTP_REPORT_FRS_READ_RESPONSE                   0xF3
#define SHTP_REPORT_FRS_READ_REQUEST                    0xF4
#define SHTP_REPORT_FRS_WRITE_RESPONSE                  0xF5
#define SHTP_REPORT_FRS_WRITE_DATA_REQUEST              0xF6
#define SHTP_REPORT_FRS_WRITE_REQUEST                   0xF7
#define SHTP_REPORT_PRODUCT_ID_RESPONSE                 0xF8
#define SHTP_REPORT_PRODUCT_ID_REQUEST                  0xF9
#define SHTP_REPORT_TIMESTAMP_REBASE                    0xFA
#define SHTP_REPORT_BASE_TIMESTAMP                      0xFB
#define SHTP_REPORT_GET_FEATURE_RESPONSE                0xFC
#define SHTP_REPORT_SET_FEATURE_COMMAND                 0xFD
#define SHTP_REPORT_GET_FEATURE_REQUEST                 0xFE

// Sensor feature definitions
#define SENSOR_REPORTID_ACCELEROMETER                   0x01
#define SENSOR_REPORTID_GYROSCOPE                       0x02
#define SENSOR_REPORTID_MAGNETIC_FIELD                  0x03
#define SENSOR_REPORTID_LINEAR_ACCELERATION             0x04
#define SENSOR_REPORTID_ROTATION_VECTOR                 0x05
#define SENSOR_REPORTID_GRAVITY                         0x06
#define SENSOR_REPORTID_UNCALIBRATED_GYRO               0x07
#define SENSOR_REPORTID_GAME_ROTATION_VECTOR            0x08
#define SENSOR_REPORTID_GEOMAGNETIC_ROTATION_VECTOR     0x09
#define SENSOR_REPORTID_PRESSURE                        0X0A
#define SENSOR_REPORTID_AMBIENT_LIGHT                   0X0B
#define SENSOR_REPORTID_HUMIDITY                        0X0C
#define SENSOR_REPORTID_PROXIMITY                       0X0D
#define SENSOR_REPORTID_TEMPERATURE                     0X0E
#define SENSOR_REPORTID_MAGNETIC_FIELD_UNCALIBRATED     0X0F
#define SENSOR_REPORTID_TAP_DETECTOR                    0x10
#define SENSOR_REPORTID_STEP_COUNTER                    0x11
#define SENSOR_REPORTID_SIGNIFICANT_MOTION              0X12
#define SENSOR_REPORTID_STABILITY_CLASSIFIER            0x13
#define SENSOR_REPORTID_RAW_ACCELEROMETER               0x14
#define SENSOR_REPORTID_RAW_GYROSCOPE                   0x15
#define SENSOR_REPORTID_RAW_MAGNETOMETER                0x16
#define SENSOR_REPORTID_STEP_DETECTOR                   0X18
#define SENSOR_REPORTID_SHAKE_DETECTOR                  0X19
#define SENSOR_REPORTID_FLIP_DETECTOR                   0X1A
#define SENSOR_REPORTID_PICKUP_DETECTOR                 0X1B
#define SENSOR_REPORTID_STABILITY_DETECTOR              0x1C
#define SENSOR_REPORTID_PERSONAL_ACTIVITY_CLASSIFIER    0x1E
#define SENSOR_REPORTID_SLEEP_DETECTOR                  0X1F
#define SENSOR_REPORTID_TILT_DETECTOR                   0X20
#define SENSOR_REPORTID_POCKET_DETECTOR                 0X21
#define SENSOR_REPORTID_CIRCLE_DETECTOR                 0X22
#define SENSOR_REPORTID_HEART_RATE_MONITOR              0X23
#define SENSOR_REPORTID_ARVR_STABILIZED_RV              0X28
#define SENSOR_REPORTID_ARVR_STABILIZED_GRV             0X29
#define SENSOR_REPORTID_GYRO_INTEGRATED_RV              0X2A
#define SENSOR_REPORTID_IZRO_MOTION_REQUEST             0X2B
#define SENSOR_REPORTID_RAW_OPTICAL_FLOW                0X2C
#define SENSOR_REPORTID_DEAD_RECKONING_POSE             0X2D
#define SENSOR_REPORTID_WHEEL_ENCODER                   0X2E

// Addressing
#define BNO_W_ADDR                                      0x96
#define BNO_R_ADDR                                      0x97
#define ADDR_BNO_DFU_W_ADDR                             0x52
#define ADDR_BNO_DFU_R_ADDR                             0x53

// FRS record IDs
#define STATIC_CALIBRATION_AGM                          0x7979
#define NOMINAL_CALIBRATION                             0x4D4D
#define STATIC_CALIBRATION_SRA                          0x8A8A
#define NOMINAL_CALIBRATION_SRA                         0x4E4E
#define DYNAMIC_CALIBRATION                             0x1F1F
#define ME_POWER_MGMT                                   0xD3E2
#define SYSTEM_ORIENTATION                              0x2D3E
#define ACCEL_ORIENTATION                               0x2D41
#define SCREEN_ACCEL_ORIENTATION                        0x2D43
#define GYROSCOPE_ORIENTATION                           0x2D46
#define MAGNETOMETER_ORIENTATION                        0x2D4C
#define ARVR_STABILIZATION_RV                           0x3E2D
#define ARVR_STABILIZATION_GRV                          0x3E2E
#define TAP_DETECT_CONFIG                               0xC269
#define SIG_MOTION_DETECT_CONFIG                        0xC274
#define SHAKE_DETECT_CONFIG                             0x7D7D
#define MAX_FUSION_PERIOD                               0xD7D7
#define SERIAL_NUMBER                                   0x4B4B
#define ES_PRESSURE_CAL                                 0x39AF
#define ES_TEMPERATURE_CAL                              0x4D20
#define ES_HUMIDITY_CAL                                 0x1AC9
#define ES_AMBIENT_LIGHT_CAL                            0x39B1
#define ES_PROXIMITY_CAL                                0x4DA2
#define ALS_CAL                                         0xD401
#define PROXIMITY_SENSOR_CAL                            0xD402
#define PICKUP_DETECTOR_CONFIG                          0x1B2A
#define FLIP_DETECTOR_CONFIG                            0xFC94
#define STABILITY_DETECTOR_CONFIG                       0xED85
#define ACTIVITY_TRACKER_CONFIG                         0xED88
#define SLEEP_DETECTOR_CONFIG                           0xED87
#define TILT_DETECTOR_CONFIG                            0xED89
#define POCKET_DETECTOR_CONFIG                          0xEF27
#define CIRCLE_DETECTOR_CONFIG                          0xEE51
#define USER_RECORD                                     0x74B4
#define ME_TIME_SOURCE_SELECT                           0xD403
#define UART_FORMAT                                     0xA1A1
#define GYRO_INTEGRATED_RV_CONFIG                       0xA1A2
#define DR_IMU_CONFIG                                   0xDED2
#define DR_VEL_EST_CONFIG                               0xDED3
#define DR_SYNC_CONFIG                                  0xDED4
#define DR_QUAL_CONFIG                                  0xDED5
#define DR_CAL_CONFIG                                   0xDED6
#define DR_LIGHT_REC_CONFIG                             0xDED8
#define DR_FUSION_CONFIG                                0xDED9
#define DR_OF_CONFIG                                    0xDEDA
#define DR_WHEEL_CONFIG                                 0xDEDB
#define DR_CAL                                          0xDEDC
#define DR_WHEEL_SELECT                                 0xDEDF
#define FRS_RECORDID_RAW_ACCELEROMETER                  0xE301
#define FRS_RECORDID_ACCELEROMETER                      0xE302
#define FRS_RECORDID_LINEAR_ACCELERATION                0xE303
#define FRS_RECORDID_GRAVITY                            0xE304
#define FRS_RECORDID_RAW_GYROSCOPE                      0xE305
#define FRS_RECORDID_GYROSCOPE_CALIBRATED               0xE306
#define FRS_RECORDID_GYROSCOPE_UNCALIBRATED             0xE307
#define FRS_RECORDID_RAW_MAGNETOMETER                   0xE308
#define FRS_RECORDID_MAGNETIC_FIELD_CALIBRATED          0xE309
#define FRS_RECORDID_MAGNETIC_FIELD_UNCALIBRATED        0xE30A
#define FRS_RECORDID_ROTATION_VECTOR                    0xE30B
#define FRS_RECORDID_GAME_ROTATION_VECTOR               0xE30C
#define FRS_RECORDID_GEOMAGNETIC_ROTATION_VECTOR        0xE30D
#define FRS_RECORDID_PRESSURE                           0xE30E
#define FRS_RECORDID_AMBIENT_LIGHT                      0xE30F
#define FRS_RECORDID_HUMIDITY                           0xE310
#define FRS_RECORDID_PROXIMITY                          0xE311
#define FRS_RECORDID_TEMPERATURE                        0xE312
#define FRS_RECORDID_TAP_DETECTOR                       0xE313
#define FRS_RECORDID_STEP_DETECTOR                      0xE314
#define FRS_RECORDID_STEP_COUNTER                       0xE315
#define FRS_RECORDID_SIGNIFICANT_MOTION                 0xE316
#define FRS_RECORDID_STABILITY_CLASSIFIER               0xE317
#define FRS_RECORDID_SHAKE_DETECTOR                     0xE318
#define FRS_RECORDID_FLIP_DETECTOR                      0xE319
#define FRS_RECORDID_PICKUP_DETECTOR                    0xE31A
#define FRS_RECORDID_STABILITY_DETECTOR                 0xE31B
#define FRS_RECORDID_PERSONAL_ACTIVITY_CLASSIFIER       0xE31C
#define FRS_RECORDID_SLEEP_DETECTOR                     0xE31D
#define FRS_RECORDID_TILT_DETECTOR                      0xE31E
#define FRS_RECORDID_POCKET_DETECTOR                    0xE31F
#define FRS_RECORDID_CIRCLE_DETECTOR                    0xE320
#define FRS_RECORDID_HEART_RATE_MONITOR                 0xE321
#define FRS_RECORDID_ARVR_STABILIZED_RV                 0xE322
#define FRS_RECORDID_ARVR_STABILIZED_GRV                0xE323
#define FRS_RECORDID_GYRO_INTEGRATED_RV                 0xE324
#define FRS_RECORDID_RAW_OPTICAL_FLOW                   0xE326

// FRS write status definitions
#define FRS_WRITE_STATUS_RECEIVED                       0
#define FRS_WRITE_STATUS_UNRECOGNIZED_FRS_TYPE          1
#define FRS_WRITE_STATUS_BUSY                           2
#define FRS_WRITE_STATUS_WRITE_COMPLETED                3
#define FRS_WRITE_STATUS_READY                          4
#define FRS_WRITE_STATUS_FAILED                         5
#define FRS_WRITE_STATUS_NOT_READY                      6
#define FRS_WRITE_STATUS_INVALID_LENGTH                 7
#define FRS_WRITE_STATUS_RECORD_VALID                   8
#define FRS_WRITE_STATUS_INVALID_RECORD                 9
#define FRS_WRITE_STATUS_DEVICE_ERROR                   10
#define FRS_WRITE_STATUS_READ_ONLY                      11

// FRS read status definitions
#define FRS_READ_STATUS_NO_ERROR                        0
#define FRS_READ_STATUS_UNRECOGNIZED_FRS_TYPE           1
#define FRS_READ_STATUS_BUSY                            2
#define FRS_READ_STATUS_READ_RECORD_COMPLETED           3
#define FRS_READ_STATUS_OFFSET_OUT_OF_RANGE             4
#define FRS_READ_STATUS_RECORD_EMPTY                    5
#define FRS_READ_STATUS_READ_BLOCK_COMPLETED            6
#define FRS_READ_STATUS_READ_BLOCK_AND_RECORD_COMPLETED 7
#define FRS_READ_STATUS_DEVICE_ERROR                    8

// Command definitions
#define COMMAND_ERRORS                                  1
#define COMMAND_COUNTER                                 2
#define COMMAND_TARE_NOW                                0
#define COMMAND_TARE_PERSIST                            1
#define COMMAND_TARE_REORIENT                           2
#define COMMAND_TARE                                    3
#define COMMAND_INITIALIZE_RESET                        1
#define COMMAND_INITIALIZE_ON                           2
#define COMMAND_INITIALIZE_SLEEP                        3
#define COMMAND_INITIALIZE                              4
#define COMMAND_DCD                                     6
#define COMMAND_ME_CALIBRATE_CONFIG                     0
#define COMMAND_ME_CALIBRATE_GET                        1
#define COMMAND_ME_CALIBRATE                            7
#define COMMAND_DCD_PERIOD_SAVE                         9
#define COMMAND_OSCILLATOR                              10
#define COMMAND_CLEAR_DCD                               11
#define COMMAND_TURNTABLE_CAL                           12
#define COMMAND_BOOTLOADER_MODE_REQ                     0
#define COMMAND_BOOTLOADER_STATUS_REQ                   1
#define COMMAND_BOOTLOADER                              13
#define COMMAND_INTERACTIVE_CAL_REQ                     14
#define COMMAND_WHEEL_REQ                               15
#define COMMAND_UNSOLICITED_INITIALIZE                  0x84

// Calibration definitions
#define CALIBRATE_ACCEL                                 0
#define CALIBRATE_GYRO                                  1
#define CALIBRATE_MAG                                   2
#define CALIBRATE_PLANAR_ACCEL                          3
#define CALIBRATE_ON_TABLE                              4
#define CALIBRATE_ACCEL_GYRO_MAG                        5
#define CALIBRATE_STOP                                  5
#define CALIBRATE_ALL                                   6

// Pre-calculated Q-scaling values: SCALE_Q(n) = 1.0f / (1 << n)
#define SCALE_Q4                                        0.06250000000000000000
#define SCALE_Q7                                        0.00781250000000000000
#define SCALE_Q8                                        0.00390625000000000000
#define SCALE_Q9                                        0.00195312500000000000
#define SCALE_Q10                                       0.00097656250000000000
#define SCALE_Q12                                       0.00024414062500000000
#define SCALE_Q14                                       0.00006103515625000000
#define SCALE_Q17                                       0.00000762939453125000
#define SCALE_Q20                                       0.00000095367431640625
#define SCALE_Q25                                       0.00000002980232238770
#define SCALE_Q30                                       0.00000000093132257462
#define SCALE_TO_Q14                                    16384.0

#define STABILITY_CLASS_UNKNOWN                         0
#define STABILITY_CLASS_ON_TABLE                        1
#define STABILITY_CLASS_STATIONARY                      2
#define STABILITY_CLASS_STABLE                          3
#define STABILITY_CLASS_IN_MOTION                       4

// Data type sizes
#define SEQUENCE_SIZE                                   6
#define HEADER_SIZE                                     4
#define RX_PACKET_SIZE                                  284
#define TX_PACKET_SIZE                                  21
#define METADATA_SIZE                                   9

// Required delay times
#define RESET_DELAY_MS                                  200
#define PORT_TIMEOUT_MS                                 500

typedef enum {
   NA = 0,
   PowerOnReset,
   InternalSystemReset,
   WatchdogTimeout,
   ExternalReset,
   Other,
} BNO_ResetCause_t;

typedef enum {
   InternalOscillator = 0,
   ExternalCrystal,
   ExternalClock,
   OscillatorError
} BNO_Oscillator_t;

typedef enum {
   ResetToBootloader = 0,
   UpgradeApp,
   ValidateImage,
   LauchApp,
   BootInvalid
} BNO_BootMode_t;

typedef enum {
   MOTION_INTENT_UNKNOWN = 0,
   INTENT_STATIONARY_WITHOUT_VIBRATION,
   MOTION_INTENT_STATIONARY_WITH_VIBRATION,
   MOTION_INTENT_IN_MOTION,
   MOTION_INTENT_IN_MOTION_ACCELERATING
} BNO_MotionIntent_t;


typedef struct __attribute__((packed)) {
   BNO_ResetCause_t rst_Cause;
   uint8_t sw_Maj;
   uint8_t sw_Min;
   uint32_t sw_PN;
   uint32_t sw_BN;
   uint16_t sw_VP;
} BNO_productID_t;

typedef struct __attribute__((packed)) {
   uint8_t status;
   uint16_t wordOffset;
} BNO_FrsWriteResp_t;

typedef struct __attribute__((packed)) {
   uint8_t len_status;
   uint16_t wordOffset;
   uint32_t data0;
   uint32_t data1;
   uint16_t frsType;
} BNO_FrsReadResp_t;

#define RESPONSE_VALUES 11
typedef struct __attribute__((packed)) {
   uint8_t seq;
   uint8_t command;
   uint8_t commandSeq;
   uint8_t respSeq;
   uint8_t r[RESPONSE_VALUES];
} BNO_CommandResp_t;

#define COMMAND_PARAMS  9
typedef struct __attribute__((packed)) {
   uint8_t reportId;
   uint8_t seq;
   uint8_t command;
   uint8_t p[COMMAND_PARAMS];
} BNO_CommandReq_t;

typedef struct __attribute__((packed)) {
   uint8_t Severity;
   uint8_t SeqNo;
   uint8_t Source;
   uint8_t Error;
   uint8_t Module;
   uint8_t Code;
} BNO_Error_t;

typedef struct __attribute__((packed)) {
   uint32_t offered;
   uint32_t accepted;
   uint32_t on;
   uint32_t attempted;
} BNO_Counts_t;

typedef enum {
   TARE_Z = 4,
   TARE_ALL = 7,
} BNO_TareAxis_t;

typedef enum {
   RotationVector = 0,
   GamingRotationVector = 1,
   GeomagneticRotationVector = 2,
   GyroIntegratedRotationVector = 3,
   ArVrStabilizedRotationVector = 4,
   ArVrStabilizedGameRotationVector = 5,
} BNO_TareRV_t;

typedef struct __attribute__((packed)) {
   uint8_t Status;
   uint8_t AccCalEnable;
   uint8_t GyroCalEnable;
   uint8_t MagCalEnable;
   uint8_t PlanCalEnable;
   uint8_t OnTableEnable;
} BNO_calibrationStat_t;

typedef struct __attribute__((packed)) {
   uint8_t OperationMode;
   uint8_t reserved;
   uint32_t Status;
   uint32_t Errors;
} BNO_Boot_t;

typedef struct __attribute__((packed)) {
   uint8_t sensorID;
   uint8_t flags;
   uint16_t changeSensitivity;
   uint32_t reportInterval_us;
   uint32_t batchInterval_us;
   uint32_t sensorSpecific;
} BNO_Feature_t;

typedef struct __attribute__((packed)) {
   float Roll;
   float Pitch;
   float Yaw;
} BNO_RollPitchYaw_t;

typedef struct __attribute__((packed)) {
   int16_t X; // [ADC counts]
   int16_t Y; // [ADC counts]
   int16_t Z; // [ADC counts]
   uint32_t TimeStamp; // [us]
} BNO_RawAccelerometer_t;

typedef struct __attribute__((packed)) {
   int16_t X;
   int16_t Y;
   int16_t Z;
} BNO_Accelerometer_t;

typedef struct __attribute__((packed)) {
   int16_t X; // [ADC Counts]
   int16_t Y; // [ADC Counts]
   int16_t Z; // [ADC Counts]
   int16_t Temperature; // [ADC Counts]
   uint32_t TimeStamp; // [uS]
} BNO_RawGyroscope_t;

typedef struct __attribute__((packed)) {
   int16_t X; // [rad/s]
   int16_t Y; // [rad/s]
   int16_t Z; // [rad/s]
} BNO_Gyroscope_t;

typedef struct __attribute__((packed)) {
   int16_t X; // [rad/s]
   int16_t Y; // [rad/s]
   int16_t Z; // [rad/s]
   int16_t BiasX; // [rad/s]
   int16_t BiasY; // [rad/s]
   int16_t BiasZ; // [rad/s]
} BNO_GyroscopeUncalibrated_t;

typedef struct __attribute__((packed)) {
   int16_t X; // [ADC Counts]
   int16_t Y; // [ADC Counts]
   int16_t Z; // [ADC Counts]
   uint32_t TimeStamp; // [us]
} BNO_RawMagnetometer_t;

typedef struct __attribute__((packed)) {
   int16_t X; // [uTesla]
   int16_t Y; // [uTesla]
   int16_t Z; // [uTesla]
} BNO_MagneticField_t;

typedef struct __attribute__((packed)) {
   int16_t X; // [uTesla]
   int16_t Y; // [uTesla]
   int16_t Z; // [uTesla]
   int16_t BiasX; // [uTesla]
   int16_t BiasY; // [uTesla]
   int16_t BiasZ; // [uTesla]
} BNO_MagneticFieldUncalibrated_t;

typedef struct __attribute__((packed)) {
   int16_t I;
   int16_t J;
   int16_t K;
   int16_t Real;
   int16_t Accuracy; // [radians]
} BNO_RotationVectorWAcc_t;

typedef struct __attribute__((packed)) {
   int16_t I;
   int16_t J;
   int16_t K;
   int16_t Real;
} BNO_RotationVector_t;

typedef enum {
   TAPDET_X = 1,
   TAPDET_X_POS = 2,
   TAPDET_Y = 4,
   TAPDET_Y_POS = 8,
   TAPDET_Z = 16,
   TAPDET_Z_POS = 32,
   TAPDET_DOUBLE = 64,
} BNO_Tap_t;

typedef struct __attribute__((packed)) {
   uint32_t Latency; // [us]
   uint16_t Steps;
} BNO_StepCounter_t;

typedef enum {
   STABILITY_CLASSIFIER_UNKNOWN = 0,
   STABILITY_CLASSIFIER_ON_TABLE,
   STABILITY_CLASSIFIER_STATIONARY,
   STABILITY_CLASSIFIER_STABLE,
   STABILITY_CLASSIFIER_MOTION,
} BNO_Stability_t;

typedef enum {
   SHAKE_X = 1,
   SHAKE_Y = 2,
   SHAKE_Z = 4,
} BNO_Shake_t;

typedef enum {
   PICKUP_LEVEL_TO_NOT_LEVEL = 1,
   PICKUP_STOP_WITHIN_REGION = 2,
} BNO_Pickup_t;

typedef enum {
   STABILITY_ENTERED = 1,
   STABILITY_EXITED = 2,
} BNO_StabilityDetector_t;

typedef enum {
   PAC_UNKNOWN = 0,
   PAC_IN_VEHICLE,
   PAC_ON_BICYCLE,
   PAC_ON_FOOT,
   PAC_STILL,
   PAC_TILTING,
   PAC_WALKING,
   PAC_RUNNING,
} BNO_PAC_t;

typedef struct __attribute__((packed)) {
   uint8_t Page;
   uint8_t LastPage;
   BNO_PAC_t MostLikelyState;
   uint8_t Confidence[10];
} BNO_PersonalActivityClassifier_t;

typedef struct __attribute__((packed)) {
   float I;
   float J;
   float K;
   float Real;
   float AngleVelX; // [rad/s]
   float AngleVelY; // [rad/s]
   float AngleVelZ; // [rad/s]
} BNO_GyroIntegratedRV_t;

typedef enum {
   IZRO_MI_UNKNOWN = 0,
   IZRO_MI_STATIONARY_NO_VIBRATION,
   IZRO_MI_STATIONARY_WITH_VIBRATION,
   IZRO_MI_IN_MOTION,
   IZRO_MI_ACCELERATING,
} BNO_IZroMotionIntent_t;

typedef enum {
   IZRO_MR_NO_REQUEST = 0,
   IZRO_MR_STAY_STATIONARY,
   IZRO_MR_STATIONARY_NON_URGENT,
   IZRO_MR_STATIONARY_URGENT,
} BNO_IZroMotionRequest_t;

typedef struct __attribute__((packed)) {
   BNO_IZroMotionIntent_t Intent;
   BNO_IZroMotionRequest_t Request;
} BNO_IZroRequest_t;

typedef struct __attribute__((packed)) {
   uint32_t TimeStamp;
   int16_t Dt;
   int16_t Dx;
   int16_t Dy;
   int16_t Iq;
   uint8_t ResX;
   uint8_t ResY;
   uint8_t Shutter;
   uint8_t FrameMax;
   uint8_t FrameAvg;
   uint8_t FrameMin;
   uint8_t LaserOn;
} BNO_RawOptFlow_t;

typedef struct __attribute__((packed)) {
   uint32_t TimeStamp;
   float LinPosX;
   float LinPosY;
   float LinPosZ;
   float I;
   float J;
   float K;
   float Real;
   float LinVelX;
   float LinVelY;
   float LinVelZ;
   float AngleVelX;
   float AngleVelY;
   float AngleVelZ;
} BNO_DeadReckoningPose_t;

typedef struct __attribute__((packed)) {
   uint32_t TimeStamp;
   uint8_t WheelIndex;
   uint8_t DataType;
   uint16_t Data;
} BNO_WheelEncoder_t;

typedef struct __attribute__((packed)) {
   uint8_t sensorId;   // Which sensor produced this event.
   uint8_t sequence;   // The sequence number increments once for each report sent. Gaps in the sequence numbers indicate missing or dropped reports.
   uint8_t status;     // bits 7-5: reserved, 4-2: exponent delay, 1-0: Accuracy 0 - Unreliable 1 - Accuracy low 2 - Accuracy medium 3 - Accuracy high
   uint64_t timestamp; // [us]
   uint32_t delay;     // [us] value is delay * 2^exponent (see status)
   union {
      BNO_RawAccelerometer_t RawAccelerometer;
      BNO_Accelerometer_t Accelerometer;
      BNO_Accelerometer_t LinearAcceleration;
      BNO_Accelerometer_t Gravity;
      BNO_RawGyroscope_t RawGyroscope;
      BNO_Gyroscope_t Gyroscope;
      BNO_GyroscopeUncalibrated_t GyroscopeUncal;
      BNO_RawMagnetometer_t RawMagnetometer;
      BNO_MagneticField_t MagneticField;
      BNO_MagneticFieldUncalibrated_t MagneticFieldUncal;
      BNO_RotationVectorWAcc_t RotationVector;
      BNO_RotationVector_t GameRotationVector;
      BNO_RotationVectorWAcc_t GeoMagRotationVector;
      float Pressure; // [hectopascals]
      float AmbientLight; // [lux]
      float Humidity; // [percent]
      float Proximity; // [cm]
      float Temperature; // [C]
      BNO_Tap_t TapDetectorFlag;
      uint32_t StepDetectorLatency; // [us]
      BNO_StepCounter_t StepCounter;
      uint16_t SignificantMotion;
      BNO_Stability_t StabilityClassifier;
      BNO_Shake_t ShakeDetector;
      uint16_t FlipDetector;
      BNO_Pickup_t PickupDetector;
      BNO_StabilityDetector_t StabilityDetector;
      BNO_PersonalActivityClassifier_t PersonalActivityClassifier;
      uint8_t SleepDetector;
      uint16_t TiltDetector;
      uint16_t PocketDetector;
      uint16_t CircleDetector;
      uint16_t HeartRateMonitor; // [bpm]
      BNO_RotationVectorWAcc_t ArVrStabilizedRV;
      BNO_RotationVector_t ArVrStabilizedGRV;
      BNO_GyroIntegratedRV_t GyroIntegratedRV;
      BNO_IZroRequest_t IzroRequest;
      BNO_RawOptFlow_t RawOptFlow;
      BNO_DeadReckoningPose_t DeadReckoningPose;
      BNO_WheelEncoder_t WheelEncoder;
   } value;
} BNO_SensorValue_t;


// Static Global Variables ---------------------------------------------------------------------------------------------

static void *spi_handle;
static bool reset_occurred, save_dcd_status;
static volatile uint8_t awaiting_interrupt_count;
static volatile bool imu_is_initialized = false, in_motion;
static uint8_t sequence_number[SEQUENCE_SIZE], command_sequence_number;
static uint8_t shtp_header[HEADER_SIZE], shtp_data[RX_PACKET_SIZE];
static motion_change_callback_t motion_change_callback;
static data_ready_callback_t data_ready_callback;
static BNO_Feature_t sensor_features;
static BNO_SensorValue_t last_sensor_reading;
static BNO_calibrationStat_t calibration_status;
static BNO_FrsWriteResp_t frs_write_response;
static BNO_FrsReadResp_t frs_read_response;
static BNO_Oscillator_t oscillator_type;
static BNO_productID_t device_id;
static BNO_Boot_t boot_loader;
static BNO_Error_t errors;
static BNO_Counts_t counts;
static BNO_Error_t errors;


// Private Helper Functions --------------------------------------------------------------------------------------------

static bool wait_for_spi(void)
{
   // Wait until the SPI interrupt line has been pulled low
   bool spi_ready = (am_hal_gpio_input_read(PIN_IMU_INTERRUPT) == 0);
   for (int i = 0; !spi_ready && (i < 250); ++i)
   {
      am_util_delay_ms(1);
      spi_ready = (am_hal_gpio_input_read(PIN_IMU_INTERRUPT) == 0);
   }
   return spi_ready;
}

static void spi_read(uint32_t read_length, uint8_t *read_buffer, bool continuation)
{
   // Create the SPI read transaction structure
   am_hal_iom_transfer_t read_transaction = {
      .uPeerInfo.ui32SpiChipSelect  = 0,
      .ui32InstrLen                 = 0,
      .ui64Instr                    = 0,
      .eDirection                   = AM_HAL_IOM_RX,
      .ui32NumBytes                 = read_length,
      .pui32TxBuffer                = NULL,
      .pui32RxBuffer                = (uint32_t*)read_buffer,
      .bContinue                    = continuation,
      .ui8RepeatCount               = 0,
      .ui8Priority                  = 1,
      .ui32PauseCondition           = 0,
      .ui32StatusSetClr             = 0
   };

   // Repeat the transfer until it succeeds
   uint32_t retries_remaining = 5;
   while (retries_remaining-- && (am_hal_iom_blocking_transfer(spi_handle, &read_transaction) != AM_HAL_STATUS_SUCCESS));
}

static void spi_write(uint32_t body_length, const uint8_t *body_buffer)
{
   // Create the SPI write transaction structure
   am_hal_iom_transfer_t write_transaction = {
      .uPeerInfo.ui32SpiChipSelect  = 0,
      .ui32InstrLen                 = 0,
      .ui64Instr                    = 0,
      .eDirection                   = AM_HAL_IOM_TX,
      .ui32NumBytes                 = body_length,
      .pui32TxBuffer                = (uint32_t*)body_buffer,
      .pui32RxBuffer                = NULL,
      .bContinue                    = false,
      .ui8RepeatCount               = 0,
      .ui8Priority                  = 1,
      .ui32PauseCondition           = 0,
      .ui32StatusSetClr             = 0
   };

   // Repeat the transfer until it succeeds
   uint32_t retries_remaining = 5;
   while (retries_remaining-- && (am_hal_iom_blocking_transfer(spi_handle, &write_transaction) != AM_HAL_STATUS_SUCCESS));
}


// IMU Chip-Specific API Functions -------------------------------------------------------------------------------------

static void send_packet(uint8_t channel_number)
{
   // Determine the packet length based on the channel type and command
   uint8_t packet_length = 5;
   if (channel_number != CHANNEL_EXECUTABLE)
   {
      switch (shtp_data[0])
      {
         case SHTP_REPORT_SENSOR_FLUSH_REQUEST:
         case SHTP_REPORT_GET_FEATURE_REQUEST:
         case SHTP_REPORT_PRODUCT_ID_REQUEST:
            packet_length = 6;
            break;
         case SHTP_REPORT_FRS_READ_REQUEST:
            packet_length = 12;
            break;
         case COMMAND_ME_CALIBRATE:
         case COMMAND_TARE:
         case COMMAND_DCD:
         case SHTP_REPORT_COMMAND_REQUEST:
         case SHTP_REPORT_FRS_WRITE_REQUEST:
            packet_length = 16;
            break;
         case SHTP_REPORT_SET_FEATURE_COMMAND:
            packet_length = 21;
            break;
      }
   }

   // Wake up the IMU for communication
   ++awaiting_interrupt_count;
   am_hal_gpio_output_clear(PIN_IMU_WAKEUP);
   wait_for_spi();
   am_hal_gpio_output_set(PIN_IMU_WAKEUP);
   --awaiting_interrupt_count;

   // Write a header and send the data currently in shtp_data
   memmove(shtp_data + 4, shtp_data, packet_length - 4);
   shtp_data[0] = packet_length;
   shtp_data[1] = 0;
   shtp_data[2] = channel_number;
   shtp_data[3] = sequence_number[channel_number]++;
   spi_write(packet_length, shtp_data);
}

static bool receive_packet()
{
   // Wait until the device is ready to communicate
   if (!wait_for_spi())
      return false;

   // Read the packet header to determine the total number of pending bytes
   spi_read(sizeof(shtp_header), shtp_header, true);
   const uint16_t data_length = *(uint16_t*)shtp_header & 0x7FFF;
   if (!data_length || (data_length > RX_PACKET_SIZE))
   {
      spi_read(0, NULL, false);
      return false;
   }

   // Read the remainder of the packet
   spi_read(data_length - 4, shtp_data, false);
   return true;
}

static imu_data_type_t parse_input_report(void)
{
   // Read the packet details
   static BNO_SensorValue_t data;
   data.sensorId = shtp_data[5];
   data.timestamp = *(uint32_t *)&shtp_data[1];
   data.sequence = shtp_data[16];
   data.status = shtp_data[7] & 0x03;

   // Read sensor-specific details
   imu_data_type_t data_type = IMU_UNKNOWN;
   switch(data.sensorId)
   {
      case SENSOR_REPORTID_RAW_ACCELEROMETER:
         data.value.RawAccelerometer.X = *(int16_t*)&shtp_data[9];
         data.value.RawAccelerometer.Y = *(int16_t*)&shtp_data[11];
         data.value.RawAccelerometer.Z = *(int16_t*)&shtp_data[13];
         data.value.RawAccelerometer.TimeStamp = *(uint32_t*)&shtp_data[17];
         data_type = IMU_ACCELEROMETER;
         break;
      case SENSOR_REPORTID_ACCELEROMETER:
         data.value.Accelerometer.X = *(int16_t*)&shtp_data[9];
         data.value.Accelerometer.Y = *(int16_t*)&shtp_data[11];
         data.value.Accelerometer.Z = *(int16_t*)&shtp_data[13];
         data_type = IMU_ACCELEROMETER;
         break;
      case SENSOR_REPORTID_LINEAR_ACCELERATION:
         data.value.LinearAcceleration.X = *(int16_t*)&shtp_data[9];
         data.value.LinearAcceleration.Y = *(int16_t*)&shtp_data[11];
         data.value.LinearAcceleration.Z = *(int16_t*)&shtp_data[13];
         data_type = IMU_LINEAR_ACCELEROMETER;
         break;
      case SENSOR_REPORTID_GRAVITY:
         data.value.Gravity.X = *(int16_t*)&shtp_data[9];
         data.value.Gravity.Y = *(int16_t*)&shtp_data[11];
         data.value.Gravity.Z = *(int16_t*)&shtp_data[13];
         data_type = IMU_GRAVITY;
         break;
      case SENSOR_REPORTID_RAW_GYROSCOPE:
         data.value.RawGyroscope.X = *(int16_t*)&shtp_data[9];
         data.value.RawGyroscope.Y = *(int16_t*)&shtp_data[11];
         data.value.RawGyroscope.Z = *(int16_t*)&shtp_data[13];
         data.value.RawGyroscope.Temperature = *(int16_t*)&shtp_data[15];
         data.value.RawGyroscope.TimeStamp = *(uint32_t*)&shtp_data[17];
         data_type = IMU_GYROSCOPE;
         break;
      case SENSOR_REPORTID_UNCALIBRATED_GYRO:
         data.value.Gyroscope.X = *(int16_t*)&shtp_data[9];
         data.value.Gyroscope.Y = *(int16_t*)&shtp_data[11];
         data.value.Gyroscope.Z = *(int16_t*)&shtp_data[13];
         data_type = IMU_GYROSCOPE;
         break;
      case SENSOR_REPORTID_GYROSCOPE:
         data.value.GyroscopeUncal.X = *(int16_t*)&shtp_data[9];
         data.value.GyroscopeUncal.Y = *(int16_t*)&shtp_data[11];
         data.value.GyroscopeUncal.Z = *(int16_t*)&shtp_data[13];
         data.value.GyroscopeUncal.BiasX = *(int16_t*)&shtp_data[15];
         data.value.GyroscopeUncal.BiasY = *(int16_t*)&shtp_data[17];
         data.value.GyroscopeUncal.BiasZ = *(int16_t*)&shtp_data[19];
         data_type = IMU_GYROSCOPE;
         break;
      case SENSOR_REPORTID_RAW_MAGNETOMETER:
         data.value.RawMagnetometer.X = *(int16_t*)&shtp_data[9];
         data.value.RawMagnetometer.Y = *(int16_t*)&shtp_data[11];
         data.value.RawMagnetometer.Z = *(int16_t*)&shtp_data[13];
         data.value.RawMagnetometer.TimeStamp = *(uint32_t*)&shtp_data[17];
         data_type = IMU_MAGNETOMETER;
         break;
      case SENSOR_REPORTID_MAGNETIC_FIELD:
         data.value.MagneticField.X = *(int16_t*)&shtp_data[9];
         data.value.MagneticField.Y = *(int16_t*)&shtp_data[11];
         data.value.MagneticField.Z = *(int16_t*)&shtp_data[13];
         data_type = IMU_MAGNETOMETER;
         break;
      case SENSOR_REPORTID_MAGNETIC_FIELD_UNCALIBRATED:
         data.value.MagneticFieldUncal.X = *(int16_t*)&shtp_data[9];
         data.value.MagneticFieldUncal.Y = *(int16_t*)&shtp_data[11];
         data.value.MagneticFieldUncal.Z = *(int16_t*)&shtp_data[13];
         data.value.MagneticFieldUncal.BiasX = *(int16_t*)&shtp_data[15];
         data.value.MagneticFieldUncal.BiasY = *(int16_t*)&shtp_data[17];
         data.value.MagneticFieldUncal.BiasZ = *(int16_t*)&shtp_data[19];
         data_type = IMU_MAGNETOMETER;
         break;
      case SENSOR_REPORTID_ROTATION_VECTOR:
         data.value.RotationVector.I = *(int16_t*)&shtp_data[9];
         data.value.RotationVector.J = *(int16_t*)&shtp_data[11];
         data.value.RotationVector.K = *(int16_t*)&shtp_data[13];
         data.value.RotationVector.Real = *(int16_t*)&shtp_data[15];
         data.value.RotationVector.Accuracy = *(int16_t*)&shtp_data[17];
         data_type = IMU_ROTATION_VECTOR;
         break;
      case SENSOR_REPORTID_GAME_ROTATION_VECTOR:
         data.value.GameRotationVector.I = *(int16_t*)&shtp_data[9];
         data.value.GameRotationVector.J = *(int16_t*)&shtp_data[11];
         data.value.GameRotationVector.K = *(int16_t*)&shtp_data[13];
         data.value.GameRotationVector.Real = *(int16_t*)&shtp_data[15];
         data_type = IMU_GAME_ROTATION_VECTOR;
         break;
      case SENSOR_REPORTID_GEOMAGNETIC_ROTATION_VECTOR:
         data.value.GeoMagRotationVector.I = *(int16_t*)&shtp_data[9];
         data.value.GeoMagRotationVector.J = *(int16_t*)&shtp_data[11];
         data.value.GeoMagRotationVector.K = *(int16_t*)&shtp_data[13];
         data.value.GeoMagRotationVector.Real = *(int16_t*)&shtp_data[15];
         data.value.GeoMagRotationVector.Accuracy = *(int16_t*)&shtp_data[17];
         data_type = IMU_ROTATION_VECTOR;
         break;
      case SENSOR_REPORTID_PRESSURE:
         data.value.Pressure = (float)(*(int32_t*)&shtp_data[9]) * SCALE_Q20;
         break;
      case SENSOR_REPORTID_AMBIENT_LIGHT:
         data.value.AmbientLight = (float)(*(int32_t*)&shtp_data[9]) * SCALE_Q8;
         break;
      case SENSOR_REPORTID_HUMIDITY:
         data.value.Humidity = (float)(*(int16_t*)&shtp_data[9]) * SCALE_Q8;
         break;
      case SENSOR_REPORTID_PROXIMITY:
         data.value.Proximity = (float)(*(int16_t*)&shtp_data[9]) * SCALE_Q4;
         break;
      case SENSOR_REPORTID_TEMPERATURE:
         data.value.Temperature = (float)(*(int16_t*)&shtp_data[9]) * SCALE_Q7;
         break;
      case SENSOR_REPORTID_TAP_DETECTOR:
         data.value.TapDetectorFlag = shtp_data[9];
         break;
      case SENSOR_REPORTID_STEP_DETECTOR:
         data.value.StepDetectorLatency = *(uint32_t*)&shtp_data[9];
         break;
      case SENSOR_REPORTID_STEP_COUNTER:
         data.value.StepCounter.Latency = *(uint32_t*)&shtp_data[9];
         data.value.StepCounter.Steps = *(uint32_t*)&shtp_data[13];
         data_type = IMU_STEP_COUNTER;
         break;
      case SENSOR_REPORTID_SIGNIFICANT_MOTION:
         data.value.SignificantMotion = *(uint16_t*)&shtp_data[9];
         break;
      case SENSOR_REPORTID_STABILITY_CLASSIFIER:
         data.value.StabilityClassifier = shtp_data[9];
         break;
      case SENSOR_REPORTID_SHAKE_DETECTOR:
         data.value.ShakeDetector = *(uint16_t*)&shtp_data[9];
         break;
      case SENSOR_REPORTID_FLIP_DETECTOR:
         data.value.FlipDetector = *(uint16_t*)&shtp_data[9];
         break;
      case SENSOR_REPORTID_PICKUP_DETECTOR:
         data.value.PickupDetector = *(uint16_t*)&shtp_data[9];
         break;
      case SENSOR_REPORTID_STABILITY_DETECTOR:
         data.value.StabilityDetector = *(uint16_t*)&shtp_data[9];
         in_motion = data.value.StabilityDetector & 0x0001;
         data_type = IMU_MOTION_DETECT;
         break;
      case SENSOR_REPORTID_PERSONAL_ACTIVITY_CLASSIFIER:
         data.value.PersonalActivityClassifier.Page = shtp_data[9] & 0x7F;
         data.value.PersonalActivityClassifier.LastPage = ((shtp_data[9] & 0x80) != 0);
         data.value.PersonalActivityClassifier.MostLikelyState = shtp_data[10];
         break;
      case SENSOR_REPORTID_SLEEP_DETECTOR:
         data.value.SleepDetector = shtp_data[9];
         break;
      case SENSOR_REPORTID_TILT_DETECTOR:
         data.value.TiltDetector = *(uint16_t*)&shtp_data[9];
         break;
      case SENSOR_REPORTID_POCKET_DETECTOR:
         data.value.PocketDetector = *(uint16_t*)&shtp_data[9];
         break;
      case SENSOR_REPORTID_CIRCLE_DETECTOR:
         data.value.CircleDetector = *(uint16_t*)&shtp_data[9];
         break;
      case SENSOR_REPORTID_HEART_RATE_MONITOR:
         data.value.HeartRateMonitor = *(uint16_t*)&shtp_data[9];
         break;
      case SENSOR_REPORTID_ARVR_STABILIZED_RV:
         data.value.ArVrStabilizedRV.I = (float)(*(int16_t*)&shtp_data[9]) * SCALE_Q14;
         data.value.ArVrStabilizedRV.J = (float)(*(int16_t*)&shtp_data[11]) * SCALE_Q14;
         data.value.ArVrStabilizedRV.K = (float)(*(int16_t*)&shtp_data[13]) * SCALE_Q14;
         data.value.ArVrStabilizedRV.Real = (float)(*(int16_t*)&shtp_data[15]) * SCALE_Q14;
         data.value.ArVrStabilizedRV.Accuracy = (float)(*(int16_t*)&shtp_data[17]) * SCALE_Q12;
         break;
      case SENSOR_REPORTID_ARVR_STABILIZED_GRV:
         data.value.ArVrStabilizedGRV.I = (float)(*(int16_t*)&shtp_data[9]) * SCALE_Q14;
         data.value.ArVrStabilizedGRV.J = (float)(*(int16_t*)&shtp_data[11]) * SCALE_Q14;
         data.value.ArVrStabilizedGRV.K = (float)(*(int16_t*)&shtp_data[13]) * SCALE_Q14;
         data.value.ArVrStabilizedGRV.Real = (float)(*(int16_t*)&shtp_data[15]) * SCALE_Q14;
         break;
      case SENSOR_REPORTID_GYRO_INTEGRATED_RV:
         data.value.GyroIntegratedRV.I = (float)(*(int16_t*)&shtp_data[9]) * SCALE_Q14;
         data.value.GyroIntegratedRV.J = (float)(*(int16_t*)&shtp_data[11]) * SCALE_Q14;
         data.value.GyroIntegratedRV.J = (float)(*(int16_t*)&shtp_data[13]) * SCALE_Q14;
         data.value.GyroIntegratedRV.Real = (float)(*(int16_t*)&shtp_data[15]) * SCALE_Q14;
         data.value.GyroIntegratedRV.AngleVelX = (float)(*(int16_t*)&shtp_data[17]) * SCALE_Q10;
         data.value.GyroIntegratedRV.AngleVelY = (float)(*(int16_t*)&shtp_data[19]) * SCALE_Q10;
         data.value.GyroIntegratedRV.AngleVelZ = (float)(*(int16_t*)&shtp_data[21]) * SCALE_Q10;
         break;
      case SENSOR_REPORTID_IZRO_MOTION_REQUEST:
         data.value.IzroRequest.Intent = (BNO_IZroMotionIntent_t)shtp_data[9];
         data.value.IzroRequest.Request = (BNO_IZroMotionRequest_t)shtp_data[10];
         break;
      case SENSOR_REPORTID_RAW_OPTICAL_FLOW:
         data.value.RawOptFlow.Dx = *(int16_t*)&shtp_data[9];
         data.value.RawOptFlow.Dy = *(int16_t*)&shtp_data[11];
         data.value.RawOptFlow.Iq = *(int16_t*)&shtp_data[13];
         data.value.RawOptFlow.ResX = shtp_data[15];
         data.value.RawOptFlow.ResY = shtp_data[16];
         data.value.RawOptFlow.Shutter = shtp_data[17];
         data.value.RawOptFlow.FrameMax = shtp_data[18];
         data.value.RawOptFlow.FrameAvg = shtp_data[19];
         data.value.RawOptFlow.FrameMin = shtp_data[20];
         data.value.RawOptFlow.LaserOn = shtp_data[21];
         data.value.RawOptFlow.Dt = *(int16_t*)&shtp_data[23];
         data.value.RawOptFlow.TimeStamp = *(int32_t *)&shtp_data[25];
         break;
      case SENSOR_REPORTID_DEAD_RECKONING_POSE:
         data.value.DeadReckoningPose.TimeStamp = *(int32_t *)&shtp_data[9];
         data.value.DeadReckoningPose.LinPosX = (float)(*(int32_t*)&shtp_data[13]) * SCALE_Q17;
         data.value.DeadReckoningPose.LinPosY = (float)(*(int32_t*)&shtp_data[17]) * SCALE_Q17;
         data.value.DeadReckoningPose.LinPosZ = (float)(*(int32_t*)&shtp_data[21]) * SCALE_Q17;

         data.value.DeadReckoningPose.I = (float)(*(int32_t*)&shtp_data[25]) * SCALE_Q30;
         data.value.DeadReckoningPose.J = (float)(*(int32_t*)&shtp_data[19]) * SCALE_Q30;
         data.value.DeadReckoningPose.K = (float)(*(int32_t*)&shtp_data[33]) * SCALE_Q30;
         data.value.DeadReckoningPose.Real = (float)(*(int32_t*)&shtp_data[37]) * SCALE_Q30;

         data.value.DeadReckoningPose.LinVelX = (float)(*(int32_t*)&shtp_data[41]) * SCALE_Q25;
         data.value.DeadReckoningPose.LinVelY = (float)(*(int32_t*)&shtp_data[45]) * SCALE_Q25;
         data.value.DeadReckoningPose.LinVelZ = (float)(*(int32_t*)&shtp_data[49]) * SCALE_Q25;

         data.value.DeadReckoningPose.AngleVelX = (float)(*(int32_t*)&shtp_data[53]) * SCALE_Q25;
         data.value.DeadReckoningPose.AngleVelY = (float)(*(int32_t*)&shtp_data[57]) * SCALE_Q25;
         data.value.DeadReckoningPose.AngleVelZ = (float)(*(int32_t*)&shtp_data[61]) * SCALE_Q25;
         break;
      case SENSOR_REPORTID_WHEEL_ENCODER:
         data.value.WheelEncoder.TimeStamp = *(int32_t*)&shtp_data[9];
         data.value.WheelEncoder.WheelIndex = shtp_data[13];
         data.value.WheelEncoder.DataType = shtp_data[14];
         data.value.WheelEncoder.Data = *(int16_t*)&shtp_data[15];
         break;
   }
   last_sensor_reading = data;
   return data_type;
}

static bool process_command_response(BNO_CommandResp_t *response)
{
   // Reset complete
   switch(response->command)
   {
      case COMMAND_ERRORS:
         errors = *(BNO_Error_t*)&shtp_data[5];
         return true;
      case COMMAND_COUNTER:
         counts = *(BNO_Counts_t*)&shtp_data[5];
         return true;
      case COMMAND_UNSOLICITED_INITIALIZE:  // Intentional fallthrough
      case COMMAND_INITIALIZE:
         if (!shtp_data[5])
         {
            reset_occurred = true;
            return true;
         }
         break;
      case COMMAND_DCD:
         save_dcd_status = shtp_data[5] == 0;
         return true;
      case COMMAND_ME_CALIBRATE:
         calibration_status = *(BNO_calibrationStat_t*)&shtp_data[5];
         return true;
      case COMMAND_OSCILLATOR:
         oscillator_type = shtp_data[5];
         return true;
      case COMMAND_TURNTABLE_CAL:
         calibration_status.Status = shtp_data[5];
         return true;
      case COMMAND_BOOTLOADER:
         boot_loader = *(BNO_Boot_t*)&shtp_data[6];
         return true;
   }
   return false;
}

static bool process_response(void)
{
   // Handle the response based on its type
   switch (shtp_data[0])
   {
      case SHTP_REPORT_UNSOLICITED_RESPONSE:
         return (shtp_header[2] == CHANNEL_COMMAND);
      case SHTP_REPORT_UNSOLICITED_RESPONSE1:
         return (shtp_header[2] == CHANNEL_EXECUTABLE);
      case SHTP_REPORT_COMMAND_RESPONSE:
         return process_command_response((BNO_CommandResp_t*)&shtp_data[1]);
      case SHTP_REPORT_FRS_READ_RESPONSE:
         frs_read_response = *(BNO_FrsReadResp_t*)&shtp_data[1];
         return true;
      case SHTP_REPORT_FRS_WRITE_RESPONSE:
         frs_write_response = *(BNO_FrsWriteResp_t*)&shtp_data[1];
         return true;
      case SHTP_REPORT_PRODUCT_ID_RESPONSE:
         device_id = *(BNO_productID_t*)&shtp_data[1];
         return true;
      case SHTP_REPORT_BASE_TIMESTAMP:
         if (shtp_header[2] == CHANNEL_REPORTS)
         {
            parse_input_report();
            return true;
         }
         break;
      case SHTP_REPORT_GET_FEATURE_RESPONSE:
         sensor_features = *(BNO_Feature_t*)&shtp_data[1];
         return true;
      case SHTP_REPORT_SENSOR_FLUSH_RESPONSE:
         return (shtp_header[2] == CHANNEL_REPORTS);
   }
   return false;
}

static bool wait_for_command_response(void)
{
   // Determine the expected command response type
   uint8_t receive_channel = CHANNEL_CONTROL;
   uint8_t expected_response = SHTP_REPORT_COMMAND_RESPONSE;
   switch (shtp_data[0])
   {
      case SHTP_REPORT_PRODUCT_ID_REQUEST:
         expected_response = SHTP_REPORT_PRODUCT_ID_RESPONSE;
         break;
      case SHTP_REPORT_SENSOR_FLUSH_REQUEST:
         receive_channel = CHANNEL_REPORTS;
         expected_response = SHTP_REPORT_SENSOR_FLUSH_RESPONSE;
         break;
      case SHTP_REPORT_GET_FEATURE_REQUEST:
         expected_response = SHTP_REPORT_GET_FEATURE_RESPONSE;
         break;
      case SHTP_REPORT_FRS_WRITE_REQUEST:
         expected_response = SHTP_REPORT_FRS_WRITE_RESPONSE;
         break;
      case SHTP_REPORT_FRS_READ_REQUEST:
         expected_response = SHTP_REPORT_FRS_READ_RESPONSE;
         break;
      default:
         break;
   }

   // Send the command and wait for a response
   bool success = false;
   ++awaiting_interrupt_count;
   send_packet(CHANNEL_CONTROL);
   for (uint8_t retry = 0; !success && (retry < 5); ++retry)
      success = receive_packet() && (shtp_header[2] == receive_channel) && (shtp_data[0] == expected_response);
   --awaiting_interrupt_count;
   return success ? process_response() : false;
}

static bool send_calibrate_command(uint8_t for_sensor)
{
   // Send a command to begin calibration for the specified sensor
   memset(shtp_data, 0, TX_PACKET_SIZE);
   calibration_status.Status = 1;
   switch (for_sensor)
   {
      case CALIBRATE_ACCEL:
         shtp_data[3] = 1;
         break;
      case CALIBRATE_GYRO:
         shtp_data[4] = 1;
         break;
      case CALIBRATE_MAG:
         shtp_data[5] = 1;
         break;
      case CALIBRATE_PLANAR_ACCEL:
         shtp_data[7] = 1;
         break;
      case CALIBRATE_ACCEL_GYRO_MAG:
         shtp_data[3] = 1;
         shtp_data[4] = 1;
         shtp_data[5] = 1;
         break;
      default:
         break;
   }
   shtp_data[0] = SHTP_REPORT_COMMAND_REQUEST;
   shtp_data[1] = command_sequence_number++;
   shtp_data[2] = COMMAND_ME_CALIBRATE;
   shtp_data[9] = ((for_sensor & 0x60) >> 5);
   return wait_for_command_response() && !calibration_status.Status;
}

static void set_feature(uint8_t report_id, uint32_t report_interval_us)
{
   // Transmit a "set feature" packet on the control channel
   memset(shtp_data, 0, TX_PACKET_SIZE);
   shtp_data[0] = SHTP_REPORT_SET_FEATURE_COMMAND;
   shtp_data[1] = report_id;
   *(uint32_t*)&shtp_data[5] = report_interval_us;
   send_packet(CHANNEL_CONTROL);

   // TODO: Should this be a wait_for_command_response instead? Datasheet says it should respond with SHTP_REPORT_GET_FEATURE_RESPONSE
}

static BNO_Feature_t get_feature(uint8_t report_id)
{
   // Request the settings for the specified feature
   static BNO_Feature_t empty_settings = { 0 };
   memset(shtp_data, 0, TX_PACKET_SIZE);
   shtp_data[0] = SHTP_REPORT_GET_FEATURE_REQUEST;
   shtp_data[1] = report_id;
   return wait_for_command_response() ? sensor_features : empty_settings;
}

/* UNUSED BUT HERE FOR FUTURE USE:
static bool write_frs(uint16_t length, uint16_t frs_type)
{
   // Send an FRS write request command
   memset(shtp_data, 0, TX_PACKET_SIZE);
   shtp_data[0] = SHTP_REPORT_FRS_WRITE_REQUEST;
   *(uint16_t*)&shtp_data[2] = length;
   *(uint16_t*)&shtp_data[4] = frs_type;
   if (wait_for_command_response())
   {
      // Analyze the response
      uint8_t send_more_data = 0, completed = 0;
      switch (shtp_data[1])
      {
         case FRS_WRITE_STATUS_RECEIVED:
         case FRS_WRITE_STATUS_READY:
            send_more_data = 1;
            break;
         case FRS_WRITE_STATUS_UNRECOGNIZED_FRS_TYPE:  // Lots of intentional fallthroughs
         case FRS_WRITE_STATUS_BUSY:
         case FRS_WRITE_STATUS_FAILED:
         case FRS_WRITE_STATUS_NOT_READY:
         case FRS_WRITE_STATUS_INVALID_LENGTH:
         case FRS_WRITE_STATUS_INVALID_RECORD:
         case FRS_WRITE_STATUS_DEVICE_ERROR:
         case FRS_WRITE_STATUS_READ_ONLY:
         case FRS_WRITE_STATUS_WRITE_COMPLETED:
            completed = 1;
            break;
         case FRS_WRITE_STATUS_RECORD_VALID:
            break;
      }
      return true;
   }
   return false;
}

static bool read_frs(uint16_t frsType)
{
   // Send an FRS read request command
   memset(shtp_data, 0, TX_PACKET_SIZE);
   shtp_data[0] = SHTP_REPORT_FRS_READ_REQUEST;
   *(uint16_t *)&shtp_data[4] = frsType;
   if (wait_for_command_response())
   {
      const uint8_t status = shtp_data[1] & 0x0F;
      switch (status)
      {
         case FRS_READ_STATUS_UNRECOGNIZED_FRS_TYPE:  // All fallthroughs are intentional
         case FRS_READ_STATUS_BUSY:
         case FRS_READ_STATUS_OFFSET_OUT_OF_RANGE:
         case FRS_READ_STATUS_DEVICE_ERROR:
            return false;
         case FRS_READ_STATUS_RECORD_EMPTY:
            return true;
         case FRS_READ_STATUS_READ_RECORD_COMPLETED:
         case FRS_READ_STATUS_READ_BLOCK_COMPLETED:
         case FRS_READ_STATUS_READ_BLOCK_AND_RECORD_COMPLETED:
            return true;
      }
   }
   return false;
}*/

static void reset_imu(void)
{
   // Reset the device
   am_hal_gpio_output_clear(PIN_IMU_RESET);
   am_util_delay_ms(1);
   am_hal_gpio_output_set(PIN_IMU_RESET);
}

static void enter_sleep_mode(bool start_sleeping)
{
   // Send a "sleep" or "wakeup" command to the IMU and flush and incoming data
   memset(shtp_data, 0, TX_PACKET_SIZE);
   shtp_data[0] = start_sleeping ? COMMAND_INITIALIZE_SLEEP : COMMAND_INITIALIZE_ON;
   send_packet(CHANNEL_EXECUTABLE);
}


// Interrupt Service Routines ------------------------------------------------------------------------------------------

static void imu_isr(void *args)
{
   // Only handle if not synchronously waiting for an interrupt
   print("Int\n");
   if (!awaiting_interrupt_count)
   {
      // Attempt to retrieve the IMU data packet
      imu_data_type_t data_type = IMU_UNKNOWN;
      if (receive_packet() && (shtp_header[2] == CHANNEL_REPORTS) && (shtp_data[0] == SHTP_REPORT_BASE_TIMESTAMP))
         data_type = parse_input_report();
      print("Type: %u\n", (uint32_t)shtp_data[0]);

      // Notify the appropriate data callback
      if ((data_type == IMU_MOTION_DETECT) && motion_change_callback)
         motion_change_callback(in_motion);
      else if ((data_type != IMU_UNKNOWN) && data_ready_callback)
         data_ready_callback(data_type);
   }
}

void imu_iom_isr(void)
{
   // Handle an IMU read interrupt
   static uint32_t status;
   AM_CRITICAL_BEGIN
   am_hal_iom_interrupt_status_get(spi_handle, false, &status);
   am_hal_iom_interrupt_clear(spi_handle, status);
   AM_CRITICAL_END
   am_hal_iom_interrupt_service(spi_handle, status);
}


// Public API Functions ------------------------------------------------------------------------------------------------

void imu_init(void)
{
   // Only initialize once
   if (imu_is_initialized)
      return;

   // Initialize the static variables
   memset(shtp_header, 0, sizeof(shtp_header));
   memset(shtp_data, 0, sizeof(shtp_data));
   memset(sequence_number, 0, sizeof(sequence_number));
   memset(&calibration_status, 0, sizeof(calibration_status));
   memset(&frs_write_response, 0, sizeof(frs_write_response));
   memset(&frs_read_response, 0, sizeof(frs_read_response));
   memset(&oscillator_type, 0, sizeof(oscillator_type));
   memset(&boot_loader, 0, sizeof(boot_loader));
   memset(&device_id, 0, sizeof(device_id));
   memset(&counts, 0, sizeof(counts));
   memset(&errors, 0, sizeof(errors));
   in_motion = reset_occurred = save_dcd_status = false;
   awaiting_interrupt_count = 0;
   command_sequence_number = 0;
   motion_change_callback = NULL;
   data_ready_callback = NULL;

   // Create an SPI configuration structure
   const am_hal_iom_config_t spi_config =
   {
      .eInterfaceMode = AM_HAL_IOM_SPI_MODE,
      .ui32ClockFreq = AM_HAL_IOM_3MHZ,
      .eSpiMode = AM_HAL_IOM_SPI_MODE_3,
      .pNBTxnBuf = NULL,
      .ui32NBTxnBufLength = 0
   };

   // Configure and assert the WAKE and RESET pins
   configASSERT0(am_hal_gpio_pinconfig(PIN_IMU_WAKEUP, am_hal_gpio_pincfg_output));
   am_hal_gpio_output_set(PIN_IMU_WAKEUP);
   configASSERT0(am_hal_gpio_pinconfig(PIN_IMU_RESET, am_hal_gpio_pincfg_output));
   am_hal_gpio_output_set(PIN_IMU_RESET);

   // Initialize the SPI module and enable all relevant SPI pins
   am_hal_gpio_pincfg_t sck_config = g_AM_BSP_GPIO_IOM0_SCK;
   am_hal_gpio_pincfg_t miso_config = g_AM_BSP_GPIO_IOM0_MISO;
   am_hal_gpio_pincfg_t mosi_config = g_AM_BSP_GPIO_IOM0_MOSI;
   am_hal_gpio_pincfg_t cs_config = g_AM_BSP_GPIO_IOM0_CS;
   sck_config.GP.cfg_b.uFuncSel = PIN_IMU_SPI_SCK_FUNCTION;
   miso_config.GP.cfg_b.uFuncSel = PIN_IMU_SPI_MISO_FUNCTION;
   mosi_config.GP.cfg_b.uFuncSel = PIN_IMU_SPI_MOSI_FUNCTION;
   cs_config.GP.cfg_b.uFuncSel = PIN_IMU_SPI_CS_FUNCTION;
   cs_config.GP.cfg_b.uNCE = 4 * IMU_SPI_NUMBER;
   configASSERT0(am_hal_iom_initialize(IMU_SPI_NUMBER, &spi_handle));
   configASSERT0(am_hal_gpio_pinconfig(PIN_IMU_SPI_SCK, sck_config));
   configASSERT0(am_hal_gpio_pinconfig(PIN_IMU_SPI_MISO, miso_config));
   configASSERT0(am_hal_gpio_pinconfig(PIN_IMU_SPI_MOSI, mosi_config));
   configASSERT0(am_hal_gpio_pinconfig(PIN_IMU_SPI_CS, cs_config));
   configASSERT0(am_hal_iom_power_ctrl(spi_handle, AM_HAL_SYSCTRL_WAKE, false));
   configASSERT0(am_hal_iom_configure(spi_handle, &spi_config));
   configASSERT0(am_hal_iom_enable(spi_handle));

   // Set up incoming interrupts from the IMU
   uint32_t imu_interrupt_pin = PIN_IMU_INTERRUPT;
   am_hal_gpio_pincfg_t int_config = AM_HAL_GPIO_PINCFG_INPUT;
   int_config.GP.cfg_b.eIntDir = AM_HAL_GPIO_PIN_INTDIR_HI2LO;
   configASSERT0(am_hal_gpio_pinconfig(PIN_IMU_INTERRUPT, int_config));
   configASSERT0(am_hal_gpio_interrupt_control(AM_HAL_GPIO_INT_CHANNEL_0, AM_HAL_GPIO_INT_CTRL_INDV_ENABLE, &imu_interrupt_pin));
   configASSERT0(am_hal_gpio_interrupt_register(AM_HAL_GPIO_INT_CHANNEL_0, PIN_IMU_INTERRUPT, imu_isr, NULL));
   NVIC_SetPriority(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_IMU_INTERRUPT), NVIC_configKERNEL_INTERRUPT_PRIORITY - 1);
   NVIC_SetPriority(IOMSTR0_IRQn + IMU_SPI_NUMBER, NVIC_configKERNEL_INTERRUPT_PRIORITY - 2);
   NVIC_EnableIRQ(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_IMU_INTERRUPT));
   NVIC_EnableIRQ(IOMSTR0_IRQn + IMU_SPI_NUMBER);

   // Reset the device and read until it stops sending messages
   reset_imu();
   while (receive_packet());

   // Validate device communications
   memset(shtp_data, 0, TX_PACKET_SIZE);
   shtp_data[0] = SHTP_REPORT_PRODUCT_ID_REQUEST;
   if (wait_for_command_response())
      print("INFO: IMU Initialized\n");
   else
      print("ERROR: IMU initialization failed\n");

   // Reset all interrupt statuses and set the initialized flag
   uint32_t status;
   am_hal_iom_interrupt_status_get(spi_handle, false, &status);
   am_hal_iom_interrupt_clear(spi_handle, status);
   imu_is_initialized = true;
}

void imu_deinit(void)
{
   // Only deinitialize once
   if (!imu_is_initialized)
      return;
   motion_change_callback = NULL;
   data_ready_callback = NULL;

   // Reset the device to stop all data processing and read until it stops sending messages
   reset_imu();
   while (receive_packet());

   // TODO: put the device into sleep mode, can we do this even when running?
   enter_sleep_mode(true);

   // Disable all IMU-based interrupts
   uint32_t imu_interrupt_pin = PIN_IMU_INTERRUPT;
   NVIC_DisableIRQ(IOMSTR0_IRQn + IMU_SPI_NUMBER);
   NVIC_DisableIRQ(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_IMU_INTERRUPT));
   am_hal_gpio_interrupt_register(AM_HAL_GPIO_INT_CHANNEL_0, imu_interrupt_pin, NULL, NULL);
   am_hal_gpio_interrupt_control(AM_HAL_GPIO_INT_CHANNEL_0, AM_HAL_GPIO_INT_CTRL_INDV_DISABLE, &imu_interrupt_pin);

   // Disable all SPI communications
   while (am_hal_iom_disable(spi_handle) != AM_HAL_STATUS_SUCCESS);
   am_hal_iom_uninitialize(spi_handle);
   imu_is_initialized = false;
}

bool imu_calibrate_sensors(imu_calibration_data_t calibration_type)
{
   // Begin calibration for any requested sensor types
   if ((calibration_type & IMU_CALIB_STOP) > 0)
      return send_calibrate_command(CALIBRATE_STOP);
   else if ((calibration_type & IMU_CALIB_ALL) > 0)
      return send_calibrate_command(CALIBRATE_ACCEL_GYRO_MAG);
   else if ((calibration_type & IMU_CALIB_ACCELEROMETER) > 0)
      return send_calibrate_command(CALIBRATE_ACCEL);
   else if ((calibration_type & IMU_CALIB_LINEAR_ACCELEROMETER) > 0)
      return send_calibrate_command(CALIBRATE_PLANAR_ACCEL);
   else if ((calibration_type & IMU_CALIB_GYROSCOPE) > 0)
      return send_calibrate_command(CALIBRATE_GYRO);
   else if ((calibration_type & IMU_CALIB_MAGNETOMETER) > 0)
      return send_calibrate_command(CALIBRATE_MAG);
   return false;
}

bool imu_store_current_calibration(void)
{
   // Store the current calibration data to flash
   save_dcd_status = false;
   memset(shtp_data, 0, TX_PACKET_SIZE);
   shtp_data[0] = SHTP_REPORT_COMMAND_REQUEST;
   shtp_data[1] = command_sequence_number++;
   shtp_data[2] = COMMAND_DCD;
   return wait_for_command_response() && save_dcd_status;
}

void imu_enable_data_outputs(imu_data_type_t data_types, uint32_t report_interval_us)
{
   // Enable any requested data types with the specified reporting interval
   if ((data_types & IMU_ROTATION_VECTOR) > 0)
      set_feature(SENSOR_REPORTID_ROTATION_VECTOR, report_interval_us);
   if ((data_types & IMU_GAME_ROTATION_VECTOR) > 0)
      set_feature(SENSOR_REPORTID_GAME_ROTATION_VECTOR, report_interval_us);
   if ((data_types & IMU_ACCELEROMETER) > 0)
      set_feature(SENSOR_REPORTID_ACCELEROMETER, report_interval_us);
   if ((data_types & IMU_LINEAR_ACCELEROMETER) > 0)
      set_feature(SENSOR_REPORTID_LINEAR_ACCELERATION, report_interval_us);
   if ((data_types & IMU_GYROSCOPE) > 0)
      set_feature(SENSOR_REPORTID_GYROSCOPE, report_interval_us);
   if ((data_types & IMU_MAGNETOMETER) > 0)
      set_feature(SENSOR_REPORTID_MAGNETIC_FIELD, report_interval_us);
   if ((data_types & IMU_STEP_COUNTER) > 0)
      set_feature(SENSOR_REPORTID_STEP_COUNTER, report_interval_us);
   if ((data_types & IMU_MOTION_DETECT) > 0)
      set_feature(SENSOR_REPORTID_STABILITY_DETECTOR, 1);
   if ((data_types & IMU_GRAVITY) > 0)
      set_feature(SENSOR_REPORTID_GRAVITY, report_interval_us);
}

void imu_register_motion_change_callback(motion_change_callback_t callback)
{
   // Store the motion-change callback
   motion_change_callback = callback;
}

void imu_register_data_ready_callback(data_ready_callback_t callback)
{
   // Store the data-ready callback
   data_ready_callback = callback;
}

void imu_read_accel_data(int16_t *x, int16_t *y, int16_t *z, uint8_t *accuracy)
{
   *x = last_sensor_reading.value.Accelerometer.X;
   *y = last_sensor_reading.value.Accelerometer.Y;
   *z = last_sensor_reading.value.Accelerometer.Z;
   *accuracy = last_sensor_reading.status;
}

void imu_read_linear_accel_data(int16_t *x, int16_t *y, int16_t *z, uint8_t *accuracy)
{
   *x = last_sensor_reading.value.LinearAcceleration.X;
   *y = last_sensor_reading.value.LinearAcceleration.Y;
   *z = last_sensor_reading.value.LinearAcceleration.Z;
   *accuracy = last_sensor_reading.status;
}

void imu_read_gravity_data(int16_t *x, int16_t *y, int16_t *z, uint8_t *accuracy)
{
   *x = last_sensor_reading.value.Gravity.X;
   *y = last_sensor_reading.value.Gravity.Y;
   *z = last_sensor_reading.value.Gravity.Z;
   *accuracy = last_sensor_reading.status;
}

void imu_read_quaternion_data(int16_t *w, int16_t *x, int16_t *y, int16_t *z, int16_t *radian_accuracy, uint8_t *accuracy)
{
   *w = last_sensor_reading.value.RotationVector.Real;
   *x = last_sensor_reading.value.RotationVector.I;
   *y = last_sensor_reading.value.RotationVector.J;
   *z = last_sensor_reading.value.RotationVector.K;
   *radian_accuracy = last_sensor_reading.value.RotationVector.Accuracy;
   *accuracy = last_sensor_reading.status;
}

void imu_read_magnetometer_data(int16_t *x, int16_t *y, int16_t *z, uint8_t *accuracy)
{
   *x = last_sensor_reading.value.MagneticField.X;
   *y = last_sensor_reading.value.MagneticField.Y;
   *z = last_sensor_reading.value.MagneticField.Z;
   *accuracy = last_sensor_reading.status;
}

void imu_read_gyro_data(int16_t *x, int16_t *y, int16_t *z, uint8_t *accuracy)
{
   *x = last_sensor_reading.value.Gyroscope.X;
   *y = last_sensor_reading.value.Gyroscope.Y;
   *z = last_sensor_reading.value.Gyroscope.Z;
   *accuracy = last_sensor_reading.status;
}

void imu_convert_q_format_to_float(imu_data_type_t data_type, int16_t q_x, int16_t q_y, int16_t q_z, int16_t q_w_optional, int16_t q_accuracy_optional, float *x, float *y, float *z, float *w, float *accuracy)
{
   // Convert from Q-format to float based on the data type
   switch(data_type)
   {
      case IMU_ROTATION_VECTOR:         // Intentional fallthrough
      case IMU_GAME_ROTATION_VECTOR:
         *x = (float)q_x * SCALE_Q14;
         *y = (float)q_y * SCALE_Q14;
         *z = (float)q_z * SCALE_Q14;
         *w = (float)q_w_optional * SCALE_Q14;
         *accuracy = (float)q_accuracy_optional * SCALE_Q14;
         break;
      case IMU_ACCELEROMETER:           // Intentional fallthrough
      case IMU_LINEAR_ACCELEROMETER:
      case IMU_GRAVITY:
         *x = (float)q_x * SCALE_Q8;
         *y = (float)q_y * SCALE_Q8;
         *z = (float)q_z * SCALE_Q8;
         break;
      case IMU_GYROSCOPE:
         *x = (float)q_x * SCALE_Q9;
         *y = (float)q_y * SCALE_Q9;
         *z = (float)q_z * SCALE_Q9;
         break;
      case IMU_MAGNETOMETER:
         *x = (float)q_x * SCALE_Q4;
         *y = (float)q_y * SCALE_Q4;
         *z = (float)q_z * SCALE_Q4;
         break;
      default:
         *x = q_x;
         *y = q_y;
         *z = q_z;
         if (w)
            *w = q_w_optional;
         if (accuracy)
            *accuracy = q_accuracy_optional;
         break;
   }
}

uint16_t imu_read_step_count()
{
   // Return the most recent step count
   return last_sensor_reading.value.StepCounter.Steps;
}

bool imu_read_in_motion(void)
{
   // Return the most recent motion status
   return in_motion;
}

imu_data_type_t imu_data_outputs_enabled(void)
{
   // Determine which output data types have been enabled
   imu_data_type_t outputs_enabled = 0;
   if (get_feature(SENSOR_REPORTID_ROTATION_VECTOR).reportInterval_us > 0)
      outputs_enabled |= IMU_ROTATION_VECTOR;
   if (get_feature(SENSOR_REPORTID_GAME_ROTATION_VECTOR).reportInterval_us > 0)
      outputs_enabled |= IMU_GAME_ROTATION_VECTOR;
   if (get_feature(SENSOR_REPORTID_ACCELEROMETER).reportInterval_us > 0)
      outputs_enabled |= IMU_ACCELEROMETER;
   if (get_feature(SENSOR_REPORTID_LINEAR_ACCELERATION).reportInterval_us > 0)
      outputs_enabled |= IMU_LINEAR_ACCELEROMETER;
   if (get_feature(SENSOR_REPORTID_GYROSCOPE).reportInterval_us > 0)
      outputs_enabled |= IMU_GYROSCOPE;
   if (get_feature(SENSOR_REPORTID_MAGNETIC_FIELD).reportInterval_us > 0)
      outputs_enabled |= IMU_MAGNETOMETER;
   if (get_feature(SENSOR_REPORTID_STEP_COUNTER).reportInterval_us > 0)
      outputs_enabled |= IMU_STEP_COUNTER;
   if (get_feature(SENSOR_REPORTID_STABILITY_DETECTOR).reportInterval_us > 0)
      outputs_enabled |= IMU_MOTION_DETECT;
   if (get_feature(SENSOR_REPORTID_GRAVITY).reportInterval_us > 0)
      outputs_enabled |=IMU_GRAVITY;
   return outputs_enabled;
}
