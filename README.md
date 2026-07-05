# ESP-NOW Split Keyboard

## 1. Project Overview

This project is the final assignment for the **Real-Time Systems** course. It
belongs to the coursework tracked in the following repository:

https://github.com/fefoliveira/real-time-systems

The goal is to build a split keyboard prototype using two ESP32 boards.

Each board represents one half of the keyboard. The left node scans its keys
and sends key events to the right node through ESP-NOW. The right node receives
those events, merges them with its local keys, keeps the complete keyboard
state, and can expose the output in two ways: directly as a Bluetooth BLE HID
keyboard for Linux, or through a serial bridge that injects keys into the
system with `uinput`.

The firmware is built with ESP-IDF and uses FreeRTOS features such as periodic
tasks, timing control, queues, and callbacks.

## 2. Development Stages and Branches

The project history is organized into branches that represent different
experimentation and development stages. The flow follows a natural progression:
communication validation, button scanning, centralized state consolidation,
and finally HID output through Bluetooth.

| Branch               | Purpose                                                                                                              |
| -------------------- | -------------------------------------------------------------------------------------------------------------------- |
| `main`               | Provide the ESP-IDF project base and validate independent builds for both boards inside the same repository.         |
| `feature/esp-now`    | Experiment with and validate ESP-NOW communication between the two ESP32 boards using periodic messages.             |
| `feature/left-node`  | Read the first buttons on the left node and send real press/release events to the right node.                        |
| `feature/right-node` | Integrate local arrow keys, a central FIFO queue, and the consolidated state mirror of all six keys on the right node. |
| `feature/bluetooth`  | Adapt the right node output so it advertises a BLE HID keyboard and sends events directly to Linux.                  |

To inspect a specific stage, use:

```bash
git switch <branch-name>
```

For example:

```bash
git switch feature/left-node
```

## 3. `feature/left-node` Goal

This branch implements the first physical key scanning stage. ESP1 acts as the
left node, monitors two buttons, and sends an ESP-NOW event whenever a key is
pressed or released.

The current mapping is:

| Key | GPIO   |
| --- | ------ |
| A   | GPIO25 |
| B   | GPIO26 |

The GPIOs use internal pull-up resistors. Each button must be connected between
its GPIO and GND:

```text
GPIO25 --- button A --- GND
GPIO26 --- button B --- GND
```

With this wiring, the GPIO stays high while the button is released and goes low
when the button is pressed.

The left node runs a FreeRTOS task that checks the buttons every 1 ms,
resulting in a 1000 Hz polling rate. A change must remain stable for 5 ms
before it is accepted, reducing false events caused by mechanical button
bounce.

When a valid change is detected, ESP1 broadcasts a `key_event_t` structure:

```c
typedef struct {
    keyboard_key_id_t key_id;
    bool is_pressed;
} key_event_t;
```

The event reports which key changed and whether it was pressed or released.
The `KEY_A` and `KEY_B` identifiers use values that match USB HID keyboard
codes, preparing the protocol for later stages.

ESP2 acts as the right node and centralizer. Besides receiving the remote A and
B events, it monitors four local buttons mapped to the arrow keys. Remote and
local events are inserted into a single FreeRTOS FIFO queue.

Both boards use Wi-Fi Station mode on channel 1. Events are still sent to the
broadcast address `FF:FF:FF:FF:FF:FF`, without encryption or unicast pairing.

## 4. Phase 2: Central Queue and Consolidated State

### Shared Debounce

Button scanning and debouncing for both nodes are centralized in the
`components/button_debounce` component. The component configures active-low
inputs with internal pull-ups and stores, for each button, the stable state,
the candidate state, and the number of consecutive samples.

Both nodes call `button_debounce_sample()` every 1 ms. The function only
produces a `key_event_t` after five equal samples and only when the validated
state actually changes. The left node sends that event through ESP-NOW; the
right node inserts it into the central queue.

The right node keeps a global mirror of the current state of all six keys:

```c
typedef struct {
    bool key_a;
    bool key_b;
    bool arrow_up;
    bool arrow_down;
    bool arrow_left;
    bool arrow_right;
} keyboard_state_t;
```

A FreeRTOS queue with room for 32 `key_event_t` instances works as the
chronological arbiter. The ESP-NOW callback publishes remote events to it, and
the `right_local_scan_task` publishes the four local arrow-key events.

Only `keyboard_consolidation_task` changes the global mirror. It blocks on
`xQueueReceive(..., portMAX_DELAY)` while the queue is empty, consumes events
in FIFO order, and prints the full keyboard snapshot. Since there is only one
writer for the mirror, no mutex is required in this phase.

The ESP-NOW callback runs in the context of ESP-IDF's high-priority Wi-Fi task,
not in a hardware ISR. Because of that, the correct publish operation is
`xQueueSend(..., 0)`, without blocking. `xQueueSendFromISR` remains reserved
for real interrupts.

The right node uses the following priorities:

| Routine                       | Priority                                    |
| ----------------------------- | ------------------------------------------- |
| ESP-NOW callback              | ESP-IDF internal Wi-Fi task context         |
| `right_local_scan_task`       | `configMAX_PRIORITIES - 2`                  |
| `keyboard_consolidation_task` | 3                                           |

The local scanner uses `xTaskDelayUntil()` with a 1 ms period and a debounce
window of five consecutive samples, just like the left node.

The physical mapping of the right node is:

| Key         | GPIO   |
| ----------- | ------ |
| Arrow up    | GPIO25 |
| Arrow down  | GPIO33 |
| Arrow left  | GPIO27 |
| Arrow right | GPIO26 |

### Project Structure

The main structure of this branch is:

```text
.
|-- CMakeLists.txt                       # Main ESP-IDF build configuration
|-- Makefile                             # Build, flash, and monitor commands for both boards
|-- README.md                            # General project and branch documentation
|-- components/
|   |-- button_debounce/                 # Shared active-low button debounce component
|   |-- keyboard_hid/                    # BLE HID keyboard output component
|   `-- shared_protocol/                 # Component shared by both firmware targets
|       |-- CMakeLists.txt               # Exports the protocol header directory
|       `-- include/
|           `-- protocol.h               # Key IDs and transmitted event format
|-- main/                                # Main application component
|   |-- CMakeLists.txt                   # Registers sources, dependencies, and shared protocol
|   |-- Kconfig.projbuild                # Selects left or right node and output mode
|   |-- main.c                           # Initializes the configured node
|   |-- left_node/
|   |   |-- left_node.c                  # Reads GPIO25/GPIO26, debounces, and sends events
|   |   `-- left_node.h                  # Public interface for the left node
|   `-- right_node/
|       |-- right_node.c                 # Receives, merges, and emits key events
|       `-- right_node.h                 # Public interface for the right node
|-- tools/
|   `-- linux_hid_bridge/                # Optional serial-to-uinput Linux bridge
|-- sdkconfig.defaults.esp1              # Selects the left node and a 1000 Hz FreeRTOS tick
`-- sdkconfig.defaults.esp2              # Selects the right node and a 1000 Hz FreeRTOS tick
```

Build directories and full `sdkconfig` files are generated by ESP-IDF. They may
not exist immediately after cloning the repository and appear as build commands
are executed.

The `shared_protocol` component ensures that sender and receiver use the same
key definitions and packet format. The `Makefile` keeps separate configurations
and build directories for both firmware images inside the same repository.

## 5. Running the Project

By default, the `Makefile` assumes:

- ESP-IDF is available at `~/esp/v5.5-rc1/esp-idf/export.sh`;
- ESP1, the left node, is connected to `/dev/ttyUSB0`;
- ESP2, the right node, is connected to `/dev/ttyUSB1`;
- the build target is `esp32`.

If your ESP-IDF installation is located somewhere else, pass the path when
running the command:

```bash
make esp1 IDF_EXPORT="$HOME/esp/esp-idf/export.sh"
```

### Wire the Buttons

With both boards powered off, connect:

```text
ESP1 GPIO25 --- button A --- GND
ESP1 GPIO26 --- button B --- GND

ESP2 GPIO25 --- arrow up button --- GND
ESP2 GPIO33 --- arrow down button --- GND
ESP2 GPIO27 --- arrow left button --- GND
ESP2 GPIO26 --- arrow right button --- GND
```

External resistors are not required for this test because the firmware enables
the ESP32 internal pull-up resistors.

### Build, Flash, and Monitor Both Boards

Connect both boards to the computer. In one terminal, build and flash the left
node firmware to ESP1:

```bash
make esp1
```

In another terminal, build and flash the receiver firmware to ESP2:

```bash
make esp2
```

The same commands are also available through aliases:

```bash
make left
make right
```

Each command uses its own `sdkconfig`, build directory, and serial port. Both
serial monitors can remain open at the same time.

When ESP1 starts, the expected output includes:

```text
I (...) APP_MAIN: Configured as LEFT half
I (...) LEFT_NODE: Starting LEFT scanner: KEY_A=GPIO25, KEY_B=GPIO26, polling=1000 Hz
I (...) main_task: Returned from app_main()
```

Returning from `app_main()` is expected. The `left_button_scan_task` continues
running and monitoring the GPIOs.

When ESP2 starts, the expected output includes:

```text
I (...) APP_MAIN: Configured as RIGHT half
I (...) RIGHT_NODE: Starting RIGHT half: UP=GPIO25 DOWN=GPIO33 LEFT=GPIO27 RIGHT=GPIO26
I (...) KEYBOARD_HID: Initializing BLE HID keyboard output
I (...) RIGHT_NODE: Central key-event queue ready
I (...) main_task: Returned from app_main()
```

ESP2 remains active through the ESP-NOW callback, `right_local_scan_task`, and
`keyboard_consolidation_task` even after `app_main()` returns.

With `CONFIG_KEYBOARD_OUTPUT_BLE_HID=y`, the ESP2 serial port is only used for
diagnostic and pairing logs. The actual key output goes through Bluetooth,
without the Python bridge. To test it, stop the monitor if desired, open the
Linux Bluetooth settings, and pair with `ESP Split Keyboard`.

When pressing and releasing button A in serial output mode, the ESP2 monitor
should show:

```text
I (...) RIGHT_NODE: Keyboard state: A=1 B=0 UP=0 DOWN=0 LEFT=0 RIGHT=0
I (...) RIGHT_NODE: Keyboard state: A=0 B=0 UP=0 DOWN=0 LEFT=0 RIGHT=0
```

For button B, the expected output is:

```text
I (...) RIGHT_NODE: Keyboard state: A=0 B=1 UP=0 DOWN=0 LEFT=0 RIGHT=0
I (...) RIGHT_NODE: Keyboard state: A=0 B=0 UP=0 DOWN=0 LEFT=0 RIGHT=0
```

When pressing the up arrow on ESP2:

```text
I (...) RIGHT_NODE: Keyboard state: A=0 B=0 UP=1 DOWN=0 LEFT=0 RIGHT=0
```

If your serial ports differ from the default configuration:

```bash
make esp1 ESP1_PORT=/dev/ttyACM0
make esp2 ESP2_PORT=/dev/ttyACM1
```

To leave the ESP-IDF serial monitor, press `Ctrl+]`.

Each stage can also be executed separately:

```bash
make esp1-build
make esp1-flash
make esp1-monitor

make esp2-build
make esp2-flash
make esp2-monitor
```

At this stage, the test is considered successful when remote and local events
correctly update the consolidated six-key snapshot while preserving the FIFO
order of the central queue.

## 6. HID Output: Direct Bluetooth or Serial Bridge

The right node supports two Kconfig-selected output modes. Both are useful
depending on whether the goal is final usage or debugging.

| Macro                              | Behavior                                                                                                  |
| ---------------------------------- | --------------------------------------------------------------------------------------------------------- |
| `CONFIG_KEYBOARD_OUTPUT_BLE_HID=y` | ESP2 advertises a BLE keyboard named `ESP Split Keyboard` and sends HID reports directly to Linux.        |
| `CONFIG_KEYBOARD_OUTPUT_SERIAL=y`  | ESP2 prints the consolidated keyboard snapshot over serial, keeping compatibility with the Python bridge. |

The current `sdkconfig.defaults.esp2` file is configured for Bluetooth:

```text
CONFIG_KEYBOARD_OUTPUT_BLE_HID=y
# CONFIG_KEYBOARD_OUTPUT_SERIAL is not set
```

The recommended current flow is:

```text
ESP1 A/B buttons
    -> ESP-NOW
ESP2 right_node
    -> FreeRTOS queue
    -> consolidated state mirror
    -> BLE HID
Linux
```

To build and flash ESP2 in this mode:

```bash
make esp2-build-flash
```

Then pair the Bluetooth device `ESP Split Keyboard` on Linux. `make py` is not
required in this mode.

Command summary by mode:

| Mode             | Active macro on ESP2              | Linux command                                      |
| ---------------- | --------------------------------- | -------------------------------------------------- |
| Direct Bluetooth | `CONFIG_KEYBOARD_OUTPUT_BLE_HID=y` | Pair `ESP Split Keyboard`; do not use `make py`.   |
| Serial debug     | `CONFIG_KEYBOARD_OUTPUT_SERIAL=y` | Use `make py-dry` or `sudo make py`.               |

### Linux Serial HID Bridge

As a debugging alternative, the Linux computer can also receive the
consolidated keys from ESP2 through a Python bridge:

```text
ESP1 A/B buttons
    -> ESP-NOW
ESP2 right_node
    -> USB serial with "Keyboard state: ..."
Python on Linux
    -> uinput
Linux applications
```

This bridge uses the ESP2 USB serial output and creates a virtual keyboard on
Linux with `uinput`. To use it, select `CONFIG_KEYBOARD_OUTPUT_SERIAL=y` in
`sdkconfig.esp2` or through `idf.py menuconfig`.

This mode remains useful when Bluetooth pairing is unstable, when the exact
text emitted by ESP2 needs to be inspected, or when key consolidation should be
tested independently from the BLE stack.

Expected configuration for `make py`:

```text
# CONFIG_KEYBOARD_OUTPUT_BLE_HID is not set
CONFIG_KEYBOARD_OUTPUT_SERIAL=y
```

After changing the macro, rebuild and flash ESP2:

```bash
make esp2-build-flash
```

### Install Dependencies

```bash
cd tools/linux_hid_bridge
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

The user must have permission to read the serial port and write to
`/dev/uinput`. If in doubt, test with `sudo` first.

### Run in Test Mode

Close `make esp2-monitor`, because only one process can use the serial port at
a time. Then run:

```bash
python rightnode_linux_hid_bridge.py --port /dev/ttyUSB1 --dry-run --echo-serial
```

Or, from the project root:

```bash
make py-dry
```

When pressing buttons, the output should show changes such as:

```text
[dry-run] A pressed
[dry-run] A released
[dry-run] LEFT pressed
[dry-run] LEFT released
```

### Run Injecting Real Keys Into Linux

With the virtual environment active:

```bash
sudo .venv/bin/python rightnode_linux_hid_bridge.py --port /dev/ttyUSB1
```

Or, from the project root:

```bash
sudo make py
```

From that moment on, the prototype buttons behave as a real keyboard for
Linux:

| ESP2 state | Key emitted on Linux     |
| ---------- | ------------------------ |
| `A=1`      | `A` pressed              |
| `B=1`      | `B` pressed              |
| `UP=1`     | arrow up pressed         |
| `DOWN=1`   | arrow down pressed       |
| `LEFT=1`   | arrow left pressed       |
| `RIGHT=1`  | arrow right pressed      |

Use `Ctrl+C` to stop the bridge. On exit, the script releases any key that is
still marked as pressed in the local mirror.
