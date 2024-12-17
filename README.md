# optee_vela

[[English](./README.md) | [中文](./README_zh-cn.md)]

## Project Overview

`optee_vela` itself is used to implement an adaptation layer between `Vela tee` and `optee os`, enabling `optee_os` to run within the `Vela tee` system.
Therefore, the `TA` (Trusted Application) and `CA` (Client Application) programs in `optee_os` can all run directly in the `vela` system.
With the help of `optee_vela`, we don't need to make any modifications to the `optee_os` project itself to make `optee_os` run completely within `vela`.

The following is the position of `optee_vela` itself within the entire `Vela tee` system:
```
+-------------------------------------------------------+
|                   optee os                            |
+-------------------------------------------------------+
|                  optee_vela                           |
+-------------------------------------------------------+
|                  vela kernel                          |
+-------------------------------------------------------+
```

## Project Description

`optee_vela` mainly contains three parts of functions:

1. `compat`: It is a system module used to support the upper framework of `optee os` to run in `vela`, such as basic modules for operations like `atomic`, `fs`, `mem`, etc.
2. `server`: It is used to receive and handle all requests for `vela tee` initiated by `vela ap`, such as operations like opening `open TA`, `invoke TA cmd`, etc.
3. `wasm`: It is used to support the `wasm TA` specific to `vela` to run in `optee os`.

The following is an introduction for each part respectively:

### 1. compat

The `compat` adaptation layer mainly implements the system APIs required by the upper framework of `optee os` using the system APIs of `vela`, such as basic APIs like `atomic`, `mem`, `fs`, etc.
The following is an introduction to the main replaced API modules:

1. fs

The `fs` module mainly consists of two parts:
- 1. `host_fs`
    `host_fs` is mainly used to implement the `ree fs` operations required by `optee os`. In `optee os`, the implementation of `ree fs` forwards all operations related to the file system to `ree` for processing.
    This is because the support for file system operations in `optee os` itself is limited, so complex file system operations need to be transferred across cores to `ree` for processing.
    However, since `vela tee` is a fully functional operating system and also supports complex file system operations in `tee`, the `ree fs` in `vela tee` can be completed directly on the `vela tee` side.
    And `host_fs` is used to implement this function.

- 2. `rpmb_fs`
    `rpmb_fs` is mainly used to implement the implementation of the `rpmb` driver required by `optee os`.
    `vela tee` itself supports the `rpmb` driver, and then through `rpmb_fs`, the `rpmb` driver can be directly used in `optee os`.

2. `atomic`

This API module mainly replaces the `atomic` and `spinlock` required for the operation of `optee os` with the `atomic` and `spinlock` interface implementations supported by the `vela` system.

### 2. server

In `vela`, the communication process between `vela ap` and `vela tee` is carried out through `rpmsg socket`.
In this process, we can regard `vela ap` as the client and `vela tee` as the server.
The `server` in `vela tee` is implemented as an `rpmsg socket server`, which is used to receive and handle requests initiated by the `rpmsg socket client`.

The `server` part itself is used to complete the request processing for `vela tee` initiated by `vela ap`.
The `server` part will create an `opteed` task.
Then, when the system starts, `opteed` will be started in the background:
```c++
opteed &
```

### 3. wasm

`vela tee` itself supports `wasm TA`. `wasm TA` means that the `TA` program itself is compiled and linked in the format of `wasm` bytecode, and then during runtime, it is loaded and run by the `wamr framework` built into `vela tee`.
