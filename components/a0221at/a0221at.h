#pragma once

#include <cstddef>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"

namespace esphome::a0221at {

/// DYP A02YYT(W) ("A0221AT") ultrasonic distance sensor, UART controlled variant.
///
/// Unlike the A02YYUW (built-in `a02yyuw` platform), this module does not
/// stream continuously: every measurement has to be requested by sending a
/// trigger byte, after which it replies with exactly one data frame.
class A0221ATComponent final : public sensor::Sensor,
                               public PollingComponent,
                               public uart::UARTDevice {
 public:
  void setup() override;     // Sanity-check the trigger period
  void update() override;    // Send the trigger for the next measurement
  void loop() override;      // Collect and parse the response frames
  void dump_config() override;

 protected:
  void check_buffer_();

  std::vector<uint8_t> buffer_;
  bool waiting_response_{false};
};

}  // namespace esphome::a0221at
