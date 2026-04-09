#include "mosaicdb/query.h"
#include <sstream>
#include <algorithm>

namespace mosaicdb {

Command QueryParser::parse(const std::string& input) {
    Command cmd;
    cmd.type = CommandType::UNKNOWN;

    std::istringstream stream(input);
    std::string op;
    stream >> op;

    std::transform(op.begin(), op.end(), op.begin(), ::toupper);

    if (op == "PUT") {
        cmd.type = CommandType::PUT;
        stream >> cmd.key;
        std::getline(stream, cmd.value);
        if (!cmd.value.empty() && cmd.value[0] == ' ')
            cmd.value = cmd.value.substr(1);
    } else if (op == "GET") {
        cmd.type = CommandType::GET;
        stream >> cmd.key;
    } else if (op == "DELETE") {
        cmd.type = CommandType::DELETE_CMD;
        stream >> cmd.key;
    } else if (op == "SCAN") {
        cmd.type = CommandType::SCAN;
        stream >> cmd.key >> cmd.end_key;
    }

    return cmd;
}

}
