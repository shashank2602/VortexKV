#pragma once


#include <string_view>



enum class DatabaseError : uint8_t {
	SUCCESS,
    KEY_NOT_FOUND,
    WRONG_TYPE,
    NOT_AN_INTEGER_OR_OUT_OF_RANGE,
	VALUE_OVERFLOW,
	NO_EXPIRATION_SET,
    OUT_OF_MEMORY
};

constexpr std::string_view databaseErrorToString(DatabaseError error) noexcept {
    switch (error) {
        case DatabaseError::SUCCESS:
            return "Success";
        case DatabaseError::KEY_NOT_FOUND:
            return "Key not found";
        case DatabaseError::WRONG_TYPE:
            return "WRONGTYPE Operation against a key holding the wrong kind of value";
        case DatabaseError::NOT_AN_INTEGER_OR_OUT_OF_RANGE:
            return "ERR value is not an integer or out of range";
		case DatabaseError::VALUE_OVERFLOW:
			return "ERR increment or decrement would overflow";
        case DatabaseError::OUT_OF_MEMORY:
            return "OOM command not allowed when used memory > 'maxmemory'";
        default:
            return "Unknown error";
    }
}