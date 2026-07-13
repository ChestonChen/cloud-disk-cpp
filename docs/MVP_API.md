# MVP API 文档

基础地址：`http://127.0.0.1:8080`

所有受保护接口都需要携带：

```http
Authorization: Bearer <token>
```

## 健康检查

```bash
curl http://127.0.0.1:8080/health
```

## 注册

```bash
curl -X POST http://127.0.0.1:8080/api/user/register \
  -H 'Content-Type: application/json' \
  -d '{"username":"alice","password":"secret123"}'
```

## 登录

```bash
curl -X POST http://127.0.0.1:8080/api/user/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"alice","password":"secret123"}'
```

## 当前用户信息

```bash
curl http://127.0.0.1:8080/api/user/me \
  -H "Authorization: Bearer $TOKEN"
```

## 创建文件夹

根目录 id 为 `0`。

```bash
curl -X POST http://127.0.0.1:8080/api/folders \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"parent_id":"0","name":"docs"}'
```

## 文件列表

```bash
curl "http://127.0.0.1:8080/api/files?parent_id=0" \
  -H "Authorization: Bearer $TOKEN"
```

## 上传小文件

上传成功后会返回文件 id、对象 id、内容哈希和引用计数。后续客户端可以用返回的 `sha256` 发起秒传。

```bash
curl -X POST "http://127.0.0.1:8080/api/files/upload?parent_id=0&name=hello.txt" \
  -H "Authorization: Bearer $TOKEN" \
  --data-binary @hello.txt
```

## 秒传

如果服务端已经存在相同内容哈希的物理对象，可以直接创建新的逻辑文件记录，不需要重复上传文件内容。

```bash
curl -X POST http://127.0.0.1:8080/api/files/instant \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"parent_id":"0","name":"copy.txt","sha256":"<sha256>","size_bytes":"1024"}'
```

## 分片上传

初始化上传会话：

```bash
curl -X POST http://127.0.0.1:8080/api/uploads/init \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"parent_id":"0","name":"big.bin","sha256":"<sha256>","size_bytes":"10485760","chunk_size":"5242880","total_chunks":"2"}'
```

上传单个分片：

```bash
curl -X POST "http://127.0.0.1:8080/api/uploads/chunk?upload_id=$UPLOAD_ID&chunk_index=0" \
  -H "Authorization: Bearer $TOKEN" \
  --data-binary @chunk-0.part
```

查询上传进度：

```bash
curl "http://127.0.0.1:8080/api/uploads/progress?upload_id=$UPLOAD_ID" \
  -H "Authorization: Bearer $TOKEN"
```

完成上传并合并分片：

```bash
curl -X POST "http://127.0.0.1:8080/api/uploads/complete?upload_id=$UPLOAD_ID" \
  -H "Authorization: Bearer $TOKEN"
```

## 下载文件

```bash
curl "http://127.0.0.1:8080/api/files/download?id=$FILE_ID" \
  -H "Authorization: Bearer $TOKEN" \
  -o downloaded.txt
```

## 软删除文件或文件夹

删除文件夹时，它下面的子文件和子文件夹也会从列表中隐藏。

```bash
curl -X DELETE "http://127.0.0.1:8080/api/files?id=$FILE_ID" \
  -H "Authorization: Bearer $TOKEN"
```

## 回收站

查看回收站：

```bash
curl http://127.0.0.1:8080/api/recycle \
  -H "Authorization: Bearer $TOKEN"
```

恢复文件：

```bash
curl -X POST "http://127.0.0.1:8080/api/recycle/restore?id=$FILE_ID" \
  -H "Authorization: Bearer $TOKEN"
```

永久删除：

```bash
curl -X DELETE "http://127.0.0.1:8080/api/recycle/permanent?id=$FILE_ID" \
  -H "Authorization: Bearer $TOKEN"
```

## 分享

创建分享链接：

```bash
curl -X POST http://127.0.0.1:8080/api/shares \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"file_id":"1","access_code":"1234","allow_download":"true"}'
```

查看公开分享信息：

```bash
curl "http://127.0.0.1:8080/api/public/share?token=$SHARE_TOKEN&code=1234"
```

通过分享链接下载：

```bash
curl "http://127.0.0.1:8080/api/public/download?token=$SHARE_TOKEN&code=1234" \
  -o shared-file
```

