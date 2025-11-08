# Phone ↔ Instrument Interaction Diagrams

This document visualizes how key features in the feature backlog play out between a user, the phone app, and the instrument firmware.

## Quick Reference

| Feature | Diagram Section | Summary |
| --- | --- | --- |
| Reference measurement | [Reference Measurement](#reference-measurement) | User triggers a reference; instrument captures baseline readings. |
| Sample measurement | [Sample Measurement](#sample-measurement) | User runs a sample after a reference; instrument returns pH and context. |
| Settings update | [Settings Update](#settings-update) | User edits settings; instrument applies and confirms the change. |
| Settings refresh | [Settings Refresh](#settings-refresh) | User requests current settings; instrument reports live values. |
| Battery status | [Battery Status](#battery-status) | User checks battery; instrument samples and reports capacity. |
| Alert propagation | [Alert Propagation](#alert-propagation) | Instrument pushes an alert; user sees remediation guidance. |
| BLE availability | [BLE Availability](#ble-availability) | User scans for devices; instrument advertises presence. |
| BLE connection | [BLE Connection](#ble-connection) | User connects; instrument negotiates GATT services. |
| Session teardown | [Session Teardown](#session-teardown) | User ends the session; instrument shuts down activity and confirms. |

## Reference Measurement

```mermaid
sequenceDiagram
    participant User as User
    participant App as Phone App
    participant Instr as Instrument Firmware

    User->>App: Tap "Start reference"
    App->>Instr: Send reference_start command over BLE
    Note over Instr: Step 1: Turn on reference LED
    Note over Instr: Step 2: Capture dark reading
    Note over Instr: Step 3: Capture signal reading
    Note over Instr: Step 4: Cache results for later sample math
    Instr-->>App: Reference bundle (raw readings, absorbance, timestamp, calibration info)
    App-->>User: Show baseline results and updated readiness state
```

## Sample Measurement

```mermaid
sequenceDiagram
    participant User as User
    participant App as Phone App
    participant Instr as Instrument Firmware

    User->>App: Tap "Start sample"
    App->>Instr: Send sample_start command over BLE
    Note over Instr: Step 1: Validate cached reference exists
    Note over Instr: Step 2: Drive sample LED
    Note over Instr: Step 3: Capture dark + signal readings
    Note over Instr: Step 4: Calculate absorbance and pH using temp/salinity
    Instr-->>App: Sample bundle (raw readings, absorbance, pH, settings, diagnostics)
    App-->>User: Display measurement, persist to history, surface warnings
```

## Settings Update

```mermaid
sequenceDiagram
    participant User as User
    participant App as Phone App
    participant Instr as Instrument Firmware

    User->>App: Adjust settings and tap "Apply"
    App->>Instr: Send settings_update payload
    Note over Instr: Step 1: Validate payload schema
    Note over Instr: Step 2: Apply runtime configuration
    Note over Instr: Step 3: Persist changes if required
    Instr-->>App: Confirmation with applied values or version hash
    App-->>User: Refresh settings screen with confirmed values
```

## Settings Refresh

```mermaid
sequenceDiagram
    participant User as User
    participant App as Phone App
    participant Instr as Instrument Firmware

    User->>App: Open settings screen
    App->>Instr: Send settings_get request
    Note over Instr: Step 1: Gather active runtime settings
    Note over Instr: Step 2: Merge persisted configuration data
    Instr-->>App: Settings snapshot (values, firmware revision)
    App-->>User: Render current settings and highlight differences
```

## Battery Status

```mermaid
sequenceDiagram
    participant User as User
    participant App as Phone App
    participant Instr as Instrument Firmware

    User->>App: Pull down to refresh battery status
    App->>Instr: Send battery_status request
    Note over Instr: Step 1: Sample battery monitor ADC
    Note over Instr: Step 2: Classify level/state (percentage, warnings)
    Instr-->>App: Battery report (percentage, state flag, optional voltage)
    App-->>User: Update battery indicator and warnings
```

## Alert Propagation

```mermaid
sequenceDiagram
    participant User as User
    participant App as Phone App
    participant Instr as Instrument Firmware

    Note over Instr: Step 1: Detect fault or threshold event
    Note over Instr: Step 2: Package alert code, severity, guidance
    Instr-->>App: Push alert notification
    App-->>User: Display alert banner with recommended actions
    User->>App: Acknowledge or request help
    App->>Instr: Send alert_ack or remediation command (optional)
```

## BLE Availability

```mermaid
sequenceDiagram
    participant User as User
    participant App as Phone App
    participant Instr as Instrument Firmware

    Note over Instr: Step 1: Advertise identifier, supported services, firmware revision
    User->>App: Open device scan screen
    App-->>User: Show scanning indicator
    Instr-->>App: Broadcast advertising packet
    App-->>User: List device with name, RSSI, firmware info
```

## BLE Connection

```mermaid
sequenceDiagram
    participant User as User
    participant App as Phone App
    participant Instr as Instrument Firmware

    User->>App: Tap instrument in scan list
    App->>Instr: Initiate BLE connection
    Note over Instr: Step 1: Accept pairing and negotiate MTU
    Note over Instr: Step 2: Expose command and notification characteristics
    Instr-->>App: Connection acknowledgment with capabilities summary
    App-->>User: Indicate connected state and enable feature buttons
```

## Session Teardown

```mermaid
sequenceDiagram
    participant User as User
    participant App as Phone App
    participant Instr as Instrument Firmware

    User->>App: Tap "End session"
    App->>Instr: Send session_close command
    Note over Instr: Step 1: Stop measurements and background tasks
    Note over Instr: Step 2: Release peripherals and save outstanding data
    Instr-->>App: Session closed acknowledgment
    App-->>User: Return to dashboard and prompt for next connection
```
