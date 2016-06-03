/*
 * Author: Anne van Rossum
 * Copyright: Distributed Organisms B.V. (DoBots)
 * Date: 24 Nov., 2014
 * License: LGPLv3+, Apache, and/or MIT, your choice
 */

/*
 * TODO: Check https://devzone.nordicsemi.com/question/1745/how-to-handle-flashwrit-in-an-safe-way/
 *       Writing to persistent memory should be done between connection/advertisement events...
 */

/*
 * For more information see:
 * http://developer.nordicsemi.com/nRF51_SDK/doc/7.0.1/s110/html/a00763.html#ga0a57b964c8945eaca2d267835ef6688c
 */
#include "drivers/cs_Storage.h"

#include <climits>
#include "cfg/cs_StateVars.h"

#include <cfg/cs_Settings.h>

extern "C"  {

	static void pstorage_callback_handler(pstorage_handle_t * handle, uint8_t op_code, uint32_t result, uint8_t * p_data,
			uint32_t data_len) {
		// we might want to check if things are actually stored, by using this callback
		if (result != NRF_SUCCESS) {
			if (op_code == PSTORAGE_LOAD_OP_CODE) {
				LOGe("Error with loading data");
			}

			LOGe("OPP_CODE: %d, ERR_CODE: %d (0x%X)", op_code, result, result);
			APP_ERROR_CHECK(result);
		} else {
			LOGd("Opcode %i executed (no error)", op_code);
		}
	}

} // extern "C"

// NOTE: DO NOT CHANGE ORDER OF THE ELEMENTS OR THE FLASH
//   STORAGE WILL GET MESSED UP!! NEW ENTRIES ALWAYS AT THE END
static storage_config_t config[] {
	{PS_ID_CONFIGURATION, {}, sizeof(ps_configuration_t)},
	{PS_ID_GENERAL, {}, sizeof(ps_general_vars_t)},
	{PS_ID_STATE, {}, sizeof(ps_state_vars_t)}
};

#define NR_CONFIG_ELEMENTS SIZEOF_ARRAY(config)

Storage::Storage() : EventListener(),
		_initialized(false), _scanning(false), writeBuffer(STORAGE_REQUEST_BUFFER_SIZE)
//		, pendingStorageRequests(0)
{
	LOGd("Storage create");

	EventDispatcher::getInstance().addListener(this);

	memset(buffer, 0, sizeof(buffer));
	writeBuffer.assign((uint8_t*)buffer, sizeof(buffer));

}

void Storage::init() {
	// call once before using any other API calls of the persistent storage module
	BLE_CALL(pstorage_init, ());

	// todo: only create it if needed?
//	requestBuffer = new CircularBuffer<buffer_element_t>(STORAGE_REQUEST_BUFFER_SIZE, false);

	for (int i = 0; i < NR_CONFIG_ELEMENTS; i++) {
		LOGi("Init %i bytes persistent storage (FLASH) for id %d, handle: %p", config[i].storage_size, config[i].id, config[i].handle.block_id);
		initBlocks(config[i].storage_size, 1, config[i].handle);
	}

	_initialized = true;
}

void Storage::resumeRequests() {
	if (!writeBuffer.empty()) {
		LOGd("Resume pstorage write requests");
		while (!writeBuffer.empty()) {
			//! get the next buffered storage request
			buffer_element_t elem = writeBuffer.pop();

//			LOGi("elem:");
//			BLEutil::printArray((uint8_t*)&elem, sizeof(elem));

			if (!_scanning) {
				BLE_CALL (pstorage_update, (&elem.storageHandle, elem.data, elem.dataSize, elem.storageOffset) );
			} else {
				// if scanning was started again, push it back on the buffer and try again during the next
				// scan break
				writeBuffer.push(elem);
				LOGe("scan started before all pstorage write requestss were processed!");
				return;
			}

//			LOGi("update done");
		}
	}
}

void resume_requests(void* p_data, uint16_t len) {
	Storage::getInstance().resumeRequests();
}

void Storage::handleEvent(uint16_t evt, void* p_data, uint16_t length) {

	switch(evt) {
	case EVT_SCAN_STARTED: {
		_scanning = true;
		break;
	}
	case EVT_SCAN_STOPPED: {
		_scanning = false;
		if (!writeBuffer.empty()) {
			app_sched_event_put(NULL, 0, resume_requests);
		}
		break;
	}
	}

}

storage_config_t* Storage::getStorageConfig(ps_storage_id storageID) {

	for (int i = 0; i < NR_CONFIG_ELEMENTS; i++) {
		if (config[i].id == storageID) {
			return &config[i];
		}
	}

	return NULL;
}

bool Storage::getHandle(ps_storage_id storageID, pstorage_handle_t& handle) {
	storage_config_t* config;

	if ((config = getStorageConfig(storageID))) {
		handle = config->handle;
		return true;
	} else {
		return false;
	}
}

/**
 * We allocate a single block of size "size". Biggest allocated size is 640 bytes.
 */
void Storage::initBlocks(pstorage_size_t size, pstorage_size_t count, pstorage_handle_t& handle) {
	// set parameter
	pstorage_module_param_t param;
	param.block_size = size;
	param.block_count = 1;
	param.cb = pstorage_callback_handler;

	// register
	BLE_CALL ( pstorage_register, (&param, &handle) );
}

/**
 * Nordic bug: We have to clear the entire block!
 */
void Storage::clearStorage(ps_storage_id storageID) {

	storage_config_t* config;
	if (!(config = getStorageConfig(storageID))) {
		// if no config was found for the given storage ID return
		return;
	}

	pstorage_handle_t block_handle;
	BLE_CALL ( pstorage_block_identifier_get, (&config->handle, 0, &block_handle) );

	BLE_CALL (pstorage_clear, (&block_handle, config->storage_size) );
}

void Storage::readStorage(pstorage_handle_t handle, ps_storage_base_t* item, uint16_t size) {
	pstorage_handle_t block_handle;

	BLE_CALL ( pstorage_block_identifier_get, (&handle, 0, &block_handle) );

	BLE_CALL (pstorage_load, ((uint8_t*)item, &block_handle, size, 0) );

#ifdef PRINT_ITEMS
	_log(INFO, "get struct: \r\n");
	BLEutil::printArray((uint8_t*)item, size);
#endif

}

void Storage::writeStorage(pstorage_handle_t handle, ps_storage_base_t* item, uint16_t size) {
	writeItem(handle, 0, (uint8_t*)item, size);
}

void Storage::readItem(pstorage_handle_t handle, pstorage_size_t offset, uint8_t* item, uint16_t size) {
	pstorage_handle_t block_handle;

	BLE_CALL ( pstorage_block_identifier_get, (&handle, 0, &block_handle) );

	BLE_CALL (pstorage_load, (item, &block_handle, size, offset) );

#ifdef PRINT_ITEMS
	_log(INFO, "read item: \r\n");
	BLEutil::printArray(item, size);
#endif

}

void Storage::writeItem(pstorage_handle_t handle, pstorage_size_t offset, uint8_t* item, uint16_t size) {
	pstorage_handle_t block_handle;

#ifdef PRINT_ITEMS
	_log(INFO, "write item: \r\n");
	BLEutil::printArray((uint8_t*)item, size);
#endif

	BLE_CALL ( pstorage_block_identifier_get, (&handle, 0, &block_handle) );

	// [31.05.16] it seems it was not (or not anymore) the meshing that is causing issues with pstorage,
	//    but the scanning. If the device is scanning, then pstorage_updates are queued by the softdevice.
	//    unfortunately, they are not automatically resumed once the device stops scanning. they are staying
	//    queued until the first pstorage_update is called while the device is not scanning. to avoid this, we
	//    queue the write calls ourselves, and only trigger the pstorage_update calls once the device stopped
	//    scanning
	if (_scanning) {
		if (!writeBuffer.full()) {
			buffer_element_t elem;
			elem.storageHandle = block_handle;
			elem.storageOffset = offset;
			elem.dataSize = size;
			elem.data = item;
			writeBuffer.push(elem);
		} else {
			LOGe("storage request buffer is full!");
		}
	} else {
    	BLE_CALL (pstorage_update, (&block_handle, item, size, offset) );
	}

}

////////////////////////////////////////////////////////////////////////////////////////
//! helper functions
////////////////////////////////////////////////////////////////////////////////////////

pstorage_size_t Storage::getOffset(ps_storage_base_t* storage, uint8_t* var) {
	uint32_t p_storage = (uint32_t)storage;
	uint32_t p_var = (uint32_t)var;
	pstorage_size_t offset = p_var - p_storage;

#ifdef PRINT_ITEMS
	LOGi("p_storage: %p", p_storage);
	LOGi("var: %p", p_var);
	LOGi("offset: %d", offset);
#endif

	return offset;
}

void Storage::setString(std::string value, char* target) {
	setString(value.c_str(), value.length(), target);
}

void Storage::setString(const char* value, uint16_t length, char* target) {
	if (length <= MAX_STRING_STORAGE_SIZE) {
		memset(target, 0, MAX_STRING_STORAGE_SIZE+1);
		memcpy(target, value, length);
	} else {
		LOGe("string too long!!");
	}
}

// helper function to get std::string from char array, or default value
// if the value read is empty, unassigned (filled with FF) or too long
void Storage::getString(char* value, std::string& target, std::string default_value) {

#ifdef PRINT_ITEMS
	_log(INFO, "get string (raw): \r\n");
	BLEutil::printArray((uint8_t*)value, MAX_STRING_STORAGE_SIZE+1);
#endif

	target = std::string(value);
	// if the last char is equal to FF that means the memory
	// is new and has not yet been written to, so we use the
	// default value. same if the stored value is an empty string
	if (target == "" || value[MAX_STRING_STORAGE_SIZE] == 0xFF) {
#ifdef PRINT_ITEMS
		LOGd("use default value");
#endif
		target = default_value;
	} else {
#ifdef PRINT_ITEMS
		LOGd("found stored value: %s", target.c_str());
#endif
	}
}

// helper function to get std::string from char array, or default value
// if the value read is empty, unassigned (filled with FF) or too long
void Storage::getString(char* value, char* target, char* default_value, uint16_t& size) {

#ifdef PRINT_ITEMS
	_log(INFO, "get string (raw): \r\n");
	BLEutil::printArray((uint8_t*)value, MAX_STRING_STORAGE_SIZE+1);
#endif

	std::string stringValue(value);
	// if the last char is equal to FF that means the memory
	// is new and has not yet been written to, so we use the
	// default value. same if the stored value is an empty string
	if (stringValue == "" || value[MAX_STRING_STORAGE_SIZE] == 0xFF) {
		std::string stringDefault(default_value);
#ifdef PRINT_ITEMS
		LOGd("use default value: %s, len: %d", stringDefault.c_str(), stringDefault.length());
#endif
		memcpy(target, stringDefault.c_str(), stringDefault.length());
		size = stringDefault.length();
	} else {
#ifdef PRINT_ITEMS
		LOGd("found stored value: %s, len: %d", stringValue.c_str(), stringValue.length());
#endif
		memcpy(target, stringValue.c_str(), stringValue.length());
		size = stringValue.length();
	}
}

void Storage::setUint8(uint8_t value, uint32_t& target) {
	target = value;
}

void Storage::getUint8(uint32_t value, uint8_t& target, uint8_t default_value) {

#ifdef PRINT_ITEMS
	uint8_t* tmp = (uint8_t*)&value;
	LOGi("raw value: %02X %02X %02X %02X", tmp[3], tmp[2], tmp[1], tmp[0]);
#endif

	// check if last byte is FF which means that memory is unnassigned
	// and value has to be ignored
	if (value == UINT32_MAX) {
#ifdef PRINT_ITEMS
		LOGd("use default value");
#endif
		target = default_value;
	} else {
		target = value;
#ifdef PRINT_ITEMS
		LOGd("found stored value: %d", target);
#endif
	}
}

void Storage::setInt8(int8_t value, int32_t& target) {
	target = value;
	target &= 0x000000FF;
}

void Storage::getInt8(int32_t value, int8_t& target, int8_t default_value) {

#ifdef PRINT_ITEMS
	uint8_t* tmp = (uint8_t*)&value;
	LOGi("raw value: %02X %02X %02X %02X", tmp[3], tmp[2], tmp[1], tmp[0]);
#endif

	// check if last byte is FF which means that memory is unnassigned
	// and value has to be ignored
	if (value == UINT32_MAX) {
#ifdef PRINT_ITEMS
		LOGd("use default value");
#endif
		target = default_value;
	} else {
		target = value;
#ifdef PRINT_ITEMS
		LOGd("found stored value: %d", target);
#endif
	}
}

void Storage::setUint16(uint16_t value, uint32_t& target) {
	target = value;
}

void Storage::getUint16(uint32_t value, uint16_t& target, uint16_t default_value) {

#ifdef PRINT_ITEMS
	uint8_t* tmp = (uint8_t*)&value;
	LOGi("raw value: %02X %02X %02X %02X", tmp[3], tmp[2], tmp[1], tmp[0]);
#endif

	// check if last byte is FF which means that memory is unnassigned
	// and value has to be ignored
	if (value == UINT32_MAX) {
#ifdef PRINT_ITEMS
		LOGd("use default value");
#endif
		target = default_value;
	} else {
		target = value;

#ifdef PRINT_ITEMS
		LOGd("found stored value: %d", target);
#endif
	}
}

void Storage::setUint32(uint32_t value, uint32_t& target) {
	if (value == UINT32_MAX) {
		LOGe("value %d too big, can only write max %d", value, INT_MAX-1);
	} else {
		target = value;
	}
}

void Storage::getUint32(uint32_t value, uint32_t& target, uint32_t default_value) {

#ifdef PRINT_ITEMS
	uint8_t* tmp = (uint8_t*)&value;
	LOGi("raw value: %02X %02X %02X %02X", tmp[3], tmp[2], tmp[1], tmp[0]);
#endif

	// check if value is equal to INT_MAX (FFFFFFFF) which means that memory is
	// unnassigned and value has to be ignored
	if (value == UINT32_MAX) {
#ifdef PRINT_ITEMS
		LOGd("use default value");
#endif
		target = default_value;
	} else {
		target = value;
#ifdef PRINT_ITEMS
		LOGd("found stored value: %d", target);
#endif
	}
}
