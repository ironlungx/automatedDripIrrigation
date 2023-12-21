
/**
 * CONFIGURATION:
 * Change as per need
 * Comment out any one to disable module
 **/

#define USE_NTP	 // Use the Network Time Protocol to sync time
#define USE_OLED // Use an OLED display (NOTE: Requires an OLED)
#define USE_WIFI // Enable web-server and WiFi connection (Required by NTP)

#if defined(USE_NTP) && !defined(USE_WIFI)
#error "The NTP module requires WiFi, please enable the wifi module, or disable the NTP module"
#endif

#include <Arduino.h>

#ifdef USE_OLED
#include <U8g2lib.h>
#endif

#include <ESP32Time.h>

#include <ArduinoJson.h>

#include <WiFi.h>
// #include <WiFiClient.h>
// #include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>

#include <vector>
#include "time.h"
#include <RTClib.h>

#include "spiffsHelper.hpp"

#define LCDWidth u8g2.getDisplayWidth()
#define ALIGN_CENTER -1
#define ALIGN_RIGHT -2
#define ALIGN_LEFT -3

#define YES_BUTTON 35
#define NO_BUTTON 34
#define RELAY 4
#define BUZZER 26

#define DEFAULT_DURATION 7000 // in ms
#define JSON_LOCATION "/Alarms.json"

#undef U8G2_WITH_CLIP_WINDOW_SUPPORT
#undef U8G2_WITH_FONT_ROTATION
#undef U8G2_WITH_UNICODE
#undef U8G2_WITH_INTERSECTION
#undef U8G2_WITH_INTERSECTION
#undef U8G2_WITH_HVLINE_SPEED_OPTIMIZATION
#undef U8G2_WITH_FONT_ROTATION

using std::sort;
using std::vector;

#ifdef USE_OLED
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0);
#endif

ESP32Time rtc;
AsyncWebServer server(80);
// WiFiManager wm;

tm nextAlarm;
vector<tm> Alarms = {};

struct Alarm
{
	tm time;
	time_t timeAdded;
}; // A structure for Alarms

/// @brief A function to init the tm structure
/// @param hours
/// @param mins
/// @param secs
/// @return tm structure
tm its(int hours, int mins, int secs) // InitTimeStructure
{
	tm x = {secs,
			mins,
			hours,
			0, 0, 0, 0, 0, 0};

	return x;
}

#ifdef USE_OLED
/// @brief             Method to easily print to display
/// @param align       The x coordinate. Send ALIGN_CENTER or ALIGN_RIGHT or ALIGN_LEFT for predefined alignments
/// @param y           The y coordinate
/// @param text        Text to print
/// @param font        Font to use
/// @param clear       Clear the display?
/// @param sendBuffer  Send the display buffer? Useful to prevent flickering and queue up text
void fastPrint(int align, int y, String text, const uint8_t *font = u8g2_font_ncenB14_tr, bool clear = true, bool sendBuffer = false)
{
	if (clear)
		u8g2.clearBuffer();

	int width = u8g2.getDisplayWidth();

	u8g2.setFont(font);

	switch (align)
	{
	case ALIGN_CENTER:
		u8g2.setCursor((width - u8g2.getUTF8Width(text.c_str())) / 2, y);
		break;
	case ALIGN_LEFT:
		u8g2.setCursor(0, y);
		break;
	case ALIGN_RIGHT:
		u8g2.setCursor(width - u8g2.getUTF8Width(text.c_str()), y);
		break;
	default:
		u8g2.setCursor(align, y);
		break;
	}
	u8g2.print(text.c_str());

	if (sendBuffer)
	{
		u8g2.sendBuffer();
	}
}

#else
void fastPrint(int align, int y, String text, const uint8_t *font = 0, bool clear = true, bool sendBuffer = false)
{
}
const uint8_t *u8g2_font_ncenB08_tr = 0;

#endif

/// @brief  Get formated time
/// @return String containing H:M:S
inline String getTimeF()
{
	return (rtc.getTime("%H:%M:%S"));
}

/// @brief           Compare two tm structures
/// @param a         Pointer to one tm structure
/// @param b         Pointer to second tm structure
/// @param threshold Threshold for comparing
/// @return
inline bool compareAlarms(const tm &a, const tm &b, int threshold = 0)
{
	if (abs(a.tm_hour - b.tm_hour) > threshold)
		return false;
	else if (abs(a.tm_min - b.tm_min) > threshold)
		return false;
	else if (abs(a.tm_sec - b.tm_sec) > threshold)
		return false;
	else
		return true;
}
/// @brief Returns the closest Alarm
/// @param now Current time
/// @param alarms Pointer to vector of current alarms
/// @return
tm getNextAlarm(const tm &now, const vector<tm> &alarms)
{
	if (alarms.empty())
	{
		return {};
	}

	vector<tm> sorted = alarms;

	sort(sorted.begin(), sorted.end(), [](const tm &a, const tm &b)
		 {
			 if (a.tm_hour > b.tm_hour)
				 return false;
			 else if (a.tm_hour < b.tm_hour)
				 return true;

			 if (a.tm_min > b.tm_min)
				 return false;
			 else if (a.tm_min < b.tm_min)
				 return true;

			 if (a.tm_sec > b.tm_sec)
				 return false;
			 else if (a.tm_sec < b.tm_sec)
				 return true;

			 return false; });

	auto it = std::lower_bound(sorted.begin(), sorted.end(), now, [](const tm &a, const tm &b)
							   {
			 if (a.tm_hour > b.tm_hour)
				 return false;
			 else if (a.tm_hour < b.tm_hour)
				 return true;

			 if (a.tm_min > b.tm_min)
				 return false;
			 else if (a.tm_min < b.tm_min)
				 return true;

			 if (a.tm_sec > b.tm_sec)
				 return false;
			 else if (a.tm_sec < b.tm_sec)
				 return true;

			 return false; });

	if (it == sorted.end())
		return sorted.front();
	else
		return *it;
}

/// @brief Load alarms from SPIFFS
/// @param alarms Pointer to the vector that should hold all of the alarms
void loadAlarms(vector<tm> *alarms)
{
	DynamicJsonDocument doc(512);
	deserializeJson(doc, SpiffsHelper::readFile(JSON_LOCATION));
	JsonArray arr = doc.as<JsonArray>();

	for (JsonVariant i : arr)
	{
		tm a = its(i["tm_hour"], i["tm_min"], i["tm_sec"]);

		alarms->push_back(a);
	}
}

void setSystemTimeFromRTC()
{
	RTC_DS3231 _rtc;
	_rtc.begin();
	// _rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

	DateTime now = _rtc.now();
	struct tm tmStruct;

	tmStruct.tm_year = now.year() - 1900;
	tmStruct.tm_mon = now.month() - 1;
	tmStruct.tm_mday = now.day();
	tmStruct.tm_hour = now.hour();
	tmStruct.tm_min = now.minute();
	tmStruct.tm_sec = now.second();

	time_t time = mktime(&tmStruct);
	struct timeval tv = {time, 0};

	settimeofday(&tv, nullptr);

	Serial.println("Time set from RTC!");
}

void setRTCTimeFromNTP()
{
	RTC_DS3231 _rtc; 

	if (!_rtc.begin())
	{
		Serial.println("Couldn't find RTC");
		while (1)
			;
	}

	// Connect to WiFi
	WiFi.disconnect();
	WiFi.mode(WIFI_STA);
	WiFi.begin("Hobbiton", "taxicab1729");

	// Wait for WiFi connection
	while (WiFi.status() != WL_CONNECTED)
	{
		delay(1000);
		Serial.println("Connecting to WiFi...");
	}
	Serial.println("Connected to WiFi");

	// Set the time zone to UTC+5:30
	configTime(19800, 0, "pool.ntp.org");
	struct tm timeinfo;
	if (getLocalTime(&timeinfo))
	{
		rtc.setTimeStruct(timeinfo);
	}

	tm now = rtc.getTimeStruct();

	// Adjust the RTC time
	Serial.println(getTimeF());
	_rtc.adjust(DateTime(now.tm_year + 1900, now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec));
	Serial.println("RTC time set from NTP!");
}

/// @brief Append an alarm, by updating SPIFFS & the internal vector
/// @param alarm The alarm that is to be appended
/// @param alarms Pointer to the vector that holds all of the alarms
void appendAlarm(tm *alarm, vector<tm> *alarms)
{
	// Add the new alarm to the vector
	alarms->push_back(*alarm);

	// Save the updated vector to the JSON file
	DynamicJsonDocument doc(512);
	JsonArray arr = doc.to<JsonArray>();

	for (const auto a : *alarms)
	{
		JsonObject obj = arr.createNestedObject();
		obj["tm_hour"] = a.tm_hour;
		obj["tm_min"] = a.tm_min;
		obj["tm_sec"] = a.tm_sec;
	}

	String jsonContents;
	serializeJson(doc, jsonContents);

	SpiffsHelper::writeFile(JSON_LOCATION, jsonContents);
}

void setup()
{
	// Setup the pins
	pinMode(RELAY, OUTPUT);
	pinMode(BUZZER, OUTPUT);
	digitalWrite(RELAY, HIGH);
	digitalWrite(BUZZER, HIGH);
#ifdef USE_OLED
	u8g2.begin(); // Start the OLED display
	fastPrint(ALIGN_LEFT, 10, "Starting Setup", u8g2_font_ncenB08_tr, true, true);
#endif
	Serial.begin(115200);
	digitalWrite(BUZZER, LOW);

	// wm.resetSettings(); // Reset previously saved WiFi credentials (Just for testing)
	/* 	wm.setHttpPort(80); // Set the WiFi manager port to 80
		wm.setDarkMode(true);

		wm.setAPCallback([](WiFiManager *wm)
						 {
							 fastPrint(ALIGN_LEFT, 10, "Using configPortal", u8g2_font_ncenB08_tr, true, false);
							 fastPrint(ALIGN_LEFT, 21, String("AP: ESP32 wm"), u8g2_font_ncenB08_tr, false, false);
							 fastPrint(ALIGN_LEFT, 32, "IP: 192.168.4.1", u8g2_font_ncenB08_tr, false, true);
							 // Serial.println("UwU");
						 });
		bool usedConfigPortal = false;
		wm.setSaveConfigCallback([&usedConfigPortal]()
								 { usedConfigPortal = true; });

		if (wm.autoConnect("ESP32 wm"))
		{
			// Connected to wifi
			if (usedConfigPortal)
				ESP.restart();
			else
			{
				fastPrint(ALIGN_LEFT, 21, "Connected to WiFi", u8g2_font_ncenB08_tr, false, true);
			}
		} */


	WiFi.mode(WIFI_AP);
	WiFi.softAP("Drip Irrigation");

	digitalWrite(BUZZER, HIGH);
	delay(100);
	digitalWrite(BUZZER, LOW);

	fastPrint(ALIGN_LEFT, 32, "Setting Time", u8g2_font_ncenB08_tr, false, true);

	/* 	delay(1000);
		configTime(19800, 0, "pool.ntp.org");
		struct tm timeinfo;
		if (getLocalTime(&timeinfo))
		{
			rtc.setTimeStruct(timeinfo);
		} */

	setSystemTimeFromRTC();

	// delay(100);
	fastPrint(ALIGN_LEFT, 43, "Starting SPIFFS", u8g2_font_ncenB08_tr, false, true);

	if (!SPIFFS.begin(true))
	{
		Serial.println("An error occurred while mounting SPIFFS");
		return;
	}

	server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
			  {
		String htmlContent = SpiffsHelper::readFile("/WebPages/root.html");
		if (htmlContent.isEmpty())
		{
			request->send(500, "text/html", "<h1>Internal Server Error</h1>");
		}
		request->send(200, "text/html", htmlContent); });

	server.on("/submit", HTTP_POST, [](AsyncWebServerRequest *request)
			  {
		String hours = request->arg("hours");
		String mins = request->arg("mins");
		String seconds = request->arg("seconds");

		tm Alarm = its(hours.toInt(), mins.toInt(), seconds.toInt());
		// Alarms.push_back(Alarm);
		appendAlarm(&Alarm, &Alarms);
		nextAlarm = getNextAlarm(rtc.getTimeStruct(), Alarms);

		// Send a redirect response to the main page
		request->redirect("/"); });

	server.on("/remove_alarm", HTTP_POST, [](AsyncWebServerRequest *request)
			  {
		String indexStr = request->arg("index");
		int index = indexStr.toInt();

		if (index >= 0 && index < Alarms.size()) {	
			Alarms.erase(Alarms.begin() + index);

			// Update the Alarms.json file
			DynamicJsonDocument doc(512);
			JsonArray arr = doc.to<JsonArray>();

			for (const auto &alarm : Alarms) {
				JsonObject obj = arr.createNestedObject();
				obj["tm_hour"] = alarm.tm_hour;
				obj["tm_min"] = alarm.tm_min;
				obj["tm_sec"] = alarm.tm_sec;
			}

			String jsonContents;
			serializeJson(doc, jsonContents);

			SpiffsHelper::writeFile(JSON_LOCATION, jsonContents);

			// Update nextAlarm
			nextAlarm = getNextAlarm(rtc.getTimeStruct(), Alarms);

			request->send(200, "application/json", "{\"success\": true}");
			request->redirect("/");
		} else {
			request->send(400, "application/json", "{\"success\": false, \"error\": \"Invalid index\"}");
		} });

	server.on("/alarms", HTTP_GET, [](AsyncWebServerRequest *request)
			  {
		// Convert alarms vector to JSON format
		String jsonAlarms = "[";
		for (const auto &alarm : Alarms)
		{
			jsonAlarms += "{";
			jsonAlarms += "\"tm_hour\":" + String(alarm.tm_hour) + ",";
			jsonAlarms += "\"tm_min\":" + String(alarm.tm_min) + ",";
			jsonAlarms += "\"tm_sec\":" + String(alarm.tm_sec);
			jsonAlarms += "},";
		}
		jsonAlarms.remove(jsonAlarms.length() - 1); // Remove the trailing comma
		jsonAlarms += "]";

		request->send(200, "application/json", jsonAlarms); });

	server.on("/isAlarm", HTTP_GET, [](AsyncWebServerRequest *request)
			  { request->send(200, "text/plain", compareAlarms(rtc.getTimeStruct(), nextAlarm, DEFAULT_DURATION) ? "YES" : "NO"); });

	server.on("/time", HTTP_GET, [](AsyncWebServerRequest *request)
			  { request->send(200, "text/plain", getTimeF()); });

	fastPrint(ALIGN_LEFT, 54, "Starting WebServer", u8g2_font_ncenB08_tr, false, true);

	server.begin();
	// delay(100);
	loadAlarms(&Alarms);
	nextAlarm = getNextAlarm(rtc.getTimeStruct(), Alarms);
}

void loop()
{
	tm now = rtc.getTimeStruct();
	tm next = nextAlarm;

	fastPrint(ALIGN_LEFT, 10, String("Time: ") + getTimeF(), u8g2_font_ncenB08_tr);
	fastPrint(ALIGN_LEFT, 21, String("Irrigating at: ") + String(nextAlarm.tm_hour) + ":" + String(nextAlarm.tm_min) + ":" + String(nextAlarm.tm_sec), u8g2_font_ncenB08_tr, false);
	fastPrint(ALIGN_LEFT, 57, String("IP: ") + WiFi.softAPIP().toString(), u8g2_font_ncenB08_tr, false);

	if (compareAlarms(now, next))
	{
		fastPrint(0, 10, "Starting Water Pump", u8g2_font_ncenB08_tr, true, false);
		fastPrint(0, 57, String("IP: ") + WiFi.localIP().toString(), u8g2_font_ncenB08_tr, false, true);

		digitalWrite(BUZZER, HIGH);
		delay(200);
		digitalWrite(BUZZER, LOW);
		delay(200);
		digitalWrite(BUZZER, HIGH);
		delay(200);
		digitalWrite(BUZZER, LOW);

		digitalWrite(RELAY, LOW);
		delay(DEFAULT_DURATION);
		digitalWrite(RELAY, HIGH);

		nextAlarm = getNextAlarm(rtc.getTimeStruct(), Alarms);
	}

	// Can do other tasks

#ifdef USE_OLED
	u8g2.sendBuffer(); // prevent flickering
#endif
}
