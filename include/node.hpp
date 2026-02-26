#include <string>

class GPUNode {
public:
    GPUNode(const std::string& ip)
        : ipAddress(ip), available(false) {};

    GPUNode(int id, const std::string& ip)
        : nodeId(id), ipAddress(ip), available(false) {};

    void updateStatus(bool isAvailable);
    const std::string& getIpAddress() const;
    bool operator<(const GPUNode& other) const;

private:
    int nodeId;
    std::string ipAddress;
    bool available;
};
