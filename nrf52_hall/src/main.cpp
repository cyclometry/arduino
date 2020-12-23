#include "Arduino.h"
#include <bluefruit.h>

// BLE Service
BLEDfu bledfu;  // OTA DFU service
BLEDis bledis;  // device information
BLEUart bleuart; // uart over ble
BLEBas blebas;  // battery

// Hall Switch vars
int analogPin = A0; // linear Hall magnetic sensor analog interface
uint32_t hallValue; // hall sensor analog value

// callback invoked when central connects
void connect_callback(uint16_t conn_handle) {
    // Get the reference to current connection
    BLEConnection *connection = Bluefruit.Connection(conn_handle);

    char central_name[32] = {0};
    connection->getPeerName(central_name, sizeof(central_name));

    Serial.print("Connected to ");
    Serial.println(central_name);
}

/**
 * Callback invoked when a connection is dropped
 * @param conn_handle connection where this event happens
 * @param reason is a BLE_HCI_STATUS_CODE which can be found in ble_hci.h
 */
void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
    (void) conn_handle;
    (void) reason;

    Serial.println();
    Serial.print("Disconnected, reason = 0x");
    Serial.println(reason, HEX);
}

void startAdv() {
    // Advertising packet
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();

    // Include bleuart 128-bit uuid
    Bluefruit.Advertising.addService(bleuart);

    // Secondary Scan Response packet (optional)
    // Since there is no room for 'Name' in Advertising packet
    Bluefruit.ScanResponse.addName();

    /* Start Advertising
     * - Enable auto advertising if disconnected
     * - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
     * - Timeout for fast mode is 30 seconds
     * - Start(timeout) with timeout = 0 will advertise forever (until connected)
     *
     * For recommended advertising interval
     * https://developer.apple.com/library/content/qa/qa1931/_index.html
     */
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);    // in unit of 0.625 ms
    Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
    Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds
}

__unused void setup() {
    Serial.begin(115200);

    Serial.println("Analog Hall Sensor device setup");
    Serial.println("---------------------------\n");

    // Setup the BLE LED to be enabled on CONNECT
    // Note: This is actually the default behaviour, but provided
    // here in case you want to control this LED manually via PIN 19
    Bluefruit.autoConnLed(true);

    // Config the peripheral connection with maximum bandwidth
    // more SRAM required by SoftDevice
    // Note: All config***() function must be called before begin()
    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);

    Bluefruit.begin();
    Bluefruit.setTxPower(4);    // Check bluefruit.h for supported values
    Bluefruit.setName("Steering");
    Bluefruit.Periph.setConnectCallback(connect_callback);
    Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

    // To be consistent OTA DFU should be added first if it exists
    bledfu.begin();

    // Configure and Start Device Information Service
    bledis.setManufacturer("Adafruit Industries");
    bledis.setModel("Bluefruit Feather52");
    bledis.begin();

    // Configure and Start BLE Uart Service
    bleuart.begin();

    // Start BLE Battery Service
    blebas.begin();
    blebas.write(100);

    // Set up and start advertising
    startAdv();

    Serial.println("Started");
}

// write a string to Serial Uart and all connected BLE Uart
void writeAll(char *str) {
    Serial.write(str);
    bleuart.write(str);
}

// used to track the time when the activity was started, for calculating elapsed time at each measurement event
unsigned long elapsedStartMillis = 0;

// used to read commands from the manager. the format is: $command_action:$any_params_for_the_action
String inputString;

enum commandAction {
    STOP_RECORDING = 0,
    START_RECORDING = 1
};
enum commandAction currentCommand;

enum recordingState {
    STOPPED, RECORDING
};
enum recordingState currentRecordingState = STOPPED;

// metric type code. maybe make this a parameter or based on the sensor characteristic.
int metricTypeCode = 1;

// the buffer we fill with metrics data and send in batches
// the buffer must be able to hold the size of the metrics data times number of batches collected per send
char outputBuffer[1024];
char *endOfBuffer = outputBuffer;
unsigned int remainingSpace = sizeof(outputBuffer);

void resetOutputBuffer() {
    endOfBuffer = outputBuffer;
    remainingSpace = sizeof(outputBuffer);
    memset(&outputBuffer[0], 0, sizeof(outputBuffer));
}

// how long it's been since we sent the metrics we've been collecting. we want to capture metrics at a higher frequency
// than we're able to send over bluetooth, so we need to send the metrics in batches
unsigned long lastSendTimeMillis = 0;

unsigned int metricsSendFrequencyMs = 1000;

// how frequently we collect the metric. i.e. how granular our data is
unsigned int recordingFrequencyMs = 200;

__unused void loop() {

    //
    // read and respond to any commands sent to us using the defined command_action and inputString values
    //
    while (bleuart.available()) {
        uint8_t ch;
        ch = (uint8_t) bleuart.read();
        inputString += ch;
    }
    if (inputString.length() > 0) {
        Serial.println(inputString);

        // grab the command, which will be a number sent as an ascii char
        currentCommand = (commandAction) (inputString.substring(0, 2).toInt() - 48);

        if (currentCommand == STOP_RECORDING) {
            Serial.println("received STOP_RECORDING command");
            currentRecordingState = STOPPED;
        } else if (currentCommand == START_RECORDING) {
            Serial.println("received START_RECORDING command");
            currentRecordingState = RECORDING;
            // set the start millis to the current value of the system clock (time since power on)
            elapsedStartMillis = millis();
        }
        inputString = ""; // clear the command
    }

    //
    // when in recording mode, collect and write the data to bluetooth
    //
    if (currentRecordingState == RECORDING) {
        hallValue = analogRead(analogPin);

        // capture metricTypeCode, elapsed time millis, sensor value
        // using ";" to delimit the entire block, and ":" to delimit each field
        int writtenBytes = snprintf(
                endOfBuffer,
                remainingSpace,
                "%i:%lu:%lu;",
                metricTypeCode,
                millis() - elapsedStartMillis,
                hallValue
        );

        if (writtenBytes > 0) {
            endOfBuffer += writtenBytes;
            remainingSpace -= writtenBytes;
        } else {
            Serial.write("Something is wrong with the buffer");
            exit(1);
        }
    }

    // if it's time to send the data, send it and reset the buffer and timer
    if (millis() - lastSendTimeMillis > metricsSendFrequencyMs) {
        // remove the last trailing delimiter so we pass a cleanly delimited set of metrics
        outputBuffer[strlen(outputBuffer)-1] = '\0';

        writeAll(outputBuffer);
        resetOutputBuffer();
        lastSendTimeMillis = millis();
    }

    delay(recordingFrequencyMs);
}
