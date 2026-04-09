#pragma once

#include <string>
#include "mosaicdb/common.h"

namespace mosaicdb {

enum class CommandType { PUT, GET, DELETE_CMD, SCAN, UNKNOWN };

struct Command {
    CommandType type;
    std::string key;
    std::string value;
    std::string end_key;
};

class QueryParser {
public:
    static Command parse(const std::string& input);
};

}
