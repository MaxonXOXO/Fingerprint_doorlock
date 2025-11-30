#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>

// Define your pins. D4 is a common choice on ESP8266. Adjust as needed.
#define RELAY_PIN D4    // Door lock relay (Active LOW assumed)

// Define SoftwareSerial pins. D2 = RX, D3 = TX. Connect:
// D2 -> Sensor TX
// D3 -> Sensor RX
SoftwareSerial fingerSerial(D2, D3); 
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerSerial);


// ========================================================
// ===================== SETUP ============================
// ========================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n--- Fingerprint Door Lock System ---");

  // Initialize Relay Pin
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);    // Relay OFF (Relay is HIGH when locked)

  // Initialize Fingerprint Sensor
  finger.begin(57600);
  delay(300);

  if (finger.verifyPassword()) {
    Serial.println("✔ Sensor connected");
    // Display stored templates and capacity
    finger.getTemplateCount();
    Serial.print("Templates stored: ");
    Serial.print(finger.templateCount);
    Serial.print(" / ");
    Serial.println(finger.capacity); // Correctly access capacity as a variable
  } else {
    Serial.println("❌ Sensor NOT found. Check wiring & power!");
    while (1); // Stop execution if sensor fails
  }

  Serial.println("Type 'enroll' to register a new finger.");
  Serial.println("Type 'clear' to delete all templates.");
}

// ========================================================
// ===================== MAIN LOOP ========================
// ========================================================
void loop() {

  // Handle Serial Commands (Enroll or Clear)
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "enroll") {
      // Find the next available ID and enroll
      uint16_t newID = getEmptyID();
      if (newID == 0) {
        Serial.println("❌ Database is full! Cannot enroll new finger.");
      } else {
        enrollFingerprint(newID);
      }
    } else if (cmd == "clear") {
      clearDatabase();
    }
  }

  // Normal fingerprint checking mode
  checkFingerprint();
  delay(100);
}


// ========================================================
// =================== HELPER FUNCTIONS ===================
// ========================================================

/**
 * Finds the next available ID slot for enrollment.
 * @return The next available ID (1 to finger.capacity), or 0 if full.
 */
uint16_t getEmptyID() {
  // Access capacity as a member variable (no parentheses)
  int max_capacity = finger.capacity; 
  
  // Get the current count of stored templates
  finger.getTemplateCount();

  if (finger.templateCount >= max_capacity) {
    return 0; // Database is full
  }
  
  // Return the next sequential ID
  return finger.templateCount + 1;
}

/**
 * Waits for the user to remove their finger from the sensor.
 */
void waitForFingerRemoval() {
  uint8_t p = 0;
  Serial.print("Waiting for finger removal");
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
    Serial.print(".");
    delay(300);
  }
  Serial.println("\nFinger removed.");
  delay(1000); 
}

/**
 * Clears the entire fingerprint database.
 */
void clearDatabase() {
  Serial.println("\nWARNING: Deleting all stored templates...");
  uint8_t p = finger.emptyDatabase();
  if (p == FINGERPRINT_OK) {
    Serial.println("🎉 Database cleared successfully!");
  } else {
    Serial.println("❌ Error clearing database.");
  }
}


// ========================================================
// =================== ENROLL FUNCTION ====================
// ========================================================

/**
 * Guides the user through the two-step enrollment process.
 * @param id The template ID to store the new fingerprint at.
 */
void enrollFingerprint(uint16_t id) {

  Serial.print("\n=== ENROLLMENT STARTED (ID: ");
  Serial.print(id);
  Serial.println(") ===");
  Serial.println("Place finger on the sensor for the FIRST time...");

  // -------- Step 1: Capture first image --------
  if (!captureImageWithTimeout(10)) {     
    Serial.println("❌ Timeout. Enrollment cancelled.");
    return;
  }

  int p = finger.image2Tz(1); // Load to character buffer 1
  if (p != FINGERPRINT_OK) {
    Serial.println("❌ Failed to convert first image (Poor quality or other error)");
    return;
  }

  Serial.println("✔ First image taken. Template loaded to Buffer 1.");
  waitForFingerRemoval();


  // -------- Step 2: Capture second image --------
  Serial.println("Place the SAME finger again for the SECOND time...");

  if (!captureImageWithTimeout(10)) {
    Serial.println("❌ Timeout. Enrollment cancelled.");
    return;
  }

  p = finger.image2Tz(2); // Load to character buffer 2
  if (p != FINGERPRINT_OK) {
    Serial.println("❌ Failed to convert second image (Poor quality or other error)");
    return;
  }
  Serial.println("✔ Second image taken. Template loaded to Buffer 2.");
  waitForFingerRemoval();

  // -------- Step 3: Create model (Compare buffers 1 & 2) --------
  Serial.println("Creating fingerprint model...");
  p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    Serial.println("✔ Templates matched and model created!");
  } else if (p == FINGERPRINT_ENROLLMISMATCH) {
    Serial.println("❌ Fingerprints did not match! Try to be consistent with placement.");
    return;
  } else {
    Serial.println("❌ Model creation failed.");
    return;
  }

  // -------- Step 4: Store fingerprint --------
  Serial.print("Storing final model at ID ");
  Serial.println(id);

  p = finger.storeModel(id);

  if (p == FINGERPRINT_OK) {
    Serial.println("🎉 Fingerprint saved successfully!");
  } else {
    Serial.print("❌ Error saving fingerprint. Code: ");
    Serial.println(p);
  }
}


// ========================================================
// ====== CAPTURE IMAGE WITH TIMEOUT (NO FREEZE MODE) =====
// ========================================================
/**
 * Tries to capture an image from the sensor within a timeout period.
 * @param timeoutSeconds The number of seconds to wait.
 * @return true if image is captured, false otherwise.
 */
bool captureImageWithTimeout(uint8_t timeoutSeconds) {

  uint8_t p;
  uint32_t start = millis();

  while (millis() - start < (uint32_t)timeoutSeconds * 1000) {

    p = finger.getImage();

    if (p == FINGERPRINT_OK) {
      return true;
    }

    delay(50); 
  }

  Serial.println("\n❌ Image capture timed out");
  return false;
}


// ========================================================
// ================= SEARCH & UNLOCK ======================
// ========================================================
/**
 * Checks for a finger on the sensor, compares it against stored templates,
 * and calls unlockDoor() if a match is found.
 */
void checkFingerprint() {

  uint8_t p = finger.getImage();
  // Exit immediately if no finger is found or an error occurs
  if (p != FINGERPRINT_OK) return;

  // FIX: Must load the image to a buffer (usually 1) before searching.
  p = finger.image2Tz(1); // Convert captured image to template and load into Buffer 1
  if (p != FINGERPRINT_OK) return;

  // Search the entire database for a match to the template in Buffer 1
  p = finger.fingerSearch();

  if (p == FINGERPRINT_OK) {
    // A match was found!
    Serial.print("\n✔ Authorized Finger Detected! ID: ");
    Serial.println(finger.fingerID); // The ID of the matched finger
    unlockDoor();
  } 
  // Optionally: Add logic for FINGERPRINT_NOTFOUND if you want feedback on every failed scan.
}


// ========================================================
// ==================== UNLOCK DOOR =======================
// ========================================================
/**
 * Activates the relay to unlock the door for a set period (10 seconds).
 */
void unlockDoor() {
  Serial.println("🔓 Door unlocked for 10 seconds...");

  digitalWrite(RELAY_PIN, LOW);    // Relay ON (assuming active LOW)
  delay(10000);
  digitalWrite(RELAY_PIN, HIGH);    // Relay OFF

  Serial.println("🔒 Door locked again.");
}