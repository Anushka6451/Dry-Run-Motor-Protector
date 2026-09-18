

/************************************************************
 * REAL-TIME BOREWELL PUMP PROTECTION AND CONTROL SYSTEM
 *
 * MCU:
 *   DOIT ESP32 DevKit V1 - 30 Pin
 *
 * Connectivity:
 *   Wi-Fi + Blynk IoT
 *
 * Hardware:
 *   Relay       -> GPIO 4
 *   Water ADC   -> GPIO 36 / VP
 *   Probe 1     -> ESP32 3.3V
 *   Probe 2     -> GPIO 36
 *   Pulldown    -> 10K from GPIO36 to GND
 *
 * Logic:
 *   ADC >= 1500 -> WATER PRESENT
 *   ADC <  1500 -> WATER ABSENT
 *
 * Timers:
 *   Startup bypass       = 90 seconds
 *   Dry-run verification  = 5 seconds
 *   User response window  = 30 seconds
 ************************************************************/


/******************** BLYNK CONFIGURATION ********************/


#define BLYNK_PRINT Serial
#define BLYNK_TEMPLATE_ID "TMPL3p-CzNFT1"
#define BLYNK_TEMPLATE_NAME "Ashwin"
#define BLYNK_AUTH_TOKEN "vfKMBSGGDuAYWcTRiDTDRYW4KFeB8AKg"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>


/******************** WIFI CONFIGURATION ********************/

char ssid[] = "POCO";
char pass[] = "567890aa";


/******************** PIN CONFIGURATION **********************/

const int RELAY_PIN = 4;
const int WATER_SENSOR_PIN = 36;


/******************** RELAY LOGIC ****************************/

/*
 * Most 5V single-channel relay modules are ACTIVE LOW.
 *
 * ACTIVE LOW:
 *   LOW  = Relay ON
 *   HIGH = Relay OFF
 *
 * If your relay behaves opposite, change this to false.
 */

const bool RELAY_ACTIVE_LOW = true;


/******************** WATER SENSOR ***************************/

const int WATER_THRESHOLD = 1500;


/******************** TIMING CONFIGURATION *******************/

const unsigned long STARTUP_BYPASS_TIME = 90000UL;  // 90 sec

const unsigned long DRY_VERIFY_TIME = 5000UL;       // 5 sec

const unsigned long USER_RESPONSE_TIME = 30000UL;  // 30 sec


/******************** BLYNK TIMER ****************************/

BlynkTimer timer;


/******************** SYSTEM STATES **************************/

enum PumpState
{
  SYSTEM_OFF,
  STARTING,
  RUNNING,
  WATER_WARNING,
  FAULT_LOCK
};

PumpState currentState = SYSTEM_OFF;


/******************** GLOBAL VARIABLES ***********************/

int waterADC = 0;

bool waterPresent = false;

bool dryVerificationStarted = false;

bool warningNotificationSent = false;

bool shutdownNotificationSent = false;

bool waterRestoredNotificationSent = false;

unsigned long startupStartTime = 0;

unsigned long dryVerificationStartTime = 0;

unsigned long responseStartTime = 0;

unsigned long pumpStartTime = 0;

unsigned long totalRuntime = 0;

unsigned long lastRuntimeUpdate = 0;


/******************** HELPER FUNCTIONS ***********************/


void relayOn()
{
  if (RELAY_ACTIVE_LOW)
  {
    digitalWrite(RELAY_PIN, LOW);
  }
  else
  {
    digitalWrite(RELAY_PIN, HIGH);
  }

  Serial.println("RELAY -> ON");
}


void relayOff()
{
  if (RELAY_ACTIVE_LOW)
  {
    digitalWrite(RELAY_PIN, HIGH);
  }
  else
  {
    digitalWrite(RELAY_PIN, LOW);
  }

  Serial.println("RELAY -> OFF");
}


bool isRelayOn()
{
  if (RELAY_ACTIVE_LOW)
  {
    return digitalRead(RELAY_PIN) == LOW;
  }
  else
  {
    return digitalRead(RELAY_PIN) == HIGH;
  }
}


/*************************************************************
 * READ WATER SENSOR
 *************************************************************/

void readWaterSensor()
{
  waterADC = analogRead(WATER_SENSOR_PIN);

  if (waterADC >= WATER_THRESHOLD)
  {
    waterPresent = true;
  }
  else
  {
    waterPresent = false;
  }

  Serial.print("Water ADC: ");
  Serial.print(waterADC);

  Serial.print(" | Water: ");

  if (waterPresent)
  {
    Serial.println("PRESENT");
  }
  else
  {
    Serial.println("ABSENT");
  }
}


/*************************************************************
 * START PUMP
 *************************************************************/

void startPump()
{
  Serial.println();
  Serial.println("=================================");
  Serial.println("START PUMP COMMAND RECEIVED");
  Serial.println("=================================");

  /*
   * Start relay.
   */

  relayOn();

  /*
   * Start startup timer.
   */

  startupStartTime = millis();

  /*
   * Record pump start time.
   */

  pumpStartTime = millis();

  /*
   * Reset dry-run detection.
   */

  dryVerificationStarted = false;

  warningNotificationSent = false;

  shutdownNotificationSent = false;

  waterRestoredNotificationSent = false;

  /*
   * Enter startup state.
   */

  currentState = STARTING;

  if (Blynk.connected())
  {
    Blynk.virtualWrite(V1, "STARTING");
    Blynk.virtualWrite(V4, "90 SEC STARTUP BYPASS");
    Blynk.virtualWrite(V9, 1);

    Blynk.logEvent(
      "pump_started",
      "Pump started. 90-second startup bypass active."
    );
  }

  Serial.println("Pump STARTING...");
}


/*************************************************************
 * STOP PUMP
 *************************************************************/

void stopPump(const char* reason)
{
  Serial.println();
  Serial.println("=================================");
  Serial.println("STOPPING PUMP");
  Serial.println(reason);
  Serial.println("=================================");

  /*
   * Turn relay OFF.
   */

  relayOff();

  /*
   * Calculate runtime.
   */

  if (pumpStartTime > 0)
  {
    totalRuntime += millis() - pumpStartTime;
  }

  pumpStartTime = 0;

  /*
   * Reset monitoring.
   */

  dryVerificationStarted = false;

  /*
   * Normal OFF state.
   */

  currentState = SYSTEM_OFF;

  if (Blynk.connected())
  {
    Blynk.virtualWrite(V0, 0);
    Blynk.virtualWrite(V1, "OFF");
    Blynk.virtualWrite(V4, "NORMAL");
    Blynk.virtualWrite(V9, 0);
    Blynk.virtualWrite(V7, 0);

    Blynk.logEvent(
      "pump_stopped",
      reason
    );
  }
}


/*************************************************************
 * DRY-RUN WARNING
 *************************************************************/

void startDryRunWarning()
{
  Serial.println();
  Serial.println("=================================");
  Serial.println("DRY-RUN WARNING");
  Serial.println("=================================");

  currentState = WATER_WARNING;

  responseStartTime = millis();

  warningNotificationSent = true;

  if (Blynk.connected())
  {
    Blynk.virtualWrite(V1, "WATER LOSS");
    Blynk.virtualWrite(V2, "ABSENT");
    Blynk.virtualWrite(V4, "DRY-RUN WARNING");
    Blynk.virtualWrite(V6, "Water not detected");
    Blynk.virtualWrite(V7, USER_RESPONSE_TIME / 1000);

    String message =
      "Water loss detected. "
      "ADC=" + String(waterADC) +
      ". Pump protection response timer started.";

    Blynk.logEvent(
      "dry_run_warning",
      message
    );
  }
}


/*************************************************************
 * DRY-RUN PROTECTION SHUTDOWN
 *************************************************************/

void activateDryRunProtection()
{
  Serial.println();
  Serial.println("#################################");
  Serial.println("DRY-RUN PROTECTION ACTIVATED");
  Serial.println("#################################");

  /*
   * Immediately stop relay.
   */

  relayOff();

  /*
   * Calculate runtime.
   */

  if (pumpStartTime > 0)
  {
    totalRuntime += millis() - pumpStartTime;
  }

  pumpStartTime = 0;

  /*
   * Enter FAULT LOCK.
   *
   * Pump will NOT automatically restart.
   *
   * User must press ON.
   */

  currentState = FAULT_LOCK;

  dryVerificationStarted = false;

  shutdownNotificationSent = true;

  if (Blynk.connected())
  {
    Blynk.virtualWrite(V0, 0);
    Blynk.virtualWrite(V1, "FAULT - PUMP OFF");
    Blynk.virtualWrite(V2, "WATER ABSENT");
    Blynk.virtualWrite(V4, "DRY-RUN PROTECTION");
    Blynk.virtualWrite(V6, "DRY-RUN FAULT");
    Blynk.virtualWrite(V7, 0);
    Blynk.virtualWrite(V9, 0);

    String message =
      "PUMP STOPPED: Dry-run protection. "
      "ADC=" + String(waterADC) +
      ". Manual ON command required for another start attempt.";

    Blynk.logEvent(
      "dry_run_shutdown",
      message
    );
  }
}


/*************************************************************
 * STARTUP BYPASS HANDLER
 *************************************************************/

void handleStartup()
{
  unsigned long elapsed =
    millis() - startupStartTime;

  unsigned long remaining =
    STARTUP_BYPASS_TIME - elapsed;

  /*
   * Display remaining startup time.
   */

  if (Blynk.connected())
  {
    Blynk.virtualWrite(
      V7,
      remaining / 1000
    );

    Blynk.virtualWrite(
      V4,
      "STARTUP BYPASS"
    );
  }

  /*
   * Startup finished.
   */

  if (elapsed >= STARTUP_BYPASS_TIME)
  {
    Serial.println();
    Serial.println("90 SECOND STARTUP BYPASS COMPLETED");

    readWaterSensor();

    if (waterPresent)
    {
      currentState = RUNNING;

      if (Blynk.connected())
      {
        Blynk.virtualWrite(V1, "RUNNING");
        Blynk.virtualWrite(V2, "PRESENT");
        Blynk.virtualWrite(V4, "NORMAL");
        Blynk.virtualWrite(V7, 0);
      }

      Serial.println("Water detected. Pump RUNNING.");
    }
    else
    {
      /*
       * Water is absent after startup.
       *
       * Start the 5-second verification.
       */

      dryVerificationStarted = true;

      dryVerificationStartTime = millis();

      Serial.println(
        "Water absent after startup. "
        "Starting 5-second verification."
      );
    }
  }
}


/*************************************************************
 * RUNNING STATE HANDLER
 *************************************************************/

void handleRunning()
{
  /*
   * If water is present, everything is normal.
   */

  if (waterPresent)
  {
    dryVerificationStarted = false;

    if (Blynk.connected())
    {
      Blynk.virtualWrite(V1, "RUNNING");
      Blynk.virtualWrite(V2, "PRESENT");
      Blynk.virtualWrite(V4, "NORMAL");
      Blynk.virtualWrite(V7, 0);
    }

    return;
  }


  /*
   * Water is absent.
   *
   * Start verification timer.
   */

  if (!dryVerificationStarted)
  {
    dryVerificationStarted = true;

    dryVerificationStartTime = millis();

    Serial.println();
    Serial.println("LOW WATER DETECTED");
    Serial.println("Starting 5-second verification...");
  }


  /*
   * Check 5-second verification.
   */

  unsigned long verificationElapsed =
    millis() - dryVerificationStartTime;


  if (verificationElapsed >= DRY_VERIFY_TIME)
  {
    readWaterSensor();

    /*
     * Water returned.
     */

    if (waterPresent)
    {
      dryVerificationStarted = false;

      Serial.println(
        "Water returned during verification."
      );

      return;
    }

    /*
     * Water still absent.
     *
     * Generate warning.
     */

    if (!warningNotificationSent)
    {
      startDryRunWarning();
    }
  }
}


/*************************************************************
 * WATER WARNING STATE
 *************************************************************/

void handleWaterWarning()
{
  /*
   * Check whether water returned.
   */

  if (waterPresent)
  {
    Serial.println();
    Serial.println("WATER RESTORED");
    Serial.println("Cancelling dry-run warning.");

    currentState = RUNNING;

    dryVerificationStarted = false;

    warningNotificationSent = false;

    if (Blynk.connected())
    {
      Blynk.virtualWrite(V1, "RUNNING");
      Blynk.virtualWrite(V2, "PRESENT");
      Blynk.virtualWrite(V4, "WATER RESTORED");
      Blynk.virtualWrite(V6, "No fault");
      Blynk.virtualWrite(V7, 0);

      if (!waterRestoredNotificationSent)
      {
        Blynk.logEvent(
          "water_restored",
          "Water restored. Pump continues running."
        );

        waterRestoredNotificationSent = true;
      }
    }

    return;
  }


  /*
   * Water is still absent.
   *
   * Calculate response countdown.
   */

  unsigned long elapsed =
    millis() - responseStartTime;


  if (elapsed < USER_RESPONSE_TIME)
  {
    unsigned long remaining =
      USER_RESPONSE_TIME - elapsed;

    int secondsRemaining =
      remaining / 1000;

    if (Blynk.connected())
    {
      Blynk.virtualWrite(
        V7,
        secondsRemaining
      );

      Blynk.virtualWrite(
        V4,
        "WAITING FOR RESPONSE"
      );
    }

    return;
  }


  /*
   * User did not respond.
   *
   * Activate automatic protection.
   */

  if (!shutdownNotificationSent)
  {
    activateDryRunProtection();
  }
}


/*************************************************************
 * FAULT LOCK STATE
 *************************************************************/

void handleFaultLock()
{
  /*
   * Relay must remain OFF.
   */

  relayOff();

  if (Blynk.connected())
  {
    Blynk.virtualWrite(V1, "FAULT - PUMP OFF");
    Blynk.virtualWrite(V4, "FAULT LOCK");
    Blynk.virtualWrite(V6, "DRY-RUN FAULT");
    Blynk.virtualWrite(V9, 0);
  }

  /*
   * IMPORTANT:
   *
   * No automatic restart.
   *
   * User must press ON.
   */
}


/*************************************************************
 * RUNTIME UPDATE
 *************************************************************/

void updateRuntime()
{
  if (pumpStartTime == 0)
  {
    return;
  }

  unsigned long currentRuntime =
    millis() - pumpStartTime;

  unsigned long total =
    totalRuntime + currentRuntime;

  unsigned long totalSeconds =
    total / 1000;

  unsigned long hours =
    totalSeconds / 3600;

  unsigned long minutes =
    (totalSeconds % 3600) / 60;

  unsigned long seconds =
    totalSeconds % 60;

  char runtimeText[30];

  sprintf(
    runtimeText,
    "%02lu:%02lu:%02lu",
    hours,
    minutes,
    seconds
  );

  if (Blynk.connected())
  {
    Blynk.virtualWrite(
      V5,
      runtimeText
    );
  }
}


/*************************************************************
 * SEND SENSOR DATA TO BLYNK
 *************************************************************/

void updateDashboard()
{
  readWaterSensor();

  if (!Blynk.connected())
  {
    return;
  }

  /*
   * ADC
   */

  Blynk.virtualWrite(
    V3,
    waterADC
  );


  /*
   * Water status
   */

  if (waterPresent)
  {
    Blynk.virtualWrite(
      V2,
      "PRESENT"
    );
  }
  else
  {
    Blynk.virtualWrite(
      V2,
      "ABSENT"
    );
  }


  /*
   * Threshold
   */

  Blynk.virtualWrite(
    V10,
    WATER_THRESHOLD
  );


  /*
   * Relay
   */

  Blynk.virtualWrite(
    V9,
    isRelayOn() ? 1 : 0
  );


  /*
   * Wi-Fi
   */

  Blynk.virtualWrite(
    V8,
    WiFi.status() == WL_CONNECTED
      ? "CONNECTED"
      : "DISCONNECTED"
  );


  /*
   * Runtime
   */

  updateRuntime();
}


/*************************************************************
 * SYSTEM STATE DISPLAY
 *************************************************************/

void updateSystemState()
{
  if (!Blynk.connected())
  {
    return;
  }

  switch (currentState)
  {
    case SYSTEM_OFF:
      Blynk.virtualWrite(
        V11,
        "OFF"
      );
      break;

    case STARTING:
      Blynk.virtualWrite(
        V11,
        "STARTING"
      );
      break;

    case RUNNING:
      Blynk.virtualWrite(
        V11,
        "RUNNING"
      );
      break;

    case WATER_WARNING:
      Blynk.virtualWrite(
        V11,
        "WATER WARNING"
      );
      break;

    case FAULT_LOCK:
      Blynk.virtualWrite(
        V11,
        "FAULT LOCK"
      );
      break;
  }
}


/*************************************************************
 * BLYNK PUMP BUTTON
 *
 * V0:
 * 1 = ON
 * 0 = OFF
 *************************************************************/

BLYNK_WRITE(V0)
{
  int command = param.asInt();

  Serial.println();
  Serial.print("Blynk Pump Command: ");
  Serial.println(command);


  /******************** USER OFF ****************************/

  if (command == 0)
  {
    stopPump("Pump stopped manually from Blynk.");

    return;
  }


  /******************** USER ON ******************************/

  if (command == 1)
  {
    /*
     * Don't start another pump if already running.
     */

    if (
      currentState == STARTING ||
      currentState == RUNNING ||
      currentState == WATER_WARNING
    )
    {
      Serial.println(
        "Pump is already active."
      );

      return;
    }


    /*
     * If system was in FAULT_LOCK,
     * pressing ON creates a NEW start attempt.
     */

    if (currentState == FAULT_LOCK)
    {
      Serial.println(
        "Manual restart command received "
        "after dry-run fault."
      );

      /*
       * Reset fault indicators.
       */

      shutdownNotificationSent = false;
      warningNotificationSent = false;
      waterRestoredNotificationSent = false;
    }


    /*
     * Start pump.
     */

    startPump();
  }
}


/*************************************************************
 * BLYNK CONNECTED
 *************************************************************/

BLYNK_CONNECTED()
{
  Serial.println();
  Serial.println("==============================");
  Serial.println("BLYNK CONNECTED");
  Serial.println("==============================");

  /*
   * Synchronize pump command.
   */

  Blynk.syncVirtual(V0);

  /*
   * Send current configuration.
   */

  Blynk.virtualWrite(
    V10,
    WATER_THRESHOLD
  );

  Blynk.virtualWrite(
    V8,
    "CONNECTED"
  );
}


/*************************************************************
 * PERIODIC SENSOR TASK
 *************************************************************/

void sensorTask()
{
  /*
   * Read sensor.
   */

  readWaterSensor();


  /*
   * State machine.
   */

  switch (currentState)
  {
    case SYSTEM_OFF:

      relayOff();

      break;


    case STARTING:

      handleStartup();

      break;


    case RUNNING:

      handleRunning();

      break;


    case WATER_WARNING:

      handleWaterWarning();

      break;


    case FAULT_LOCK:

      handleFaultLock();

      break;
  }


  /*
   * Dashboard.
   */

  updateDashboard();

  updateSystemState();
}


/*************************************************************
 * SETUP
 *************************************************************/

void setup()
{
  Serial.begin(115200);

  delay(500);

  Serial.println();
  Serial.println();
  Serial.println("=========================================");
  Serial.println("BOREWELL PUMP PROTECTION SYSTEM");
  Serial.println("ESP32 + BLYNK");
  Serial.println("=========================================");


  /******************** GPIO *******************************/

  pinMode(
    RELAY_PIN,
    OUTPUT
  );

  pinMode(
    WATER_SENSOR_PIN,
    INPUT
  );


  /******************** SAFE RELAY STATE *******************/

  /*
   * ALWAYS start with relay OFF.
   */

  relayOff();


  /******************** ADC *******************************/

  /*
   * 12-bit ADC:
   *
   * 0 - 4095
   */

  analogReadResolution(12);

  /*
   * ESP32 ADC attenuation.
   *
   * This allows a wider input range.
   */

  analogSetPinAttenuation(
    WATER_SENSOR_PIN,
    ADC_11db
  );


  /******************** INITIAL SENSOR READ ***************/

  readWaterSensor();


  /******************** BLYNK *****************************/

  Blynk.begin(
    BLYNK_AUTH_TOKEN,
    ssid,
    pass
  );


  /******************** PERIODIC TASK *********************/

  /*
   * Sensor + state machine:
   * every 100 ms
   */

  timer.setInterval(
    100L,
    sensorTask
  );


  /*
   * Runtime:
   * every 1 second
   */

  timer.setInterval(
    1000L,
    updateRuntime
  );


  Serial.println();
  Serial.println("SYSTEM READY");
  Serial.println("Pump is OFF.");
  Serial.println("Waiting for Blynk ON command.");
}


/*************************************************************
 * MAIN LOOP
 *************************************************************/

void loop()
{
  /*
   * Blynk communication.
   */

  Blynk.run();


  /*
   * Timers.
   */

  timer.run();
}