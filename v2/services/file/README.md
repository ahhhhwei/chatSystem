# File demo

This demo only implements local file storage. It has no RPC, etcd, Docker, or
Protobuf dependency yet.

Build and run the automatic round-trip example:

```bash
cmake -S v2 -B v2/build
cmake --build v2/build -j
./v2/build/services/file/file_demo
```

Store and retrieve a real file:

```bash
FILE_ID=$(./v2/build/services/file/file_demo put ./example.png)
./v2/build/services/file/file_demo get "$FILE_ID" ./downloaded.png
```

Stored files are placed in `file_demo_data/` under the current working directory.
