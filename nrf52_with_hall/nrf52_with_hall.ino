#include <bluefruit.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

// BLE Service
BLEDfu bledfu;  // OTA DFU service
BLEDis bledis;  // device information
BLEUart bleuart; // uart over ble
BLEBas blebas;  // battery

// Hall Switch vars
int analogPin = A0; // linear Hall magnetic sensor analog interface
int hallValue; // hall sensor analog value


void setup() {
    Serial.begin(115200);

#if CFG_DEBUG
    // Blocking wait for connection when debug mode is enabled via IDE
  while ( !Serial ) yield();
#endif

    Serial.println("Bluefruit52 With Analog Hall");
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
    Bluefruit.setName("Bluefruit52");
    //Bluefruit.setName(getMcuUniqueID()); // useful testing with multiple central connections
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

    Serial.println("Please use Adafruit's Bluefruit LE app to connect in UART mode");
    Serial.println("Once connected, enter character(s) that you wish to send");
}

void startAdv(void) {
    // Advertising packet
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();

    // Include bleuart 128-bit uuid
    Bluefruit.Advertising.addService(bleuart);

    // Secondary Scan Response packet (optional)
    // Since there is no room for 'Name' in Advertising packet
    Bluefruit.ScanResponse.addName();

    /* Start Advertising
       - Enable auto advertising if disconnected
       - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
       - Timeout for fast mode is 30 seconds
       - Start(timeout) with timeout = 0 will advertise forever (until connected)
  
       For recommended advertising interval
       https://developer.apple.com/library/content/qa/qa1931/_index.html
    */
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);    // in unit of 0.625 ms
    Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
    Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds
}


// used to track the time when the activity was started, for calculating elapsed time at each measurement event
unsigned long elapsedStartMillis = 0;

// used to read commands from the manager. the format is: $command_action:$any_params_for_the_action
// e.g. "1" to stop recording
//      "2corner x" to name the activity
//      "3" to start recording
String inputString;

enum commandAction {
    STOP_RECORDING = 0,
    NAME_ACTIVITY = 1,
    START_RECORDING = 2
};
enum commandAction currentCommand;

enum recordingState {
    STOPPED, RECORDING
};
enum recordingState currentRecordingState = STOPPED;

// default the activity name, but the controller should be expected to send a name command
String activity = "unnamed";

// the category of this device for collection type identification. maybe make this a parameter or based on the sensor characteristic
int category = 1;

void loop() {

    // read and respond to any commands sent to us using the defined command_action and inputString values
    while (bleuart.available()) {
        uint8_t ch;
        ch = (uint8_t) bleuart.read();
        inputString += ch;
    }
    if (inputString.length() > 0) {
        Serial.println(inputString);

        int foo = inputString.substring(0, 2).toInt();
        Serial.print("foo is ");
        Serial.println(foo);

        currentCommand = (commandAction) (inputString.substring(0, 2).toInt() - 48);

        if (currentCommand == STOP_RECORDING) {
            Serial.println("received STOP_RECORDING command");
            currentRecordingState = STOPPED;
        } else if (currentCommand == START_RECORDING) {
            Serial.println("received START_RECORDING command");
            currentRecordingState = RECORDING;
            // set the start millis to the current value of the system clock (time since power on)
            elapsedStartMillis = millis();
        } else if (currentCommand == NAME_ACTIVITY) {
            Serial.println("received NAME_ACTIVITY command");
            // the activity name is expected to be the value after the first character of the inputString
            activity = inputString.substring(2);
        }

        inputString = ""; // clear the command
    }

    if (currentRecordingState == RECORDING) {
        hallValue = analogRead(analogPin);
        // Serial.println(hallValue);

        // send category, elapsed time millis, sensor value
        char buffer[32];
        sprintf(buffer, "%i:%i:%i", category, millis() - elapsedStartMillis, hallValue);
        bleuart.write(buffer, sizeof(buffer));
    }

    // todo - collect a batch of finer-grained timestamped measurements and send those. e.g. send batches of 20 measurements per second?
    delay(500);
}

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
   Callback invoked when a connection is dropped
   @param conn_handle connection where this event happens
   @param reason is a BLE_HCI_STATUS_CODE which can be found in ble_hci.h
*/
void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
    (void) conn_handle;
    (void) reason;

    Serial.println();
    Serial.print("Disconnected, reason = 0x");
    Serial.println(reason, HEX);
}
