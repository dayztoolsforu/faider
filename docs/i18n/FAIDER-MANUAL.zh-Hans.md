# Faider 用户手册

[English](../FAIDER-MANUAL.md)

**版本 {version} · Windows 10 (2004) 或更高版本**

Faider 是一款 Windows 实时音频路由器兼调音台，以大型调音台为蓝本。
它从你的音频接口、Windows 设备和各个程序获取声音，经过通道条、总线和输出处理，再从你选择的任何设备播放：耳机、音箱、直播，或是用于通话和游戏的虚拟麦克风。

本手册涵盖应用的全部功能，带你一步步完成常见任务，并告诉你出问题时该怎么办。

---

<a id="contents"></a>

## 目录

1. [开始之前](#1-before-you-start)
2. [首次设置](#2-first-time-setup)
3. [窗口](#3-the-window)
4. [路由的工作原理](#4-how-routing-works)
5. [调音台](#5-the-console)
6. [通道条，逐段介绍](#6-the-channel-strip-section-by-section)
7. [RAW 通道条：应用、打击垫、磁带和组合器](#7-raw-strips-apps-pads-tape-and-combinators)
8. [输出](#8-outputs)
9. [插件](#9-plugins)
10. [ROUTING 页面](#10-the-routing-page)
11. [工具：打击垫](#11-tools-pads)
12. [工具：磁带、录音机和回放](#12-tools-tape-recorder-and-replay)
13. [设备](#13-devices)
14. [场景、撤销和会话](#14-scenes-undo-and-sessions)
15. [设置](#15-settings)
16. [远程控制、悬浮层和托盘](#16-remote-control-overlay-and-tray)
17. [操作指南](#17-how-to-guides)
18. [故障排除](#18-troubleshooting)
19. [参考](#19-reference)

---

<a id="1-before-you-start"></a>

## 1. 开始之前

<a id="what-you-need"></a>

### 你需要什么

| | |
|---|---|
| **Windows** | Windows 10 2004 版或更高版本，或 Windows 11。捕获单个程序的声音需要 2004 或更高版本 |
| **音频接口** | 可选。安装其 ASIO 驱动后，Faider 会使用接口的时钟运行，延迟最低。没有接口时，它使用你的 Windows 播放设备，或内置时钟运行(见[时钟](#clocking)) |
| **耳机** | 接到音频接口(或任何 Windows 播放设备)上，这样你才能听到自己的混音 |
| **VB-CABLE** | 可选，免费 (vb-audio.com/Cable)。一个虚拟麦克风，让 Discord、游戏、OBS 等应用能听到你的混音 |
| **ffmpeg** | 可选。PATH 中有 `ffmpeg.exe`，或在 **SETTINGS > Recording > FFmpeg** 中选好了它时，录音机还能写出 MP3、AAC、Opus 和 ALAC |

<a id="words-used-in-this-manual"></a>

### 本手册用到的词

| 词 | 含义 |
|---|---|
| **通道**(输入) | 调音台上的一个声源：麦克风、设备、程序、打击垫、磁带 |
| **通道条** | 调音台上控制一个通道、总线或输出的那一列 |
| **路由** | 把通道或总线送往某处的连接：送到总线或输出 |
| **总线** | 子混音。送入它的声音会被求和、一起处理，然后继续送出 |
| **组合器** | 把声音分成并行路径的总线，每条路径有自己的插件，最后再求和 |
| **输出** | 在某个设备(耳机、音箱、虚拟线缆)上播放的最终混音 |
| **接口** | 你的 ASIO 音频接口(如果有) |
| **时钟** | 一切运行时所依据的节拍：你的接口，没有则用 Windows 播放设备，再没有则用内置时钟 |

<a id="2-first-time-setup"></a>

## 2. 首次设置

本章带你从安装程序一直走到一套可用的设置：你的麦克风声音进入耳机，应用能听到你的麦克风，某个程序的声音有自己的通道。请按顺序操作。每一步只需一两分钟。

<a id="step-1-install-faider"></a>

### 步骤 1：安装 Faider

1. 运行安装程序 `Faider-<version>-Setup.exe`。
2. 由于安装程序没有代码签名，Windows SmartScreen 可能会提示这是无法识别的应用。点击**更多信息**，然后点击**仍要运行**。
3. 选择 **Install for all users**(推荐；请允许管理员权限提示)或 **Install for me only**(不需要管理员权限)，然后接受许可协议。如果想要桌面快捷方式，请在安装过程中勾选(默认不勾选)。更新会按已安装副本的方式安装，不再询问。
4. 安装程序会把应用放到 `C:\Program Files\Faider`(仅为你自己安装时：`%LOCALAPPDATA%\Programs\Faider`)，并在开始菜单中添加 **Faider**。所需的一切都已包含在内。
5. 在最后一页，你可以立即启动 Faider。如果没有安装 VB-CABLE，它还会提示是否打开 VB-CABLE 的下载页面。

你的混音和设置保存在 `%APPDATA%\Faider`，与程序本身分开。卸载时会询问是否一并删除它们；除非你同意，否则这些内容会保留。

**不使用向导安装**。安装程序支持 Inno Setup 的开关，供脚本和部署工具使用：

| 开关 | |
|---|---|
| `/SILENT` | 不显示向导，只显示进度窗口 |
| `/VERYSILENT` | 什么都不显示 |
| `/SUPPRESSMSGBOXES` | 与上面任一开关一起使用：消息框会得到默认回答 |
| `/NORESTART` | 安装后绝不重启 Windows |
| `/TASKS="desktopicon"` | 创建桌面快捷方式 |
| `/ALLUSERS`、`/CURRENTUSER` | 为所有用户安装(默认：除非以管理员身份运行，否则 Windows 会要求管理员权限)，或为运行它的用户安装 |
| `/LOG="file"` | 把日志写入该文件。日志总会写入：不加此开关时，它是 `%TEMP%` 中的 `Setup Log <date>.txt` |

例如：`Faider-<version>-Setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER`。静默安装会先退出正在运行的 Faider，安装后既不启动 Faider，也不提示打开 VB-CABLE 的页面。卸载程序(程序文件夹中的 `unins000.exe`)同样支持 `/SILENT` 和 `/VERYSILENT`。静默卸载时，绝不会删除你的混音和设置。

<a id="step-2-install-vb-cable-optional"></a>

### 步骤 2：安装 VB-CABLE(可选)

VB-CABLE 提供一个其他应用可以选用的虚拟麦克风。如果你只想为自己的耳机做混音，可以跳过这一步。

1. 从 vb-audio.com/Cable 下载 VB-CABLE，并以管理员身份运行其安装程序。
2. 提示时重启 Windows。
3. Windows 现在多了两个设备。**CABLE Input** 是播放设备：播放到它里面的声音会从 **CABLE Output** 出来，后者是录制设备，应用可以把它选作麦克风。

> **Windows 会把你的声音切换到线缆上。** 安装虚拟线缆后，它常常会成为 Windows 的默认设备：**CABLE Input** 成为你的扬声器，**CABLE Output** 成为你的麦克风。这样你就听不到 Windows 的任何声音，而使用默认麦克风的应用听到的是线缆。安装后请马上把两者都切换回来。Faider 可以替你完成。

**用 Faider 把你的设备改回来**：点击 **SET MY DEFAULTS**：它在 SETUP 的 **APPS HEAR YOU** 页面(步骤 4)、PRE-FLIGHT 的 **WINDOWS DEFAULTS** 行，或 FIND 中的 **WINDOWS DEFAULTS**。它会列出你真正的扬声器和麦克风。选择你的耳机或音箱，然后再点一次并选择你的麦克风。每一个都会成为 Windows 的默认设备，以及它的默认通信设备。

**或在 Windows 中改回来：**

1. 打开**设置 > 系统 > 声音**(或右键点击任务栏的扬声器图标 > **声音设置**)。
2. 在**输出**下，选择你的耳机或音箱(或你的接口的输出)，而不是 **CABLE Input**。
3. 在**输入**下，选择你真正的麦克风，而不是 **CABLE Output**。
4. Windows 为通话单独保留一个默认设备。向下滚动到**更多声音设置**。在**播放**选项卡上，右键点击你的耳机或音箱 > **设为默认通信设备**。在**录制**选项卡上，对你的麦克风做同样的操作。

设置输出还有更快的办法：点击任务栏的扬声器图标，点击音量滑块旁边的 **>**，再选择你的设备。

请让真实设备保持为 Windows 的默认设备。Faider 只会把声音播放到你设置了路由的线缆上，而每个应用会自己选择 **CABLE Output** 作为麦克风(步骤 6)。如果 Windows 的任何一个默认设备是线缆，PRE-FLIGHT 会在 **WINDOWS DEFAULTS** 下报告。

<a id="step-3-start-faider"></a>

### 步骤 3：启动 Faider

打开开始菜单，点击 **Faider**(或者使用桌面快捷方式，如果你创建了的话)。第一次启动时，**SETUP** 会在自己的窗口中打开。

<a id="step-4-answer-setups-four-questions"></a>

### 步骤 4：回答 SETUP 的四个问题

![SETUP 的麦克风页面：已选择 IN 1 和耳麦麦克风](../images/console/setup.jpg)

SETUP 每页问一个问题，共四个，并根据你的回答建立混音。点击卡片进行选择。除第三页外，每页都可以多选。**NEXT** 继续，**BACK** 返回。

1. **WHERE YOU LISTEN**：选择你的耳机或音箱。Windows 的默认设备已被选中。
2. **MICROPHONES**：选择你对着说话的麦克风。它们是接口的输入(**IN 1**、**IN 2** …)和 Windows 录制设备。
3. **APPS HEAR YOU**：选择应用用作麦克风的虚拟线缆(完成步骤 2 后是 **CABLE Input**)，或选 **NOT NOW**。如果 Windows 仍在用线缆播放或录音，底部的 **SET MY DEFAULTS** 会把你真正的扬声器和麦克风改回来(步骤 2)。
4. **PROGRAMS**：选择你想放到调音台上的程序：现在正在发声的，以及正在运行但显示为 "Not playing yet" 的(它们一发声就会被捕获)。
5. 点击 **BUILD THE MIX**。**DONE** 会列出所创建的内容。
6. 点击 **OPEN THE MIXER** 查看。

每个回答会创建什么：

| 页面 | 创建的内容 |
|---|---|
| **WHERE YOU LISTEN** | 每个设备上的一个输出。第一个叫 **HEADPHONES**，其他的以设备名命名 |
| **MICROPHONES** | 每个麦克风一个通道，名为 **MICROPHONE** (MICROPHONE 2 …) |
| **APPS HEAR YOU** | 线缆上名为 **MIC FOR APPS** 的输出。每个麦克风都路由到它 |
| **PROGRAMS** | 每个程序一个 RAW 通道，路由到你的收听位置，外加一条应用规则，每当该程序发声时自动把它带回来 |

你的麦克风不会路由到你自己的耳机，所以你不会听到自己声音的延迟回声。想听到自己，请把麦克风路由到你的耳机(步骤 6)。

SETUP 不再是一个页面：每当你添加设备或程序时，都可以从 SETTINGS > Startup & checks 中的 **Run setup** 或 **FIND**(**SETUP: BUILD A MIX**) 再次运行它。它会补上缺少的内容，不会删除任何东西。

<a id="step-5-check-the-audio-is-running"></a>

### 步骤 5：检查音频是否在运行

看右上角的**状态灯**。

- 带百分比的**绿色**：音频正在运行。继续步骤 6。
- **红色，显示 OFF**：音频没有运行。可能有其他程序占用了你的接口(DAW、驱动的控制面板)。关闭它并重新启动 Faider。见 [18.4](#184-the-interface-wont-start-or-stopped)。

点击状态灯打开 **DEVICES**。**STREAMS** 显示正在使用的时钟(ASIO、Windows 或内置时钟；见[时钟](#clocking))，**BUFFER** 设置其缓冲区大小。不产生爆音的最小值延迟最低。256 或 512 个采样适合大多数系统。

<a id="step-6-hear-yourself-and-let-apps-hear-you"></a>

### 步骤 6：听到自己，也让应用听到你

调音台为每个通道、总线和输出各显示一条通道条。每条通道条 HEADER 中的**路由框**显示它被送到哪里：每条总线和每个输出设备一个框，已路由的地方亮起(见 [4](#4-how-routing-works))。

1. 说话，看你的麦克风电平表跳动。
2. 想听到自己，在麦克风的路由框中点亮你耳机的那个框。
3. 在 Discord、游戏、OBS 或任何其他应用中，选择 **CABLE Output** 作为麦克风。
4. 检查它们收到了什么：**HEAR WHAT THEY HEAR**(在 PRE-FLIGHT 的 **MIC OUTPUT** 行上，或在 **FIND** 中，Ctrl+K)会录下应用收到的 5 秒声音并回放给你。

如果你听到某个程序两遍，见 [18.7](#187-i-hear-something-twice-or-an-echo)。

侧边栏中的 **ADD CHANNEL** 和 **ADD APP** 添加通道(**ADD APP** 还会列出正在运行但 **NOT PLAYING YET** 的程序)。把某个声音路由到还没有输出的设备，就会在该设备上建立输出。右键点击输出可使用 **DEVICE...**(它在哪里播放)和 **HEADPHONE EQ**。右键点击任何通道条可使用 **RENAME...**、**COLOUR**、**IN THE MINI MIXER** 和 **REMOVE**。删除程序的通道也会删除它的应用规则，所以通道不会自己回来。

<a id="step-7-pro-for-the-whole-console"></a>

### 步骤 7：PRO，整个调音台

调音台就是整个控制台：每个通道的输入、滤波器、动态和 EQ、总线和组合器、插件。想要更小的视图，侧边栏中的 **COMPACT** 会把每条通道条折叠成只剩名称栏和推子部分。再点一次恢复完整的通道条(见[侧边栏](#the-sidebar))。

要调整你的麦克风，见 [17.1](#171-hear-yourself-set-up-a-microphone)。每个控件都在 [6](#6-the-channel-strip-section-by-section) 中。

<a id="step-8-run-pre-flight"></a>

### 步骤 8：运行 PRE-FLIGHT

启动几秒后，**PRE-FLIGHT** 会检查设置，只在有需要注意的地方时才打开(首次启动时不会，因为先出现的是 SETUP)。你可以随时从 **SETTINGS > Startup & checks** 中的 **Run pre-flight** 或 **FIND** (Ctrl+K) 运行它。每一行都应该是绿色。见 [18.1](#181-pre-flight)。

<a id="step-9-finish"></a>

### 步骤 9：完成

- **你的混音会自动保存**，任何改动后一秒内就会保存。若要以名称保留一个版本，请使用 **SETTINGS > Sessions** 中的 **Save as…**。
- **随 Windows 启动：** **SETTINGS > General** 中它的开关会在你登录时让 Faider 在托盘中启动，这样你的麦克风从一开始就能工作。
- **关闭窗口不会停止音频。** Faider 会继续在系统托盘中运行，所以你的麦克风和耳机保持工作。要停止它，请右键点击托盘图标并选择 **Quit Faider**。程序在托盘中时再次启动它，会让窗口重新出现。

每个页面第一次打开时，都会有一条简短的提示进行说明。可用 **SETTINGS > General** 中的 **Tips** 关闭提示。

设置到此完成。本手册的其余部分详细介绍每个部分。

<a id="3-the-window"></a>

## 3. 窗口

![带演示混音的 MIXER 页面：两个麦克风、两个应用通道、打击垫和磁带、三条总线和两个输出](../images/console/mixer.jpg)

<a id="the-sidebar"></a>

### 侧边栏

页面是左侧自上而下排列的卡片：**MIXER**、**ROUTING**、**TOOLS**、**DEVICES**、**SETTINGS**。当前显示的页面带有亮起的蓝色边缘。显示 MIXER 时，侧边栏还会出现下列卡片：

| 分组 | 卡片 | 作用 |
|---|---|---|
| **DESK** | **ADD CHANNEL** | 打开输入菜单：接口输入、Windows 设备、正在发声的应用、尚未发声的程序、浏览器（整个浏览器，或单独隔离一个标签页）、测试音。打击垫和磁带的通道在它们各自的页面（**TOOLS**）中添加 |
| | **ADD APP** | 列出现在正在发声的程序，然后在 **NOT PLAYING YET** 下列出正在运行、带有窗口但没有发声的程序。选一个，就会给它一个通道；尚未发声的程序会从它开始发声的那一刻起被捕获 |
| | **ADD BUS** | 一条总线，或一个组合器(起始为两条路径) |
| | **PLUGINS** | 把插件面板从通道条下方滑出(见 [9](#9-plugins)) |
| | **COMPACT** | 把每条通道条折叠成只剩名称栏和推子部分(声像、SOLO、CUT、SEL、推子及其读数)；调音台随之变矮并回到顶部。紧凑时亮起；再点一次恢复完整的通道条。FIND 中的 **COMPACT DESK** / **FULL DESK** 也一样 |
| **SELECT** | **AUTO** | 触碰通道条上的任何控件都会选中该通道条 |
| | **FOLLOW** | 调音台平滑滚动到所选通道条，在别处选中时(FIND、快捷键)也一样 |
| **SECTIONS** | HEADER … FADER | 通道条的各个部分(COMPACT 时只有 NAME 和 FADER)。亮着的就是屏幕上显示的。点击其一可滑动过去 |

SELECT 和 COMPACT 的选择会被记住。通道电平表的测量位置(**IN**、**PRE** 或 **POST**)在 **SETTINGS > Audio > Channel meter point** 中设置。

<a id="the-top-bar"></a>

### 顶部栏

![顶部栏：场景、状态灯、MINI、HISTORY、FIND 和缩放](../images/console/topbar.jpg)

从右往左：

| 控件 | 作用 |
|---|---|
| **+ 100% -** | 缩放。在 100%、125% 和 150% 之间切换。点击百分比回到 100%。Ctrl + / Ctrl - / Ctrl 0 效果相同 |
| **FIND** (Ctrl+K) | 按名称搜索每个页面、通道、场景、宏、打击垫和命令 |
| **HISTORY** (Ctrl+H) | 你做过的每一次更改。选一个就能回到那一刻 |
| **MINI** | 迷你调音台：一个小调音台窗口(点击托盘图标也能打开)。它上面的 **PIN** 让它保持置顶 |
| **状态灯** | 绿色：音频正常。琥珀色：音频负载很高，或最近有设备数据不足。红色：最近 10 秒内有可听见的故障，或没有音频在运行。数字是音频负载。点击它进入 DEVICES |
| **SCENES** | 你保存的混音。**+** 保存一个新的(见 [14.1](#141-scenes)) |

窗口变窄时，状态灯最先消失，然后是 MINI，再是 HISTORY。FIND 可以找到它们全部。

<a id="the-transport"></a>

### 走带控制

在顶部栏下方，每个 Mixer 视图都会显示磁带机的时间(已播放时间和曲目长度)、曲目名称及其按键：**倒带 10 秒**、**停止**、**播放/暂停**、**快进 10 秒**、**录音**、**循环**。

- **录音**就是录音机的 **REC**：它按录音机的格式和文件夹，录制在 TOOLS > TAPE 上选择的声源，录音进行期间保持点亮。再按一次停止。
- **循环**依次切换：**全部重复** → **单曲重复**(标有 **1**) → **关闭**。

---

<a id="4-how-routing-works"></a>

## 4. 路由的工作原理

路由决定每个声音去往何处。路由之前什么都听不到，而且任何声音都可以路由到你想要的任意多个地方。

```mermaid
flowchart LR
    S[Source] --> C[Channel strip] --> P[Plugins] --> F[Fader · pan · cut]
    F --> B[Buses] --> O[Outputs]
    F --> O
    O --> D[Devices]
```

<a id="the-path-of-a-sound"></a>

### 声音的路径

1. **通道**接收一个声源，让它通过自己的通道条(输入、滤波器、动态和 EQ，顺序由其 HEADER 显示)，然后经过插件，再经过推子、声像和静音。
2. **路由**把通道同时送往任意数量的总线和输出(各最多 8 个)。每条路由携带的都是通过推子之后的通道，所以推子和 **CUT** 会影响它被路由到的所有地方。
3. **总线**把送入它的所有声音求和，经过自己的通道条和插件，并以同样的方式继续路由到输出(或其他总线)。
4. **输出**把送入它的所有声音求和，经过它的总线压缩器、混音 EQ 和限幅器(**LIMIT**)，然后在它的设备上播放。

<a id="outputs-are-devices"></a>

### 输出就是设备

每个输出在一个设备上播放：你接口的输出、USB 耳麦、音箱、**CABLE Input**。你不用手动创建输出。把某个声音路由到一个设备，就会在该设备上建立输出。这就是路由框既列出总线也列出设备的原因。

- 每个设备只有一个输出。把多个通道路由到同一个设备，它们会在它的输出中汇合。
- 不同的输出可以携带不同的混音。你的耳机可以听到游戏、音乐和人声，而直播输出只获得音乐和人声。
- 拔掉设备后，它的输出会静音，直到设备回来。

<a id="three-places-to-route"></a>

### 三个路由的位置

这三处修改的是同一组路由。哪个离你最近就用哪个。

| 位置 | 方式 |
|---|---|
| **路由框**(每个通道条的 HEADER) | 每条总线和每个输出设备一个框。亮起：已路由到那里。点击切换 |
| **ROUTE TO**(右键点击通道条) | 同一份列表，做成菜单，已路由的地方打勾 |
| **ROUTING 页面** | 每个通道条是一个方块，每条路由是一条线。先点一个方块，再点另一个(见 [10](#10-the-routing-page)) |

<a id="buses"></a>

### 总线

当几个通道需要同样的处理，或需要共用一个推子时，用总线。把通道路由到总线，再把总线路由到输出。然后关掉这些通道到该输出的直接路由，否则它们会到达两次(一次直达，一次经过总线)。总线可以送到另一条总线，而且不增加延迟。

<a id="combinators"></a>

### 组合器

组合器是一条被分成并行**路径**的总线(起始为两条)。每条路径有自己的插件，各路径在组合器的推子处求和。你可以把声源路由到一条路径或多条路径(见 [7](#7-raw-strips-apps-pads-tape-and-combinators))。

<a id="rules-that-keep-it-safe"></a>

### 保证安全的规则

- **反馈回路会被拒绝。** 会让声音回到它来源处的路由(一条总线进入送给它自己的总线，或把输出的声音送回它自己的输入)会立刻被再次关闭，并且事件日志会指出这个回路。
- 任何通道条上的 **Solo** 会让所有没有被独奏的通道条静音，已标记为独奏安全的除外。
- **应用通道与重复。** 程序除了送入它的通道，也会在自己的设备上播放(见 [18.7](#187-i-hear-something-twice-or-an-echo))。

<a id="clocking"></a>

### 时钟

由一个设备决定时钟。Faider 启动时按以下顺序选择：

1. **ASIO**：你的音频接口，通过其 ASIO 驱动。延迟最低。
2. **Windows**：没有 ASIO 驱动时，使用你的 Windows 默认播放设备(虚拟线缆、显示器扬声器或 VR 头显，仅在没有其他设备时才用)。你接口的输出会成为该设备的输出，麦克风则作为 Windows 设备添加。
3. **内置时钟**：完全没有播放设备时。所有设备，包括你的耳机，都跟随它。

其他每个设备和每个被捕获的程序都按自己的时钟运行，并通过一个修正漂移的小缓冲区加入，所以时钟漂移也绝不会产生爆音。**DEVICES > STREAMS** 会显示每一个，以及正在使用的时钟。

如果决定时钟的 Windows 设备被拔出、被关闭或停止工作，下一个时钟会自动接替，并有通知告诉你是哪一个。无法启动的设备(因为被其他程序占用)会被跳过，直到你的设备发生变化，然后再次尝试。使用内置时钟时，只要接入了播放设备，它就会从内置时钟那里接替。

若要自己选择时钟，请使用 **SETTINGS > Audio** 中的 **Clock**：**The interface (ASIO)**(上面的顺序，默认)、按名称选择的 Windows 播放设备，或 **The built-in clock**。选择在 Faider 下次启动时生效，**RESTART NOW** 会立即重启。你的混音会随之带过去，并仍为你的接口保存。选择内置时钟时，不会有设备从它那里接替。

<a id="5-the-console"></a>

## 5. 调音台

<a id="layout"></a>

### 布局

调音台从左到右显示所有通道条：**通道**，然后是**总线**(紫色名称栏)，然后是**输出**(琥珀色名称栏)。每个通道条从上到下有七个部分：

| 部分 | 内容 |
|---|---|
| **HEADER** | 电平表、FILT / EQ / EXT 键、处理顺序、路由框、通道条编号 |
| **INPUT** | 输入微调、极性、麦克风驱动、输入开关和立体声宽度 |
| **DYNAMICS** | 压缩器和门限 |
| **FILTERS** | 低通、高通和 NOISE 开关 |
| **EQ** | 四个频段 |
| **NAME** | 名称栏和电源开关 |
| **FADER** | 声像、SOLO、CUT、SEL、推子及其读数、DUCK 指示灯 |

<a id="mouse-and-keyboard"></a>

### 鼠标和键盘

| 操作 | 结果 |
|---|---|
| 上下**拖动**旋钮或推子 | 改变它。拖动或悬停时，它的值会显示在旁边 |
| **双击**旋钮或推子 | 恢复默认值 |
| **点击**按键或开关 | 开 / 关 |
| **点击**名称栏 | 选中该通道条 |
| 左右**拖动**通道条的名称栏 | 在同类中移动：通道、总线或输出(见 [17.4](#174-reorder-channels)) |
| **点击**名称栏上的电源符号 | 关闭或打开该通道条 |
| **右键点击**通道条 | 该通道条的菜单 |
| **鼠标滚轮** | 横向滚动调音台。**Shift + 滚轮**上下滚动 |
| **Ctrl + / Ctrl -** | 缩放整个窗口 |

左键单击从不打开菜单。菜单始终在右键上。

<a id="selecting-a-strip"></a>

### 选中通道条

被选中的通道条带有蓝色轮廓。点击名称栏或它的 **SEL** 键，或在 **AUTO** 开启时触碰它的任何控件，都能选中通道条。被选中的通道条就是插件面板所显示的通道条，也是 FIND、快捷键和远程控制所作用的通道条。

调音台获得焦点时(先点击它)：**Left / Right** 选择上一个 / 下一个通道条，**M** 将其静音，**S** 将其独奏，**Up / Down** 把它的推子移动 0.5 dB(按住 **Shift** 为 3 dB)。

<a id="switching-a-strip-off"></a>

### 关闭通道条

点击名称栏左边的电源符号。通道条会淡出并变灰。它不再被处理，它的设备或程序也会被释放。关闭期间，**它上面的任何东西都不能更改**：旋钮、按键和插件面板都会忽略点击，它的菜单只提供 **TURN ON** 和间隔条。再次点击电源符号即可恢复。

<a id="spacers"></a>

### 间隔条

间隔条是在分组之间留出空间的空白通道条。右键点击一个通道条 > **ADD SPACER BEFORE** / **ADD SPACER AFTER**。右键点击间隔条可使用 **NARROW**、**ADD ANOTHER** 和 **REMOVE SPACER**。当它前面的通道条移动时，间隔条会跟随它。

<a id="the-strip-menu-right-click"></a>

### 通道条菜单(右键点击)

| 项目 | |
|---|---|
| **INPUT** | 更换声源：**INTERFACE** 输入、**WINDOWS DEVICES**(或 WINDOWS DEFAULT)、**WHAT AN OUTPUT PLAYS**(环回)、**TEST TONE**、**NOTHING** |
| **ROUTE TO** | 每条总线和每个输出设备，已发送的地方打勾 |
| **PLUGINS** | 打开一个、**ADD PLUGIN...**、**REMOVE** |
| **MAKE MONO / MAKE STEREO** | 仅限通道 |
| **NOISE** | 降噪：**ON**(同通道条上的 NOISE 开关)、**NEURAL** 或 **SPECTRAL**，以及它的 **DEPTH**(-6 到 -30 dB) |
| **KEY FROM** | 通道条的 **EXT** 键控收听的通道(它的侧链)，或 **NONE**。应用、打击垫、磁带或组合器通道条上没有此项 |
| **DUCK UNDER** | 另一个通道被听到时，这个通道压低到它下面；选择那个通道或 **NONE**，以及压低多少(**DEPTH**，-6 到 -24 dB)。应用通道上也可用，这样你说话时音乐会压低 |
| **COLOUR** | 通道的颜色，显示在它的名称栏上和它的 ROUTING 方块上 |
| **IN THE MINI MIXER** | 勾选后，该通道条在迷你调音台中有一个推子(见 [16](#16-remote-control-overlay-and-tray)) |
| **ADD SPACER BEFORE / AFTER** | |
| **RENAME...** | |
| **REMOVE** | 删除通道或总线 |

---

<a id="6-the-channel-strip-section-by-section"></a>

## 6. 通道条，逐段介绍

<a id="header"></a>

### HEADER

![HEADER：通道条最上面的部分](../images/console/strip-header.jpg)

| 控件 | 作用 |
|---|---|
| **电平表** | 以一列圆形 LED 显示通道的电平(取较响的一侧)，点亮的最高一格会保持片刻。它按 SETTINGS > Audio > Meter standard 中选择的表头标准读数：使用该标准自己的 LED、颜色和刻度(VU 列读数为 -20 到 +3，PPM BBC 列为 1 到 7)，测量位置按 SETTINGS > Audio > Channel meter point 的设置(IN、PRE 或 POST)。输出的电平表也以同样方式读数 |
| **FILT** | 把滤波器送入门限/压缩器的检测器(键控滤波) |
| **EQ** | 接入或旁路 EQ |
| **EXT** | 动态处理收听外部键控信号(侧链)，而不是通道本身：即在右键 > **KEY FROM** 中选择的通道。尚未选择时按下它，会打开那个列表 |
| **处理顺序** | 三个块(FILT、EQ、DYN)，自上而下是通道运行它们的顺序。**点击一个块**让它上移一位。最上面的会移到末尾。亮着的块在使用中 |
| **路由框** | 每条总线和每个输出设备一个框。亮起：通道被送到那里。点击切换(见[输出就是设备](#outputs-are-devices)) |
| **编号** | 该通道条在调音台上的位置 |

<a id="input"></a>

### INPUT

![INPUT：IN TRIM、极性、麦克风驱动、输入开关和 WIDTH](../images/console/strip-input.jpg)

| 控件 | |
|---|---|
| **IN TRIM** | 输入增益 |
| **ø** | 极性反转 |
| **MIC** | 驱动：增加温暖感和电平(以百分比显示)。信号源有信号时，它旁边的绿色 LED 会亮起 |
| **PRE** | 接入或旁路输入级(旁路时，输入微调、极性和驱动都被绕过) |
| **WIDTH** | 立体声宽度：0 % 为单声道，100 % 保持原样，200 % 为两倍宽。在单声道通道上显示为淡色 |

<a id="dynamics"></a>

### DYNAMICS

![DYNAMICS：上方是压缩器，下方是门限，中间是它们的增益衰减 LED](../images/console/strip-dynamics.jpg)

**IN** 开关(带电源符号)同时接入或旁路压缩器和门限。

**压缩器**

| 控件 | |
|---|---|
| **MIX** | 湿/干混合(并行压缩)。拖动绿色读数 |
| **ratio**、**thresh**、**rel** | 压缩比、阈值和释放 |
| **fast** | 快速起音 (3 ms)。关闭为 10 ms |
| **pk** | 峰值检测。开启(默认)时，压缩器对峰值做出反应。关闭时，它对平均 (RMS) 电平做出反应，更平滑 |
| **增益衰减 LED** | 衰减 3、6、10、14 和 20 dB |

**门限 / 扩展器**

| 控件 | |
|---|---|
| **range**、**thresh**、**rel**、**hold** | 关闭多少、在哪里打开、关闭多快、保持多久 |
| **exp** | 扩展器模式(更柔和的门限) |
| **fast** | 快速起音 (0.1 ms)。关闭为 1.5 ms |
| **门限 LED** | 门限正在关闭的程度 |

即使压缩器和门限被旁路，LED 也会显示它们*将会*怎么做，所以你可以在接入之前先设置好。

<a id="filters"></a>

### FILTERS

![FILTERS：低通和高通](../images/console/strip-filters.jpg)

**lp**(低通)和 **hp**(高通)。完全拧到最小时，高通关闭。完全拧到最大时，低通关闭。

**NOISE** 开启或关闭降噪：嘶声、嗡声和房间噪声会被压低，而人声保持不变。右键点击通道条 > **NOISE** 可选择 **NEURAL**(默认，在 48 kHz 下运行；在其他采样率下按 SPECTRAL 运行)或 **SPECTRAL**，以及降噪的深度。关闭时不占用任何资源。

<a id="eq"></a>

### EQ

![EQ：HF、HMF、LMF 和 LF，以及 EQ IN 开关](../images/console/strip-eq.jpg)

| 频段 | 控件 |
|---|---|
| **HF** | gain、freq、**bell**(关闭时为搁架式) |
| **HMF** | gain、freq、q |
| **LMF** | gain、freq、q |
| **LF** | gain、freq、**bell**(关闭时为搁架式) |

**EQ IN** 开关接入或旁路整个 EQ(与 HEADER 上的 EQ 键相同)。

<a id="fader"></a>

### FADER

![名称栏和 FADER 部分：声像、SOLO、CUT、SEL、推子及其读数](../images/console/strip-fader.jpg)

| 控件 | |
|---|---|
| **PAN** | 左/右。双击回到中间 |
| **SOLO** | 原位独奏 |
| **CUT** | 让通道静音(调音台上的 **M** 键) |
| **SEL** | 选中此通道条 |
| **推子** | 电平，从 +12 dB 到关闭。下方的读数以 dB 显示电平 |
| **safe** | 独奏安全：其他通道被独奏时，该通道仍可听见 |
| **listen** | 监听动态处理的键控信号 |
| **duck** | 通道被压低到另一个通道下面时亮起(右键点击 > **DUCK UNDER**) |

### 语音特效

除了推子条自身的处理，每个通道还带有自己的语音特效：

| 特效 | 作用 |
|---|---|
| **ROBOT** | 环形调制：声音与一个音调相乘，听起来像金属声 |
| **ECHO** | 延迟，最长一秒 |
| **RADIO** | 限制在 300 Hz - 3 kHz 并加入过载，听起来像一只小音箱 |
| **PITCH** | 声音升高或降低，两个方向最多各一个八度 |

它们用宏切换：动作为 **FX ON**、**FX OFF** 或 **FX TOGGLE**，目标是通道 ([15](#15-settings))。
打击垫、快捷键、触发器或远程控制都可以触发这个宏，所以特效随混音一起进出。

---

<a id="7-raw-strips-apps-pads-tape-and-combinators"></a>

## 7. RAW 通道条：应用、打击垫、磁带和组合器

有些通道携带的是已经处理完成的声音：程序的音频、打击垫、磁带。它们使用 **RAW 通道条**，有路由、需要时可用的插件、声像、推子和按键，但没有内置效果。当它们出现在调音台上时，其内置效果会被关闭一次(并记入事件日志)。它们的插件会保留。

![RAW 通道条：两个应用通道 (MUSIC、GAME)、打击垫和磁带](../images/console/raw-strips.jpg)

![两个组合器，每个都显示其路径、输入它的声源及其插件](../images/console/combinators.jpg)

| 通道条 | 其中间部分的内容 |
|---|---|
| **App** | 插件列表(点击一个即可打开)和 **ADD PLUGIN** |
| **Pads** | 分组键 **A–D**、4×4 打击垫(按下播放；HOLD 打击垫按住期间播放)和 **STOP ALL** |
| **Tape** | 磁带机的屏幕和寻位条、走带控制(上一首、倒带、停止、播放、暂停、快进、下一首)、**SHUFFLE**、**ADD...** 和播放列表(点击曲目即可播放) |
| **Combinator** | 每条路径：**FROM**，即输入它的通道和总线(点击一个跳转到它的通道条)，以及该路径上的 **PLUGINS**(点击一个即可打开)。**EDIT IN ROUTING** 打开 ROUTING 页面 |

组合器本身没有效果。它的声音来自输入它的内容和每条路径的插件。右键点击它 > **PATHS** 可以添加路径，并且针对每条路径，打开、添加或移除其插件，重命名它或移除它。

---

<a id="8-outputs"></a>

## 8. 输出

![两个输出通道条：HEADPHONES 和 STREAM，带总线压缩器和混音 EQ](../images/console/outputs.jpg)

每个输出都有自己的 Dark strip：

| 控件 | |
|---|---|
| **DEVICE** | 它播放所在的设备。点击可选择另一个 |
| **LIMIT** | 推子之后的限幅器：任何声音都不会超过 -1 dBFS 输出 |
| **MON** | 监听此输出 |
| **LUFS** | 输出声音的短期响度(静音时为 "-- LUFS") |
| **WIDTH** | 立体声宽度 |
| **BUS COMP** | In 开关、VU 式增益衰减表、**threshold**、**ratio**、**attack**、**release**、**make-up**、**mix** |
| **MIX EQ** | In 开关、HF 增益和频率、LF 增益和频率 |
| **CUT**、**SEL**、推子 | 与通道上相同 |

右键点击输出可使用 **DEVICE...**、**HEADPHONE EQ**(耳机校正：**NONE**，或你的型号，按品牌列出)、**ALERT IF SILENT**(低于 -60 dBFS 持续 **10 S**、**30 S**、**1 MIN** 或 **5 MIN**，或 **OFF**)和 **ALERT IF CLIPPING**(LIMIT 关闭时)(它们会让托盘图标闪烁并写入事件日志)、**IN THE MINI MIXER**、**MIX-MINUS AND STEMS...**、间隔条、**COLOUR**(显示在它的名称栏上和它的 ROUTING 方块上)、**RENAME...** 和 **REMOVE**。

输出会自动创建，每个设备一个(见[输出就是设备](#outputs-are-devices))。

**MIX-MINUS AND STEMS...**(在输出的右键菜单上，或在 FIND 中)会为每位嘉宾建立一路馈送(除他们自己的声音外的所有声音)，并建立人声、音乐和游戏分轨。

---

<a id="9-plugins"></a>

## 9. 插件

Faider 在通道、总线和组合器路径上承载 **VST3** 插件，并自动补偿延迟。

<a id="adding-a-plugin"></a>

### 添加插件

- 在通道上：右键点击 > **PLUGINS > ADD PLUGIN...**，或 RAW 通道条上的 **ADD PLUGIN**，或插件面板中的 **ADD**。
- 在组合器路径上：右键点击组合器 > **PATHS > PATH n > ADD PLUGIN...**。在组合器通道条上点击某条路径的插件即可打开它。

每个插件**都会先在程序的一个独立副本中测试**，所以加载时崩溃或卡死的插件不会拖垮你的混音。它有 30 秒的加载时间(SETTINGS > Audio 中的 **Plugin test time** 可给慢的插件 60 或 120 秒)。结果会被记住，直到插件文件发生变化。

<a id="the-plugin-panel"></a>

### 插件面板

![所选通道条的插件面板](../images/console/plugins.jpg)

点击侧边栏中的 **PLUGINS**。一个面板会从通道条下方滑出，按运行顺序显示**所选通道条**的插件。**FIND** > **PLUGINS: name** 会在该通道条上打开它。每张卡片显示：

- **CPU**：插件占处理时间的份额(绿色，超过 2 % 为琥珀色，超过 5 % 为红色)
- **Latency**：它增加的延迟
- **BYPASSED**：已关闭但仍处于加载状态(仍占用内存)
- **HEAVY**：开销异常大

每张卡片上的按键：**BYPASS**、**OPEN**(它的窗口)、**REMOVE**，以及把它前移或后移的箭头。末尾的 **ADD** 再添加一个。

---

<a id="10-the-routing-page"></a>

## 10. ROUTING 页面

ROUTING 页面把整个混音显示为一张地图：每个通道条是一个方块，每条路由是一条线。用它来查看所有声音的去向，也可以通过点击两个方块来设置路由。

![带演示混音的 ROUTING 页面：输入、总线和输出，各自位于其通道条下方](../images/console/routing.jpg)

<a id="reading-the-page"></a>

### 如何阅读此页面

- **三行：** 顶部是 **INPUTS**，中间是 **BUSES**，底部是 **OUTPUTS**。还没有输出的输出设备以灰色显示在 OUTPUTS 行的右端。
- **每个方块位于它的通道条下方。** 方块在页面上的横向位置与它的通道条在调音台上的位置相同，包括间隔条，页面随调音台一起横向滚动(鼠标滚轮)。重新排列调音台，地图会跟着变化。
- **每个方块显示**通道条的名称和编号。已关闭的通道条显示为暗色。有自己颜色的通道会以色调和左侧的色带显示该颜色。
- **线就是路由。** 每条线从其声源笔直向下，进入其所在行下方空隙中属于它自己的车道，横向穿过，再向下进入它所馈送的方块，以箭头结束。输入路由为青色，总线路由为紫色。同一个方块上进出的多条线会按顺序铺开，所以永不重叠。
- **组合器**在方块底部以 **P1、P2、…** 单元格显示它的路径。线终止于它所馈送的路径。

<a id="routing-block-to-block"></a>

### 在方块之间设置路由

![已选中 HOST MIC：它的路由和它馈送的方块突出显示，其余变暗](../images/console/routing-selected.jpg)

1. **点击一个方块。** 它会亮起蓝色并跳动。它路由到的方块，或路由到它的方块，边框会高亮，它们的线会变亮，其余的一切变暗。
2. **点击另一个方块。** 它们之间的路由会打开(出现一条线)或关闭(线消失)。两种顺序都可以：先声源后目标，或先目标后声源。
3. 第一个方块**保持选中**，所以可以继续点击，把它路由到多个地方，或移除多条路由。
4. **再次点击它**、点击空白处，或按 **Esc** 即可放开。

如果这两个方块无法建立路由(比如两个输出)，则会改为选中第二个。

<a id="examples"></a>

### 示例

| 目的 | 点击 |
|---|---|
| 听到你的麦克风 | MIC，然后是耳机的输出 |
| 让应用听到你的麦克风 | MIC，然后是 **CABLE Input**(在输出行中，建立输出之前为灰色) |
| 通过总线发送一组通道 | 先点总线，再逐个点通道；然后点总线，再点它的输出 |
| 把一个通道从直播中拿掉 | 该通道，然后是直播的输出(线消失) |
| 输入一条组合器路径 | 声源，然后是路径单元格 (P1、P2…)。再点击其他单元格可输入多条路径。在单元格之外点击组合器会输入它的第一条路径 |

点击灰色的设备方块会创建它的输出，新的输出会立即出现在调音台上。

沿着所在行横向拖动方块，可移动它的通道条(见 [17.4](#174-reorder-channels))。立刻又被关闭的路由会形成反馈回路(见 [18.8](#188-a-route-wont-switch-on))。

<a id="11-tools-pads"></a>

## 11. 工具：打击垫

**TOOLS > PADS** 提供四组(**A–D**)，每组 16 个打击垫：共 64 个声音，最多同时播放 32 个。

![TOOLS > PADS：A 组已载入八个声音](../images/console/pads.jpg)

![所选打击垫的打击垫编辑器](../images/console/pad-editor.jpg)

<a id="getting-sound-out"></a>

### 让声音输出

打击垫播放到一个 **PADS 通道**。点击 **CREATE PADS CHANNEL**(之后它会显示 **PADS CHANNEL ACTIVE**)，或在 **PLAY INTO** 菜单中选择另一个输入，让打击垫通过该通道的插件和路由播放。没有打击垫通道时，按下会被忽略。

<a id="loading-and-playing"></a>

### 载入与播放

| 操作 | |
|---|---|
| 点击**空**打击垫 | 选择一个声音文件 |
| 点击已载入的打击垫 | 播放它 |
| 右键点击打击垫 | 选中它进行编辑，不播放 |
| 把文件拖放到打击垫上 | 载入到该打击垫以及该组中随后的打击垫 |
| **Ctrl + 拖动**到另一个打击垫 | 交换这两个打击垫 |
| **STOP ALL** | 淡出所有打击垫 |

格式：WAV、FLAC、AIFF、OGG、MP3。文件已被移动的打击垫显示 **MISSING: FILE**。

<a id="the-pad-editor"></a>

### 打击垫编辑器

| 设置 | |
|---|---|
| 名称 | 双击重命名 |
| **LOAD**、**CLEAR**、**COLOUR** | |
| **REPLAY** | 即时回放缓冲区变成该打击垫的声音(以 WAV 保存在录音文件夹中)。请先在 TOOLS > TAPE 中启用回放 |
| **MODE** | **ONE-SHOT** 播放到结束(再次按下会重新开始)。**HOLD** 按住期间播放。**LOOP** 按一次开始，再按一次停止 |
| **CHOKE** | OFF 或组 1–8。同一组的打击垫会互相停止 |
| **LEVEL** | -INF 到 +12 dB |
| **HOTKEY** | 点击(**LEARN...**)，然后按下按键(见 [HOTKEYS](#hotkeys)) |
| **MACRO** | 随打击垫一起运行的宏(打击垫可以只有宏而没有声音) |
| **TRIM** | 在波形上拖动起始线和结束线。双击选取整个文件 |
| **FADE IN / FADE OUT** | 0–5 秒 |

---

<a id="12-tools-tape-recorder-and-replay"></a>

## 12. 工具：磁带、录音机和回放

<a id="the-tape-deck"></a>

### 磁带机

![TOOLS > TAPE：磁带机、录音机和回放，播放列表在右侧](../images/console/tape.jpg)

**TOOLS > TAPE** 把播放列表播放到一个 **TAPE 通道**。点击 **CREATE TAPE CHANNEL** 来创建。

- **ADD FILES**、**ADD FOLDER**，或把文件和文件夹拖放到页面的任何位置。**EJECT** 清空播放列表。
- 双击曲目播放，拖动曲目重新排序，按 Delete 删除选中的曲目。
- 走带控制：**PREV**、**REW**(后退 10 秒；按住快速倒带)、**PLAY**、**PAUSE**、**STOP**、**FF**(前进 10 秒；按住快速快进)、**NEXT**。另有 **SHUFFLE**、**REPEAT**(全部 / 单曲 / 关闭)和 **LEVEL**。
- 曲目无缝播放。暂停和跳转会在 30 ms 内淡变，所以绝不会产生爆音。

顶部栏的走带控制和调音台上的磁带通道条控制的是同一台磁带机。

<a id="the-recorder"></a>

### 录音机

| | |
|---|---|
| **Source** | 任何输出、总线或输入，或 **ALL CHANNELS, ONE FILE EACH**(多轨：每个输入和每个输出，采样对齐) |
| **Format** | WAV 24-bit(默认)、16-bit、32-bit、32-bit float；FLAC 24/16-bit；AIFF 24/16-bit；OGG Vorbis 128–320k。有 ffmpeg 时：MP3、AAC (M4A)、Opus、ALAC。每种格式都会显示磁盘能容纳多长时间 |
| **REC** | 开始和停止(顶部栏的录音键作用相同)。屏幕显示时间、大小、剩余时间，以及任何丢弃的数据块 |
| **PATH** / **REVEAL** | 选择文件夹 / 在资源管理器中打开 |

除非你选择了文件夹，录音都保存到 `Music\Faider Recordings`。ffmpeg 格式会先录成 WAV，停止时再转换(转换失败时会保留 WAV)。Faider 使用在 **SETTINGS > Recording > FFmpeg** 中选择的 `ffmpeg.exe`，否则使用 PATH 中的那个。剩余空间不足 1 GB 时，录音会自动停止。

<a id="instant-replay"></a>

### 即时回放

**ARM** 保留录音机声源最近的 **30 秒**、**1**、**2** 或 **5 分钟**。**SAVE** 把它写入文件(右键点击 SAVE 可为它设置全局快捷键)。选择多轨时，回放使用第一个输出。

---

<a id="13-devices"></a>

## 13. 设备

**DEVICES** 列出每个 Windows 音频设备和 ASIO 驱动。由什么决定时钟，在 **SETTINGS > Audio > Clock** 中选择(见[时钟](#clocking))。

**Windows audio** 也在 **SETTINGS > Audio** 中，显示 Windows 音频引擎(audiodg.exe，承载每个 Windows 设备和被捕获的应用)的优先级：**Normal priority** 是 Windows 的默认值，无法读取时为 **Priority unknown**。它旁边有：

- **Boost** 把它提高到 High，在某些电脑上可以消除 Windows 设备和被捕获应用的爆音。这需要管理员权限，所以 Windows 会先询问；拒绝则什么都不会改变。重启 Windows 音频或电脑后它不会保留：Faider 会监视它，在它回到 Normal 时从托盘通知你一次，托盘菜单上的 **Boost Windows audio again...**(或这里的 **Boost**，或 FIND 中的 **BOOST WINDOWS AUDIO**)会再次提升它。已提升时，这个键显示为 **Stop boosting**，点击它会停止监视；它会保持 High，直到 Windows 音频重启或你选择 **Back to normal**。
- **Pin to one core**(它下面的开关，除非你选择，否则关闭)从下一次 Boost 起，还会把它固定在你的最后一个处理器核心上。它只限制运行位置，从不增加核心：仅在 USB 耳机有爆音时使用。
- **Back to normal**(当它不是 Normal，或已被固定时)立即把它恢复为所有核心上的 Normal 优先级，无需等待重启。和 Boost 一样，Windows 会先询问，Faider 也会停止监视。

**Boost** 和 **Back to normal** 需要为所有用户安装的 Faider(位于 Program Files)。仅为你自己安装的副本，或从源代码构建的副本，会在该行下方显示 **Needs Faider installed for all users**，并且不会询问 Windows：任何程序都能更改这类文件夹里的文件，而这些文件随后会以管理员身份运行。

它不影响 ASIO 接口，因为它们不经过 Windows 音频。

| 列 | |
|---|---|
| **I/O** | ASIO、OUT 或 IN |
| **DEVICE**、**HARDWARE** | 它的名称和它所在的适配器 |
| **FORMAT** | 声道数和采样率 |
| **STATE** | 活动、已拔出、已禁用，或 **off in Faider** |
| **TAGS** | Windows 默认设备的 DEFAULT 和 CALLS |

点击表头排序。选择设备(Ctrl/Shift 多选)，然后使用：

| 键 | |
|---|---|
| **EXCLUDE / INCLUDE** | Faider 停止(或恢复)使用该设备。被排除的设备永远不会被打开，也不会出现在列表中 |
| **DISABLE / ENABLE IN WINDOWS** | 在 Windows 本身中关闭或打开该设备 |
| **RENAME** | 你给设备起的名字，在整个 Faider 中使用(也可以双击一行) |

<a id="streams"></a>

### Streams

列表下方的 **STREAMS** 显示音频引擎的状况：

- **CALLBACK LOAD**(及其峰值)、**OVERLOADS**、**BLOCKS SKIPPED**。任何一项发生过，就会变成警告色
- **BUFFER**：你接口的缓冲区大小(越小延迟越低，负载越高)
- 每个设备：缓冲的音频、总延迟、**DRIFT**(以 ppm 表示的时钟修正)、**UNDER**(数据耗尽，播放了静音)、**OVER**(溢出，音频被丢弃)、**RESYNC**(重新居中)。丢过音频的行显示为红色
- **RESET STATS** 清除计数器

<a id="vb-cable"></a>

### VB-CABLE

如果安装了 VB-CABLE，顶部的一栏会设置它的**采样率**和**最大延迟**。**APPLY** 会请求一次管理员权限并重启线缆。

底部的事件日志记录每一次设备接入、移除或恢复。

---

<a id="14-scenes-undo-and-sessions"></a>

## 14. 场景、撤销和会话

<a id="141-scenes"></a>

### 14.1 场景

场景保存每个通道的推子、声像、静音、独奏、电源和路由(如果你选择，还包括插件设置)。

- **保存：** 点击顶部栏的 **+** 并给它起个名字。
- **调用：** 点击它的键。推子和声像在半秒内平滑过渡，其余的立即切换。
- **右键点击场景**可以：仅调用 CUT / 仅调用电平、存入它、包含插件设置、重命名、全局快捷键、**Recall on device**(设备出现时调用；离开时切回)、**Auto-scene**(应用开始播放时、设备连接时，或在一天中的某个时间)、MIDI 学习、移动、删除。

设备和通道本身不属于场景。

<a id="142-undo-and-history"></a>

### 14.2 撤销和历史

**Ctrl+Z** 撤销，**Ctrl+Y**(或 Ctrl+Shift+Z)重做。**HISTORY** (Ctrl+H) 列出每一次更改。选一个就能跳回到那里。

<a id="143-sessions"></a>

### 14.3 会话

你的混音在任何改动后一秒内以及退出时自动保存。

| | |
|---|---|
| **SESSION TIMELINE** | SETTINGS > Sessions 中的 **Open timeline**、FIND，或托盘菜单上的 **Session timeline...**：按日期列出保留的每个版本。**RESTORE EVERYTHING**(Faider 会以它重新启动)，或无需重启的 **PUT ONE CHANNEL BACK**(它的电平、声像、CUT 和机架) |
| **Save as…** | SETTINGS > Sessions > This mix(或 FIND 中的 **SAVE SESSION AS...**)：以名称保留混音。旁边的 **Export…** 把混音保存为文件 |
| **Saved sessions** | SETTINGS > Sessions：**Open** 一个命名的混音(Faider 会以它重新启动)，或把它 **Export** 为文件；**Import…** 从文件载入一个 |

最多保留 60 个早期版本(最多每 5 分钟一个)。如果你的混音无法载入(接口缺失、文件损坏)，它会被**搁置**而不是丢失，并以一个全新的混音开始。见 [18.2](#182-my-mix-is-gone-a-fresh-mix-started)。

---

<a id="15-settings"></a>

## 15. 设置

**SETTINGS** 的左侧有一个侧边栏：顶部是搜索框，下面是分成四组的页面。**APP**：**General**、**Notifications**、**Startup & checks**、**Updates & about**。**AUDIO**：**Audio**、**Recording**。**MIX**：**Apps**、**Sessions**。**CONTROL**：**MIDI & OSC**、**Hotkeys**、**Macros**。你选中的页面以一列填满其余部分：它的标题、一行说明它的用途，然后每个设置一行，包括名称、名称下方说明它做什么，以及右侧的控件。开/关设置是开关，开启时发出奶油色的光；只有几个选项时会并排显示，选中的那个为奶油色；带菜单的值在点击时打开菜单。

在搜索框中输入，即可看到所有页面中匹配的设置，每个都列在其页面名称下(Esc 清除)。**FIND** (Ctrl+K) 也能按名称找到每个设置，并打开它的页面，让它那一行短暂亮起。名称或关键词匹配的页面(如“hotkey”“shortcut”“macro”)会带着 **Open** 列在最前面。

<a id="general"></a>

### GENERAL

**APP** 页面：

| 页面 | 设置 | 作用 |
|---|---|---|
| **General** | **Language** | 界面的语言：English, Deutsch, Español, Français, Italiano, 日本語, 한국어, Nederlands, Polski, Português (Brasil), Svenska, Türkçe, 简体中文，各以其本身的名称显示。首次启动时，如果 Windows 的显示语言是其中之一就跟随它，否则为英语。更改从下次启动起生效：**RESTART NOW** 立即重启 Faider，**LATER** 留到那时。调音台的按键、事件日志和产品名称保持英文 |
| | **Start with Windows** | 你登录时在托盘中打开 Faider |
| | **Zoom** | **100%**、**125%**、**150%**，或用 **Custom…** 设为 75 到 200% 之间的任意大小。Ctrl + 和 Ctrl - 在前三个之间切换 |
| | **Look** | 调音台的颜色与字体；它的菜单列出你的 LOOK 和 **Save as…**（见 [LOOKS](#looks)） |
| | **Tips** | 每个页面第一次显示时的一条提示。**Show again** 让每个页面的提示再显示一次 |
| **Notifications** | **Warn if I talk while cut** | 被 CUT 的麦克风超过 -30 dBFS 时给出通知 |
| | 角落通知 | 每项一个开关：设备连接或消失、通道削波(其声源或输出达到满刻度)、场景被调用、宏被运行、撤销和重做。无论开关如何，事件日志都会记录全部内容 |
| **Startup & checks** | **Pre-flight on start** | 启动几秒后运行 PRE-FLIGHT，只在有问题时才显示 |
| | **Pre-flight**、**Zero the desk** | **Run pre-flight** 和 **Check** 立即运行它们(见 [18.1](#181-pre-flight)) |
| | **Setup: build a mix** | **Run setup** 再次提出 SETUP 的四个问题(见 [2](#2-first-time-setup))：只补上缺少的内容，不会删除任何东西 |
| **Updates & about** | **Check for updates** | 默认开启：每天最多一次(启动半分钟后，或在持续运行期间满一天时)，Faider 向 GitHub 发送一个请求，询问它的最新版本。该请求只带有应用的名称和版本(例如 `Faider/0.1.5`)；不会下载或安装任何东西。如果有新版本，托盘会告诉你，托盘菜单上的 **Get Faider x.y.z...**、FIND 中的 **GET FAIDER x.y.z** 或这里的 **Get Faider x.y.z…** 会打开它的页面。关闭后，Faider 从不询问。 |
| | **Version** | 当前运行的版本。**Check now**(或 FIND 中的 **CHECK FOR UPDATES NOW**)会立即询问，并在你已是最新版本时告诉你 |
| | **About Faider** | 版本和致谢 |
| | **Privacy** | **Read policy** 打开 GitHub 上 Faider 的隐私政策 |
| | **Problem report** | **Save report…** 把日志、最近一次崩溃和各个版本保存为一个你可以发送的 .zip。不会自动发送任何东西 |

**AUDIO** 和 **MIX** 页面：

| 页面 | 设置 | 作用 |
|---|---|---|
| **Audio** | **Clock** | 从下次启动起由什么决定时钟：**The interface (ASIO)**、按名称选择的 Windows 播放设备，或 **The built-in clock**(见[时钟](#clocking))。它下方显示现在由什么决定时钟 |
| | **Windows audio** | Windows 音频引擎的优先级，以及 **Boost** 和 **Back to normal**(见 [13](#13-devices)) |
| | **Pin to one core** | Boost 时，把 Windows 音频固定在最后一个核心上：仅用于 USB 耳机的爆音 |
| | **Channel meter point** | 每个通道电平表的测量位置：**IN**(原始声源)、**PRE**(效果之后)或 **POST**(推子之后，默认) |
| | **Meter standard** | 电平表的读数方式，以及调音台电平表的 LED、颜色和刻度：**Peak**(默认：以 dBFS 表示的采样峰值，快速回落)、**Peak IEC**、**True Peak**(dBTP，ITU-R BS.1770：4 倍过采样，所以也能显示采样点之间的峰值；其刻度到 +3)、**RMS**、**VU**、**PPM**(EBU、BBC、DIN、Nordic)、**K-20**、**K-14**、**K-12** 和 **LUFS M**(瞬时响度，EBU R128) |
| | **Plugin test time** | **30 S**(默认)、**60 S** 或 **120 S**：新插件在测试中完成加载的时限，超时即失败 |
| **Recording** | **FFmpeg** | `ffmpeg.exe` 的位置，或 **Not found**。**Pick…** 选择它(优先于 PATH 中的那个)；**Use PATH** 忘掉选好的那个，改用 PATH 中的 |
| **Apps** | **App rules** | 一张表：每个程序、它去往哪里、**Change** 和 **Delete**。**Add rule** 先选一个正在播放的程序(或有通道的程序)，再选总线或输出。删除程序的通道会同时删除它的规则。FIND 中的 **APP RULES** 会带你来到这张表 |
| **Sessions** | **This mix** | **Save as…** 以名称保留混音；**Export…** 把它保存为文件 |
| | **Saved sessions** | 以名称保留的混音，以及各自的保存时间：**Open**(Faider 会以它重新启动)或 **Export** 为文件。**Import…** 从文件打开一个会话 |
| | **Session timeline** | **Open timeline**：按日期列出保留的每个版本(见 [14.3](#143-sessions)) |

<a id="macros"></a>

### LOOKS

**LOOK** 就是调音台绘制时使用的每一种颜色，再加上与之相配的字体和尺寸。通道条、数值显示、旋钮、按键和指示灯都从当前生效的 LOOK 取值绘制，所以换一个 LOOK，整张调音台会一起变。

**Look**(SETTINGS > General)列出你已有的 LOOK，并启用你选中的那个；一有改变，调音台就会立刻重新绘制。列表末尾的 **Save as…** 会把当前生效的 LOOK 写成一个新文件，名字由你定。

一个 LOOK 就是 `%APPDATA%\Faider\looks` 里的一个 JSON 文件，因此你可以手写一个，再复制到别的电脑上。文件缺失或损坏时，内置 LOOK 保持不变；文件里没写的项目会退回内置值——只写了一半的文件照样能用。

| | |
|---|---|
| `name` | LOOK 的名字，也就是列表里显示的样子 |
| `font` | Windows 自带的一种字体，在调音台的小字号下依然清楚 |
| `keyRadius` | 按键圆角有多圆，以像素计 |
| `fontSizes` | `legend`、`caption`、`readout`：三种印刷尺寸 |
| `colours` | 每个颜色写成 `#rrggbb`，需要透明时写成 `#rrggbbaa` |

颜色的名字就是它绘制的那部分：面板与印刷、按键、推子的面板、数值显示、按功能分的旋钮、指示灯、通道条的体、通道／总线／输出的样子、强调色、凹槽和明暗。**Save as…** 会把 LOOK 拥有的每个名字都写出来，所以最快的办法是在调音台上改一下、保存它，再编辑那个文件。

`palette` 这一组列出布局文件可以加在某个部件上的颜色：`red`、`green`、`blue`、`black`、`white`、`violet`、`grey`、`cream`、`orange`、`amber`、`teal`、`salmon`。

### MACROS

宏是由快捷键、打击垫、触发器或远程控制运行的**动作**列表。

1. 点击 **+ NEW**，然后命名(双击名称)。
2. **+ ACTION**：CUT、UNCUT、CUT TOGGLE、SET FADER (dB、渐变时间)、ASSIGN ON/OFF/TOGGLE (一条路由)、CHANNEL ON/OFF、TRIGGER PAD、STOP PAD、ALL NOTES OFF、TRANSPORT PLAY/PAUSE、TRANSPORT NEXT、RECORD START/STOP、WAIT、SCENE RECALL、FX ON/OFF/TOGGLE (一个语音特效，见 [语音特效](#voice-effects))、WINDOWS DEFAULT OUTPUT/INPUT、PLUGIN SETTING。
3. 可选，**+ TRIGGER**：APP STARTS、APP STOPS、DEVICE CONNECTED、DEVICE DISCONNECTED、OUTPUT SILENT (持续 n 秒)、OUTPUT HAS SOUND、TIME (24 小时制)。
4. 可选，一个 **HOTKEY** 和 **MOMENTARY**(松开按键时撤销这些动作，用于按键通话)。
5. **FIRE** 立即运行。

<a id="hotkeys"></a>

### HOTKEYS

所有可分配的快捷键都在 **Hotkeys** 页面上，分为两张表。**Assigned** 列出打击垫、宏、场景和 **Save instant replay**(磁带)，每项都注明还能在哪里设置，以及它的按键。点击一个按键，然后按下新的按键(Esc 取消，Delete 清除)。快捷键必须包含 Ctrl、Alt 或 F 键，并且在游戏拥有焦点时也有效。Faider 中其他项目(打击垫、宏、场景、即时回放或固定按键)已经占用的按键会被拒绝，在这里以及其他设置快捷键的地方都是如此，顶部的提示会说明是谁占用了它。被 Windows 拒绝的按键(可能已被其他应用占用)，或在旧版本的混音中仍由两项功能共用的按键，会在其所在行下方标为红色：其中只有一项有效。**Fixed** 列出始终不变的按键，以及它们在哪里有效([19.1](#191-keyboard-shortcuts))。其中的 **On the mixer** 按键在调音台上同样有效([5](#5-the-console))。

<a id="control"></a>

### CONTROL

**MIDI & OSC** 页面：

| 设置 | |
|---|---|
| **MIDI map** | **Start mapping** 启动 MIDI MAP(见 [16](#16-remote-control-overlay-and-tray))。Ctrl+L 在任何地方都能启动它；Esc 或 **Finish mapping** 结束它 |
| **Inputs and bindings** | **Open**：开启或关闭每个 MIDI 输入，以及所有绑定。它下方显示有多少个绑定 |
| **OSC** | 一个开关：监听这台电脑上的程序发来的 OSC。它下方显示它监听的端口 |
| **OSC port** | 端口(1024–65535，初始为 9000)：输入后点击 **Apply**。如果 OSC 已开启，会立即改到该端口 |

---

<a id="16-remote-control-overlay-and-tray"></a>

## 16. 远程控制、悬浮层和托盘

| | |
|---|---|
| **MIDI MAP** (Ctrl+L) | Ctrl+L 或 SETTINGS > MIDI & OSC 中的 **Start mapping** 启动它；Esc 结束它。点击屏幕上的一个控件，然后转动控制器上的旋钮或按键。每个通道条的推子、声像、CUT、SOLO、电源和路由键都可以映射，场景、打击垫和磁带也可以。映射时鼠标滚轮滚动调音台(Shift+滚轮为上下) |
| **OSC** | 这台电脑上的程序可以发送到端口 9000，或在 **OSC port**(SETTINGS > MIDI & OSC)中设置的端口(它从不监听网络)：`/faider/input/NAME/fader -12` (dB)、`/faider/output/NAME/mute 1`、`/faider/bus/NAME/pan -0.5`、`/faider/input/NAME/solo 1`、`/faider/scene NAME`、`/faider/macro/NAME 1` (0 表示松开)。名称中的空格写成 `_`。所有地址：[OSC 参考](https://github.com/dayztoolsforu/faider/blob/main/docs/OSC.md)（英文） |
| **In-game overlay** (Ctrl+Shift+O) | 每个麦克风通道的说话指示灯、LIVE / CUT 和电平，另外最多四个场景键。绝不会从游戏抢走焦点。可拖到任何位置 |
| **MINI** | 迷你调音台：一个小窗口，为右键菜单中勾选了 **IN THE MINI MIXER** 的每个通道条提供推子、电平表和 **CUT**(都没勾选时为每个输出提供)。**PIN** 让它保持在所有窗口之上。**OPEN** 返回主窗口 |
| **Tray** | 点击图标显示迷你调音台(稍等片刻、确认没有第二次点击后才打开)，双击只打开窗口。右键点击：Show、场景调用、Session timeline、Get Faider x.y.z...(有新版本时)、Start with Windows、Save a problem report...、About、Quit。出现警报时图标闪烁琥珀色 |

Faider 始终以你的普通用户权限运行，即使是从管理员窗口启动的。它会自己以普通权限重新启动，因为游戏内悬浮层无法访问以管理员身份运行的程序。少数需要管理员权限的操作(应用 VB-CABLE 设置、重新启用被 Windows 关闭的设备)会自行请求许可。

---

<a id="17-how-to-guides"></a>

## 17. 操作指南

<a id="171-hear-yourself-set-up-a-microphone"></a>

### 17.1 听到自己：设置麦克风

1. **ADD CHANNEL** > 你的接口输入(或 Windows 录制设备)。它会作为一个新的通道条出现。
2. 说话，并设置 **IN TRIM**，使电平表的峰值在 -12 到 -6 dB 左右。
3. 设置路由：点击你耳机输出的路由框(或使用 ROUTING 页面)。
4. 清理声音：用 **hp** 去除低频噪声(80–100 Hz 左右)，然后是门限和压缩器(DYNAMICS 上的 **IN**)，再是 EQ。

<a id="172-let-apps-hear-your-mic-discord-games-obs"></a>

### 17.2 让应用听到你的麦克风(Discord、游戏、OBS)

1. 安装 **VB-CABLE**(PRE-FLIGHT 会提供链接)，然后把你真正的扬声器和麦克风设回 Windows 的默认设备([步骤 2](#step-2-install-vb-cable-optional))。
2. 把你的麦克风通道路由到 **CABLE Input** 设备。它上面会创建一个输出。
3. 在应用中，选择 **CABLE Output** 作为它的麦克风。
4. 运行 **Run pre-flight**，检查途中没有被 CUT 的环节，也没有回路。

<a id="173-bring-a-program-into-the-mix"></a>

### 17.3 把程序带入混音

1. 启动程序。
2. **ADD APP** > 选中它(如果它现在没有发声，就在 **NOT PLAYING YET** 下)。它会得到一个 RAW 通道(没有效果)。
3. 把它路由到你想听到它的地方。如果你听到它两遍，见 [18.7](#187-i-hear-something-twice-or-an-echo)。

<a id="174-reorder-channels"></a>

### 17.4 重新排列通道

在 Mixer 上左右拖动通道的**名称栏**(向下滚动到 NAME 部分)，或在 ROUTING 页面上拖动它的方块。一条蓝色竖条显示它将落在哪里，通道条会平滑滑入位置。间隔条跟随它前面的通道条移动。总线和输出以同样的方式移动，各自在同类之间，它们的路由也随之移动。

<a id="175-group-channels-into-a-bus"></a>

### 17.5 把通道编成一条总线

1. **ADD BUS**，然后重命名(右键点击 > **RENAME...**)。
2. 在 ROUTING 页面上，点击总线，再点击每个通道，把它路由进来。
3. 点击总线，再点击它的输出，把总线继续路由出去。
4. 如果你只想让这些通道经过总线，请关闭它们到该输出的直接路由。

<a id="176-parallel-processing-with-a-combinator"></a>

### 17.6 用组合器进行并行处理

1. **ADD BUS > COMBINATOR**。它起始有两条路径。右键点击它 > **PATHS > ADD PATH** 可添加更多。
2. 在 ROUTING 页面上，把声源路由到路径单元格(**P1**、**P2**…)。
3. 对每条路径，右键点击 > **PATHS > PATH n > ADD PLUGIN...**。

<a id="177-make-a-stream-mix-and-a-headphone-mix"></a>

### 17.7 制作直播混音和耳机混音

1. 把你的通道送到两个输出，例如你的耳机和直播软件捕获的设备。
2. 每个输出有自己的推子、总线压缩器、EQ 和限幅器(**LIMIT**)。给直播输出打开 **LIMIT**。
3. 若要让某样东西不进入直播，请关闭该通道到直播输出的路由(用它的路由框或在 ROUTING 页面上)。

<a id="178-record"></a>

### 17.8 录音

1. TOOLS > TAPE > **RECORDER**：选择声源和格式。
2. 按 **REC**(或顶部栏的录音键)。再按一次停止。
3. **REVEAL** 打开文件夹。

<a id="179-make-a-push-to-talk-key"></a>

### 17.9 制作按键通话键

1. SETTINGS > MACROS > **+ NEW**。
2. **+ ACTION** > **UNCUT** > 你的麦克风通道。
3. 打开 **MOMENTARY** 并设置一个 **HOTKEY**。按住它时麦克风是开着的，松开后又会被 CUT。请从麦克风被 CUT 的状态开始。

---

<a id="18-troubleshooting"></a>

## 18. 故障排除

当悬浮层、MIDI 控制器或 OSC 没有反应时：

| 问题 | 检查 |
|---|---|
| **悬浮层不显示** | Ctrl+Shift+O 是整个 Windows 范围的快捷键。如果它被其他应用占用，SETTINGS > HOTKEYS 会把它标为红色，事件日志也会说明：FIND 中的 **SHOW OVERLAY** 无需按键即可打开悬浮层。悬浮层是一个保持置顶的窗口，为无边框窗口中的游戏而设计，所以请把游戏设为无边框或窗口模式。它只列出麦克风通道(接口输入和 Windows 设备)，一个都没有时显示 **NO MIC CHANNELS** |
| **MIDI 控制器没有反应** | 每个 MIDI 输入在你于 SETTINGS > MIDI & OSC > **Inputs and bindings** 中打开之前都是关闭的。Faider 启动后才插入的控制器不会自动打开：在那里按 **REFRESH**，把它的输入关闭再打开，或重新启动 Faider。动一下某个控件，该列表底部的 **LAST** 会显示收到了什么。控件在 **MIDI MAP** (Ctrl+L) 中映射之前不起任何作用。只读取控制变更和音符：推子和声像跟随旋钮或推子，而不是按键；按键用于切换 CUT、SOLO 等 |
| **OSC 没有反应** | 在它的开关打开之前，OSC 是关闭的(SETTINGS > MIDI & OSC)。发送方必须使用开关下方显示的端口(除非你改过，否则是 9000)，并在这台电脑上运行：Faider 只监听 `127.0.0.1`，所以手机或另一台电脑无法连到它。如果端口无法打开(可能被其他程序占用)，Faider 会提示，OSC 保持关闭：在 **OSC port** 中输入另一个并点击 **Apply**。然后检查地址：通道条的名称，空格写成 `_`(见 [OSC 参考](https://github.com/dayztoolsforu/faider/blob/main/docs/OSC.md)（英文）) |

<a id="181-pre-flight"></a>

### 18.1 PRE-FLIGHT

PRE-FLIGHT 在启动几秒后运行(也可从 SETTINGS > Startup & checks 中的 **Run pre-flight** 或 FIND 运行)，只在出问题时才会出现。每一行有一个指示灯、检查结果和一个修复方法：

| 检查 | 问题 | 修复 |
|---|---|---|
| **AUDIO** | 没有启动(可能被其他应用占用) | **DEVICES** |
| **MIC CABLE** | 没有 VB-CABLE：应用无法把你的混音当作麦克风听到 | **GET VB-CABLE** |
| **MIC OUTPUT** | 没有输出播放到麦克风线缆 | **ROUTING**(正常时，它的键是 **HEAR WHAT THEY HEAR**) |
| **MIC CHANNELS** | 通往麦克风的途中有东西被 CUT | **UNCUT** |
| **WINDOWS DEFAULTS** | Windows 的默认扬声器或麦克风(或其默认通信设备)是虚拟线缆 | **SET MY DEFAULTS** |
| **HEARD ONCE** | 反馈回路，或有东西被听到两遍 | **SOUND SETTINGS** |
| **WINDOWS AUDIO** | 混音中有 Windows 设备或被捕获的应用，而 Windows 音频引擎处于 Normal 优先级：黄色，仅为建议；如果你的提升被重启撤销则为红色。只用 ASIO 时为绿色 | **BOOST** |

**ZERO THE DESK**(左下角；也是 SETTINGS > Startup & checks 中的 **Zero the desk**，在 FIND 中叫 **ZERO DESK**)检查上一次会话可能遗留的设置：独奏、key listen、偏离超过 12 dB 的增益微调，以及被 CUT 的输出。每一项都有一个 **FIX**。

<a id="182-my-mix-is-gone-a-fresh-mix-started"></a>

### 18.2 我的混音不见了，开始了一个全新的混音

你的混音没有丢失。启动时无法使用它时，它会被**搁置**为 `session-unloaded-<date>.xml`。事件日志会说明原因：

| 原因 | 该怎么做 |
|---|---|
| 保存的接口没有连接 | 连接它，然后 **SESSION TIMELINE**(SETTINGS > Sessions、FIND 或托盘) > 带 "kept:" 的版本 > **RESTORE EVERYTHING** |
| 保存的接口无法打开(被其他程序占用) | 关闭其他程序，然后重新启动 Faider。在此期间不会有任何东西覆盖你的混音 |
| 无法读取 session.xml | **SESSION TIMELINE** > 一个较新的版本 > **RESTORE EVERYTHING** |

<a id="183-no-sound"></a>

### 18.3 没有声音

沿着信号路径逐一检查：

1. **状态灯**为红色并显示 OFF：音频没有运行。见 [18.2](#182-my-mix-is-gone-a-fresh-mix-started) 和 [18.4](#184-the-interface-wont-start-or-stopped)。
2. **通道的电平表**(Channel meter point: IN)：如果它不动，检查它的 **Input**(右键点击)以及声源是否在播放。已关闭的通道条不会通过任何声音。
3. **CUT** 已关闭，推子已推上去，并且没有其他东西被独奏(另一个通道条的 **SOLO** 会让其他所有东西静音)。
4. **路由：** 至少有一个路由框亮着，或者 ROUTING 页面上有一条线。
5. **输出**：没有 CUT，推子已推上去，**DEVICE** 显示了正确的设备(不是 "NO DEVICE" 或 "(OFF)")。
6. **设备**已接入，并且在 DEVICES 中没有被排除。

<a id="184-the-interface-wont-start-or-stopped"></a>

### 18.4 接口无法启动，或已停止

- 可能有其他程序占用了它(DAW、驱动的控制面板)。关闭它并重新启动。
- 托盘上的 **INTERFACE DOWN**：接口自己重置了，并且无法重新打开。在 DEVICES 上更改它的缓冲区大小，或重新启动 Faider。
- 在接口自己的控制面板中所做的采样率或缓冲区更改会被自动识别。
- 若要暂时不用接口运行，请在 **SETTINGS > Audio > Clock** 中选择一个 Windows 播放设备或内置时钟，然后点击 **RESTART NOW**。你的混音会随之带过去，并仍为你的接口保存：再次选择 **The interface (ASIO)** 即可回到接口。
- 或这样启动 Faider：`"C:\Program Files\Faider\Faider.exe" --clock windows`(你的 Windows 播放设备)或 `--clock builtin`(内置时钟)。`--clock` 后面跟 ASIO 驱动名称的一部分(`--clock focusrite`)会选择那个驱动。在这次运行中它优先于 SETTINGS > Audio > Clock，下次不带它启动时会回到你的选择。

<a id="185-clicks-crackles-or-dropouts"></a>

### 18.5 咔嗒声、爆音或掉音

1. 查看**状态灯**和 **DEVICES > STREAMS**。
2. **CALLBACK LOAD** 很高或有 OVERLOADS：调大 **BUFFER**，或在插件面板中找出开销大的插件(**HEAVY**、CPU 高)，将它们旁路或移除。
3. 某一个设备上的 **UNDER / OVER** 持续增长：那个设备吃力了。试试另一个 USB 端口，关闭其他使用它的程序，或者如果你不用它就把它排除。
4. 稳定的 **DRIFT** 是正常的。这是让各时钟保持同步的修正。
5. 只有 Windows 设备或被捕获的应用有爆音：试试 **Boost**(SETTINGS > Audio > Windows audio，或 FIND 中的 **BOOST WINDOWS AUDIO**)。它把 Windows 音频引擎从 Windows 默认的 Normal 提高到 High。Windows 会请求管理员批准，重启 Windows 音频或电脑会撤销它(Faider 会告诉你)。它不影响 ASIO 接口。对于仍有爆音的 USB 耳机，可以选用 **Pin to one core**，它只是把它限制在一个核心上。

<a id="186-an-app-doesnt-appear-in-add-app"></a>

### 18.6 应用没有出现在 ADD APP 中

- 程序必须正在**发声**，或带着自己的窗口在运行(这时它在 **NOT PLAYING YET** 下)。没有窗口的程序，或窗口由 Windows 代为托管的 Microsoft Store 应用，要到发声时才会列出：开始播放，然后再次打开菜单。
- 已经有通道的程序会显示为灰色(**ON THE DESK**)。
- 低于 Windows 10 2004 版的 Windows 无法捕获单个程序("Windows refused app capture")。

<a id="187-i-hear-something-twice-or-an-echo"></a>

### 18.7 我听到两遍声音，或有回声

- 程序除了送入它的通道，也会继续在自己的设备上播放。如果它的通道被路由到同一个设备的输出上，你就会既直接听到它，又通过 Faider 听到它。关闭那条路由(通道上它的路由框)，或把程序移到另一个设备。PRE-FLIGHT 会就此警告。
- Windows 的默认扬声器被设成了虚拟线缆。PRE-FLIGHT > **WINDOWS DEFAULTS** 会指出它，**SET MY DEFAULTS** 会把你的扬声器改回来。

<a id="188-a-route-wont-switch-on"></a>

### 18.8 路由无法打开

它会形成**反馈回路**(见[保证安全的规则](#rules-that-keep-it-safe))。事件日志会指出这个回路。移除造成反馈的那条路由。

<a id="189-i-cant-change-anything-on-a-strip"></a>

### 18.9 我无法更改通道条上的任何东西

该通道条已**关闭**(变暗并变灰)。点击它名称栏上的电源符号。

<a id="1810-dragging-a-channel-does-nothing"></a>

### 18.10 拖动通道没有反应

请拖动**名称栏**(NAME 部分，靠近底部；需要时向下滚动)，或在 ROUTING 页面上拖动方块。通道条只能在同类之间移动：总线不能移到通道之间。

<a id="1811-a-plugin-wont-load"></a>

### 18.11 插件无法加载

每个插件都会先在程序的一个独立副本中测试，并报告失败原因：它没有在时限内完成加载(30 秒，除非 SETTINGS > Audio 中的 **Plugin test time** 给它 60 或 120 秒)、测试时崩溃了，或文件中没有 VST3 插件。请更新或重新安装该插件。其文件一旦变化，就会再次测试。

<a id="1812-the-program-crashed"></a>

### 18.12 程序崩溃了

报告会写入 `%APPDATA%\Faider\crash.log`，并附带小型转储文件 `crash.dmp`。你的混音在任何改动后一秒内就会保存，所以你最多丢失最后一刻的更改。

要报告问题，请右键点击托盘图标 > **Save a problem report...**(或 FIND 中的 **SAVE A PROBLEM REPORT**)。它会把崩溃报告、事件日志和状态快照(存在的那些)，连同 Faider 和 Windows 的版本，放进一个 .zip，保存到你选择的位置(起初是桌面)，然后在资源管理器中显示它。不会向任何地方发送任何东西：请自己附上该文件。

<a id="1813-recording-stopped-by-itself"></a>

### 18.13 录音自己停止了

磁盘剩余空间不足 1 GB 时(或接近该格式的大小限制时)，录音会停止。托盘显示 **RECORDING STOPPED, NO ROOM**。请腾出一些空间，或选择另一个 **PATH**。

<a id="1814-no-sound-from-windows-after-installing-vb-cable"></a>

### 18.14 安装 VB-CABLE 后 Windows 没有声音

Windows 把线缆设成了它的默认设备。**SET MY DEFAULTS**(PRE-FLIGHT 的 **WINDOWS DEFAULTS** 行，或 FIND 中的 **WINDOWS DEFAULTS**)会重新把耳机或音箱设为默认输出，把你真正的麦克风设为默认输入，并且各自也设为默认通信设备。也可以在**设置 > 系统 > 声音**中手动设置。见[步骤 2](#step-2-install-vb-cable-optional)。

---

<a id="19-reference"></a>

## 19. 参考

<a id="191-keyboard-shortcuts"></a>

### 19.1 键盘快捷键

| 按键 | |
|---|---|
| **Ctrl+K** | FIND |
| **Ctrl+Z / Ctrl+Y** (Ctrl+Shift+Z) | 撤销 / 重做 |
| **Ctrl+H** | 历史 |
| **Ctrl+L** | MIDI MAP |
| **Ctrl + / Ctrl - / Ctrl 0** | 放大 / 缩小 / 100% |
| **Ctrl+Shift+O** | 显示 / 隐藏游戏内悬浮层(任何地方都有效) |
| **Left / Right** | 选择上一个 / 下一个通道条(调音台获得焦点时) |
| **M / S** | 将所选通道条静音 / 独奏 |
| **Up / Down** | 所选推子 ±0.5 dB (按住 Shift 为 ±3 dB) |
| **Esc** | 放开所选方块 (ROUTING) |

<a id="192-command-line"></a>

### 19.2 命令行

| | |
|---|---|
| `--minimised` | 在托盘中启动(**Start with Windows** 用的就是它) |
| `--quit` | 退出正在运行的副本，与托盘的 **Quit** 相同。没有副本在运行时，什么都不会发生 |
| `--allow-admin` | 以管理员身份启动时，保持管理员身份。不加它，Faider 会以普通权限重新启动自己(见 [16](#16-remote-control-overlay-and-tray)) |
| `--clock windows`、`--clock builtin`、`--clock <driver>` | 在这次运行中，时钟是你的 Windows 播放设备、内置时钟，或名称中包含 `<driver>` 的 ASIO 驱动。它优先于 SETTINGS > Audio > Clock(见[时钟](#clocking)) |
| `--boost-audiodg [--pin-core N]`、`--unboost-audiodg` | **Boost** 和 **Back to normal**(SETTINGS > Audio > Windows audio)以管理员身份启动的命令：把 Windows 音频引擎设为 High 优先级(仅在给出 N 时固定到核心 N)，或恢复为所有核心上的 Normal，然后退出。0：完成；2：参数错误；3：找不到 audiodg；4：访问被拒绝；5：优先级被拒绝；6：已设为 High 但固定被拒绝；7：已设为 Normal 但恢复所有核心被拒绝 |
| `--scan <plugin> --out <file>` | 内部使用：插件加载前，Faider 在单独的副本中运行的测试(见 [9](#9-plugins))。没有窗口，没有音频 |
| `--after <process id>` | 内部使用：Faider 重新启动自己时添加(**RESTORE EVERYTHING**、**RESTART NOW**、放弃管理员权限)。新副本会等待该进程结束，最多一分钟 |

<a id="193-files"></a>

### 19.3 文件

位于 `%APPDATA%\Faider` 中：

| 文件 | |
|---|---|
| `session.xml` | 你的混音 |
| `history\` | 早期版本(最多 60 个) |
| `sessions\` | 命名的混音 |
| `session-unloaded-*.xml` | 启动时搁置的混音 |
| `settings.xml` | 偏好设置 |
| `looks\` | LOOK 文件，每个一个 JSON（见 [LOOKS](#looks)） |
| `console.xml` | 间隔条和 SELECT 的选择 |
| `plugins.xml` | 找到的插件，以及每个插件的测试结果 |
| `events.log` | 发生过的一切(达到 5 MB 时轮换) |
| `status.txt` | 每隔几秒的引擎快照：设备、电平、缓冲区、漂移 |
| `crash.log`、`crash.dmp` | 崩溃报告 |

程序本身位于 `C:\Program Files\Faider`(仅为你自己安装时：`%LOCALAPPDATA%\Programs\Faider`)。除非你选择其他文件夹，录音都保存到 `Music\Faider Recordings`。

<a id="194-colours"></a>

### 19.4 颜色

| 颜色 | 含义 |
|---|---|
| 蓝色轮廓 | 已选中 |
| 灰色名称栏 | 通道 |
| 紫色 | 总线 |
| 琥珀色 | 输出 |
| 青色线 | 通道的路由 |
| 紫色线 | 总线的路由 |
| 绿色状态灯 | 音频正常；琥珀色：吃力；红色：有故障或未运行 |
