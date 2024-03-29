/* ************************************************************************* *
   Matrix Voice Audio Streamer

   This program is written to be a streaming audio server running on the Matrix
   Voice. This is typically used for Rhasspy.
   See https://rhasspy.readthedocs.io/en/latest/ for more information

   Original Author:  Paul Romkesg
   Date:    Januari 2021
   Version: 7.0


   Requirements
   ============

   Look at the list of required libraries in General.hpp. For example:
   install  https://github.com/me-no-dev/ESPAsyncWebServer
   install 'RingBuffer' library from Locoduino https://github.com/Locoduino/RingBuffer
   install 'PubSubClient' library from https://github.com/knolleary/pubsubclient.git
   https://github.com/matrix-io/matrixio_hal_esp32.git
   https://github.com/marvinroger/async-mqtt-client.git
   https://github.com/me-no-dev/AsyncTCP.git
   install 'ArduinoJson' library from https://github.com/bblanchon/ArduinoJson.git
   ESP Async WebServer
   m5stack/M5Atom
   fastled/FastLED
   yveaux/AC101 @ ^0.0.1


   

   Changelog:
   ==========
   v1:
    - first code release. It needs a lot of improvement, no hardcoding stuff
   v2:
    - Change to Arduino IDE
   v2.1:
    - Changed to pubsubclient and fixed other stability issues
   v3:
    - Add OTA
   v3.1:
    - Only listen to SITEID to toggle hotword
    - Got rid of String, leads to Heap Fragmentation
    - Add dynamic brightness, post {"brightness": 50 } to SITEID/everloop
    - Fix stability, using semaphores
   v3.2:
    - Add dynamic colors, see readme for documentation
    - Restart the device by publishing hashed password to SITEID/restart
    - Adjustable framerate, more info at
      https://snips.gitbook.io/documentation/advanced-configuration/platform-configuration
    - Rotating animation possible, not finished or used yet
   v3.3:
    - Added support for Rhasspy https://github.com/synesthesiam/rhasspy
    - Started implementing playBytes, not finished
   v3.4:
    - Implemented playBytes, basics done but sometimes audio garbage out
   v4.0:
    - playBytes working, only plays 44100 samplerate (mono/stereo) correctly. Work in progress
    - Upgrade to ArduinoJSON 6
    - Add mute/unmute via MQTT
    - Fixed OTA issues, remove webserver
   v4.1:
    - Configurable mic gain
    - Fix on only listening to Dutch Rhasspy
   v4.2:
    - Support platformIO
    v4.3:
    - Force platform 1.9.0. Higher raises issues with the mic array
    - Add muting of output and switching of output port
   v4.4:
    - Fix distortion issues, caused by incorrect handling of incoming audio
    - Added resampling using Speex, resamples 8000 and up and converts mono 
      to stereo. 
   v4.5:
    - Support streaming audio
   v4.5.1:
    - Fix distortion on lower samplerates
   v5.0:
    - Added ondevice wakeword detection using WakeNet, only Alexa available
   v5.1:
    - Added volume control, publish {"volume": 50} to the sitesid/audio topic
   v5.12:
    - Add dynamic hotword brightness, post {"hotword_brightness": 50 } to SITEID/everloop
   v5.12.1:
    - Fixed a couple of defects regarding input mute and disconnects
   v6.0:
    - Added configuration webserver
    - Improved stability for MQTT stream
   v7.0:
    - Complete rewrite using StateMachine
    - Support multiplate devices
    - Removed Snips.ia support
    - Removed local hotword detected (does not compile against latest espressif32)
      Will hopefully be replaced by a porcupine lib soon
   v7.1:
    - Audio task should run on core 1
* ************************************************************************ */

#include <Arduino.h>
//#include <ArduinoOTA.h>
#include <WiFi.h>
#include "device.h"


#define M5ATOMECHO 0
#define MATRIXVOICE 1
#define AUDIOKIT 2
#define TTGOCAMWHITE 3

#define DEVICE_TYPE 0 // Select from the list above.

boolean I2StaskCreated = false;


// Set these values below if you want this device to have a fixed IP address.
/*
#define HOST_IP "192.168.1.100"
#define HOST_GATEWAY "192.168.1.1"
#define HOST_SUBNET "255.255.255.0"
#define HOST_DNS1 "192.168.1.1"
#define HOST_DNS2 "192.168.1.1"
*/


// This is where you can include your device, make sure to create a *device
// The *device is used to call methods 
#if DEVICE_TYPE == M5ATOMECHO
  #include "devices/M5AtomEcho.hpp"
  M5AtomEcho *device = new M5AtomEcho();
#elif DEVICE_TYPE == MATRIXVOICE
  #include "devices/MatrixVoice.hpp"
  MatrixVoice *device = new MatrixVoice();  
#elif DEVICE_TYPE == AUDIOKIT
  #include "devices/AudioKit.hpp"
  AudioKit *device = new AudioKit();
#elif DEVICE_TYPE == TTGOCAMWHITE
  #include "devices/TTGOCamWhite.hpp"
  TTGOCamWhite *device = new TTGOCamWhite();
#endif

#include "General.hpp"
#include "StateMachine.hpp"

#define WIFI_SSID "sterrenkijker_nomap"
#define WIFI_PASS "cocobongodancewoman"

#define OTA_PASS_HASH "start"
#define SITEID "ATOMECHO"
#define HOSTNAME "atomecho"
#define MQTT_IP "192.168.2.165"
#define MQTT_PORT 1883
#define MQTT_USER ""
#define MQTT_PASS ""
#define MQTT_MAX_PACKET_SIZE 2000
//#define CONFIG_ASYNC_TCP_RUNNING_CORE 1




void setup() {
  Serial.begin(115200);
  Serial.println("Booting");

  if (wbSemaphore == NULL)  // Not yet been created?
  {
    wbSemaphore = xSemaphoreCreateMutex();  // Create a mutex semaphore
    if ((wbSemaphore) != NULL) xSemaphoreGive(wbSemaphore);  // Free for all
  }

  device->init();

  if (!SPIFFS.begin(true)) {
      Serial.println("Failed to mount file system");
  } else {
      Serial.println("Loading configuration");
      loadConfiguration(configfile, config);
  }

  device->setGain(config.gain);
  device->setVolume(config.volume);

  // ---------------------------------------------------------------------------
  // ArduinoOTA
  // ---------------------------------------------------------------------------
  /*
  ArduinoOTA.setPasswordHash(OTA_PASS_HASH);

  ArduinoOTA
    .onStart([]() {
      Serial.println("Uploading...");
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR)
        Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR)
        Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR)
        Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR)
        Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR)
        Serial.println("End Failed");
    });
  */
  fsm::start();

  //server.on("/", handleRequest);
  //server.begin();
  Serial.println(audioFrameTopic.c_str());
  Serial.println(F("end of setup"));

}

void loop() {
  if (WiFi.isConnected()) {
    //ArduinoOTA.handle();
  }
  fsm::run();
}
