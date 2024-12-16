/**
 * Created by Hoonapps
 *
 * Email: didgns10@gmail.com
 *
 */

// library settings
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>

#if defined(ESP32) || defined(ARDUINO_RASPBERRY_PI_PICO_W) || defined(ARDUINO_GIGA) || defined(ARDUINO_OPTA)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#elif __has_include(<WiFiNINA.h>) || defined(ARDUINO_NANO_RP2040_CONNECT)
#include <WiFiNINA.h>
#elif __has_include(<WiFi101.h>)
#include <WiFi101.h>
#elif __has_include(<WiFiS3.h>) || defined(ARDUINO_UNOWIFIR4)
#include <WiFiS3.h>
#elif __has_include(<WiFiC3.h>) || defined(ARDUINO_PORTENTA_C33)
#include <WiFiC3.h>
#elif __has_include(<WiFi.h>)
#include <WiFi.h>
#endif

// api key, auth settings
#define API_KEY "Web_API_KEY"
#define USER_EMAIL "USER_EMAIL"
#define USER_PASSWORD "USER_PASSWORD"

// variable settings
String Version = "1.0.0";

String firebaseHost = "DATABASE_HOST_URL";
String firebasePath = "DATABASE_PATH_UTL"; //  /test/stream.json
String fireTestPath1 = "/test/stream/path1.json"

String firebaseAuth = "";          // Id Token
String firebaseRefreshToken = "";  // Refresh Token
unsigned long tokenExpiryTime = 0; // token exprirytime (ms)

unsigned long cnt_heartbeat = 0;
unsigned long cnt_error = 0;
unsigned long heartbeat = 0;

long transaction_oid = 0;

unsigned long ms = 0;

bool wm_nonblocking = false;

unsigned long lastReconnectAttempt = 0;
unsigned long lastStreamCheck = 0;

WiFiManager wm;
WiFiManagerParameter custom_field;
WiFiClientSecure authClient, streamClient;

// setup
void setup(){
  Serial.begin(115200);
  Serial.println("Version. " + Version);

  WiFi.mode(WIFI_STA); 

  //wm setup
  wmSetup();

  // auth, stream client setinsecure
  authClient.setInsecure();
  streamClient.setInsecure();

  // Firebase Auth and Stream setup
  firebaseAuthAndStreamSetup();
}

// loop
void loop(){
  if (wm_nonblocking) wm.process();  // avoid delays() in loop when non-blocking and other long running code

  //client logic loop
  streamClientLoop();

  //reset logic loop
  resetLoop();

  //refresh token
  refreshFirebaseTokenLoop();
}

// wifi manager setup
void wmSetup(){
  wm.setConnectRetries(3);

  if (wm_nonblocking) wm.setConfigPortalBlocking(false);

  int customFieldLength = 40;
  const char *custom_radio_str = "<br/><label for='customfieldid'>Custom Field Label</label><input type='radio' name='customfieldid' value='1' checked> One<br><input type='radio' name='customfieldid' value='2'> Two<br><input type='radio' name='customfieldid' value='3'> Three";
  new (&custom_field) WiFiManagerParameter(custom_radio_str);  // custom html input

  wm.addParameter(&custom_field);
  wm.setSaveParamsCallback(saveParamCallback);

  std::vector<const char *> menu = { "wifi", "info", "param", "sep", "restart", "exit" };
  wm.setMenu(menu);

  // set dark theme
  wm.setClass("invert");
  wm.setConfigPortalTimeout(120);  

  bool res;
  res = wm.autoConnect();  // auto generated AP name from chipid

  if (!res) {
    Serial.println("Failed to connect or hit timeout");
    ESP.restart();
  } else {
    //if you get here you have connected to the WiFi
    Serial.println("connected...WIFI :)");
  }

  while (WiFi.status() != WL_CONNECTED)
  {
      Serial.print(".");
      delay(300);
  }


  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  Serial.println("Initializing app...");
}

void firebaseAuthAndStreamSetup(){
  if (authenticateFirebase(USER_EMAIL, USER_PASSWORD)) {
      Serial.println("Firebase authentication successful!");
  } else {
      Serial.println("Firebase authentication failed!");
  }

  if (!firebaseAuth.isEmpty()) {
    //stream setup
    if (connectToFirebaseStream()) {
        Serial.println("Firebase stream connected!");
    } else {
        Serial.println("Failed to connect to Firebase stream.");
    }
  }
}

void streamClientLoop(){
  if (!streamClient.connected()) {
    Serial.println("Stream disconnected. Reconnecting...");
    if (!connectToFirebaseStream()) {
      Serial.println("Reconnect failed. Retrying...");
      delay(5000); // delay 5 seconds
    }
  } else {
    if (millis() - lastStreamCheck > 1000) { // 100ms 
      handleFirebaseStream();
      lastStreamCheck = millis();
    }

    if(millis() - ms > 10000){
      ms = millis();

      //heartbeat 
      if (putData(fireTestPath1.c_str(), cnt_heartbeat)) {
        cnt_heartbeat++;

        if (cnt_heartbeat > 2147483640) {  // Max value
        cnt_heartbeat = 100;
        cnt_led = 100;
      }
    }
  } 
}

void resetLoop(){
  if (cnt_error > 5 && cnt_heartbeat > 10) {
    cnt_error = 0;
    cnt_heartbeat = 0;
    ESP.restart();
  }
}

//check tokenExpiryTime and refresh token
void refreshFirebaseTokenLoop(){
  if (millis() > tokenExpiryTime - 60000) { // refresh 1 minute before expriation
    Serial.println("Refreshing ID Token...");
    if (!refreshFirebaseToken()) {
        Serial.println("Failed to refresh ID Token.");
        return;
    }
  }
}


String getParam(String name){
  //read parameter from server, for customhmtl input
  String value;
  if (wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  return value;
}

void saveParamCallback(){
  Serial.println("[CALLBACK] saveParamCallback fired");
  Serial.println("PARAM customfieldid = " + getParam("customfieldid"));
}

bool isEven(int number){
  return (number % 2) == 0;
}

// Firebase authetincate & get id token
bool authenticateFirebase(const char* email, const char* password){
    String authUrl = "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" + String(API_KEY);

    // create JSON Body 
    String requestBody;
    StaticJsonDocument<200> doc;
    doc["email"] = email;
    doc["password"] = password;
    doc["returnSecureToken"] = true;
    serializeJson(doc, requestBody);

    // Connect HTTPS
    authClient.setInsecure();
    if (!authClient.connect("identitytoolkit.googleapis.com", 443)) {
        Serial.println("Connection to Firebase Auth failed!");
        return false;
    }

    authClient.println("POST " + authUrl + " HTTP/1.1");
    authClient.println("Host: identitytoolkit.googleapis.com");
    authClient.println("Content-Type: application/json");
    authClient.println("Content-Length: " + String(requestBody.length()));
    authClient.println();
    authClient.print(requestBody);

    // set Response
    String response = "";
    while (authClient.connected()) {
        if (authClient.available()) {
            response = authClient.readString();
            break;
        }
    }

    // check HTTP Status Code
    if (!response.startsWith("HTTP/1.1 200")) {
        Serial.println("Non-200 HTTP response received:");
        return false;
    }

    // remove Header
    int bodyStartIndex = response.indexOf("\r\n\r\n");
    if (bodyStartIndex != -1) {
        response = response.substring(bodyStartIndex + 4);
    }

    // remove chunked data
    int chunkSizeEndIndex = response.indexOf("\r\n");
    if (chunkSizeEndIndex != -1) {
        response = response.substring(chunkSizeEndIndex + 2);
    }

    // Parsing JSON response
    StaticJsonDocument<500> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);
    if (error) {
        Serial.print("JSON parse error: ");
        Serial.println(error.c_str());
        return false;
    }

    if (responseDoc.containsKey("idToken") && responseDoc.containsKey("refreshToken")) {
        firebaseAuth = responseDoc["idToken"].as<String>();
        firebaseRefreshToken = responseDoc["refreshToken"].as<String>();
        tokenExpiryTime = millis() + (responseDoc["expiresIn"].as<unsigned long>() * 1000); 
        Serial.println("ID Token: " + firebaseAuth);
        Serial.println("Refresh Token: " + firebaseRefreshToken);
        return true;
    } else {
        Serial.println("Failed to authenticate. Response:");
        Serial.println(response);
        return false;
    }
}

// Firebase refresh token
bool refreshFirebaseToken(){
    String refreshUrl = "https://securetoken.googleapis.com/v1/token?key=" + String(API_KEY);

    // create JSON Body 
    String requestBody;
    StaticJsonDocument<200> doc;
    doc["grant_type"] = "refresh_token";
    doc["refresh_token"] = firebaseRefreshToken;
    serializeJson(doc, requestBody);

    // Connect HTTPS
    authClient.setInsecure();
    if (!authClient.connect("securetoken.googleapis.com", 443)) {
        Serial.println("Connection to Firebase Token Refresh failed!");
        return false;
    }

    authClient.println("POST " + refreshUrl + " HTTP/1.1");
    authClient.println("Host: securetoken.googleapis.com");
    authClient.println("Content-Type: application/json");
    authClient.println("Content-Length: " + String(requestBody.length()));
    authClient.println();
    authClient.print(requestBody);

    // check HTTP Status Code
    if (!response.startsWith("HTTP/1.1 200")) {
        Serial.println("Non-200 HTTP response received:");
        return false;
    }

    // remove Header
    int bodyStartIndex = response.indexOf("\r\n\r\n");
    if (bodyStartIndex != -1) {
        response = response.substring(bodyStartIndex + 4);
    }

    // remove chunked data
    int chunkSizeEndIndex = response.indexOf("\r\n");
    if (chunkSizeEndIndex != -1) {
        response = response.substring(chunkSizeEndIndex + 2);
    }

    // set Response
    String response = "";
    while (authClient.connected()) {
        if (authClient.available()) {
            response = authClient.readString();
            break;
        }
    }

    // Parsing JSON response
    StaticJsonDocument<500> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);
    if (error) {
        Serial.print("JSON parse error: ");
        Serial.println(error.c_str());
        return false;
    }

    if (responseDoc.containsKey("id_token")) {
        firebaseAuth = responseDoc["id_token"].as<String>();
        tokenExpiryTime = millis() + (responseDoc["expires_in"].as<unsigned long>() * 1000);
        Serial.println("Refreshed ID Token: " + firebaseAuth);
        return true;
    } else {
        Serial.println("Failed to refresh token. Response:");
        Serial.println(response);
        return false;
    }
}

bool connectToFirebaseStream(){
  if (millis() - lastReconnectAttempt < 5000) {
      return false;
  }

  lastReconnectAttempt = millis();

  // Create Firebase stream URL 
  String url = String("https://") + firebaseHost.c_str() + firebasePath + "?auth=" + firebaseAuth.c_str();

  // Connect Firebase
  Serial.print("Connecting to Firebase...");
  if (!streamClient.connect(firebaseHost.c_str(), 443)) {
      Serial.println("Connection failed!");
      return false;
  }
  Serial.println("Connected!");

  // Write HTTP streamClient
  streamClient.println("GET " + url + " HTTP/1.1");
  streamClient.println("Host: " + String(firebaseHost.c_str()));
  streamClient.println("Accept: text/event-stream");
  streamClient.println("Connection: keep-alive");
  streamClient.println();

  String locationHeader = ""; 
  while (streamClient.connected()) {
    if (streamClient.available()) {
      String line = streamClient.readStringUntil('\n');
      line.trim(); 

      Serial.print("Line Length: ");
      Serial.println(line.length());
      Serial.print("Raw Line: ");
      Serial.println(line);

      if (line.startsWith("Location: ")) {
          String extractedSubstring = line.substring(10);
          locationHeader = extractedSubstring; 
          locationHeader.trim(); 
          Serial.println("Location Header Found: " + locationHeader);
      }

      if (line == "") {
          break;
      }
    }
  }

  if (!locationHeader.isEmpty()) {
      Serial.println("Redirecting to: " + locationHeader);
      streamClient.stop();
      return connectToRedirectedStream(locationHeader);
  }

  Serial.println("Failed to receive HTTP headers.");
  return false;
}

bool connectToRedirectedStream(const String& newUrl){
  // Parsing URL
  String host, path;
  int pathIndex = newUrl.indexOf("/", 8);
  if (pathIndex > 0) {
    host = newUrl.substring(8, pathIndex);
    path = newUrl.substring(pathIndex);
  } else {
    Serial.println("Invalid redirect URL.");
    return false;
  }

  Serial.print("Connecting to redirected host: ");
  Serial.println(host);

  if (!streamClient.connect(host.c_str(), 443)) {
    Serial.println("Connection to redirected host failed!");
    return false;
  }

  Serial.println("Connected to redirected host!");

  streamClient.println("GET " + path + " HTTP/1.1");
  streamClient.println("Host: " + host);
  streamClient.println("Accept: text/event-stream");
  streamClient.println("Connection: keep-alive");
  streamClient.println();

  while (streamClient.connected()) {
    String line = streamClient.readStringUntil('\n');
    if (line == "\r") {
        Serial.println("HTTP headers received from redirected host.");

        if (putData(fireTestPath1.c_str(), 0)) {
            Serial.println("path1 successfully reset to 0.");
        } else {
            Serial.println("Failed to reset path1.");
        }

        return true;
    }
  }

  Serial.println("Failed to receive HTTP headers from redirected host.");
  return false;
}

void handleFirebaseStream(){
  while (streamClient.available()) {
    String line = streamClient.readStringUntil('\n');
    line.trim();

    // event
    if (line.startsWith("event:")) {
        String eventType = line.substring(6); 
        eventType.trim();
        Serial.println("Event: " + eventType);
    }

    // data
    if (line.startsWith("data:")) {
      String eventData = line.substring(5);
      eventData.trim();
      Serial.println("Data: " + eventData);

      if (eventData == "null") {
        Serial.println("Keep-alive received.");
        continue;
      }{
        handlePathAndData(eventData);
      }
    }
  }
}

// handle json data
void handlePathAndData(const String& eventData){
  StaticJsonDocument<512> doc;

  DeserializationError error = deserializeJson(doc, eventData);
  if (error) {
      Serial.print("JSON parse error: ");
      Serial.println(error.c_str());
      return;
  }

  if (doc.containsKey("path") && doc.containsKey("data")) {
    String path = doc["path"];
    int data = doc["data"];

    //check data
    Serial.printf("path: %s, data: %d \n", path, data)

  } else {
      Serial.println("Required keys (path, data) not found in JSON.");
  }
}

// Firebase PUT Data
bool putData(const String& path, int value){
  WiFiClientSecure putClient;
  putClient.setInsecure();

  String url = String("https://") + firebaseHost.c_str() + path + "?auth=" + firebaseAuth.c_str();

  Serial.print("Connecting to Firebase for PUT...");
  if (!putClient.connect(firebaseHost.c_str(), 443)) {
      Serial.println("Connection failed!");
      return false;
  }
  Serial.println("Connected!");

  String jsonPayload = String(value);

  // Write HTTP PUT 
  putClient.println("PUT " + url + " HTTP/1.1");
  putClient.println("Host: " + String(firebaseHost.c_str()));
  putClient.println("Content-Type: application/json");
  putClient.print("Content-Length: ");
  putClient.println(jsonPayload.length());
  putClient.println("Connection: close");
  putClient.println();
  putClient.println(jsonPayload);

  while (putClient.connected()) {
      String line2 = putClient.readStringUntil('\n');
      line2.trim();
      if (line2.startsWith("HTTP/1.1") && line2.indexOf("200")) {
          Serial.println("Response: " + line2);
          cnt_error = 0;
          return true;
      }
  }

  Serial.println("No response or failed to put data.");
  cnt_error++;
  return false;
}

