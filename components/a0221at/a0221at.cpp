// DYP A02YYT(W) / "A0221AT" - UART controlled ultrasonic distance sensor.
//
// This is the controlled sibling of the A02YYUW; for the auto-output variant
// use the built-in esphome/components/a02yyuw integration instead.
// Manufacturer interface specification:
// https://www.dypcn.com/uploads/A02-Output-Interfaces.pdf
//
//   - UART 9600 baud, 8 data bits, no parity, 1 stop bit, TTL level
//   - A measurement starts when the RX pin sees a falling edge pulse or any
//     single byte of serial data; the trigger period must be > 70 ms
//   - After the measurement the module sends exactly one data frame:
//
//       [0] 0xFF   frame header
//       [1] DATA_H \ distance in millimetres, big endian
//       [2] DATA_L /
//       [3] SUM    checksum: ([0] + [1] + [2]) & 0xFF

#include "a0221at.h"

#include "esphome/core/log.h"

// ESPHOME_LOG_TAG() is the standard tag declaration since ESPHome 2026.10.
// This fallback keeps the component usable with older releases, as documented
// in https://developers.esphome.io/architecture/logging/best_practices/
#ifndef ESPHOME_LOG_TAG
#define ESPHOME_LOG_TAG(name, tag) static const char *const name = tag
#endif

namespace esphome::a0221at {

ESPHOME_LOG_TAG(TAG, "a0221at.sensor");

static constexpr uint8_t FRAME_HEADER = 0xFF;    // Frame header byte
static constexpr size_t FRAME_SIZE = 4;          // Header + 2 data bytes + checksum
static constexpr uint16_t MIN_DISTANCE_MM = 30;  // Blind zone: 3 cm

void A0221ATComponent::setup() {
  const uint32_t interval = this->get_update_interval();
  if (interval != 0 && interval < 100) {
    ESP_LOGW(TAG, "update_interval %u ms is too short, the sensor needs more than 70 ms between triggers",
             interval);
  }
}

void A0221ATComponent::update() {
  if (this->waiting_response_) {
    ESP_LOGW(TAG, "Sensor did not respond to the last trigger, check wiring and baud rate");
  }
  this->waiting_response_ = true;

  // Drop stale bytes so a late reply can not be mistaken for the new one.
  this->buffer_.clear();
  while (this->available() > 0) {
    uint8_t byte;
    if (!this->read_byte(&byte)) {
      break;
    }
  }

  // Any single byte starts a measurement; the frame header is used for
  // symmetry so the trigger is easy to spot in a logic analyser trace.
  this->write_byte(FRAME_HEADER);
  ESP_LOGV(TAG, "Trigger sent");
}

void A0221ATComponent::loop() {
  // On ESPHome 2026.11+ this can be written with the read_available()
  // helper, like the built-in a02yyuw component does:
  //   this->read_available([this](uint8_t byte) { ... });
  while (this->available() > 0) {
    uint8_t byte;
    if (!this->read_byte(&byte)) {
      return;
    }
    if (this->buffer_.empty() && byte != FRAME_HEADER) {
      continue;  // Resynchronise on the frame header
    }
    this->buffer_.push_back(byte);
    if (this->buffer_.size() == FRAME_SIZE) {
      this->check_buffer_();
    }
  }
}

void A0221ATComponent::check_buffer_() {
  const uint8_t checksum = (this->buffer_[0] + this->buffer_[1] + this->buffer_[2]) & 0xFF;
  if (this->buffer_[3] == checksum) {
    this->waiting_response_ = false;
    const uint16_t distance = (this->buffer_[1] << 8) + this->buffer_[2];  // mm
    if (distance > MIN_DISTANCE_MM) {
      ESP_LOGV(TAG, "Distance from sensor: %d mm", distance);
      this->publish_state(distance);
    } else {
      // Reading inside the sensor's blind zone (< 3 cm): no usable reflection.
      ESP_LOGW(TAG, "Invalid data read from sensor: %d mm", distance);
    }
  } else {
    ESP_LOGW(TAG, "Checksum failed: %02X != %02X", checksum, this->buffer_[3]);
  }
  this->buffer_.clear();
}

void A0221ATComponent::dump_config() { LOG_SENSOR("", "A0221AT Sensor", this); }

}  // namespace esphome::a0221at
