#include "secrets.h"

#include <WiFi.h>
#include <sstream>  
#include <ESP32Servo.h>
#include <Firebase_ESP_Client.h>

// Provide the token generation process info.
#include <addons/TokenHelper.h>

// Provide the RTDB payload printing info and other helper functions.
#include <addons/RTDBHelper.h>

#include <SPI.h>
#include <MFRC522.h>
#define PIR_ONE 22

#define TRIG_ONE 2
#define ECHO_ONE 4

#define TRIG_TWO 27
#define ECHO_TWO 14

long cm1=-1;
long cm2=-1;

#define RST_PIN         15          // Configurable, see typical pin layout above
#define SS_PIN          21          // Configurable, see typical pin layout above
#define SERVO_PIN 5
#define SERVO2_PIN 25
#define BUZZER_PIN 32

String ID;
Servo myservo;  // create servo object to control a servo
Servo myservo2;



int irOneVal = 0;
int irOneState = LOW;

long closeOneTimer = 0;
bool shouldCloseOne = false;
bool shouldCloseTwo = false;


void openDoor1(){
  myservo.write(0);
}
void closeDoor1(){
  myservo.write(180);
}

void openDoor2(){
  myservo2.write(0);
}

void closeDoor2(){
  myservo2.write(180);
}


MFRC522 mfrc522(SS_PIN, RST_PIN);   // Create MFRC522 instance.

MFRC522::MIFARE_Key key;

// Define Firebase Data object
FirebaseData fbdo, lockrs;
String lockrStates[2] = {"true","true"};

String lockrsPath = "/lockrs";
String individualLockrs[2] = {"/1/isClosed","/2/isClosed"};

volatile bool dataChanged= false;

long microsecondsToCentimeters(long microseconds) {
   return microseconds / 29 / 2;
}

void lockrStreamCallback(MultiPathStream stream){
  size_t numLockrs = sizeof(individualLockrs) / sizeof(individualLockrs[0]);
  Serial.println("Handling stream");
  //Serial.println(numLockrs);
  for(size_t i=0; i<numLockrs; i++){
    if (stream.get(individualLockrs[i])){
      if (dataChanged){
        Serial.println("Data already changed");
        return;
      }
      String currLockerState = stream.value;
      Serial.println("Stream Locker State:");
      Serial.println(currLockerState);
      Serial.println("Past Locker State: ");
      String pastLockerState = lockrStates[i];
      Serial.println(pastLockerState);
      if(currLockerState != pastLockerState){
        Serial.println("Changing door state!");
        //handle door 1 changing
        if(i==0){
          if(pastLockerState.compareTo("true") == 0){
            Serial.println("Opening door 1!");
            openDoor1();
          }
          else if(pastLockerState.compareTo("false") == 0) {
            Serial.println("Closing door 1!");
            closeDoor1();
            shouldCloseOne = false;
          }
        }
        //handle door 2 changing
        else if(i==1){
          if(pastLockerState.compareTo("true") == 0){
            Serial.println("Opening door 2!");
            openDoor2();
          }
          else if(pastLockerState.compareTo("false") == 0) {
            Serial.println("Closing door 2!");
            closeDoor2();
            shouldCloseTwo = false;
          }          
        }
        else {
          Serial.println("Past");
          Serial.println(pastLockerState);
        }
        lockrStates[i] = currLockerState;
        dataChanged = true;
      }
    }
  }
}

void lockrStreamTimeoutCallback(bool timeout)
{
  if (timeout)
    Serial.println("stream timed out, resuming...\n");

  if (!lockrs.httpConnected())
    Serial.printf("error code: %d, reason: %s\n\n", lockrs.httpCode(), lockrs.errorReason().c_str());
}

FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;

unsigned long count = 0;

String getID(){
  String userid;
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    userid += String(mfrc522.uid.uidByte[i], HEX);
  }
  return userid;
}


void setup()
{

  Serial.begin(115200);  

  //setting ultrasonics
  pinMode(TRIG_ONE, OUTPUT);
  pinMode(ECHO_ONE, INPUT);

  pinMode(BUZZER_PIN,OUTPUT);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION);

  /* Assign the api key (required) */
  config.api_key = API_KEY;

  /* Assign the user sign in credentials */
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  /* Assign the RTDB URL (required) */
  config.database_url = DATABASE_URL;

  /* Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h

  // Or use legacy authenticate method
  // config.database_url = DATABASE_URL;
  // config.signer.tokens.legacy_token = "<database secret>";

  // To connect without auth in Test Mode, see Authentications/TestMode/TestMode.ino

  //////////////////////////////////////////////////////////////////////////////////////////////
  // Please make sure the device free Heap is not lower than 80 k for ESP32 and 10 k for ESP8266,
  // otherwise the SSL connection will fail.
  //////////////////////////////////////////////////////////////////////////////////////////////

  Firebase.begin(&config, &auth);

  // Comment or pass false value when WiFi reconnection will control by your code or third party library
  Firebase.reconnectWiFi(true);

  if(!Firebase.RTDB.beginMultiPathStream(&lockrs, lockrsPath))
    Serial.printf("stream begin error, %s\n\n", lockrs.errorReason().c_str());
  
  //start the data stream
  Serial.println("Starting locker stream");
  Firebase.RTDB.setMultiPathStreamCallback(&lockrs, lockrStreamCallback, lockrStreamTimeoutCallback);

  Firebase.setDoubleDigits(5);

  Serial.println("Setting up RFID...");
    while (!Serial);    // Do nothing if no serial port is opened (added for Arduinos based on ATMEGA32U4)
    SPI.begin();        // Init SPI bus
    mfrc522.PCD_Init(); // Init MFRC522 card
    delay(50);
    Serial.println("MFRC Set up!");
  /** Timeout options.

  //WiFi reconnect timeout (interval) in ms (10 sec - 5 min) when WiFi disconnected.
  config.timeout.wifiReconnect = 10 * 1000;

  //Socket connection and SSL handshake timeout in ms (1 sec - 1 min).
  config.timeout.socketConnection = 10 * 1000;

  //Server response read timeout in ms (1 sec - 1 min).
  config.timeout.serverResponse = 10 * 1000;

  //RTDB Stream keep-alive timeout in ms (20 sec - 2 min) when no server's keep-alive event data received.
  config.timeout.rtdbKeepAlive = 45 * 1000;

  //RTDB Stream reconnect timeout (interval) in ms (1 sec - 1 min) when RTDB Stream closed and want to resume.
  config.timeout.rtdbStreamReconnect = 1 * 1000;

  //RTDB Stream error notification timeout (interval) in ms (3 sec - 30 sec). It determines how often the readStream
  //will return false (error) when it called repeatedly in loop.
  config.timeout.rtdbStreamError = 3 * 1000;

  Note:
  The function that starting the new TCP session i.e. first time server connection or previous session was closed, the function won't exit until the
  time of config.timeout.socketConnection.

  You can also set the TCP data sending retry with
  config.tcp_data_sending_retry = 1;

  */
 // Allow allocation of all timers
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  myservo.setPeriodHertz(50);    // standard 50 hz servo
  myservo2.setPeriodHertz(50);
  myservo2.attach(SERVO2_PIN, 1000, 2000);
  myservo.attach(SERVO_PIN, 1000, 2000); // attaches the servo on pin 18 to the servo object
  // using default min/max of 1000us and 2000us
  // different servos may require different min/max settings
  // for an accurate 0 to 180 sweep
}

void loop()
{
  count++;
  //ultrasonic distance readings if one of them is open
  if(lockrStates[0].compareTo("false")==0 || lockrStates[1].compareTo("false")==0 && !shouldCloseOne){
  digitalWrite(TRIG_ONE,LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_ONE,HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_ONE,LOW);
  long duration, dx1, newCM1;
  duration = pulseIn(ECHO_ONE,HIGH);

  newCM1 = microsecondsToCentimeters(duration);
  dx1= cm1 < 0 ? 0 : abs(newCM1 - cm1);
  Serial.println(dx1);
  // Serial.println(dx1);
  if(dx1>15){
    Serial.println("Hand detected!");
    shouldCloseOne=true;
    return;
  }
  cm1 = newCM1;
  //Serial.print("CM: ");
  //Serial.println(cm);
  }
  //else if you should close door one
  if(shouldCloseOne){
    if(closeOneTimer<200){
    Serial.println(closeOneTimer);
    closeOneTimer++;
    }
    else if(closeOneTimer>=200){
      Serial.printf("Closing door one... %s\n", Firebase.RTDB.setBool(&fbdo, F("/lockrs/1/isClosed"),true ) ? "ok" : fbdo.errorReason().c_str());
      closeOneTimer=0;
      shouldCloseOne=false;
    }
  }




  if (count >=500 && Firebase.ready() && mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      //Serial.println("Found a card!");
        // delay(100);
        // delay(75);
        ID = getID();
        Serial.println(ID);
        tone(BUZZER_PIN, 390);
        delay(200);
        noTone(BUZZER_PIN);
        tone(BUZZER_PIN, 523);
        delay(175);
        noTone(BUZZER_PIN);
        if (Firebase.RTDB.getString(&fbdo, FPSTR("/currCard"))) {
            Serial.println("User already logged in.");            
            return;
        }
        //else add them to the db
        else {
          Serial.printf("Set curr User... %s\n", Firebase.RTDB.setString(&fbdo, F("/currCard"), ID) ? "ok" : fbdo.errorReason().c_str());
        }
      count=0;
      Serial.println(count);
  }
  //   // For the usage of FirebaseJson, see examples/FirebaseJson/BasicUsage/Create_Edit_Parse.ino
  //   FirebaseJson json;

  //   if (count == 0)
  //   {
  //     json.set("value/round/" + String(count), F("cool!"));
  //     json.set(F("value/ts/.sv"), F("timestamp"));
  //     Serial.printf("Set json... %s\n", Firebase.RTDB.set(&fbdo, F("/test/json"), &json) ? "ok" : fbdo.errorReason().c_str());
  //   }
  //   else
  //   {
  //     json.add(String(count), F("smart!"));
  //     Serial.printf("Update node... %s\n", Firebase.RTDB.updateNode(&fbdo, F("/test/json/value/round"), &json) ? "ok" : fbdo.errorReason().c_str());
  //   }

  //   Serial.println();

  //   // For generic set/get functions.

  //   // For generic set, use Firebase.RTDB.set(&fbdo, <path>, <any variable or value>)

  //   // For generic get, use Firebase.RTDB.get(&fbdo, <path>).
  //   // And check its type with fbdo.dataType() or fbdo.dataTypeEnum() and
  //   // cast the value from it e.g. fbdo.to<int>(), fbdo.to<std::string>().

  //   // The function, fbdo.dataType() returns types String e.g. string, boolean,
  //   // int, float, double, json, array, blob, file and null.

  //   // The function, fbdo.dataTypeEnum() returns type enum (number) e.g. fb_esp_rtdb_data_type_null (1),
  //   // fb_esp_rtdb_data_type_integer, fb_esp_rtdb_data_type_float, fb_esp_rtdb_data_type_double,
  //   // fb_esp_rtdb_data_type_boolean, fb_esp_rtdb_data_type_string, fb_esp_rtdb_data_type_json,
  //   // fb_esp_rtdb_data_type_array, fb_esp_rtdb_data_type_blob, and fb_esp_rtdb_data_type_file (10)

  //   count++;
  // }
  if (dataChanged){
    dataChanged= false;
  }
  //delay(500);
}

/** NOTE:
 * When you trying to get boolean, integer and floating point number using getXXX from string, json
 * and array that stored on the database, the value will not set (unchanged) in the
 * FirebaseData object because of the request and data response type are mismatched.
 *
 * There is no error reported in this case, until you set this option to true
 * config.rtdb.data_type_stricted = true;
 *
 * In the case of unknown type of data to be retrieved, please use generic get function and cast its value to desired type like this
 *
 * Firebase.RTDB.get(&fbdo, "/path/to/node");
 *
 * float value = fbdo.to<float>();
 * String str = fbdo.to<String>();
 *
 */

/// PLEASE AVOID THIS ////

// Please avoid the following inappropriate and inefficient use cases
/**
 *
 * 1. Call get repeatedly inside the loop without the appropriate timing for execution provided e.g. millis() or conditional checking,
 * where delay should be avoided.
 *
 * Everytime get was called, the request header need to be sent to server which its size depends on the authentication method used,
 * and costs your data usage.
 *
 * Please use stream function instead for this use case.
 *
 * 2. Using the single FirebaseData object to call different type functions as above example without the appropriate
 * timing for execution provided in the loop i.e., repeatedly switching call between get and set functions.
 *
 * In addition to costs the data usage, the delay will be involved as the session needs to be closed and opened too often
 * due to the HTTP method (GET, PUT, POST, PATCH and DELETE) was changed in the incoming request.
 *
 *
 * Please reduce the use of swithing calls by store the multiple values to the JSON object and store it once on the database.
 *
 * Or calling continuously "set" or "setAsync" functions without "get" called in between, and calling get continuously without set
 * called in between.
 *
 * If you needed to call arbitrary "get" and "set" based on condition or event, use another FirebaseData object to avoid the session
 * closing and reopening.
 *
 * 3. Use of delay or hidden delay or blocking operation to wait for hardware ready in the third party sensor libraries, together with stream functions e.g. Firebase.RTDB.readStream and fbdo.streamAvailable in the loop.
 *
 * Please use non-blocking mode of sensor libraries (if available) or use millis instead of delay in your code.
 *
 * 4. Blocking the token generation process.
 *
 * Let the authentication token generation to run without blocking, the following code MUST BE AVOIDED.
 *
 * while (!Firebase.ready()) <---- Don't do this in while loop
 * {
 *     delay(1000);
 * }
 *
 */





