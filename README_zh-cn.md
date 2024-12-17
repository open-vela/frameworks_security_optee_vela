# optee_vela

[[English](./README.md) | [中文](./README_zh-cn.md)]

## 项目概览

`optee_vela`本身是用于实现`Vela tee`和`optee os`之间的适配层，让`optee_os`可以运行在`Vela tee`系统当中。
因此`optee_os`当中的`TA`,`CA`程序都是可以直接在`vela`系统当中运行的。
借助于`optee_vela`,使得我们不需要对`optee_os`工程本身做任何修改，就可以使得`optee_os`在`vela`当中完整运行。

下面是`optee_vela`本身在整个`Vela tee`系统当中的位置:
```
+-------------------------------------------------------+
|                   optee os                            |
+-------------------------------------------------------+
|                  optee_vela                           |
+-------------------------------------------------------+
|                  vela kernel                          |
+-------------------------------------------------------+
```

## 项目描述

`optee_vela`当中主要是包含3部分功能:

1. `compat`: 用于支持`optee os`上层framework在`vela`当中运行的系统模块，例如`atomic`, `fs`, `mem`操作等基础模块
2. `server`: 用于接收和处理所有来自`vela ap`发起的对`vela tee`的请求，例如打开`open TA`,`invoke TA cmd`等操作
3. `wasm`: 用于支持`vela`特有的`wasm TA`在`optee os`当中运行

下面分别介绍:

### 1. compat

`compat`适配层主要是将`optee os`上层framework需要用到的系统API使用`vela`的系统API进行实现，例如`atomic`,`mem`,`fs`等基础API.
下面介绍一下主要的被替换的API模块:

1. fs

fs模块当中主要是包含了2部分:
- 1. `host_fs`
    `host_fs`主要是用于实现`optee os`当中需要的`ree fs`操作.在`optee os`当中，`ree fs`的实现是将所有的和fs相关的操作都转发到`ree`当中进行处理.
    这是因为`optee os`当中本身对`fs`的操作支持有限，所以复杂的`fs`操作都需要跨核传递到`ree`当中来处理.
    但是由于`vela tee`是一个全功能的`os`,在`tee`当中也支持复杂的`fs`操作，因此`vela tee`当中的`ree fs`可以直接在`vela tee`侧来完成.
    而`host_fs`就是用于实现该功能.

- 2. `rpmb_fs`
    `rpmb_fs`主要是用于实现`optee os`当中需要的操作`rpmb驱动`的实现.
    `vela tee`本身支持`rpmb`驱动，然后通过`rpmb_fs`就可以在`optee os`当中直接使用`rpmb`驱动.

2. `atomic`

这个API模块主要是将`optee os`运行需要的`atomic`，`spinlock`替换成`vela`系统支持的`atomic`和`spinlock`接口实现。

### 2. server

在`vela`当中，`vela ap`和`vela tee`之间的通信过程是通过`rpmsg socket`来进行的。
在这个过程当中，我们可以将`vela ap`作为客户端，然后`vela tee`作为服务端。
`vela tee`当中的`server`就是实现为`rpmsg socket server`，用于接收和处理`rpmsg socket client`发起的请求.

`server`部分本身用于完成`vela ap`发起的对`vela tee`的请求处理.
`server`部分会创建一个`opteed`的task出来.
然后在系统启动的时候，会在后台启动`opteed`:
```c++
opteed &
```

### 3. wasm

`vela tee`本身支持`wasm TA`, `wasm TA`是指的`TA`程序本身是以`wasm`字节码的格式来编译链接，然后在运行的时候，由`vela tee`内置的`wamr framework`来
加载和运行该`wasm TA`.
