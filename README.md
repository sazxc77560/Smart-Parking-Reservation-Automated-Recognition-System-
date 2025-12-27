# Smart Parking Reservation & Automated Recognition System 🚗

## 📖 Overview
This project implements a complete smart parking solution integrating "Linux Kernel Space" control with "User Space" AI applications. It features a custom "Character Device Driver" for hardware control and uses "YOLOv5" for real-time license plate recognition.

## 🚀 Key Features

### 1. Linux Character Device Driver (Kernel v6.12)
* GPIO Base 512 Resolution: Manually mapped GPIO registers to resolve the Base 512 mapping issue on newer kernel versions.
* Non-blocking I/O: Implemented `O_NONBLOCK` logic using "Kernel Timers" to optimize CPU usage, avoiding busy loops.

### 2. System Integration & IPC
* Inter-Process Communication: Utilized "Named Pipes (FIFO)" for low-latency communication between the C-based driver interface and the Python AI module.
* I/O Multiplexing: Implemented `select()` system call to monitor multiple file descriptors simultaneously, ensuring real-time responsiveness.
* Network Sync: Developed a "C-based TCP/IP Client" to synchronize parking status with a backend server using a custom protocol.

### 3. Edge AI Deployment
* License Plate Recognition: Deployed YOLOv5 and EasyOCR on Raspberry Pi.
* Accuracy Optimization: Integrated RegEx (Regular Expression) logic to post-process OCR results, correcting common character sequencing errors.

## 🛠️ Hardware & Tech Stack
* Hardware: Raspberry Pi 3, Camera Module, LED
* Software: C/C++, Python 3.13, Linux Kernel 6.12.
* Tools: GPIO Subsystem, Make
## 🔧 How to Build

1. Compile the Kernel Module
   cd driver
   make
   sudo insmod alarm_driver.ko
2. Run the LPR
   python LPR.py
