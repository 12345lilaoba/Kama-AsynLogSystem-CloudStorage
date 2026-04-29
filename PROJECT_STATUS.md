# Kama-AsynLogSystem-CloudStorage Project Status

更新时间：2026-04-29

## 1. 项目定位

本项目是一个基于 C++ 的云存储系统，同时集成了自研异步日志系统。当前目标不是做完整商业网盘，而是形成一个适合学习、展示和面试讲解的工程项目：

- 支持文件上传、文件列表展示、文件下载、文件删除。
- 支持普通存储和深度压缩存储。
- 支持元数据持久化，服务重启后可恢复文件信息。
- 支持异步日志，降低日志写入对业务线程的影响。
- 支持日志按大小/日期轮转和按数量/时间清理，避免日志目录无限增长。
- 支持基础安全校验，例如文件名校验、URL 解码校验、HTML 转义。
- 支持断点续传下载，展示存储系统的工程能力。
- 后续交互入口以 Web 页面为主，不再继续扩展独立客户端上传工具。

## 2. 总体架构

项目主要分为日志系统和云存储服务端两块核心模块，另保留早期客户端上传工具代码作为历史参考：

```text
Kama-AsynLogSystem-CloudStorage/
├── log_system/                 # 自研日志系统
│   ├── logs_code/
│   │   ├── AsyncLogger.hpp     # 异步日志器
│   │   ├── AsyncWorker.hpp     # 异步刷盘工作线程
│   │   ├── AsyncBuffer.hpp     # 异步缓冲区
│   │   ├── LogFlush.hpp        # 日志落地策略
│   │   ├── Manager.hpp         # Logger 管理器
│   │   ├── Message.hpp         # 日志消息结构
│   │   ├── MyLog.hpp           # 日志系统总入口
│   │   ├── ThreadPoll.hpp      # 线程池
│   │   ├── Util.hpp            # 日志系统工具类和配置读取
│   │   └── config.conf         # 日志系统配置
│   └── examples/               # 日志系统示例
│
├── src/
│   ├── server/                 # 云存储服务端
│   │   ├── Test.cpp            # 服务端启动入口
│   │   ├── Service.hpp         # HTTP 服务、上传/下载/删除/列表逻辑
│   │   ├── HttpUtil.hpp        # HTTP 相关工具函数、安全校验、Range 解析、hash 计算
│   │   ├── PageRender.hpp      # 文件列表页面渲染
│   │   ├── DataManager.hpp     # 文件元数据管理和持久化
│   │   ├── Config.hpp          # 服务端配置单例
│   │   ├── Util.hpp            # 文件、JSON、URL 工具
│   │   ├── Storage.conf        # 服务端配置
│   │   ├── index.html          # 前端页面模板
│   │   ├── base64.*            # 文件名 base64 编解码支持
│   │   ├── bundle.h            # 压缩/解压库接口
│   │   └── Makefile            # 服务端构建文件
│   │
│   └── client/                 # 早期客户端上传工具，后续不作为主要开发方向
│       ├── Test.cpp
│       ├── Storage.hpp
│       ├── DataManage.hpp
│       └── Util.hpp
│
├── README.md
├── PROJECT_FLOW_OVERVIEW.svg   # 项目流程图，当前为未跟踪文件
└── PROJECT_STATUS.md           # 当前项目状态文档
```

## 3. 服务端核心流程

### 3.1 启动流程

1. `src/server/Test.cpp` 初始化日志系统。
2. 根据命令行参数可指定服务端配置文件和日志配置文件。
3. 创建全局 `DataManager`，加载 `storage.data` 中的文件元数据。
4. 启动 `Service`，基于 libevent 监听 HTTP 请求。

### 3.2 上传流程

入口：`Service::Upload`

1. 从请求体读取文件内容。
2. 校验文件体是否为空、是否超过 `max_upload_size`。
3. 从 `FileName` 请求头读取 base64 文件名并解码。
4. 校验文件名是否合法，阻止路径穿越。
5. 从 `StorageType` 请求头判断普通存储还是深度存储。
6. 检查同名文件是否已存在，存在则返回 `409`。
7. 如果选择深度存储，会先根据文件后缀判断是否值得压缩。
8. 对 `.zip`、`.jpg`、`.png`、`.mp4`、`.mp3`、`.pdf` 等已压缩格式直接保存，避免二次压缩。
9. 写入 `.uploading` 临时文件。
10. 写入成功后 `rename` 到正式文件路径。
11. 构造 `StorageInfo`，记录存储类型、是否压缩、文件大小和上传时间，写入 `DataManager`。
12. 上传时计算文件内容 hash，作为后续内容校验和去重的基础元数据。
13. `DataManager` 将元数据持久化到 `storage.data`。

### 3.3 文件列表流程

入口：`Service::ListShow`

1. 从 `DataManager` 获取全部文件信息。
2. 读取 `index.html` 模板。
3. 动态生成文件列表 HTML。
4. 文件名做 HTML 转义，避免 XSS。
5. 下载和删除 URL 做百分号编码，避免特殊字符导致 URL 失效。
6. 页面只展示用户关心的信息：文件名、存储类型、文件大小、上传时间和操作按钮。

### 3.4 下载流程

入口：`Service::Download`

1. URL 解码并校验。
2. 根据 `/download/<filename>` 从 `DataManager` 查找元数据。
3. 根据元数据中的 `compressed_` 判断是否真的需要解压。
4. 如果文件经过 bundle 压缩，先解压到普通存储目录的临时下载文件。
5. 如果文件只是深度目录中保存的已压缩格式文件，则直接下载原文件。
6. 支持普通完整下载，返回 `200`。
7. 支持 Range 下载，返回 `206` 和 `Content-Range`。
8. 非法 Range 返回 `416`。
9. 深度存储下载后删除临时解压文件。

### 3.5 删除流程

入口：`Service::Delete`

1. 仅允许 `DELETE /delete/<filename>`。
2. URL 解码并校验。
3. 校验文件名合法性。
4. 根据下载 URL 查询元数据。
5. 删除真实文件。
6. 从 `DataManager` 删除元数据。
7. 重新持久化 `storage.data`。

## 4. 当前已经完成的优化

### 4.1 存储服务

- 新增文件删除功能：
  - 服务端路由：`/delete/<filename>`
  - 前端删除按钮：`index.html`
  - 元数据删除：`DataManager::Delete`
- 上传增加基础安全校验：
  - 缺失 `FileName` 返回 `400`
  - 文件名 base64 非法返回 `400`
  - 非法文件名返回 `400`
  - 缺失 `StorageType` 返回 `400`
  - 非法存储类型返回 `400`
  - 超过 `max_upload_size` 返回 `400`
  - 同名文件返回 `409`
- 文件写入更安全：
  - 上传文件先写 `.uploading`，再 `rename` 到正式路径。
  - 元数据先写 `storage.data.tmp`，再 `rename` 到 `storage.data`。
- 下载更健壮：
  - 文件不存在返回 `404`
  - 解压失败返回 `500`
  - `evbuffer_add_file` 失败会关闭 fd 并返回 `500`
- URL 和页面安全：
  - URL 解码失败返回 `400`，不再 `assert` 崩溃。
  - 文件列表 HTML 对文件名做转义。
  - 下载/删除链接对文件名做 URL 编码。
- 恢复并完善断点续传：
  - 支持 `Range: bytes=0-3`
  - 支持 `Range: bytes=4-`
  - 支持 `Range: bytes=-4`
  - 合法范围返回 `206`
  - 非法范围返回 `416`
- 深度存储优化：
  - 新增常见已压缩/媒体格式后缀判断。
  - `.zip`、`.gz`、`.bz2`、`.xz`、`.7z`、`.rar` 等压缩包不再重复压缩。
  - `.jpg`、`.jpeg`、`.png`、`.gif`、`.webp` 等图片不再重复压缩。
  - `.mp4`、`.mkv`、`.avi`、`.mov`、`.mp3`、`.aac`、`.flac` 等音视频不再重复压缩。
  - `.pdf` 不再重复压缩。
  - 下载时不再根据路径猜测是否需要解压，而是根据元数据 `compressed_` 判断。

### 4.2 元数据管理

- 修复 `StorageInfo::NewStorageInfo` 中 `mtime_` 和 `atime_` 赋值反了的问题。
- 新增 `DataManager::Delete`。
- `StorageInfo` 新增：
  - `storage_type_`：记录普通存储或深度存储。
  - `compressed_`：记录文件是否真正经过 bundle 压缩。
  - `upload_time_`：记录文件上传时间。
  - `original_size_`：记录上传时的原始文件大小。
  - `stored_size_`：记录实际落盘后的存储大小。
  - `content_type_`：记录上传请求中的文件 MIME 类型。
  - `content_hash_`：记录上传内容的 hash 指纹。
  - `hash_algo_`：记录 hash 算法，目前为 `fnv1a64`。
- 兼容旧 `storage.data`：旧记录没有新字段时，根据路径推断 `storage_type_`，旧深度存储记录默认视为已压缩。
- 兼容旧 hash 元数据：旧记录没有 `content_hash_` 和 `hash_algo_` 时保持为空，不影响列表、下载和删除。
- 元数据持久化改为临时文件加原子替换。
- `storage::JsonUtil::UnSerialize` 成功时返回 `true`。
- 文件列表页面做了展示收敛：前端只展示文件大小和上传时间，原始大小、实际落盘大小、MIME 类型等字段保留在后端元数据中备用。
- 下载响应新增 `X-Storage-Type`、`X-Storage-Compressed`、`X-Original-Size`、`X-Stored-Size`、`X-Content-Hash`、`X-Hash-Algorithm` 元数据头。
- 当前 hash 使用轻量 `FNV-1a 64-bit` 内容指纹，无额外依赖，适合作为项目当前阶段的内容标识和去重基础；如果后续需要更强完整性校验，可替换为 SHA-256。

### 4.3 配置

- `Config` 支持通过 `SetConfigFile` 指定配置文件路径。
- `Storage.conf` 新增：

```json
"max_upload_size" : 104857600,
"metadata_store" : "json"
```

- `log_system/logs_code/config.conf` 新增：

```json
"roll_file_max_size" : 1048576,
"roll_file_max_count" : 10,
"roll_file_max_age_days" : 7
```

- `Test.cpp` 支持命令行传入配置：

```bash
./test <server_config> <log_config>
```

### 4.4 日志系统

- 修复日志系统 JSON 解析成功却返回 `false` 的问题。
- `JsonData` 支持启动前指定日志配置文件路径。
- `AsyncWorker::Stop` 改得更稳：
  - 避免重复 Stop。
  - 唤醒生产者和消费者。
  - join 前判断线程是否 joinable。
  - 确保缓冲区数据刷完再退出。
- `RollFileFlush` 完善日志轮转和清理：
  - 支持按文件大小滚动。
  - 支持按日期变化滚动。
  - 日志文件名改为更规整的时间戳格式，例如 `RollFile_log20260428-131412-1.log`。
  - 支持按保留数量清理旧日志。
  - 支持按保留天数清理过期日志。
  - `FileFlush` 和 `RollFileFlush` 析构时会关闭文件句柄。
- 服务端日志初始化从 `config.conf` 读取轮转参数，不再把滚动大小写死在代码中。

### 4.5 构建

- `src/server/Makefile` 已更新：
  - 头文件变化会触发重新编译。
  - `test` 和 `gdb_test` 都依赖 `base64.cpp` 和相关 `.hpp`。

### 4.6 代码结构收敛

- 对 `src/server/Service.hpp` 做了低风险拆分，避免服务层继续膨胀：
  - 新增 `src/server/HttpUtil.hpp`，承接文件名安全校验、HTML 转义、URL 编码、已压缩格式判断、Content-Type 归一化、FNV-1a hash 计算、原子写文件和 Range 解析。
  - 新增 `src/server/PageRender.hpp`，承接文件大小格式化、上传时间格式化和文件列表 HTML 渲染。
  - `Service.hpp` 保留 HTTP 请求分发以及上传、列表、删除、下载主流程。
- 拆分后当前行数：
  - `Service.hpp`：约 495 行。
  - `HttpUtil.hpp`：约 221 行。
  - `PageRender.hpp`：约 98 行。
- 这次拆分不改变接口行为，只降低单文件复杂度，后续如果继续拆分，可以再考虑 `UploadHandler.hpp`、`DownloadHandler.hpp`、`DeleteHandler.hpp`。

### 4.7 C++ 工程化增强

当前阶段开始把优化重心转向 C++ 后端岗位更关注的工程能力：

- `Service::RunModule` 使用 `std::unique_ptr` + 自定义 deleter 管理 `event_base` 和 `evhttp`：
  - `event_base_free` 和 `evhttp_free` 不再依赖手动分支释放。
  - 绑定端口失败、创建 HTTP 上下文失败等提前返回路径也能自动释放资源。
  - `evhttp` 会先于 `event_base` 析构，释放顺序更清晰。
- 新增 `HttpUtil::UniqueFd`：
  - 下载流程打开文件后由 RAII 对象管理文件描述符。
  - `evbuffer_add_file` 失败时自动关闭 fd。
  - `evbuffer_add_file` 成功后通过 `Release()` 把 fd 所有权交给 libevent，避免重复关闭。
- `DataManager` 增加读写锁 RAII Guard：
  - `ReadLockGuard` 封装 `pthread_rwlock_rdlock / pthread_rwlock_unlock`。
  - `WriteLockGuard` 封装 `pthread_rwlock_wrlock / pthread_rwlock_unlock`。
  - `Insert`、`Update`、`Delete`、`GetOneByURL`、`GetOneByStoragePath`、`GetAll` 不再手动配对 unlock，减少异常路径或提前返回导致忘记解锁的风险。
- `DataManager` 禁止拷贝：
  - `DataManager(const DataManager&) = delete`
  - `operator=(const DataManager&) = delete`
  - 避免包含 `pthread_rwlock_t` 的管理类被误拷贝。
- `GetOneByURL` 改为使用 `find` 迭代器读取，避免在读锁保护下使用 `operator[]`。

### 4.8 MySQL 元数据接口抽象起步

已开始 P1 的元数据存储抽象，为后续 MySQL 元数据存储做铺垫：

- 新增 `src/server/StorageInfo.hpp`：
  - 将 `StorageInfo` 从 `DataManager.hpp` 拆出。
  - `Service.hpp`、`PageRender.hpp` 等业务代码仍可通过 `DataManager.hpp` 间接使用 `StorageInfo`，外部调用方式保持不变。
- 新增 `src/server/MetadataStore.hpp`：
  - 定义 `MetadataStore` 抽象接口。
  - 当前接口包含 `Insert`、`Update`、`Delete`、`GetOneByURL`、`GetAll`、`SaveAll`。
  - `SaveAll` 用于保留当前 JSON 全量落盘语义，避免 `DataManager::Storage` 通过循环 `Update` 多次写文件。
- 新增 `JsonMetadataStore`：
  - 承接原来 `DataManager` 中的 `storage.data` 读取、JSON 反序列化、兼容旧字段、序列化和临时文件原子替换逻辑。
  - 继续兼容旧元数据字段和 hash 元数据字段。
- `DataManager` 调整职责：
  - 保留内存缓存 `table_` 和读写锁。
  - 初始化时从 `MetadataStore` 加载元数据。
  - 插入、更新、删除时通过 `MetadataStore` 持久化。
  - 通过 `Config::GetMetadataStoreType()` 选择元数据后端。
  - 当前支持配置值 `json` 和 `mysql`，未知值会记录警告并回退到 `JsonMetadataStore`。
- 新增 `src/server/MysqlMetadataStore.hpp`：
  - 使用 MySQL C API 封装连接、自动建表、插入/更新、删除、按 URL 查询、全量查询和全量保存。
  - 表名为 `file_metadata`。
  - 字段对应 `StorageInfo` 当前元数据结构。
  - 为 `url` 建主键，为 `content_hash` 和 `upload_time` 建索引。
  - 密码不写入配置文件，通过 `mysql_password_env` 指定的环境变量读取，默认 `CLOUD_STORAGE_MYSQL_PASSWORD`。
  - 当前源码支持条件编译：未安装 MySQL 开发包时仍可按 JSON 后端正常编译；安装 `libmysqlclient-dev` 后会通过 `mysql_config` 自动加入编译和链接参数。

## 5. 已验证的功能

在 `src/server` 下编译通过：

```bash
make
```

也手动执行过完整编译：

```bash
g++ -o test Test.cpp base64.cpp -std=c++17 -lpthread -lstdc++fs -ljsoncpp -lbundle -levent
```

已通过 curl 验证：

- 列表页返回 `200`
- 非法文件名上传返回 `400`
- 正常普通存储上传返回 `200`
- 重复上传同名文件返回 `409`
- 普通文件下载内容正确
- 删除接口使用 `GET` 返回 `405`
- `DELETE` 删除普通文件成功
- 删除后再次下载返回 `404`
- 深度存储上传成功
- 深度存储下载时可解压并返回原始内容
- 深度存储 `.zip` 文件不会二次压缩，下载直接返回原始内容
- 深度存储 `.txt` 文件仍会压缩，下载时可解压返回原始内容
- 深度存储删除成功
- 非法 URL 编码返回 `400`
- 断点续传：
  - `Range: bytes=0-3` 返回 `206` 和部分内容
  - `Range: bytes=4-` 返回 `206` 和部分内容
  - `Range: bytes=-4` 返回 `206` 和部分内容
  - 越界范围返回 `416`
- 日志轮转/清理：
  - `make` 编译通过。
  - 使用临时目录 `/tmp/codex_rolltest` 强制小文件滚动验证过。
  - 设置最多保留 2 个滚动日志文件时，最终只保留 2 个最新日志文件。
  - 临时验证文件和临时日志目录已清理。
- 2026-04-29 收口验证：
  - `make` 编译通过，目标已是最新。
  - 列表页 `GET /` 返回 `200`。
  - 普通存储测试文件上传返回 `200`，下载返回原始内容。
  - 深度存储测试文件上传返回 `200`，下载返回解压后的原始内容。
  - `Range: bytes=0-4` 返回 `206` 和 `Content-Range`。
  - 测试文件已通过 `DELETE` 删除。
- 2026-04-29 hash 元数据验证：
  - 使用临时端口 `18081` 启动独立服务端实例，不影响当前 `8081` 服务。
  - 上传测试文件后，临时 `storage.data` 已持久化 `content_hash_` 和 `hash_algo_`。
  - 下载响应已返回 `X-Content-Hash` 和 `X-Hash-Algorithm`。
  - 临时服务端、临时配置和临时存储目录已清理。
- 2026-04-29 `Service.hpp` 拆分验证：
  - 新增 `HttpUtil.hpp` 和 `PageRender.hpp` 后，`make` 编译通过。
  - `Service.hpp` 中原有工具逻辑和页面渲染逻辑已迁出，业务主流程保持不变。
- 2026-04-29 C++ 工程化验证：
  - `event_base`、`evhttp`、下载文件 fd、元数据读写锁已做 RAII 化。
  - `make` 编译通过。
- 2026-04-29 MySQL 元数据阶段验证：
  - 已提交 `Add configurable metadata store selection`。
  - 新增 MySQL 非敏感配置项和 `MysqlMetadataStore` 代码。
  - 安装 `libmysqlclient-dev` 后，`mysql_config --cflags` 和 `mysql_config --libs` 可用。
  - `make clean && make` 已通过真实 MySQL C API 编译和链接验证。
  - 修复 `MysqlMetadataStore::Connect` 初始化顺序，确保连接成功后能自动创建 `file_metadata` 表。
  - 本地切换 `metadata_store=mysql` 并设置 `CLOUD_STORAGE_MYSQL_PASSWORD` 后，上传测试文件成功写入 MySQL `file_metadata`。
  - 已验证 MySQL 后端下载返回原始内容。
  - 已验证 MySQL 后端删除文件后同步删除元数据记录。
  - 默认配置已恢复 `metadata_store=json`，避免没有 MySQL 环境时影响默认启动。
- 2026-04-29 测试数据收口：
  - 历史 JSON 元数据不再迁移到 MySQL。
  - 旧测试数据已清空，后续从干净状态继续验证。
  - 如后续需要重新清理，可删除 `storage.data`、清空 `low_storage` / `deep_storage`，并对 MySQL 执行 `TRUNCATE TABLE file_metadata;`。
- 2026-04-29 MySQL 错误日志增强：
  - 连接未就绪时，增删改查会记录具体操作和 URL。
  - MySQL 连接失败时记录 host、port、user、database 等上下文，不记录密码。
  - 环境变量缺失时明确记录变量名和连接上下文。
  - 建表、插入、更新、删除、查询和事务提交/回滚失败时记录具体操作。
  - `make` 编译通过。

注意：当前 `8081` 上已有一个 `test` 服务端进程在监听，本轮验证复用了该进程，没有额外启动第二个服务端。

## 6. 当前工作区状态

当前阶段的主线改动已经基本完成，重点集中在服务端云存储能力、元数据管理、Web 页面、日志系统稳定性、代码结构收敛和 C++ 工程化上。

当前工作区干净，没有未提交源码改动。

当前阶段已经完成但还需要最终收口的点：

- hash 元数据已实现并验证。
- `Service.hpp` 已拆出 `HttpUtil.hpp` 和 `PageRender.hpp`，编译通过。
- 阶段 1 的 C++ 工程化增强已完成第一轮：RAII 管理 libevent 资源、fd 和读写锁。
- 当前已提交 `MetadataStore` / `JsonMetadataStore` 抽象改动。
- 当前已提交可配置元数据后端选择改动。
- 当前已新增 `MysqlMetadataStore` 代码，默认继续使用 `json` 元数据后端。
- 当前已完成 MySQL 后端真实编译和上传、下载、删除功能验证。
- 历史测试数据已清空，不再做 JSON 到 MySQL 迁移工具。
- 当前已完成 MySQL 后端错误日志增强。
- 下一步建议提交当前 MySQL 错误日志增强改动，然后进入文件列表分页或基于 hash 的重复上传检测。

## 7. 下一步计划

### 优先级 P0：提交当前小阶段

1. 再查看一次 `git diff --stat`，确认只包含 MySQL 错误日志和项目状态文档更新。
2. 再跑一次 `git diff --check`。
3. 提交当前小阶段，建议 commit message：
   - `Improve MySQL metadata error logging`

### 优先级 P1：MySQL 元数据接口抽象

后续要更贴近 C++ 后端岗位，建议优先把当前 JSON 元数据存储抽象成接口，为 MySQL 做准备：

- 新增 `MetadataStore` 抽象接口：已开始。
  - `Insert`
  - `Update`
  - `Delete`
  - `GetOneByURL`
  - `GetAll`
- 将当前 `storage.data` JSON 持久化逻辑迁移为 `JsonMetadataStore`：已开始。
- `DataManager` 负责内存缓存和并发保护，底层持久化通过 `MetadataStore` 完成：已开始。
- `Storage.conf` 支持配置 `metadata_store`：已开始，当前默认 `json`。
- `DataManager` 根据配置选择元数据后端：已开始，当前未知配置会回退到 `JsonMetadataStore`。
- 新增 `MysqlMetadataStore`：已开始。
  - 封装 MySQL 连接。
  - 使用清晰的 SQL 封装，并对字符串做 MySQL 转义。
  - 为 `url`、`content_hash`、`upload_time` 建索引。
- MySQL 后端已完成真实编译和 curl 功能验证。
- 不做历史元数据迁移，旧测试数据已清空。
- MySQL 错误日志已增强：
  - 连接失败时记录 host、port、user、database，不记录密码。
  - 环境变量缺失时明确提示变量名。
  - 建表、插入、更新、删除失败时记录操作和 URL。

### 优先级 P1：线程库能力增强

当前项目已有异步日志后台线程和读写锁。后续可以继续强化线程库相关能力：

- 梳理并优化已有 `ThreadPool`：
  - 明确任务队列。
  - 使用 `std::mutex` 和 `std::condition_variable`。
  - 支持优雅停止。
  - 避免重复 Stop、遗漏唤醒和线程 join 问题。
- 增加后台维护任务：
  - 定期扫描存储目录。
  - 检查文件和元数据是否一致。
  - 发现孤儿文件或缺失元数据时记录日志。
- 当前不建议直接把 `evhttp_request` 跨线程处理，先把线程池用于后台维护任务更稳。

### 优先级 P2：元数据损坏恢复

增强 `storage.data` 损坏时的恢复能力：

- JSON 解析失败时记录明确错误日志。
- 将损坏的 `storage.data` 改名备份，例如 `storage.data.bad.<timestamp>`。
- 使用空元数据继续启动服务，避免整个服务不可用。
- 后续可以增加从磁盘目录扫描恢复元数据的能力。

### 优先级 P2：文件列表分页

当前文件列表一次性渲染全部文件。后续文件变多时，页面会变长，可以做前端分页：

- 每页显示固定数量文件，例如 10 或 20。
- 搜索、排序后重新分页。
- 后续如果文件量更大，再扩展成后端分页接口。

### 优先级 P2：功能增强

- 将当前 `fnv1a64` 内容指纹升级为 SHA-256，用于更强的完整性校验。
- 基于文件 hash 做重复内容检测，为后续秒传/去重打基础。
- 文件列表支持分页，避免文件很多时页面过长。
- 元数据加载失败时给出更明确的错误日志。
- 后续可把本地 `storage.data` 迁移到 MySQL。
- 后续可加用户体系和鉴权，但当前阶段不建议优先做。

### 优先级 P2：日志系统继续增强

日志轮转和清理已完成基础版本，后续可以继续补：

- 清理动作失败时记录具体文件名和错误原因。
- 支持不同 logger 使用不同轮转目录和保留策略。
- 支持按日志级别拆分文件，例如 `info.log`、`error.log`。
- 增加日志轮转单元测试或独立示例程序。

## 8. 下次继续时建议先读这些文件

建议阅读顺序：

1. `PROJECT_STATUS.md`
2. `src/server/Service.hpp`
3. `src/server/DataManager.hpp`
4. `src/server/Util.hpp`
5. `src/server/Config.hpp`
6. `src/server/index.html`
7. `log_system/logs_code/AsyncWorker.hpp`
8. `log_system/logs_code/LogFlush.hpp`
9. `log_system/logs_code/Util.hpp`

## 9. 常用命令

进入服务端目录：

```bash
cd /home/alex/projects/clound_storage/Kama-AsynLogSystem-CloudStorage/src/server
```

编译：

```bash
make
```

启动服务：

```bash
./test
```

访问列表页：

```bash
curl -i http://127.0.0.1:8081/
```

普通存储上传：

```bash
curl -i -X POST \
  -H FileName:Y29kZXhfY2hlY2sudHh0 \
  -H StorageType:low \
  --data-binary codex-ok \
  http://127.0.0.1:8081/upload
```

深度存储上传：

```bash
curl -i -X POST \
  -H FileName:Y29kZXhfZGVlcC50eHQ= \
  -H StorageType:deep \
  --data-binary deep-ok \
  http://127.0.0.1:8081/upload
```

下载：

```bash
curl -i http://127.0.0.1:8081/download/codex_check.txt
```

断点续传：

```bash
curl -i -H Range:bytes=0-3 http://127.0.0.1:8081/download/codex_range.txt
```

删除：

```bash
curl -i -X DELETE http://127.0.0.1:8081/delete/codex_check.txt
```

查看端口占用：

```bash
ss -ltnp
```

## 10. 面试讲解主线

可以这样讲这个项目：

> 我实现了一个基于 C++ 和 libevent 的轻量云存储服务，支持文件上传、普通存储、深度压缩存储、下载、断点续传和删除。文件元数据由 `DataManager` 管理，并持久化到本地 JSON 文件中。为了保证可靠性，文件和元数据写入都采用临时文件加原子 `rename` 的方式。项目还集成了自研异步日志系统，通过异步缓冲和后台刷盘降低业务线程的日志开销，并在退出时尽量保证日志不丢失。

重点可展开：

- HTTP 请求如何分发到上传、下载、删除、列表。
- 普通存储和深度存储的区别。
- 元数据如何组织和恢复。
- 为什么要用临时文件加 `rename`。
- 如何防止路径穿越和 XSS。
- 断点续传如何解析 Range。
- 异步日志如何避免阻塞业务线程。
- 当前还有哪些可扩展方向：压缩判断、日志轮转、MySQL 元数据、hash 去重、多用户鉴权。
