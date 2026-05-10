# 🍎 Project: Spring Workers (Fruit Picking Simulation)

## **📝 Overview**

This project is a visual simulation of a fruit farm based on the **Producer-Consumer Problem** in Operating Systems.  
In this simulation, **3 Picker threads** act as producers by picking fruits and placing them into a shared storage area, while **1 Loader thread** acts as the consumer by collecting and loading the fruits.  
The main objective of the project is to demonstrate **Multi-threading**, **thread synchronization**, and efficient resource sharing using **POSIX Threads (Pthreads)** without causing conflicts or data inconsistency.

---

## **💻 Setup Instructions**
Before running this project, you must install the **GCC Compiler** for the C language, regardless of which operating system you are using.
The GCC compiler is required to compile and run the C source code.

---

### **🪟 For Windows Users (Recommended Path)**
If you are on Windows, follow these steps to prepare your environment:

### 1. Install MSYS2

Download it from: https://www.msys2.org/

This provides the necessary tools to run C code on Windows.

### 2. Install GCC & GTK

Open the **MSYS2 MinGW 64-bit** terminal and run:

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-gtk3 pkg-config
```

### 3. Install WSL(Windows Subsystem for Linux)

Open **PowerShell as Administrator** and type:

```powershell
wsl --install
```

This adds a Linux layer (Ubuntu) to your Windows system.

### 4. Install Ubuntu

After restarting, open PowerShell again and run:

```powershell
wsl --install -d Ubuntu-24.04
```

Note: After installation, a terminal window will pop up. It will ask you to create a Username and Password. Keep these safe!

### 5. Install VS Code & Connect to WSL

#### Install VS Code
Download and install VS Code from the official website:

- [Visual Studio Code](https://code.visualstudio.com?utm_source=chatgpt.com)

---

#### Install the WSL Extension

1. Open VS Code.
2. Click on the **Extensions** icon on the left sidebar (the 4 squares icon).
3. Search for **"WSL"**.
4. Click **Install** on the extension named **Remote - WSL**.

---

#### Connect to Your Ubuntu Environment

1. Look at the bottom-left corner of the VS Code window.
2. You will see a small blue or green button that looks like this:

   ```text
   ><
   ```
3. Click this button. A command menu will open at the top of your screen.
4. Select one of the following options:
    - Connect to WSL
    - New WSL Window using Distro... → then choose Ubuntu-24.04

##### **Success Check**

Once connected, the bottom-left corner of VS Code should display:
```
WSL: Ubuntu-24.04
```
Now, whenever you open a terminal inside VS Code using:
```
Ctrl + ~
```
it will automatically open your Ubuntu Linux terminal.

This allows you to run your C and GTK code seamlessly inside the Linux environment.

---

### **🐧 For Linux Users**
If you are on Linux System, follow these steps to prepare your environment:

Open your terminal and run:

```bash
sudo apt update && sudo apt install build-essential libgtk-3-dev pkg-config
```

---

### **🍎 For macOS Users**
If you are on macOS, follow these steps to prepare your environment:

### 1. Open Terminal

Open the **Terminal** app on your Mac.

Shortcut:
- Press `Command + Space`
- Search for `Terminal`

### 2. Install Homebrew

Run this command:

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

### 3. Enter Password

Type your macOS password if prompted and press **Enter**.

### 4. Verify Installation

Run:

```bash
brew --version
```

If installed successfully, it will show the Homebrew version.

#### Official Website

https://brew.sh/

### 5. Install GCC & GTK   

```bash
brew install gtk+3 pkg-config gcc
```
---

## **🏗️ Compile & Run**


### 1. Open Your Terminal

Use one of the following:

- WSL
- MSYS2
- Linux Terminal
- macOS Terminal
- VScode Terminal

### 2. Navigate to the Project Folder

Go to the folder where you saved the file:

```text
Threads_GTK.c
```

### 3. Compile the Program

Run:
```bash
gcc -o SpringWorkers Threads_GTK.c -lpthread $(pkg-config --cflags --libs gtk+-3.0)
```

### 4. Run the Program

#### Linux / WSL / macOS

```bash
./SpringWorkers
```

#### Windows (MSYS2)

```bash
./SpringWorkers.exe
```

---