# Keyboard Jog Demo

Windows/Ubuntu控制台示例，不使用Qt。程序连接纳博特上位机SDK后切换至直角坐标系：

- 按住 `Up`：X+
- 按住 `Down`：X-
- 按住 `Left`：Y+
- 按住 `Right`：Y-
- 松开按键：立即调用停止点动
- 同一轴的两个相反方向键同时按下：该轴停止
- X、Y两个轴的方向键可以同时按下
- `Esc` 或 `Ctrl+C`：停止、断开并退出

Windows版本在控制台失去焦点时停止。Ubuntu版本直接读取Linux evdev设备，能够可靠获得按下和松开事件，但输入是全局的：进入`ENABLE`状态后，即使切换到其他窗口，方向键仍然有效。

点动保持命令每100 ms发送一次，与SDK所附Qt示例一致。连接后程序要求操作者输入 `ENABLE`，然后依次执行：

1. 保存当前操作模式。
2. 切换到示教模式（`mode=0`）。
3. 切换到点动示教类型（`type=0`）。
4. 查询伺服状态；报警状态不会被自动清除。
5. 必要时将伺服切换到就绪状态，然后执行机器人上电。
6. 确认伺服进入运行状态（`state=3`）后才开放键盘点动。
7. 退出时先停止当前点动；如果机器人由本程序上电，则执行下电，并恢复进入程序前的操作模式。

如果程序启动前机器人已经处于伺服运行状态，程序不会在退出时擅自将其下电。

## Ubuntu构建

当前仓库已经包含x86-64 GCC 9版SDK动态库。Ubuntu 22.04可以直接构建：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

如SDK位置不同：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DNRC_SDK_ROOT=/absolute/path/to/Inexbot_API_v2.0.4-22.07
```

### 查找键盘evdev设备

优先使用稳定的`by-id`或`by-path`设备名，不要随意选择某个`eventN`：

```bash
ls -l /dev/input/by-id/*-event-kbd 2>/dev/null
ls -l /dev/input/by-path/*-event-kbd 2>/dev/null
```

选择实际键盘对应的一条路径。可以用下面的命令查看最终指向的设备：

```bash
readlink -f /dev/input/by-id/usb-your-keyboard-event-kbd
```

如果提示没有读取权限，检查设备权限和当前用户组：

```bash
ls -l /dev/input/eventN
id
```

Ubuntu通常可将当前用户加入`input`组，然后注销并重新登录使权限生效：

```bash
sudo usermod -aG input "$(id -un)"
```

`input`组可以读取全局键盘事件，只应授予受信任的本机用户。

### Ubuntu运行

Linux下第4个参数是明确的键盘设备路径：

```bash
./build/keyboard_jog_demo \
  192.168.1.13 6001 10 \
  /dev/input/by-id/usb-your-keyboard-event-kbd
```

启动后输入`ENABLE`。按住方向键点动，松开立即停止；`Esc`或`Ctrl+C`退出。由于evdev不提供桌面窗口焦点，程序运行期间不要在其他窗口使用方向键。

### 不连接机械臂测试键盘

先列出稳定的键盘设备路径及当前用户是否可读：

```bash
./build/keyboard_input_test --list
```

然后选择实际键盘对应的路径进行测试，例如笔记本内置键盘：

```bash
./build/keyboard_input_test \
  /dev/input/by-path/platform-i8042-serio-0-event-kbd
```

依次按下、松开四个方向键，终端会打印每次状态变化。按 `Esc` 或 `Ctrl+C` 退出。此诊断程序不加载机器人控制流程，也不会连接或控制机械臂。

## Windows构建

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

## Windows运行

```powershell
.\build\Release\keyboard_jog_demo.exe 192.168.1.13 6001 10
```

参数依次为控制器IP、端口和速度百分比。默认值为 `192.168.1.13`、`6001`、`10`。

当前按SDK的状态式接口设计使用 `coord=1`、`axis=1` 表示直角坐标X方向，`axis=2` 表示Y方向。首次连接真机前，应向设备提供方确认该映射，并在低速、空载、安全区域进行验证。
