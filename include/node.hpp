#include <string>

class GPUNode {
public:
    GPUNode(int id, const std::string& ip)
        : nodeId(id), ipAddress(ip), available(false) {};
    void updateStatus(bool isAvailable);
private:
    int nodeId;
    std::string ipAddress;
    bool available;
};
