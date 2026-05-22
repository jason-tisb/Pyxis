#ifndef APP_PRODUCT_REGISTERMAP_MODBUS_REGISTER_MAP_H_
#define APP_PRODUCT_REGISTERMAP_MODBUS_REGISTER_MAP_H_

/*
 * Register index definitions.
 *
 * IMPORTANT:
 * - Holding registers (4xxxx/5xxxx): writable config/commands (usSRegHoldBuf[]).
 * - Input registers (3xxxx): read-only telemetry/outputs (usSRegInBuf[]).
 */

/* ========================= Holding Registers (4xxxx/5xxxx) ========================= */

/* System and communication (40001..) */
#define MB_REG_SLAVE_ID                    0U
#define MB_REG_BAUDRATE                   1U
#define MB_REG_PARITY                     2U
#define MB_REG_RESPONSE_DELAY             3U
#define MB_REG_MACHINE_PROFILE            4U
#define MB_REG_BASELINE_COMMAND           5U
#define MB_REG_TACH_ENABLE                6U
#define MB_REG_TACH_SOURCE                7U

/* Holding-register aliases (4xxxx) to match external documentation. */
#define MB_REG_WATCHDOG                   MB_REG_MACHINE_PROFILE
#define MB_REG_SYSTEM_COMMAND             MB_REG_BASELINE_COMMAND
#define MB_REG_ANALOG_DATA_FORMAT         MB_REG_TACH_ENABLE

/* Kinematic and bearing configuration */
/* NOTE: mapped to external holding addresses 51521..51530 (40001-based index). */
#define MB_REG_GEAR_RATIO                 11520U  /* 51521 */
#define MB_REG_BASE_FREQ_HZ               11521U  /* 51522 */
#define MB_REG_BEARING_BALL_COUNT         11522U  /* 51523 */
#define MB_REG_BEARING_BALL_SIZE          11523U  /* 51524 */
#define MB_REG_BEARING_DIAMETER           11524U  /* 51525 */
#define MB_REG_BEARING_ALPHA_ANGLE        11525U  /* 51526 */
/* Optional: gear mesh analysis input */
#define MB_REG_GEAR_TEETH_NUMBER          11529U  /* 51530 */

/* Signal processing configuration */
#define MB_REG_FILTER_HIGH_PASS           11526U  /* 51527 */
#define MB_REG_FILTER_LOW_PASS            11527U  /* 51528 */
#define MB_REG_REFRESH                    11528U  /* 51529 */

/* Remapping address table (Holding): 51649..51773 (125 words). */
#define MB_REG_REMAPPING_ADDR_START       11648U  /* 51649 */
#define MB_REG_REMAPPING_ADDR_COUNT       125U
#define MB_REG_REMAPPING_ADDR_END         (MB_REG_REMAPPING_ADDR_START + MB_REG_REMAPPING_ADDR_COUNT - 1U) /* 51773 */
#define MB_REG_REMAPPING_ADDR             MB_REG_REMAPPING_ADDR_START

/* ============================= Input Registers (3xxxx) ============================= */

/* Identity block (30001..) */
#define MB_IN_DEVICE_CODE                0U
#define MB_IN_HARDWARE_VERSION           1U
#define MB_IN_FIRMWARE_VERSION           2U
#define MB_IN_MANUFACTURING_DATE         3U
#define MB_IN_SERIAL_NUMBER_UPPER_W0     4U
#define MB_IN_SERIAL_NUMBER_UPPER_W1     5U

#define MB_DEVICE_CODE_DEFAULT            0U
#define MB_HARDWARE_VERSION_DEFAULT       0U
#define MB_FIRMWARE_VERSION_DEFAULT       0U
#define MB_MANUFACTURING_DATE_DEFAULT     0U
#define MB_SERIAL_NUMBER_UPPER_W0_DEFAULT 0U
#define MB_SERIAL_NUMBER_UPPER_W1_DEFAULT 0U

/* Basic runtime telemetry */
#define MB_REG_TEMPERATURE                1600U
#define MB_REG_MOTOR_TEMPERATURE          1601U

/* X-axis vibration result block */
#define MB_REG_X_ACC_RMS                  1616U
#define MB_REG_X_ACC_PEAK                 1617U
#define MB_REG_X_ACC_P2P                  1618U
#define MB_REG_X_CREST_FACTOR             1619U
#define MB_REG_X_VEL_RMS                  1620U
#define MB_REG_X_VEL_PEAK                 1621U
#define MB_REG_X_DISP_P2P                 1622U
#define MB_REG_X_ENV_KURTOSIS             1623U
#define MB_REG_X_ISO_CLASS                1624U
#define MB_REG_X_FAULT_FLAGS              1625U
#define MB_REG_X_PEAK_FREQ_01             1626U
#define MB_REG_X_PEAK_FREQ_02             1627U
#define MB_REG_X_PEAK_FREQ_03             1628U
#define MB_REG_X_PEAK_FREQ_04             1629U
#define MB_REG_X_PEAK_FREQ_05             1630U
#define MB_REG_X_PEAK_FREQ_06             1631U
#define MB_REG_X_PEAK_FREQ_07             1632U
#define MB_REG_X_PEAK_FREQ_08             1633U
#define MB_REG_X_PEAK_FREQ_09             1634U
#define MB_REG_X_PEAK_FREQ_10             1635U
#define MB_REG_X_PEAK_FREQ_11             1636U
#define MB_REG_X_PEAK_FREQ_12             1637U
#define MB_REG_X_PEAK_FREQ_13             1638U
#define MB_REG_X_PEAK_FREQ_14             1639U
#define MB_REG_X_PEAK_FREQ_15             1640U
#define MB_REG_X_PEAK_FREQ_16             1641U
#define MB_REG_X_PEAK_FREQ_17             1642U
#define MB_REG_X_PEAK_FREQ_18             1643U
#define MB_REG_X_PEAK_FREQ_19             1644U
#define MB_REG_X_PEAK_FREQ_20             1645U
#define MB_REG_X_PEAK_MAG_01              1646U
#define MB_REG_X_PEAK_MAG_02              1647U
#define MB_REG_X_PEAK_MAG_03              1648U
#define MB_REG_X_PEAK_MAG_04              1649U
#define MB_REG_X_PEAK_MAG_05              1650U
#define MB_REG_X_PEAK_MAG_06              1651U
#define MB_REG_X_PEAK_MAG_07              1652U
#define MB_REG_X_PEAK_MAG_08              1653U
#define MB_REG_X_PEAK_MAG_09              1654U
#define MB_REG_X_PEAK_MAG_10              1655U
#define MB_REG_X_PEAK_MAG_11              1656U
#define MB_REG_X_PEAK_MAG_12              1657U
#define MB_REG_X_PEAK_MAG_13              1658U
#define MB_REG_X_PEAK_MAG_14              1659U
#define MB_REG_X_PEAK_MAG_15              1660U
#define MB_REG_X_PEAK_MAG_16              1661U
#define MB_REG_X_PEAK_MAG_17              1662U
#define MB_REG_X_PEAK_MAG_18              1663U
#define MB_REG_X_PEAK_MAG_19              1664U
#define MB_REG_X_PEAK_MAG_20              1665U

/* Y-axis vibration result block */
#define MB_REG_Y_ACC_RMS                  1712U
#define MB_REG_Y_ACC_PEAK                 1713U
#define MB_REG_Y_ACC_P2P                  1714U
#define MB_REG_Y_CREST_FACTOR             1715U
#define MB_REG_Y_VEL_RMS                  1716U
#define MB_REG_Y_VEL_PEAK                 1717U
#define MB_REG_Y_DISP_P2P                 1718U
#define MB_REG_Y_ENV_KURTOSIS             1719U
#define MB_REG_Y_ISO_CLASS                1720U
#define MB_REG_Y_FAULT_FLAGS              1721U
#define MB_REG_Y_PEAK_FREQ_01             1722U
#define MB_REG_Y_PEAK_FREQ_02             1723U
#define MB_REG_Y_PEAK_FREQ_03             1724U
#define MB_REG_Y_PEAK_FREQ_04             1725U
#define MB_REG_Y_PEAK_FREQ_05             1726U
#define MB_REG_Y_PEAK_FREQ_06             1727U
#define MB_REG_Y_PEAK_FREQ_07             1728U
#define MB_REG_Y_PEAK_FREQ_08             1729U
#define MB_REG_Y_PEAK_FREQ_09             1730U
#define MB_REG_Y_PEAK_FREQ_10             1731U
#define MB_REG_Y_PEAK_FREQ_11             1732U
#define MB_REG_Y_PEAK_FREQ_12             1733U
#define MB_REG_Y_PEAK_FREQ_13             1734U
#define MB_REG_Y_PEAK_FREQ_14             1735U
#define MB_REG_Y_PEAK_FREQ_15             1736U
#define MB_REG_Y_PEAK_FREQ_16             1737U
#define MB_REG_Y_PEAK_FREQ_17             1738U
#define MB_REG_Y_PEAK_FREQ_18             1739U
#define MB_REG_Y_PEAK_FREQ_19             1740U
#define MB_REG_Y_PEAK_FREQ_20             1741U
#define MB_REG_Y_PEAK_MAG_01              1742U
#define MB_REG_Y_PEAK_MAG_02              1743U
#define MB_REG_Y_PEAK_MAG_03              1744U
#define MB_REG_Y_PEAK_MAG_04              1745U
#define MB_REG_Y_PEAK_MAG_05              1746U
#define MB_REG_Y_PEAK_MAG_06              1747U
#define MB_REG_Y_PEAK_MAG_07              1748U
#define MB_REG_Y_PEAK_MAG_08              1749U
#define MB_REG_Y_PEAK_MAG_09              1750U
#define MB_REG_Y_PEAK_MAG_10              1751U
#define MB_REG_Y_PEAK_MAG_11              1752U
#define MB_REG_Y_PEAK_MAG_12              1753U
#define MB_REG_Y_PEAK_MAG_13              1754U
#define MB_REG_Y_PEAK_MAG_14              1755U
#define MB_REG_Y_PEAK_MAG_15              1756U
#define MB_REG_Y_PEAK_MAG_16              1757U
#define MB_REG_Y_PEAK_MAG_17              1758U
#define MB_REG_Y_PEAK_MAG_18              1759U
#define MB_REG_Y_PEAK_MAG_19              1760U
#define MB_REG_Y_PEAK_MAG_20              1761U

/* Z-axis vibration result block */
#define MB_REG_Z_ACC_RMS                  1808U
#define MB_REG_Z_ACC_PEAK                 1809U
#define MB_REG_Z_ACC_P2P                  1810U
#define MB_REG_Z_CREST_FACTOR             1811U
#define MB_REG_Z_VEL_RMS                  1812U
#define MB_REG_Z_VEL_PEAK                 1813U
#define MB_REG_Z_DISP_P2P                 1814U
#define MB_REG_Z_ENV_KURTOSIS             1815U
#define MB_REG_Z_ISO_CLASS                1816U
#define MB_REG_Z_FAULT_FLAGS              1817U
#define MB_REG_Z_PEAK_FREQ_01             1818U
#define MB_REG_Z_PEAK_FREQ_02             1819U
#define MB_REG_Z_PEAK_FREQ_03             1820U
#define MB_REG_Z_PEAK_FREQ_04             1821U
#define MB_REG_Z_PEAK_FREQ_05             1822U
#define MB_REG_Z_PEAK_FREQ_06             1823U
#define MB_REG_Z_PEAK_FREQ_07             1824U
#define MB_REG_Z_PEAK_FREQ_08             1825U
#define MB_REG_Z_PEAK_FREQ_09             1826U
#define MB_REG_Z_PEAK_FREQ_10             1827U
#define MB_REG_Z_PEAK_FREQ_11             1828U
#define MB_REG_Z_PEAK_FREQ_12             1829U
#define MB_REG_Z_PEAK_FREQ_13             1830U
#define MB_REG_Z_PEAK_FREQ_14             1831U
#define MB_REG_Z_PEAK_FREQ_15             1832U
#define MB_REG_Z_PEAK_FREQ_16             1833U
#define MB_REG_Z_PEAK_FREQ_17             1834U
#define MB_REG_Z_PEAK_FREQ_18             1835U
#define MB_REG_Z_PEAK_FREQ_19             1836U
#define MB_REG_Z_PEAK_FREQ_20             1837U
#define MB_REG_Z_PEAK_MAG_01              1838U
#define MB_REG_Z_PEAK_MAG_02              1839U
#define MB_REG_Z_PEAK_MAG_03              1840U
#define MB_REG_Z_PEAK_MAG_04              1841U
#define MB_REG_Z_PEAK_MAG_05              1842U
#define MB_REG_Z_PEAK_MAG_06              1843U
#define MB_REG_Z_PEAK_MAG_07              1844U
#define MB_REG_Z_PEAK_MAG_08              1845U
#define MB_REG_Z_PEAK_MAG_09              1846U
#define MB_REG_Z_PEAK_MAG_10              1847U
#define MB_REG_Z_PEAK_MAG_11              1848U
#define MB_REG_Z_PEAK_MAG_12              1849U
#define MB_REG_Z_PEAK_MAG_13              1850U
#define MB_REG_Z_PEAK_MAG_14              1851U
#define MB_REG_Z_PEAK_MAG_15              1852U
#define MB_REG_Z_PEAK_MAG_16              1853U
#define MB_REG_Z_PEAK_MAG_17              1854U
#define MB_REG_Z_PEAK_MAG_18              1855U
#define MB_REG_Z_PEAK_MAG_19              1856U
#define MB_REG_Z_PEAK_MAG_20              1857U

/* Timing registers */
#define MB_REG_TIME_PREV_W0               1904U
#define MB_REG_TIME_PREV_W1               1905U
#define MB_REG_TIME_PREV_W2               1906U
#define MB_REG_TIME_PREV_W3               1907U
#define MB_REG_TIME_NOW_W0                1908U
#define MB_REG_TIME_NOW_W1                1909U
#define MB_REG_TIME_NOW_W2                1910U
#define MB_REG_TIME_NOW_W3                1911U
#define MB_REG_TIME_DIFF_W0               1912U
#define MB_REG_TIME_DIFF_W1               1913U
#define MB_REG_TIME_DIFF_W2               1914U
#define MB_REG_TIME_DIFF_W3               1915U

/* Advanced analytics block */
#define MB_REG_CURRENT_RPM                1916U
#define MB_REG_TACH_QUALITY               1917U
#define MB_REG_SPEED_STATE                1918U
#define MB_REG_ORDER_1X                   1919U
#define MB_REG_ADV_ENV_KURTOSIS           1920U
#define MB_REG_SIDEBAND_DENSITY           1921U
#define MB_REG_HARMONIC_CONSISTENCY       1922U
#define MB_REG_CEPSTRUM_GEAR_INDEX        1923U
#define MB_REG_SPECTRAL_KURTOSIS          1924U
#define MB_REG_CYCLOSTATIONARY_INDEX      1925U
#define MB_REG_RUL_DAYS                   1926U
#define MB_REG_FAILURE_PROBABILITY        1927U
#define MB_REG_DEGRADATION_INDEX          1928U
#define MB_REG_TIME_TO_MAINTENANCE        1929U
#define MB_REG_BASELINE_UPDATE_COUNT      1930U
#define MB_REG_BASELINE_ACTIVE            1931U
#define MB_REG_ACTIVE_MACHINE_PROFILE     1932U
#define MB_REG_AGING_FACTOR               1933U

#endif
