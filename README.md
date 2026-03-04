# Simple Chat (C++ & Boost.Asio)

A lightweight asynchronous chat application based on a client-server architecture using the **Boost.Asio** library.


### Installing Dependencies

**Ubuntu/Debian:**
```bash
sudo apt install libboost-all-dev cmake build-essential
```
### 🚀 Building the Project

The project uses a standard CMake out-of-source build process.

Configure the project:
```Bash
cmake .
```
Compile the targets:
```Bash
cmake --build .
```
### 💻 Usage
1. Start the Server

```Bash
./server <port>
```
2. Start the Client

Open a new terminal window and run the client to connect to the local server:
```Bash
./chat_client localhost <port>
```
