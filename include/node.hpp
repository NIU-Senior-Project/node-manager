#include <string>

class GPUNode {
public:
    GPUNode(const std::string& ip)
        : ipAddress(ip), available(false) {};

    GPUNode(int id, const std::string& ip)
        : nodeId(id), ipAddress(ip), available(false) {};

    GPUNode(int id, const std::string& ip, const std::string& gpuModel)
        : nodeId(id), ipAddress(ip), available(false), gpu_model(gpuModel) {};

    void updateStatus(bool isAvailable);
    const std::string& getIpAddress() const;
    const std::string& getGpuModel() const;
    bool operator<(const GPUNode& other) const;

private:
    int nodeId;
    std::string ipAddress;
    bool available;
    std::string gpu_model;
};
