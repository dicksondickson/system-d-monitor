#include "server_info.h"

#include <ArduinoJson.h>
#include <string.h>
#include <strings.h>

namespace {
void copy(char* dst, size_t n, const char* src) {
  if (!src) return;
  strncpy(dst, src, n - 1);
  dst[n - 1] = '\0';
}
}  // namespace

bool parseHealth(const char* json, size_t len, ServerInfo& info) {
  JsonDocument filter;
  filter["status"] = true;
  filter["warming"] = true;
  filter["max_batch_size"] = true;
  filter["version"] = true;
  filter["model"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return false;
  if (!doc.is<JsonObject>() || !doc["status"].is<const char*>()) return false;
  info.health_ok = strcmp(doc["status"].as<const char*>(), "ok") == 0;
  info.warming = doc["warming"] | false;
  if (doc["max_batch_size"].is<float>()) info.max_batch = doc["max_batch_size"].as<float>();
  copy(info.version, sizeof info.version, doc["version"] | (const char*)nullptr);
  if (!info.model[0]) copy(info.model, sizeof info.model, doc["model"] | (const char*)nullptr);
  return true;
}

bool parseModels(const char* json, size_t len, ServerInfo& info) {
  JsonDocument filter;
  filter["data"][0]["id"] = true;
  filter["data"][0]["context_length"] = true;
  filter["data"][0]["max_model_len"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return false;
  JsonObject m = doc["data"][0];
  if (m.isNull() || !m["id"].is<const char*>()) return false;
  copy(info.model, sizeof info.model, m["id"].as<const char*>());
  if (m["context_length"].is<float>()) info.context_length = m["context_length"].as<float>();
  else if (m["max_model_len"].is<float>()) info.context_length = m["max_model_len"].as<float>();
  return true;
}

bool parseVersion(const char* json, size_t len, ServerInfo& info) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return false;
  if (!doc["version"].is<const char*>()) return false;
  copy(info.version, sizeof info.version, doc["version"].as<const char*>());
  return true;
}

Identity identify(const Sample& s, const char* server_header, const ServerInfo& info) {
  Identity id;
  if (s.n_tensorfold > s.n_vllm) id.engine = Engine::TENSORFOLD;
  else if (s.n_vllm > s.n_tensorfold) id.engine = Engine::VLLM;
  else return id;  // tie or nothing recognised
  if (id.engine == Engine::TENSORFOLD)
    id.confirmed = (server_header && strcasecmp(server_header, "TensorFold") == 0) || info.version[0];
  else
    id.confirmed = info.version[0] != '\0';  // from /version
  return id;
}

const char* engineName(Engine e) {
  switch (e) {
    case Engine::TENSORFOLD: return "TensorFold";
    case Engine::VLLM: return "vLLM";
    default: return "???";
  }
}
