# Node-Manager

這個儲存庫負責管理和監控出租 GPU 的節點。
他提供 API 來註冊、更新和查詢節點資訊，並支援節點的健康檢查和資源分配。

## 功能

- 節點註冊
- 節點資訊更新
- 節點查詢

啟動後預設監聽 `8080`，可用以下方式註冊 GPUNode：

```bash
curl -X POST http://localhost:8080/register \
	-H "Content-Type: application/json" \
	-d '{"ip":"10.0.0.12"}'
```

或使用 form 格式：

```bash
curl -X POST http://localhost:8080/register \
	-H "Content-Type: application/x-www-form-urlencoded" \
	-d "ip=10.0.0.12"
```
- 健康檢查
- 資源分配

## 安裝

```bash
git clone https://github.com/NIU-Senior-Project/node-manager.git
cd node-manager
cmake -B build
cmake --build build
cmake --install build
```

## 使用

```bash
node-manager
```
