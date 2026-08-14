# Keyboard Jog Demo


cd ~/wydata/keyboard_jog_demo

KEYBOARD_DEVICE="$(readlink -f /dev/input/by-id/*ITE*8910*-event-kbd)"

./build/keyboard_jog_demo \
  192.168.1.13 \
  6001 \
  10 \
  "$KEYBOARD_DEVICE"

Windows/Ubuntu控制台示例，不使用Qt。程序连接纳博特上位机SDK后切换至直角坐标系：

- 按住 `Up`：X+
- 按住 `Down`：X-
- 按住 `Left`：Y+
- 按住 `Right`：Y-
- 按住主键盘 `Shift`+`=` 或数字键盘 `+`：Z+
- 按住主键盘 `-` 或数字键盘 `-`：Z-
- 松开按键：立即调用停止点动
- 同一轴的两个相反方向键同时按下：该轴停止
- X、Y、Z 三个轴可以同时按下；同一轴的相反方向会互相抵消
- 遥操作中按 `Esc`：停止点动并返回主菜单
- `Ctrl+C`、主菜单输入 `0` 或关闭终端：停止、确认下电、断开并退出

Windows版本在控制台失去焦点时停止。Ubuntu版本直接读取Linux evdev设备，能够可靠获得按下和松开事件，但输入是全局的：进入菜单 `6` 的遥操作后，即使切换到其他窗口，方向键仍然有效。

连接成功后程序显示数字调试菜单，输入序号并按 Enter 后直接执行：

1. 循环清错并上电，直到确认伺服进入 `Running(state=3)`。
2. 停止活动点动并循环下电，直到确认安全状态。
3. 进入二级菜单，切换示教、远程或运行模式。
4. 读取当前关节位置，发送 J1 相对 `+1°` 的 `MoveJ`，速度为 `10%`。
5. 读取当前直角坐标，发送 X 相对 `+50 mm` 的 `MoveL`，速度为 `20 mm/s`。
6. 进入现有 XYZ 键盘遥操作；点动保持命令每 `100 ms` 发送一次，`Esc` 返回菜单。
7. 读取并打印末端位姿、关节位置以及当前激活 TCP 的编号和 XYZABC 参数。
8. 进入 TCP 子菜单：设置当前 TCP 的 `Z=-342 mm`，或把当前 TCP 的 XYZABC 归零；负载参数保持不变。
9. 发送 50 点 MoveL 阶梯队列：X 总前进 `100 mm`、速度 `100 mm/s`，每批 25 点进行拼接；执行完毕后自动关闭队列模式并下电。
10. 独立循环清错；即使伺服已经是 `Ready(state=1)`，也会至少成功发送一次 `clear_error`，但不会执行上电。
0. 如果队列模式仍然开启，直接关闭队列模式；随后安全下电并退出。

菜单 `4`、`5`、`9` 会直接发送运动命令，不进行二次确认；执行前必须确保工作区安全。队列奇数点相对起点抬高 Z `5 mm`，偶数点回到起始 Z，每点沿 X 前进 `2 mm`。菜单 `9` 会等待队列剩余长度变为 0；菜单 `4`、`5` 不等待运动完成。当前不做可达性、NaN/Inf 或超时重发检查。

伺服处于报警态（`state=2`）时，程序会持续执行清错并尝试切换到就绪态（`state=1`），不会开放键盘运动。只有连续3次确认伺服处于运行态（`state=3`）后才允许点动。运行中不注册错误/警告回调，仅每200 ms主动检查伺服状态；只有实际离开`state=3`时才停止全部轴并进入恢复流程，普通警告不会触发清错或电源切换。恢复循环没有自动超时，可使用`Ctrl+C`安全退出。

程序不会在SDK连接前额外建立裸TCP探测连接，因为部分控制器的6001服务为单连接，探测会干扰紧随其后的SDK连接。程序只能主动清理本进程持有的SDK连接，不能安全地强制关闭其他进程建立的机器人会话。

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

启动后输入菜单序号并按 Enter。先使用 `1` 完成上电，再输入 `6` 进入键盘遥操作；按住方向键点动，松开立即停止，`Esc`返回菜单，`Ctrl+C`安全下电退出。由于evdev不提供桌面窗口焦点，遥操作期间不要在其他窗口使用方向键。

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

当前按SDK的状态式接口设计使用 `coord=1`，`axis=1/2/3` 分别表示直角坐标 X/Y/Z。首次连接真机前，应向设备提供方确认该映射，并在低速、空载、安全区域进行验证。

代码模块划分、公共接口、上下电/清错/模式切换流程和后续新增函数的方法见 [遥操作控制模块说明.md](docs/遥操作控制模块说明.md)。

以后接入或调试其他型号机械臂时，可直接复用 [新机械臂终端调试标准流程.md](docs/新机械臂终端调试标准流程.md) 中的目录分类、分阶段调试步骤、运行命令模板和 Codex 固定指令。
