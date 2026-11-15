// Minimal WebSocket client over TLS for the ESP32.
//
// Why not a library? The common Arduino WebSockets library refuses any incoming message
// over 15 KB, and an ElevenLabs agent sends audio messages of hundreds of KB. This client
// buffers each message in PSRAM, however large, and hands it to onText() when complete.
//
// Text frames only (that is all the agent protocol uses). Handles fragmentation, ping/pong
// and close. Certificates are not verified (workshop only).
#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include "mbedtls/base64.h"
#include "esp_random.h"

class WsClient {
 public:
  typedef void (*TextHandler)(char *msg, size_t len);
  typedef void (*IdleHook)();
  TextHandler onText = nullptr;   // called with a complete, NUL-terminated message
  IdleHook    onIdle = nullptr;   // called while waiting for the rest of a big message

  bool connect(const char *host, uint16_t port, const char *path) {
    close();
    client.setInsecure();
    if (!client.connect(host, port)) return false;
    client.setNoDelay(true);

    uint8_t rnd[16]; unsigned char key[32]; size_t klen = 0;
    esp_fill_random(rnd, sizeof rnd);
    mbedtls_base64_encode(key, sizeof key, &klen, rnd, sizeof rnd);
    key[klen] = 0;

    client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                  "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",
                  path, host, (char *)key);

    client.setTimeout(8000);
    String status = client.readStringUntil('\n');
    if (status.indexOf("101") < 0) {
      lastError = status; lastError.trim();
      client.stop();
      return false;
    }
    while (client.connected()) {               // skip the rest of the response headers
      String line = client.readStringUntil('\n');
      if (line.length() <= 1) break;
    }
    up = true;
    msgLen = 0;
    return true;
  }

  bool connected() { return up && client.connected(); }

  void close() {
    if (up) { uint8_t none = 0; sendFrame(0x8, &none, 0); }
    up = false;
    client.stop();
  }

  // Sends one text message. NOTE: masks `data` in place, so the buffer is scrambled after.
  bool sendText(char *data, size_t len) { return sendFrame(0x1, (uint8_t *)data, len); }

  // Call often. Reads any complete frames that have arrived.
  void poll() {
    for (int i = 0; i < 4 && up && client.available() >= 2; i++) readFrame();
  }

  String lastError;
  uint16_t closeCode = 0;

 private:
  WiFiClientSecure client;
  bool up = false;
  char *msg = nullptr;
  size_t msgLen = 0, msgCap = 0;

  bool readExact(uint8_t *dst, size_t n, uint32_t timeoutMs = 8000) {
    size_t got = 0;
    uint32_t t0 = millis();
    while (got < n) {
      int a = client.read(dst + got, n - got);
      if (a > 0) { got += a; t0 = millis(); }
      else {
        if (!client.connected() || millis() - t0 > timeoutMs) return false;
        delay(1);
      }
      if (onIdle) onIdle();
    }
    return true;
  }

  void fail(const char *why) { lastError = why; up = false; client.stop(); }

  void readFrame() {
    uint8_t h[2];
    if (!readExact(h, 2)) return fail("read header");
    bool fin = h[0] & 0x80;
    uint8_t op = h[0] & 0x0F;
    bool masked = h[1] & 0x80;
    uint64_t len = h[1] & 0x7F;
    if (len == 126) { uint8_t e[2]; if (!readExact(e, 2)) return fail("read len"); len = (e[0] << 8) | e[1]; }
    else if (len == 127) {
      uint8_t e[8]; if (!readExact(e, 8)) return fail("read len");
      len = 0; for (int i = 0; i < 8; i++) len = (len << 8) | e[i];
    }
    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked && !readExact(mask, 4)) return fail("read mask");

    if (op >= 0x8) {                           // control frame: small, never fragmented
      uint8_t ctl[126];
      if (len > 125 || !readExact(ctl, len)) return fail("control frame");
      if (op == 0x9) sendFrame(0xA, ctl, len);                     // ping -> pong
      if (op == 0x8) { closeCode = len >= 2 ? (ctl[0] << 8) | ctl[1] : 0; fail("closed by server"); }
      return;
    }

    if (msgLen + len + 1 > msgCap) {           // grow the message buffer in PSRAM
      size_t want = msgLen + len + 1;
      char *p = (char *)ps_realloc(msg, want);
      if (!p) return fail("out of memory");
      msg = p; msgCap = want;
    }
    if (!readExact((uint8_t *)msg + msgLen, len)) return fail("read payload");
    if (masked) for (size_t i = 0; i < len; i++) msg[msgLen + i] ^= mask[i & 3];
    msgLen += len;
    if (fin) {
      msg[msgLen] = 0;
      size_t n = msgLen;
      msgLen = 0;
      if (onText) onText(msg, n);
    }
  }

  bool sendFrame(uint8_t op, uint8_t *data, size_t len) {
    if (!client.connected()) return false;
    uint8_t h[14]; int n = 0;
    h[n++] = 0x80 | op;
    if (len < 126) h[n++] = 0x80 | len;
    else if (len < 65536) { h[n++] = 0x80 | 126; h[n++] = len >> 8; h[n++] = len & 0xFF; }
    else { h[n++] = 0x80 | 127; for (int i = 7; i >= 0; i--) h[n++] = ((uint64_t)len >> (8 * i)) & 0xFF; }
    uint8_t mask[4]; esp_fill_random(mask, 4);
    memcpy(h + n, mask, 4); n += 4;
    for (size_t i = 0; i < len; i++) data[i] ^= mask[i & 3];      // clients must mask
    if (client.write(h, n) != (size_t)n) return false;
    return len == 0 || client.write(data, len) == len;
  }
};
