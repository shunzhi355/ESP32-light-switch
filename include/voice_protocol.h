#pragma once

#include <stdint.h>

namespace VoiceProtocol {

constexpr uint8_t REQUEST_HEADER_1 = 0xAA;
constexpr uint8_t REQUEST_HEADER_2 = 0x55;
constexpr uint8_t RESPONSE_HEADER_1 = 0x55;
constexpr uint8_t RESPONSE_HEADER_2 = 0xAA;
constexpr uint32_t FRAME_TIMEOUT_MS = 100;

enum Command : uint8_t {
  LIGHT_1 = 0x01,
  LIGHT_2 = 0x02,
  ALL_LIGHTS = 0x03,
};

enum Parameter : uint8_t {
  OFF = 0x00,
  ON = 0x01,
};

enum Result : uint8_t {
  OK = 0x00,
  INVALID_COMMAND = 0x01,
  INVALID_PARAMETER = 0x02,
};

struct Frame {
  uint8_t command;
  uint8_t parameter;
};

constexpr uint8_t requestChecksum(uint8_t command, uint8_t parameter) {
  return REQUEST_HEADER_1 ^ REQUEST_HEADER_2 ^ command ^ parameter;
}

constexpr uint8_t responseChecksum(uint8_t command, uint8_t result) {
  return RESPONSE_HEADER_1 ^ RESPONSE_HEADER_2 ^ command ^ result;
}

class Parser {
 public:
  bool push(uint8_t value, uint32_t nowMs, Frame& frame) {
    if (state_ != WAIT_HEADER_1 &&
        static_cast<uint32_t>(nowMs - lastByteMs_) > FRAME_TIMEOUT_MS) {
      reset();
    }
    lastByteMs_ = nowMs;

    switch (state_) {
      case WAIT_HEADER_1:
        if (value == REQUEST_HEADER_1) {
          state_ = WAIT_HEADER_2;
        }
        break;

      case WAIT_HEADER_2:
        if (value == REQUEST_HEADER_2) {
          state_ = WAIT_COMMAND;
        } else if (value != REQUEST_HEADER_1) {
          reset();
        }
        break;

      case WAIT_COMMAND:
        command_ = value;
        state_ = WAIT_PARAMETER;
        break;

      case WAIT_PARAMETER:
        parameter_ = value;
        state_ = WAIT_CHECKSUM;
        break;

      case WAIT_CHECKSUM: {
        const bool valid = value == requestChecksum(command_, parameter_);
        if (valid) {
          frame.command = command_;
          frame.parameter = parameter_;
        }
        reset();
        return valid;
      }
    }

    return false;
  }

  void reset() {
    state_ = WAIT_HEADER_1;
    command_ = 0;
    parameter_ = 0;
  }

 private:
  enum State : uint8_t {
    WAIT_HEADER_1,
    WAIT_HEADER_2,
    WAIT_COMMAND,
    WAIT_PARAMETER,
    WAIT_CHECKSUM,
  };

  State state_ = WAIT_HEADER_1;
  uint8_t command_ = 0;
  uint8_t parameter_ = 0;
  uint32_t lastByteMs_ = 0;
};

}  // namespace VoiceProtocol

