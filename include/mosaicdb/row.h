#pragma once

#include <string>
#include <vector>
#include "mosaicdb/types.h"
#include "mosaicdb/schema.h"

namespace mosaicdb {

class RowSerializer {
public:
    // Serialize a row (vector of Values) into a byte string
    static std::string serialize(const TableSchema& schema, const std::vector<Value>& values);

    // Deserialize a byte string back into a vector of Values
    static std::vector<Value> deserialize(const TableSchema& schema, const std::string& data);
};

}
