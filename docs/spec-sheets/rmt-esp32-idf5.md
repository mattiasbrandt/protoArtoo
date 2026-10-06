# ESP32 RMT on ESP-IDF 5.x

## 1. Scope

This document summarizes the ESP32 RMT (Remote Control Transceiver) peripheral model in ESP-IDF 5.x, with focus on the modern RMT v2 driver APIs and migration from legacy RMT usage.

The API behaviour below comes from Espressif documentation. Community projects
appear in Section 10 as implementation examples; where one disagrees with
Espressif's documentation, the documentation describes the driver.

## 2. RMT Data Model

RMT operates on symbols, not bytes.

- Primitive unit: `rmt_symbol_word_t`
- Symbol layout: two duration/level pairs
  - `duration0` + `level0`
  - `duration1` + `level1`
- Duration units are in RMT ticks (`1 / resolution_hz` seconds per tick).

This symbol-oriented model is why RMT is good for pulse/timing protocols and less direct than UART/SPI for byte streams.

## 3. Driver Architecture in IDF 5.x

IDF 5.x uses the redesigned channel-handle model:

- Allocate TX channel: `rmt_new_tx_channel()`
- Allocate RX channel: `rmt_new_rx_channel()`
- Enable/disable channel: `rmt_enable()` / `rmt_disable()`
- Delete channel: `rmt_del_channel()`

Key design points:

- Channels are allocated dynamically from a pool.
- TX and RX have separate install/config paths.
- Interrupt ownership is managed by the driver.
- Direct RMT RAM ownership juggling is no longer exposed as user API.

## 4. RX Transaction Model

RX setup has two phases:

1. Install and enable channel.
2. Start each receive transaction with `rmt_receive()`.

Important semantics:

- `rmt_receive()` is non-blocking.
- RX completion is delivered via `rmt_rx_event_callbacks_t::on_recv_done`.
- `on_recv_done` may be fired for partial receive in one transaction.
- RX stops when a pulse exceeds `signal_range_max_ns` (stop signal condition).
- For a new receive window, call `rmt_receive()` again.

Critical config parameters:

- `signal_range_min_ns`: pulses shorter than this are treated as glitches.
- `signal_range_max_ns`: pulses longer than this terminate the receive transaction.

Received data:

- The `edata` pointer is valid only during the callback. Espressif: *"please do not try to save this pointer and use that outside of the callback function."*
- The receive buffer may be overwritten by later data; copy what is to be kept or processed later.

## 5. TX Transaction Model

TX in IDF 5.x is encoder-driven:

- User payload is converted to `rmt_symbol_word_t` by an encoder.
- Transaction launch: `rmt_transmit()`
- Completion callback: `rmt_tx_event_callbacks_t::on_trans_done`

Common encoder options:

- Copy encoder (`rmt_new_copy_encoder`)
- Bytes encoder (`rmt_new_bytes_encoder`)
- Simple callback encoder (`rmt_new_simple_encoder`)

Operational notes:

- Encoder callbacks can be called multiple times per transaction.
- `rmt_transmit()` queues work and may return before physical transmission starts.
- Payload must remain valid until transaction completion.

Encoder state:

- RMT encoders are stateful. `rmt_encoder_t::reset` *"should reset the encoder state back to the initial state"*.
- Espressif: *"If the RMT transmitter is manually stopped without resetting its corresponding encoder, subsequent encoding session can be erroneous."*
- `rmt_disable()` calls the encoder reset implicitly.

## 6. ISR, IRAM, and Cache-Safe Constraints

RMT callbacks run in interrupt context.

Requirements:

- Do not block in callbacks.
- Use only ISR-safe FreeRTOS APIs (`FromISR` variants).
- Treat callback event pointers (`edata`) as callback-lifetime data.

Cache/flash interaction:

- Flash erase/write can defer non-IRAM-safe interrupt handling.
- For deterministic latency under cache-disabled windows, use IRAM-safe strategy.
- IRAM safety requires all reachable ISR code in IRAM and ISR-used data in internal RAM/DRAM.

Related Kconfig options:

- `CONFIG_RMT_TX_ISR_CACHE_SAFE`
- `CONFIG_RMT_RX_ISR_CACHE_SAFE`
- `CONFIG_RMT_RECV_FUNC_IN_IRAM`

With `CONFIG_RMT_TX_ISR_CACHE_SAFE` or `CONFIG_RMT_RX_ISR_CACHE_SAFE` enabled, the
documentation requires every function the ISR uses to be in IRAM and the driver
object to be in DRAM. A custom encoder's functions must also be in IRAM; the
documentation decorates them with `RMT_ENCODER_FUNC_ATTR`.

## 7. Memory and Throughput Considerations

`mem_block_symbols` behavior differs by mode:

- Without DMA: controls dedicated channel memory block sizing.
- With DMA: controls DMA buffer sizing.

General sizing guidance:

- Increase symbol buffer depth for long/complex waveforms.
- For looped TX, total encoded symbols must fit hardware constraints.
- For RX, user buffer overflow drops symbols and causes truncation behavior.

## 8. Migration from Legacy RMT Driver

Legacy include path:

- `driver/rmt.h` (deprecated)

New include paths:

- `driver/rmt_tx.h`
- `driver/rmt_rx.h`
- `driver/rmt_encoder.h`
- `driver/rmt_common.h`
- `driver/rmt_types.h`

Major conceptual migration:

- `rmt_channel_t` -> `rmt_channel_handle_t`
- `rmt_item32_t` -> `rmt_symbol_word_t`
- explicit ISR register/unregister removed from user API path
- direct RMTMEM access/ownership control removed from user-facing driver model

Common API migrations:

- `rmt_driver_install` -> `rmt_new_tx_channel` / `rmt_new_rx_channel`
- `rmt_driver_uninstall` -> `rmt_del_channel`
- `rmt_tx_start` / `rmt_rx_start` -> `rmt_enable`
- `rmt_tx_stop` / `rmt_rx_stop` -> `rmt_disable`
- `rmt_set_rx_idle_thresh` -> `rmt_receive_config_t::signal_range_max_ns`
- `rmt_set_rx_filter` -> `rmt_receive_config_t::signal_range_min_ns`
- `rmt_wait_tx_done` -> `rmt_tx_wait_all_done`
- translator API model -> encoder API model

Migration consequence:

- Any implementation that depends on `rmt_isr_register`, direct `RMTMEM` mutation, or runtime pin/channel ownership patterns from legacy APIs is not IDF5-compliant without explicit low-level HAL redesign.

## 9. IDF 5.x Version Notes

Observed migration-impact highlights:

- The major RMT architectural migration is documented in 4.4 -> 5.0 migration notes.
- 5.3 introduces peripheral driver component split including `esp_driver_rmt`.
- Later 5.x migration pages reviewed here did not introduce major new RMT architecture shifts comparable to 5.0.

## 10. Community Example Patterns

### 10.1 UART-over-RMT component example

Repository: `brmmm3/esp32-rmt-uart-v5`

Useful takeaways:

- Demonstrates practical UART-like framing over RMT symbols.
- Shows explicit throughput and format constraints in a real component.
- Documents chip-specific receive limits encountered by the author.

Caution:

- Its limits and chip coverage are the author's findings; they are not corroborated by Espressif docs or the target TRM.

### 10.2 FastAccelStepper IDF5 RMT backend

Reference page: DeepWiki summary of `gin66/FastAccelStepper` IDF5 RMT driver path.

Useful takeaways:

- Shows encoder-callback driven refill pattern with `rmt_new_simple_encoder`.
- Illustrates IRAM callback handling for high-rate pulse generation.
- Demonstrates queue-to-symbol conversion and explicit stop/start state logic.

Caution:

- DeepWiki is an indexed secondary view of the code, not the upstream source.

### 10.3 Forum migration pain point example

Thread: "RMT migration to IDF 5.0" on esp32.com.

Useful takeaway:

- Captures a common migration issue pattern: legacy code expecting custom ISR registration and direct RMTMEM control no longer maps directly to IDF5 driver model.

Caution:

- The linked thread is a question post, not API guidance.

## 11. Practical Migration Checklist

1. Replace legacy includes with `rmt_tx/rmt_rx/rmt_encoder` headers.
2. Convert fixed channel index design to channel handles.
3. Replace direct interrupt registration paths with RMT event callbacks.
4. Replace direct RMTMEM programming flow with encoder-based TX and user-buffer RX flow.
5. Re-tune receive window thresholds (`signal_range_min_ns/max_ns`) based on protocol timing.
6. Validate callback IRAM/data placement for cache-disabled latency cases.
7. Re-size symbol/user buffers to avoid truncation under worst-case traffic.

## 12. Quick Reference

- Topic: TX channel allocation. API: `rmt_new_tx_channel()`.
- Topic: RX channel allocation. API: `rmt_new_rx_channel()`.
- Topic: Channel lifecycle start/stop. API: `rmt_enable()` / `rmt_disable()`.
- Topic: RX transaction start. API: `rmt_receive()`.
- Topic: TX transaction start. API: `rmt_transmit()`.
- Topic: TX completion wait. API: `rmt_tx_wait_all_done()`.
- Topic: Channel teardown. API: `rmt_del_channel()`.
- Topic: RX glitch threshold. Config field: `signal_range_min_ns`.
- Topic: RX stop threshold. Config field: `signal_range_max_ns`.
- Topic: Legacy ISR registration in IDF5. Status: not part of modern public driver flow.
- Topic: Direct RMTMEM ownership control in IDF5. Status: not part of modern public driver flow.

## 13. Sources

Primary (Espressif):

- ESP-IDF RMT API reference (latest):
  - https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/rmt.html
- ESP-IDF interrupt allocation and IRAM-safe ISR guidance:
  - https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/intr_alloc.html
- ESP-IDF 5.0 peripheral migration guide (v5.1 docs path):
  - https://docs.espressif.com/projects/esp-idf/en/v5.1/esp32/migration-guides/release-5.x/5.0/peripherals.html

Additional references (examples/context):

- UART over RMT component:
  - https://github.com/brmmm3/esp32-rmt-uart-v5
- FastAccelStepper IDF5 RMT summary:
  - https://deepwiki.com/gin66/FastAccelStepper/4.2.1-esp32-rmt-driver-(idf-5+)
- ESP32 forum migration thread:
  - https://esp32.com/viewtopic.php?t=37020
