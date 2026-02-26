#include "node.hpp"

void GPUNode::updateStatus(bool isAvailable) {
    available = isAvailable;
}

const std::string& GPUNode::getIpAddress() const {
    return ipAddress;
}

bool GPUNode::operator<(const GPUNode& other) const {
    return ipAddress < other.ipAddress;
}
