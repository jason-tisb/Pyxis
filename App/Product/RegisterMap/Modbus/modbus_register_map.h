#ifndef APP_PRODUCT_REGISTERMAP_MODBUS_REGISTER_MAP_H_
#define APP_PRODUCT_REGISTERMAP_MODBUS_REGISTER_MAP_H_

/* Shared image registers: currently used by both holding and input callbacks. */

/* System and communication */
#define MB_REG_SLAVE_ID                    0U
#define MB_REG_BAUDRATE                   1U
#define MB_REG_PARITY                     2U
#define MB_REG_RESPONSE_DELAY             3U
#define MB_REG_MACHINE_PROFILE            4U
#define MB_REG_BASELINE_COMMAND           5U
#define MB_REG_TACH_ENABLE                6U
#define MB_REG_TACH_SOURCE                7U

/* Kinematic and bearing configuration */
#define MB_REG_GEAR_RATIO                 9U
#define MB_REG_MOTOR_SPEED                10U
#define MB_REG_BEARING_BALL_COUNT         11U
#define MB_REG_BEARING_BALL_SIZE          12U
#define MB_REG_BEARING_DIAMETER           13U
#define MB_REG_BEARING_ALPHA_ANGLE        14U
/* Optional: gear mesh analysis input */
#define MB_REG_GEAR_TEETH_NUMBER          18U

/* Signal processing configuration */
#define MB_REG_FILTER_HIGH_PASS           15U
#define MB_REG_FILTER_LOW_PASS            16U
#define MB_REG_REFRESH                    17U

/* Basic runtime telemetry */
#define MB_REG_TEMPERATURE                30U

/* X-axis vibration result block */
#define MB_REG_X_ACC_RMS                  50U
#define MB_REG_X_ACC_PEAK                 51U
#define MB_REG_X_ACC_P2P                  52U
#define MB_REG_X_CREST_FACTOR             53U
#define MB_REG_X_VEL_RMS                  54U
#define MB_REG_X_VEL_PEAK                 55U
#define MB_REG_X_DISP_P2P                 56U
#define MB_REG_X_ENV_KURTOSIS             57U
#define MB_REG_X_ISO_CLASS                58U
#define MB_REG_X_FAULT_FLAGS              59U
#define MB_REG_X_PEAK_FREQ_01             60U
#define MB_REG_X_PEAK_FREQ_02             61U
#define MB_REG_X_PEAK_FREQ_03             62U
#define MB_REG_X_PEAK_FREQ_04             63U
#define MB_REG_X_PEAK_FREQ_05             64U
#define MB_REG_X_PEAK_FREQ_06             65U
#define MB_REG_X_PEAK_FREQ_07             66U
#define MB_REG_X_PEAK_FREQ_08             67U
#define MB_REG_X_PEAK_FREQ_09             68U
#define MB_REG_X_PEAK_FREQ_10             69U
#define MB_REG_X_PEAK_FREQ_11             70U
#define MB_REG_X_PEAK_FREQ_12             71U
#define MB_REG_X_PEAK_FREQ_13             72U
#define MB_REG_X_PEAK_FREQ_14             73U
#define MB_REG_X_PEAK_FREQ_15             74U
#define MB_REG_X_PEAK_FREQ_16             75U
#define MB_REG_X_PEAK_FREQ_17             76U
#define MB_REG_X_PEAK_FREQ_18             77U
#define MB_REG_X_PEAK_FREQ_19             78U
#define MB_REG_X_PEAK_FREQ_20             79U
#define MB_REG_X_PEAK_MAG_01              80U
#define MB_REG_X_PEAK_MAG_02              81U
#define MB_REG_X_PEAK_MAG_03              82U
#define MB_REG_X_PEAK_MAG_04              83U
#define MB_REG_X_PEAK_MAG_05              84U
#define MB_REG_X_PEAK_MAG_06              85U
#define MB_REG_X_PEAK_MAG_07              86U
#define MB_REG_X_PEAK_MAG_08              87U
#define MB_REG_X_PEAK_MAG_09              88U
#define MB_REG_X_PEAK_MAG_10              89U
#define MB_REG_X_PEAK_MAG_11              90U
#define MB_REG_X_PEAK_MAG_12              91U
#define MB_REG_X_PEAK_MAG_13              92U
#define MB_REG_X_PEAK_MAG_14              93U
#define MB_REG_X_PEAK_MAG_15              94U
#define MB_REG_X_PEAK_MAG_16              95U
#define MB_REG_X_PEAK_MAG_17              96U
#define MB_REG_X_PEAK_MAG_18              97U
#define MB_REG_X_PEAK_MAG_19              98U
#define MB_REG_X_PEAK_MAG_20              99U

/* Y-axis vibration result block */
#define MB_REG_Y_ACC_RMS                  100U
#define MB_REG_Y_ACC_PEAK                 101U
#define MB_REG_Y_ACC_P2P                  102U
#define MB_REG_Y_CREST_FACTOR             103U
#define MB_REG_Y_VEL_RMS                  104U
#define MB_REG_Y_VEL_PEAK                 105U
#define MB_REG_Y_DISP_P2P                 106U
#define MB_REG_Y_ENV_KURTOSIS             107U
#define MB_REG_Y_ISO_CLASS                108U
#define MB_REG_Y_FAULT_FLAGS              109U
#define MB_REG_Y_PEAK_FREQ_01             110U
#define MB_REG_Y_PEAK_FREQ_02             111U
#define MB_REG_Y_PEAK_FREQ_03             112U
#define MB_REG_Y_PEAK_FREQ_04             113U
#define MB_REG_Y_PEAK_FREQ_05             114U
#define MB_REG_Y_PEAK_FREQ_06             115U
#define MB_REG_Y_PEAK_FREQ_07             116U
#define MB_REG_Y_PEAK_FREQ_08             117U
#define MB_REG_Y_PEAK_FREQ_09             118U
#define MB_REG_Y_PEAK_FREQ_10             119U
#define MB_REG_Y_PEAK_FREQ_11             120U
#define MB_REG_Y_PEAK_FREQ_12             121U
#define MB_REG_Y_PEAK_FREQ_13             122U
#define MB_REG_Y_PEAK_FREQ_14             123U
#define MB_REG_Y_PEAK_FREQ_15             124U
#define MB_REG_Y_PEAK_FREQ_16             125U
#define MB_REG_Y_PEAK_FREQ_17             126U
#define MB_REG_Y_PEAK_FREQ_18             127U
#define MB_REG_Y_PEAK_FREQ_19             128U
#define MB_REG_Y_PEAK_FREQ_20             129U
#define MB_REG_Y_PEAK_MAG_01              130U
#define MB_REG_Y_PEAK_MAG_02              131U
#define MB_REG_Y_PEAK_MAG_03              132U
#define MB_REG_Y_PEAK_MAG_04              133U
#define MB_REG_Y_PEAK_MAG_05              134U
#define MB_REG_Y_PEAK_MAG_06              135U
#define MB_REG_Y_PEAK_MAG_07              136U
#define MB_REG_Y_PEAK_MAG_08              137U
#define MB_REG_Y_PEAK_MAG_09              138U
#define MB_REG_Y_PEAK_MAG_10              139U
#define MB_REG_Y_PEAK_MAG_11              140U
#define MB_REG_Y_PEAK_MAG_12              141U
#define MB_REG_Y_PEAK_MAG_13              142U
#define MB_REG_Y_PEAK_MAG_14              143U
#define MB_REG_Y_PEAK_MAG_15              144U
#define MB_REG_Y_PEAK_MAG_16              145U
#define MB_REG_Y_PEAK_MAG_17              146U
#define MB_REG_Y_PEAK_MAG_18              147U
#define MB_REG_Y_PEAK_MAG_19              148U
#define MB_REG_Y_PEAK_MAG_20              149U

/* Z-axis vibration result block */
#define MB_REG_Z_ACC_RMS                  150U
#define MB_REG_Z_ACC_PEAK                 151U
#define MB_REG_Z_ACC_P2P                  152U
#define MB_REG_Z_CREST_FACTOR             153U
#define MB_REG_Z_VEL_RMS                  154U
#define MB_REG_Z_VEL_PEAK                 155U
#define MB_REG_Z_DISP_P2P                 156U
#define MB_REG_Z_ENV_KURTOSIS             157U
#define MB_REG_Z_ISO_CLASS                158U
#define MB_REG_Z_FAULT_FLAGS              159U
#define MB_REG_Z_PEAK_FREQ_01             160U
#define MB_REG_Z_PEAK_FREQ_02             161U
#define MB_REG_Z_PEAK_FREQ_03             162U
#define MB_REG_Z_PEAK_FREQ_04             163U
#define MB_REG_Z_PEAK_FREQ_05             164U
#define MB_REG_Z_PEAK_FREQ_06             165U
#define MB_REG_Z_PEAK_FREQ_07             166U
#define MB_REG_Z_PEAK_FREQ_08             167U
#define MB_REG_Z_PEAK_FREQ_09             168U
#define MB_REG_Z_PEAK_FREQ_10             169U
#define MB_REG_Z_PEAK_FREQ_11             170U
#define MB_REG_Z_PEAK_FREQ_12             171U
#define MB_REG_Z_PEAK_FREQ_13             172U
#define MB_REG_Z_PEAK_FREQ_14             173U
#define MB_REG_Z_PEAK_FREQ_15             174U
#define MB_REG_Z_PEAK_FREQ_16             175U
#define MB_REG_Z_PEAK_FREQ_17             176U
#define MB_REG_Z_PEAK_FREQ_18             177U
#define MB_REG_Z_PEAK_FREQ_19             178U
#define MB_REG_Z_PEAK_FREQ_20             179U
#define MB_REG_Z_PEAK_MAG_01              180U
#define MB_REG_Z_PEAK_MAG_02              181U
#define MB_REG_Z_PEAK_MAG_03              182U
#define MB_REG_Z_PEAK_MAG_04              183U
#define MB_REG_Z_PEAK_MAG_05              184U
#define MB_REG_Z_PEAK_MAG_06              185U
#define MB_REG_Z_PEAK_MAG_07              186U
#define MB_REG_Z_PEAK_MAG_08              187U
#define MB_REG_Z_PEAK_MAG_09              188U
#define MB_REG_Z_PEAK_MAG_10              189U
#define MB_REG_Z_PEAK_MAG_11              190U
#define MB_REG_Z_PEAK_MAG_12              191U
#define MB_REG_Z_PEAK_MAG_13              192U
#define MB_REG_Z_PEAK_MAG_14              193U
#define MB_REG_Z_PEAK_MAG_15              194U
#define MB_REG_Z_PEAK_MAG_16              195U
#define MB_REG_Z_PEAK_MAG_17              196U
#define MB_REG_Z_PEAK_MAG_18              197U
#define MB_REG_Z_PEAK_MAG_19              198U
#define MB_REG_Z_PEAK_MAG_20              199U

/* Timing registers */
#define MB_REG_TIME_PREV_W0               200U
#define MB_REG_TIME_PREV_W1               201U
#define MB_REG_TIME_PREV_W2               202U
#define MB_REG_TIME_PREV_W3               203U
#define MB_REG_TIME_NOW_W0                204U
#define MB_REG_TIME_NOW_W1                205U
#define MB_REG_TIME_NOW_W2                206U
#define MB_REG_TIME_NOW_W3                207U
#define MB_REG_TIME_DIFF_W0               208U
#define MB_REG_TIME_DIFF_W1               209U
#define MB_REG_TIME_DIFF_W2               210U
#define MB_REG_TIME_DIFF_W3               211U

/* Advanced analytics block */
#define MB_REG_CURRENT_RPM                220U
#define MB_REG_TACH_QUALITY               221U
#define MB_REG_SPEED_STATE                222U
#define MB_REG_ORDER_1X                   223U
#define MB_REG_ADV_ENV_KURTOSIS           224U
#define MB_REG_SIDEBAND_DENSITY           225U
#define MB_REG_HARMONIC_CONSISTENCY       226U
#define MB_REG_CEPSTRUM_GEAR_INDEX        227U
#define MB_REG_SPECTRAL_KURTOSIS          228U
#define MB_REG_CYCLOSTATIONARY_INDEX      229U
#define MB_REG_RUL_DAYS                   230U
#define MB_REG_FAILURE_PROBABILITY        231U
#define MB_REG_DEGRADATION_INDEX          232U
#define MB_REG_TIME_TO_MAINTENANCE        233U
#define MB_REG_BASELINE_UPDATE_COUNT      234U
#define MB_REG_BASELINE_ACTIVE            235U
#define MB_REG_ACTIVE_MACHINE_PROFILE     236U
#define MB_REG_AGING_FACTOR               237U

#endif
