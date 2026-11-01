# Bluetooth LE MIDI

Formation's BLE-MIDI port: off at power-on, switched from the SYSTEM menu (BLUETOOTH, knob 4).

| File | What | From |
| --- | --- | --- |
| `ble_*.c`, `ble_*.h`, `ble.h`, `rf_share.h` | the BLE stack: link layer, GATT, the BLE-MIDI service, the radio driver for the AC791N / WL82 baseband | [Optimist](https://github.com/w0ts/optimist) (w0ts, GPL-3.0-only), commit `5b06939`, as [fm1-dock](https://github.com/joakimeriksson/fm1-dock) took it in its commit `4248805` |
| `../../hal/fm1_ble.h`, `fm1_ble_rf.h`, `ble_rf_tables_v15.h`, `../../gen/ble_rf_tables.h` | the baseband's registers and the radio's start-up values | the same |
| `fm_ble.c` | the glue: the queues between the stack and the app, the radio's start, the switch | fm1-dock's `hx.c` (Joakim Eriksson, GPL-3.0-only), taken out of its separate flash unit and linked into the app |

The stack files are as fm1-dock has them, which is as Optimist has them except that `ble_stack.c` takes `BLE_API`
from its includer. **Keep them that way**: to update, copy Optimist's (or fm1-dock's) newer files over them whole and
change `fm_ble.c` only where the stack's interface changed. Their design, the hardware facts and the provenance are
Optimist's `docs/BLE-STACK.md` and `docs/BLE-HW-FACTS.md`.

How it is wired in (`platform.c`):

* `usb.c`'s `midi_out_event`, where every MIDI packet the app sends passes, calls `bt_out()` (`APP_MIDI_OUT`).
* TIMER5's 2 kHz part calls `bt_poll()` (`APP_T5_POLL`), which moves what a Bluetooth device sent into `usb.c`'s
  MIDI-in ring, so `midi_get()` returns it with USB's.
* `fm1_cstart` calls `bt_attach()` (the two BLE vectors, masked) **before** `fm1_guard_lock_top()`: after that the
  vector table is write-locked, and attaching later crashes at boot (fm1-dock's first build did).
* `fm_ble.c` is part of the app's one translation unit: every name in it starts with `bt_`.

The radio's factory values (stock's VM, flash `0xE8000`) are only read. `FL_RANGE_OK` refuses to write there, and a copy
is kept in the settings (`bt_kept`). Without them the radio never starts (BT_NOCAL).

Not here: the central role (Formation does not connect out to other devices) and pairing.
