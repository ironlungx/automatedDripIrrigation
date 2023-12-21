# Configuration

## To Change any parameter change the following defines

```cpp
// Comment out these lines to disable module
#define USE_EXTERNAL_RTC                        // Use an external i2c Real Time Clock to fetch time (NOTE: Requires a relay)
#define USE_OLED                                // Use an OLED display (NOTE: Requires an OLED)
#define DEF_SSID "<SSID to try to connect to>"    // Default Wi-Fi Name
#define DEF_PASS "<Passkey to DEF_SSID>"          // Password for Wi-Fi name
```