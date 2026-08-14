# Windows 兼容构建入口

本目录不再维护独立的机器人控制源代码。`main.cpp` 和 `CMakeLists.txt` 会引用项目根目录中的正式模块，保证 Windows 与 Linux 使用同一套：

- 示教点动模式切换
- 报警循环清错
- 伺服下电、就绪和重新上电
- X/Y/Z 三轴点动
- 运行状态主动检查
- 停止、下电、恢复原模式和断开连接

公共接口、文件职责和后续增加函数的方法见：

```text
../docs/遥操作控制模块说明.md
```

## Visual Studio 构建

在本目录执行：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

## 运行

```powershell
.\build\Release\keyboard_jog_demo.exe 192.168.1.13 6001 10
```

参数依次为控制器 IP、端口和速度百分比。启动后输入 `ENABLE` 才会进入示教点动和上电流程。
