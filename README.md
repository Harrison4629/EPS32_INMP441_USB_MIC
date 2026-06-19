# ESP32-S3 INMP441 USB Microphone (UAC)

这是一个基于 ESP32-S3 芯片与 INMP441 数字麦克风实现的 USB 免驱高品质麦克风 (UAC 协议) 项目

## 硬件准备

* **开发板**: ESP32-S3-DevKitC-1 WROOM-1-N16R8或同规格板子，烧录时type-c连接com口
* **麦克风**: INMP441 I2S 麦克风模块
* **接线引脚**:

| INMP441 引脚 | ESP32-S3 GPIO | 说明 |
  | :--- | :--- | :--- |
  | **SCK** | GPIO 4 | 串行时钟线 |
  | **WS** | GPIO 5 | 字选择/左右声道选择线 |
  | **SD** | GPIO 6 | 串行数据线 |
  | **L/R** | GND | 接地（选择左声道） |
  | **VDD** | 3.3V | 供电 |
  | **GND** | GND | 接地 |

---

## 软件环境

* **操作系统**：Windows
* **IDE**：VS Code
* **SDK 版本**：ESP-IDF v6.0.1 (或你实际使用的版本)
* **核心组件**：`espressif/usb_device_uac`

---

## 快速开始与复现

### 1. 克隆/下载项目

将本仓库克隆到本地，并用 VS Code 打开项目根目录:

```bash
git clone https://github.com/Harrison4629/EPS32_INMP441_USB_MIC.git
```

### 2.初始化与依赖拉取

本工程使用 ESP-IDF 组件管理器。在你进行项目配置或编译时，系统会自动读取 `main/idf_component.yml `并联网下载官方 `usb_device_uac` 组件：

```bash
dependencies:
  idf:
    version: '>=4.1.0'
  espressif/usb_device_uac: '*'
```

### 3.SDK核心配置

在vscode下方找到齿轮图标(SDK配置编辑器)并点击，跳转到配置界面

#### 内存与闪存设置

* Flash size: 修改为16MB(根据板子参数填写)

* Support for external, SPI-connected RAM: 勾选，在次级菜单中：
  * Mode (QUAD/OCT): 选择`Octal Mode PSRAM`
  * Set RAM clock speed: 选择`80Mhz clock speed`

#### UAC音频精简(纯麦克风)

* 进入 `Component config` -> `USB Device UAC`：
  * UAC Speaker channel number: 修改为 `0`
  * UAC sample rate: 确保为 `48000`
  * UAC bit resolution: 确保为 `16-bit`

### 4.编译与烧录

1. 点击ESP-IDF扩展，选择 `打开ESP-IDF终端`
2. 在终端中输入以下命令
3. 等待烧录完成。

```bash
idf.py build flash
```

### 5.测试效果

若烧录无明显报错，将com口拔出，连接usb口，windows音频设置中会多出`usb uac`麦克风，可以在录音软件中测试音质