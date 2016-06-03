/**
 * Author: Dominik Egger
 * Copyright: Distributed Organisms B.V. (DoBots)
 * Date: Jun 3, 2016
 * License: LGPLv3+
 */
#pragma once

enum CommandHandlerTypes {
	CMD_SWITCH,                        // 0
	CMD_PWM,
	CMD_SET_TIME,
	CMD_GOTO_DFU,
	CMD_FACTORY_RESET,
	CMD_KEEP_ALIVE_STATE,              // 5
	CMD_KEEP_ALIVE,
	CMD_ENABLE_MESH,
	CMD_ENABLE_ENCRYPTION,
	CMD_ENABLE_IBEACON,
	CMD_ENABLE_CONT_POWER_MEASURE,     // 10 - 0x0A
	CMD_ENABLE_SCANNER,
	CMD_SCAN_DEVICES,
	CMD_SAMPLE_POWER,
	CMD_USER_FEEDBACK,
	CMD_RESET,                         // 15 - 0x0F,
	CMD_SCHEDULE_ENTRY
};

struct __attribute__((__packed__)) switch_message_payload_t {
	uint8_t switchState;
};

struct __attribute__((__packed__)) opcode_message_payload_t {
	uint8_t opCode;
};

struct __attribute__((__packed__)) enable_message_payload_t {
	bool enable;
};

struct __attribute__((__packed__)) enable_scanner_message_payload_t {
	bool enable;
	uint16_t delay;
};

struct __attribute__((__packed__)) factory_reset_message_payload_t {
	uint32_t resetCode;
};

