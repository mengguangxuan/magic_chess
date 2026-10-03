#include "doubao_client.h"

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <cJSON.h>
#include <string.h>

#include "network_config.h"
#include "tts_output.h"

extern "C" {
extern const uint8_t _binary_x509_crt_bundle_start[];
extern const uint8_t _binary_x509_crt_bundle_end[];
}

namespace {
const uint32_t WIFI_WAIT_MS = 5000;
const uint32_t HTTP_CONNECT_TIMEOUT_MS = 5000;
const uint32_t HTTP_TOTAL_TIMEOUT_MS = 12000;
const uint8_t REQUEST_QUEUE_LENGTH = 2;

struct DoubaoRequest {
  MoveQuality quality;
  BlunderKind kind;
  int loss;
  int bestScore;
  int playedScore;
  char playedMove[6];
  char bestMove[6];
};

QueueHandle_t requestQueue = NULL;

bool keyConfigured()
{
  return ARK_API_KEY[0] != '\0';
}

bool waitForWifi()
{
  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startedAt < WIFI_WAIT_MS) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  return WiFi.status() == WL_CONNECTED;
}

String buildPrompt(const DoubaoRequest& request)
{
  String prompt;
  prompt.reserve(520);
  prompt += "你是智能国际象棋棋盘的幽默解说员。棋局判断已经由TSCP引擎完成，";
  prompt += "不得自行修改或补充棋局事实。只调侃这一步棋，不攻击玩家本人，";
  prompt += "不使用脏话，不提自己是AI。根据严重程度输出一句自然、口语化的";
  prompt += "简体中文吐槽，不超过24个汉字，不加引号、前缀或解释。只使用";
  prompt += "常用汉字、数字和逗号句号，不使用表情或生僻符号。\n";
  prompt += "走法质量=";
  prompt += moveQualityName(request.quality);
  prompt += "；错误类型=";
  prompt += blunderKindName(request.kind);
  prompt += "；评分损失=";
  prompt += String(request.loss);
  prompt += "分；最佳走法=";
  prompt += request.bestMove;
  prompt += "；实际走法=";
  prompt += request.playedMove;
  prompt += "。";
  return prompt;
}

String buildRequestBody(const DoubaoRequest& request)
{
  cJSON* root = cJSON_CreateObject();
  if (root == NULL) return String();

  const String prompt = buildPrompt(request);
  cJSON_AddStringToObject(root, "model", ARK_MODEL);
  cJSON_AddStringToObject(root, "input", prompt.c_str());
  cJSON_AddBoolToObject(root, "store", false);
  cJSON_AddNumberToObject(root, "max_output_tokens", 64);
  cJSON* thinking = cJSON_AddObjectToObject(root, "thinking");
  if (thinking != NULL) {
    cJSON_AddStringToObject(thinking, "type", "disabled");
  }

  char* serialized = cJSON_PrintUnformatted(root);
  String body;
  if (serialized != NULL) {
    body = serialized;
    cJSON_free(serialized);
  }
  cJSON_Delete(root);
  return body;
}

String extractOutputText(const String& responseBody)
{
  cJSON* root = cJSON_Parse(responseBody.c_str());
  if (root == NULL) return String();

  String result;
  const cJSON* output = cJSON_GetObjectItemCaseSensitive(root, "output");
  if (cJSON_IsArray(output)) {
    const cJSON* outputItem = NULL;
    cJSON_ArrayForEach(outputItem, output) {
      const cJSON* content =
          cJSON_GetObjectItemCaseSensitive(outputItem, "content");
      if (!cJSON_IsArray(content)) continue;

      const cJSON* contentItem = NULL;
      cJSON_ArrayForEach(contentItem, content) {
        const cJSON* type =
            cJSON_GetObjectItemCaseSensitive(contentItem, "type");
        const cJSON* text =
            cJSON_GetObjectItemCaseSensitive(contentItem, "text");
        if (cJSON_IsString(type) && cJSON_IsString(text) &&
            strcmp(type->valuestring, "output_text") == 0) {
          result = text->valuestring;
          break;
        }
      }
      if (!result.isEmpty()) break;
    }
  }

  cJSON_Delete(root);
  result.trim();
  return result;
}

const char* fallbackTaunt(const DoubaoRequest& request)
{
  switch (request.kind) {
    case BlunderAllowedMate:
      return "王都快被将死了，你还在散步。";
    case BlunderMissedMate:
      return "将杀机会送到手边，你却没看见。";
    case BlunderHungQueen:
      return "后就这样送了，你可真大方。";
    case BlunderHungRook:
      return "这辆车开得不错，直接开给对面了。";
    case BlunderMissedQueenCapture:
      return "对面的后都送上门了，你居然不要。";
    case BlunderMissedRookCapture:
      return "白捡一辆车的机会，被你放走了。";
    case BlunderLostMaterial:
      return "这一手下去，棋子开始排队送人了。";
    default:
      return request.quality >= MoveQualityBlunder
                 ? "这一步很有勇气，可惜没带脑子。"
                 : "这一步有点松，棋盘都替你着急。";
  }
}

void speakFallbackTaunt(const DoubaoRequest& request)
{
  const char* taunt = fallbackTaunt(request);
  Serial.printf("Local fallback taunt: %s\n", taunt);
  if (!queueTtsText(taunt)) {
    Serial.println("Local fallback taunt could not be queued for TTS.");
  }
}

bool requestTaunt(const DoubaoRequest& request)
{
  const String requestBody = buildRequestBody(request);
  if (requestBody.isEmpty()) {
    Serial.println("Doubao: failed to build JSON request.");
    return false;
  }

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
  if (!http.begin(secureClient, ARK_RESPONSES_URL)) {
    Serial.println("Doubao: could not initialize HTTPS request.");
    return false;
  }

  http.addHeader("Authorization", String("Bearer ") + ARK_API_KEY);
  http.addHeader("Content-Type", "application/json");
  const int statusCode = http.POST(requestBody);
  const String responseBody = statusCode > 0 ? http.getString() : String();
  http.end();

  if (statusCode != 200) {
    if (statusCode > 0) {
      Serial.printf("Doubao request failed: HTTP %d.\n", statusCode);
      if (!responseBody.isEmpty()) {
        Serial.printf("Doubao response: %.500s\n", responseBody.c_str());
      }
    } else {
      Serial.printf("Doubao HTTPS failed: %s (%d).\n",
                    HTTPClient::errorToString(statusCode).c_str(), statusCode);
    }
    return false;
  }

  const String taunt = extractOutputText(responseBody);
  if (taunt.isEmpty()) {
    Serial.println("Doubao: HTTP 200 but no output_text was found.");
    Serial.printf("Doubao response: %.500s\n", responseBody.c_str());
    return false;
  }

  Serial.printf("Doubao taunt: %s\n", taunt.c_str());
  if (!queueTtsText(taunt.c_str())) {
    Serial.println("Doubao taunt could not be queued for TTS.");
  }
  return true;
}
}

bool initDoubaoClient()
{
  if (!keyConfigured()) {
    Serial.println("Doubao disabled: fill ARK_API_KEY in network_config.h.");
    return false;
  }

  requestQueue = xQueueCreate(REQUEST_QUEUE_LENGTH, sizeof(DoubaoRequest));
  if (requestQueue == NULL) {
    Serial.println("Doubao disabled: failed to create request queue.");
    return false;
  }
  Serial.printf("Doubao ready: model=%s, trigger loss >= %d cp.\n",
                ARK_MODEL, CHESS_TAUNT_MIN_LOSS_CP);
  return true;
}

bool queueDoubaoTaunt(const MoveQualityReport& report,
                      const char* playedMove,
                      const char* bestMove)
{
  if (report.loss < CHESS_TAUNT_MIN_LOSS_CP || playedMove == NULL ||
      bestMove == NULL) {
    return false;
  }

  DoubaoRequest request = {};
  request.quality = report.quality;
  request.kind = report.kind;
  request.loss = report.loss;
  request.bestScore = report.bestScore;
  request.playedScore = report.playedScore;
  strlcpy(request.playedMove, playedMove, sizeof(request.playedMove));
  strlcpy(request.bestMove, bestMove, sizeof(request.bestMove));

  // The board remains entertaining without Internet access or an API key.
  if (requestQueue == NULL) {
    speakFallbackTaunt(request);
    return true;
  }

  if (xQueueSend(requestQueue, &request, 0) == pdPASS) {
    Serial.printf("Doubao request queued for %s (loss=%d cp).\n",
                  request.playedMove, request.loss);
    return true;
  }

  // A fresh chess comment is more useful than a stale one. Remove one old
  // item and make a single non-blocking attempt to enqueue the latest move.
  DoubaoRequest discarded = {};
  xQueueReceive(requestQueue, &discarded, 0);
  if (xQueueSend(requestQueue, &request, 0) == pdPASS) {
    Serial.println("Doubao queue full: oldest request replaced.");
    return true;
  }

  Serial.println("Doubao queue full: request dropped.");
  return false;
}

void doubaoTask(void* parameter)
{
  (void)parameter;
  DoubaoRequest request = {};
  while (true) {
    if (xQueueReceive(requestQueue, &request, portMAX_DELAY) != pdPASS) {
      continue;
    }

    if (!waitForWifi()) {
      Serial.println("Doubao unavailable: Wi-Fi is not connected.");
      speakFallbackTaunt(request);
      continue;
    }

    if (!requestTaunt(request)) {
      speakFallbackTaunt(request);
    }
  }
}

