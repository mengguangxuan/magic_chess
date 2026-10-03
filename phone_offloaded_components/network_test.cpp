#include "network_test.h"

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <time.h>

#include "network_config.h"

extern "C" {
extern const uint8_t _binary_x509_crt_bundle_start[];
extern const uint8_t _binary_x509_crt_bundle_end[];
}

namespace {
const uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
const uint32_t TIME_SYNC_TIMEOUT_MS = 10000;
const uint32_t HTTP_CONNECT_TIMEOUT_MS = 5000;
const uint32_t HTTP_TOTAL_TIMEOUT_MS = 10000;
const time_t MIN_VALID_UNIX_TIME = 1700000000;

bool credentialsConfigured()
{
  return CHESS_WIFI_SSID[0] != '\0';
}

bool connectWifi()
{
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.printf("Wi-Fi: connecting to 2.4 GHz hotspot '%s'", CHESS_WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(CHESS_WIFI_SSID, CHESS_WIFI_PASSWORD);

  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
    Serial.print('.');
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("Wi-Fi: connection failed (status=%d).\n",
                  static_cast<int>(WiFi.status()));
    WiFi.disconnect();
    return false;
  }

  Serial.printf("Wi-Fi: connected, IP=%s, RSSI=%d dBm, channel=%ld.\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                static_cast<long>(WiFi.channel()));
  return true;
}

bool synchronizeClock()
{
  if (time(NULL) >= MIN_VALID_UNIX_TIME) {
    return true;
  }

  Serial.print("Network: synchronizing clock for TLS");
  configTime(0, 0, "ntp.aliyun.com", "pool.ntp.org", "time.cloudflare.com");
  const uint32_t startedAt = millis();
  while (time(NULL) < MIN_VALID_UNIX_TIME &&
         millis() - startedAt < TIME_SYNC_TIMEOUT_MS) {
    Serial.print('.');
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  Serial.println();

  if (time(NULL) < MIN_VALID_UNIX_TIME) {
    Serial.println("Network: NTP time synchronization failed.");
    return false;
  }

  Serial.printf("Network: clock synchronized (Unix time=%lld).\n",
                static_cast<long long>(time(NULL)));
  return true;
}

bool testHttpsRoute()
{
  NetworkClientSecure secureClient;
  const size_t certificateBundleSize =
      static_cast<size_t>(_binary_x509_crt_bundle_end -
                          _binary_x509_crt_bundle_start);
  secureClient.setCACertBundle(_binary_x509_crt_bundle_start,
                              certificateBundleSize);
  secureClient.setHandshakeTimeout(HTTP_CONNECT_TIMEOUT_MS / 1000);

  HTTPClient http;
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(HTTP_TOTAL_TIMEOUT_MS);
  if (!http.begin(secureClient, CHESS_NETWORK_TEST_URL)) {
    Serial.println("Network test: could not initialize HTTPS request.");
    return false;
  }

  const int statusCode = http.GET();
  http.end();

  if (statusCode == 401) {
    Serial.println("Network test PASSED: Ark endpoint returned expected "
                   "HTTP 401 (no API key was sent).");
    return true;
  }
  if (statusCode > 0) {
    Serial.printf("Network test PASSED: HTTPS server returned HTTP %d "
                  "(401 is normally expected).\n", statusCode);
    return true;
  }

  Serial.printf("Network test FAILED during HTTPS request: %s (%d).\n",
                HTTPClient::errorToString(statusCode).c_str(), statusCode);
  return false;
}
}

bool initNetworkTest()
{
  if (!credentialsConfigured()) {
    Serial.println("Wi-Fi test disabled: fill CHESS_WIFI_SSID and "
                   "CHESS_WIFI_PASSWORD in network_config.h.");
    return false;
  }
  return true;
}

void networkTestTask(void* parameter)
{
  (void)parameter;

  bool testPassed = false;
  while (true) {
    if (!testPassed) {
      if (connectWifi() && synchronizeClock()) {
        testPassed = testHttpsRoute();
      }
      if (!testPassed) {
        Serial.printf("Network test will retry in %u ms.\n",
                      static_cast<unsigned>(CHESS_NETWORK_RETRY_MS));
      }
    } else if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Wi-Fi: connection lost; restarting network test.");
      testPassed = false;
    }

    vTaskDelay(pdMS_TO_TICKS(testPassed ? 1000 : CHESS_NETWORK_RETRY_MS));
  }
}
