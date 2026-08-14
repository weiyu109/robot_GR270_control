# Keyboard Jog Demo

Windows控制台示例，不使用Qt。程序连接纳博特上位机SDK后切换至直角坐标系：

- 按住 `Up`：X+
- 按住 `Down`：X-
- 按住 `Left`：Y+
- 按住 `Right`：Y-
- 松开按键：立即调用停止点动
- 同时按下多个方向键：停止
- 控制台失去焦点：停止且不响应全局方向键
- `Esc` 或 `Ctrl+C`：停止、断开并退出

点动保持命令每100 ms发送一次，与SDK所附Qt示例一致。连接后程序要求操作者输入 `ENABLE`，然后依次执行：

1. 保存当前操作模式。
2. 切换到示教模式（`mode=0`）。
3. 切换到点动示教类型（`type=0`）。
4. 查询伺服状态；报警状态不会被自动清除。
5. 必要时将伺服切换到就绪状态，然后执行机器人上电。
6. 确认伺服进入运行状态（`state=3`）后才开放键盘点动。
7. 退出时先停止当前点动；如果机器人由本程序上电，则执行下电，并恢复进入程序前的操作模式。

如果程序启动前机器人已经处于伺服运行状态，程序不会在退出时擅自将其下电。

## 构建

使用Visual Studio x64工具链：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

如SDK位置不同：

```powershell
cmake -S . -B build -A x64 `
  -DNRC_SDK_ROOT="D:/path/to/Inexbot_API_v2.0.4-22.07"
```

构建脚本会把匹配架构的 `nrc_host.dll` 复制到程序输出目录。

## 运行

```powershell
.\build\Release\keyboard_jog_demo.exe 192.168.1.13 6001 10
```

参数依次为控制器IP、端口和速度百分比。默认值为 `192.168.1.13`、`6001`、`10`。

当前按SDK的状态式接口设计使用 `coord=1`、`axis=1` 表示直角坐标X方向，`axis=2` 表示Y方向。首次连接真机前，应向设备提供方确认该映射，并在低速、空载、安全区域进行验证。
