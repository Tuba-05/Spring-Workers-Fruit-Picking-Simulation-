
# 🍎 Project: Spring Workers (Fruit Picking Simulation)

<p align="center">
  <img src="https://img.shields.io/badge/C_Language-00599C?style=for-the-badge&logo=c&logoColor=white" alt="C Language" />
  <img src="https://img.shields.io/badge/GTK3-7FE71E?style=for-the-badge&logo=gnome&logoColor=black" alt="GTK3" />
  <img src="https://img.shields.io/badge/POSIX_Threads-000000?style=for-the-badge&logo=linux&logoColor=white" alt="Pthreads" />
  <img src="https://img.shields.io/badge/Operating_Systems-FF9900?style=for-the-badge&logo=cpu&logoColor=white" alt="OS Concepts" />
</p>

This project is a GUI-based visual simulation of a fruit farm demonstrating the classic **Producer-Consumer Problem** in Operating Systems. 

In this simulation:
- **3 Picker threads** act as *producers* by picking fruits and placing them into a shared storage area.
- **1 Loader thread** acts as the *consumer* by collecting and loading the fruits.

The main objective is to showcase **Multi-threading**, **thread synchronization**, and efficient resource sharing using **POSIX Threads (Pthreads)** with Mutex locks to prevent data races and inconsistency.

---

## 📑 Table of Contents
1. [Key Concepts Demonstrated](#-key-concepts-demonstrated)
2. [Folder Structure](#-folder-structure)
3. [Setup & Installation](#-setup--installation)
4. [Compilation & Run](#-compilation--run)
5. [Contributors](#-contributors)

---

## 🧠 Key Concepts Demonstrated
* **Multithreading:** Concurrent execution of tasks using `pthread_create`.
* **Mutual Exclusion (Mutex):** Implementation of `pthread_mutex_t` to lock critical sections.
* **Thread Synchronization:** Using condition variables (`pthread_cond_t`) to orchestrate picker and loader loops.
* **GUI Development in C:** Utilizing **GTK+ 3.0** to render a real-time progress simulation of the farm.

---

## 📂 Folder Structure
```text
Spring-Workers/
├── Threads_GTK.c        # Main C source file containing GUI & Pthreads logic
├── Project_Report.pdf   # Academic project report (detailed explanation)
└── README.md            # Documentation readme
```

---

## 💻 Setup & Installation

Before compiling, you need a **GCC compiler** and **GTK+ 3.0 dev libraries** installed on your machine.

### 🪟 For Windows Users (via WSL or MSYS2)

#### Path A: Using WSL (Recommended)
1. Open PowerShell as Administrator and install WSL (Ubuntu):
   ```powershell
   wsl --install
   wsl --install -d Ubuntu-24.04
   ```
2. Once installed, open your WSL Ubuntu terminal and run:
   ```bash
   sudo apt update && sudo apt install build-essential libgtk-3-dev pkg-config
   ```
3. Open VS Code, install the **WSL Extension (Remote - WSL)**, and connect your workspace using the green `><` button in the bottom-left corner of VS Code.

#### Path B: Using MSYS2
1. Download and install MSYS2 from [msys2.org](https://www.msys2.org/).
2. Open **MSYS2 MinGW 64-bit** terminal and run:
   ```bash
   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-gtk3 pkg-config
   ```

---

### 🐧 For Linux Users
Open your terminal and run:
```bash
sudo apt update && sudo apt install build-essential libgtk-3-dev pkg-config
```

---

### 🍎 For macOS Users
1. Open your terminal and install **Homebrew** (if not installed):
   ```bash
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```
2. Install GCC and GTK3 libraries via Brew:
   ```bash
   brew install gtk+3 pkg-config gcc
   ```

---

## 🏗️ Compilation & Run

### 1. Navigate to Project Directory
Navigate to the folder containing your source file (`Threads_GTK.c`):
```bash
cd path/to/Spring-Workers
```

### 2. Compile the Program
Compile using `gcc` and link both `pthread` and `gtk+-3.0` configurations:
```bash
gcc -o SpringWorkers Threads_GTK.c -lpthread $(pkg-config --cflags --libs gtk+-3.0)
```

### 3. Run the Executable

- **On Linux / WSL / macOS:**
  ```bash
  ./SpringWorkers
  ```
 
- **On Windows (MSYS2):**
  ```bash
  ./SpringWorkers.exe
  ```

---

## 👥 Contributors

This academic project was collaboratively designed by:

* **Tuba Naushad** 
* **Khadija Sehar** 
* **Tahir Ali** 
```
