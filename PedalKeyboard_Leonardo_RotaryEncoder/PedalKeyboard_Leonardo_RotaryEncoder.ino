#include <hidboot.h>
#include <usbhub.h>
#include <Keyboard.h>
// Satisfy the IDE, which needs to see the include statment in the ino too. //This is from the usb host example, not sure what it does
#ifdef dobogusinclude
#include <spi4teensy3.h>
#endif
#include <SPI.h>
#include <EEPROM.h>
//------------------------------------------------------
//_____Variables_To_Change_____
// These are the defaults. They can also be tuned live (and saved to the Arduino) with ConfigApp/index.html
// Settings saved from the app are ignored if you change any of these defaults and re-upload
const long WALK_SPEED = 2000; // Speed at which walk is triggered
const long SPRINT_SPEED = 16000;//22000; // Speed at which sprint is triggered
const long STOP_SPRINT_SPEED = 9000;//6000 12000; // Speed must drop below this value to stop sprinting
const bool SPRINT_ON_PRESS = false; //True if pressing sprint makes you faster, false if it makes you slower (eg csgo pressing shift)
const bool MOVE_WHEN_NO_INPUT = true; //walk forwards and backwards if pedalling and no keys pressed
const bool REVERSE_FLIPS_KEYS = false; //When pedalling backwards, each movement key you hold presses its opposite (W<->S, A<->D)
const bool QUICK_STOP = true; //Stop moving as soon as you stop pedalling, instead of when the pedals stop spinning (eg cs2)
const long QUICK_STOP_DROP = 20; // Stopping is detected when the speed falls this % below the slowest point of the last pedal stroke
const bool COUNTER_STRAFE = true; // For a moment after the pedals let go of W/A/S/D, pressing W/A/S/D yourself works like a normal keyboard, so you can counter strafe
const long COUNTER_STRAFE_WINDOW = 300; // ms that moment lasts
const unsigned long COUNTER_STRAFE_MAX = 250; // ms. A key pressed in that moment is let go after this long at most, so you still have to pedal to move
const long SMOOTHING_TIME = 20; // ms. When you slow down, the speed used for move and sprint falls over about this long, so the slow point of each pedal stroke doesn't stop you. Speeding up is instant. 0 = off
const long STOP_MOVING_PERCENT = 75; // Once moving, keep moving until the speed drops below this % of WALK_SPEED, so it doesn't flicker on and off
const long MIN_KEY_HOLD = 50; // ms. Movement keys pressed by the pedals stay down at least this long...
const long MIN_KEY_RELEASE = 50; // ms. ...and stay up at least this long, so the game never sees inhumanly fast taps
const unsigned long OPPOSITE_KEY_GAP = 30; // ms. When switching W/S or A/D, the new key waits until the old one is let go plus this long (no Snap Tap style switching)
const unsigned long KEY_STAGGER_MIN = 10; // ms. Random gap between any two keys the pedals press or release,
const unsigned long KEY_STAGGER_MAX = 30; // so keys like Shift and W never change in the exact same instant
const uint8_t sprintKey = KEY_LEFT_SHIFT; 
//---------------------------------------

//_____Settings_Stuff_____
struct Settings {
  long walkSpeed;
  long sprintSpeed;
  long stopSprintSpeed;
  bool sprintOnPress;
  bool moveWhenNoInput;
  bool reverseFlipsKeys;
  bool quickStop;
  long quickStopDrop;
  bool counterStrafe;
  long counterStrafeWindow;
  long stopMovingPercent;
  long minKeyHold;
  long minKeyRelease;
  long smoothingTime;
};
const Settings DEFAULT_SETTINGS = {WALK_SPEED, SPRINT_SPEED, STOP_SPRINT_SPEED, SPRINT_ON_PRESS, MOVE_WHEN_NO_INPUT, REVERSE_FLIPS_KEYS,
                                   QUICK_STOP, QUICK_STOP_DROP, COUNTER_STRAFE, COUNTER_STRAFE_WINDOW, STOP_MOVING_PERCENT, MIN_KEY_HOLD, MIN_KEY_RELEASE, SMOOTHING_TIME};
const long MIN_KEY_HOLD_FLOOR = 50; // The config app can't set the key times below these, to keep inputs human looking for anticheat
const long MIN_KEY_RELEASE_FLOOR = 30;
Settings settings = DEFAULT_SETTINGS; // Values actually in use

// Layout of the settings saved in EEPROM. The defaults are saved too so changing them in code invalidates the saved settings
const uint16_t SETTINGS_MAGIC = 0x504E; // Change this if the layout of SavedSettings changes
struct SavedSettings {
  uint16_t magic;
  Settings defaults;
  Settings values;
};
bool settingsSaved = true; // False if settings were changed from the app but not saved yet

char commandBuffer[40]; // Serial commands from the config app, one per line
uint8_t commandLength = 0;


//_____Keyboard_codes_____
const uint8_t EscapeCode = 0x29;
const uint8_t BackspaceCode = 0x2A;
const uint8_t TabCode = 0x2B;
const uint8_t EnterCode = 0x28;
const uint8_t RightArrowCode = 0x4F;
const uint8_t LeftArrowCode = 0x50;
const uint8_t DownArrowCode = 0x51;
const uint8_t UpArrowCode = 0x52;
const uint8_t CapsLockCode = 0x39;
const uint8_t PageUpCode = 0x49;
const uint8_t PageDownCode = 0x4A;
const uint8_t EndCode = 0x4B;
const uint8_t HomeCode = 0x4C;
//Add additional codes here as needed

//_____Rotary_Encoder_Stuff_____
#include <Arduino.h>
int encoder_Pin_1 = 2;
int encoder_Pin_2 = 3;
volatile int lastEncoded = 0;
volatile long encoderValue = 0;
long lastEncoderValue = 0;
unsigned long lastTime = 0; // Last time the speed was calculated
float speed = 0;            // Speed of rotation
float movementSpeed = 0;    // Smoothed speed, used for the move and sprint thresholds
bool pedalForward = true;      // Direction of rotation, true if pedalling forwards


// movement keys wasd
// Define the keycodes for WASD keys
const uint8_t WKey = 0x1A; // Keycode for 'W'
const uint8_t AKey = 0x04; // Keycode for 'A'
const uint8_t SKey = 0x16; // Keycode for 'S'
const uint8_t DKey = 0x07; // Keycode for 'D'
bool wPressed = false;
bool aPressed = false;
bool sPressed = false;
bool dPressed = false;
bool crouched = false;
bool moving = false;          // True while pedalling fast enough to move (see settings.stopMovingPercent)
bool movingForward = true;    // Direction of movement, changes once the pedals clearly turn the other way

// Movement and sprint keys held down by the pedals
struct PedalKey {
  uint8_t key;
  bool held;                  // True while the pedals are holding this key down
  unsigned long lastWanted;   // Last time the pedals wanted it down
  unsigned long changedAt;    // When the pedals last pressed or released it
  bool wanted;                // Set by movementInputs each loop
  PedalKey* opposite;         // W<->S, A<->D, set in setup
  bool manual;                // True while you're pressing it yourself for a counter strafe
  unsigned long manualUntil;  // When that press gets let go at the latest
};
PedalKey pedalW = {'w', false, 0, 0, false, NULL, false, 0};
PedalKey pedalA = {'a', false, 0, 0, false, NULL, false, 0};
PedalKey pedalS = {'s', false, 0, 0, false, NULL, false, 0};
PedalKey pedalD = {'d', false, 0, 0, false, NULL, false, 0};
PedalKey pedalSprint = {sprintKey, false, 0, 0, false, NULL, false, 0};
PedalKey* const pedalKeys[] = {&pedalSprint, &pedalW, &pedalA, &pedalS, &pedalD};
const uint8_t pedalKeyCount = sizeof(pedalKeys) / sizeof(pedalKeys[0]);
unsigned long lastPedalKeyChange = 0; // When the pedals last pressed or released any key
unsigned long pedalKeyStagger = 0;    // Random gap needed before the next change
unsigned long lastPedalMoveTime = 0;  // Last time the pedals were holding W/A/S/D

bool pedalHolds(uint8_t key){
  return (key == WKey && pedalW.held) || (key == AKey && pedalA.held) || (key == SKey && pedalS.held) || (key == DKey && pedalD.held);
}

// Manual counter strafe: just after you stop pedalling, W/A/S/D you press yourself go through at their real length
// (the timing is all yours, so it isn't input automation). Returns true if it handled the key
bool counterStrafeKeyDown(uint8_t key){
  bool pedalsMoving = pedalW.held || pedalA.held || pedalS.held || pedalD.held;
  if (pedalsMoving){
    return true; // movementInputs presses it for the pedals, a short tap here would just blip
  }
  if (!settings.counterStrafe || lastPedalMoveTime == 0
      || millis() - lastPedalMoveTime > (unsigned long)settings.counterStrafeWindow){
    return false;
  }
  PedalKey &pedalKey = (key == WKey) ? pedalW : (key == AKey) ? pedalA : (key == SKey) ? pedalS : pedalD;
  pedalKey.manual = true;
  pedalKey.manualUntil = millis() + COUNTER_STRAFE_MAX;
  Keyboard.press(pedalKey.key);
  return true;
}

bool counterStrafeKeyUp(uint8_t key){
  PedalKey &pedalKey = (key == WKey) ? pedalW : (key == AKey) ? pedalA : (key == SKey) ? pedalS : pedalD;
  if (!pedalKey.manual){
    return false;
  }
  pedalKey.manual = false;
  if (!pedalKey.held){
    Keyboard.release(pedalKey.key);
  }
  return true;
}

//_____Quick_Stop_Stuff_____
const float SPEED_SMOOTHING = 0.3; // 0-1, lower smooths the speed more before looking for a stop
const float TURN_MARGIN = 0.05; // Speed must move this fraction away from a peak/low to count as a change of direction
const long TURN_NOISE = 300; // ...plus this many pulses/sec, to ignore encoder noise at low speeds
const long SMOOTHING_RISE_TIME = 20; // ms. Speeding up only gets this much smoothing, to ignore encoder noise
const uint8_t REVERSE_CONFIRM_SAMPLES = 6; // Pedalling the other way for this many speed updates (5 ms each) switches direction, without stopping first
uint8_t reversedSamples = 0;
const unsigned long STROKE_TIMEOUT = 1500; // ms. If no strokes are seen for this long (very smooth pedalling), compare to the top speed instead
float smoothSpeed = 0;
bool speedFalling = false;
float speedExtreme = 0;       // Highest speed since it started rising, or lowest since it started falling
float lastStrokeLow = 0;      // Slowest point between the last two pedal pushes
unsigned long lastStrokeTime = 0;
float fallPeak = 0;           // Speed when it started falling
bool quickStopped = false;    // True while the pedals are only spinning from momentum
bool waitForPush = false;     // True after you counter strafe, so coasting pedals don't start moving you forwards again

//_____Keyboard_Stuff_____
const unsigned long timerInterval = 5; // Speed update every timerInterval ms
const unsigned long unpressInterval = 2 * timerInterval; // Allows smooth movement if its bigger than timer interval


class KbdRptParser : public KeyboardReportParser
{
    void PrintKey(uint8_t mod, uint8_t key);

  protected:
    void OnControlKeysChanged(uint8_t before, uint8_t after);
    void OnSpecialKeysChanged(uint8_t key, bool pressed);
    void OnKeyDown	(uint8_t mod, uint8_t key);
    void OnKeyUp	(uint8_t mod, uint8_t key);
    void showASCII(uint8_t key);
};

/* //PrintKey is unused
void KbdRptParser::PrintKey(uint8_t m, uint8_t key)
{
  MODIFIERKEYS mod;
  *((uint8_t*)&mod) = m;
  Serial.print((mod.bmLeftCtrl   == 1) ? "C" : " ");
  Serial.print((mod.bmLeftShift  == 1) ? "S" : " ");
  Serial.print((mod.bmLeftAlt    == 1) ? "A" : " ");
  Serial.print((mod.bmLeftGUI    == 1) ? "G" : " ");

  Serial.print(F(" >"));
  PrintHex<uint8_t>(key, 0x80);
  Serial.print(F("< "));

  Serial.print((mod.bmRightCtrl   == 1) ? "C" : " ");
  Serial.print((mod.bmRightShift  == 1) ? "S" : " ");
  Serial.print((mod.bmRightAlt    == 1) ? "A" : " ");
  Serial.print((mod.bmRightGUI    == 1) ? "G" : " ");
};
*/

void KbdRptParser::OnKeyDown(uint8_t mod, uint8_t key)
{
  //Serial.print(F("DN "));
  //PrintKey(mod, key);
  switch (key){
        case WKey:
          Serial.println(F("W pressed override for movement"));
          wPressed = true;
          break;
        case AKey:
          Serial.println(F("A pressed override for movement"));
          aPressed = true;
          break;
        case SKey:
          Serial.println(F("S pressed override for movement"));
          sPressed = true;
          break;
        case DKey:
          Serial.println(F("D pressed override for movement"));
          dPressed = true;
          break;
  }
  if ((key == WKey || key == AKey || key == SKey || key == DKey) && counterStrafeKeyDown(key)){
    return;
  }
  uint8_t c = OemToAscii(mod, key);
  if (key == EnterCode){
    Serial.println(F("ENTER OVERRIDE")); //Enter key was being picked up as an ascii one
    OnSpecialKeysChanged(key, true);
  } else{
    if (c){
      showASCII(c);
      Serial.println(F(" pressed"));
      Keyboard.press(c);
  } else{
      OnSpecialKeysChanged(key, true);
  }
  //Serial.println(F("pressed"));
  //Serial.println(key);
  }
}

void KbdRptParser::OnKeyUp(uint8_t mod, uint8_t key){
  //Serial.print(F("keypress "));
  //PrintKey(mod, key);
  switch (key){
        case WKey:
          Serial.println(F("W unpressed ovveride for movement"));
          wPressed = false;
          break;
        case AKey:
          Serial.println(F("A unpressed ovveride for movement"));
          aPressed = false;
          break;
        case SKey:
          Serial.println(F("S unpressed ovveride for movement"));
          sPressed = false;
          break;
        case DKey:
          Serial.println(F("D unpressed ovveride for movement"));
          dPressed = false;
          break;
  }
  if ((key == WKey || key == AKey || key == SKey || key == DKey) && counterStrafeKeyUp(key)){
    return;
  }
  if (pedalHolds(key)){
    return; // The pedals are still holding it, releasing it here would make it blip off and on
  }
  uint8_t c = OemToAscii(mod, key);
  if (key == EnterCode){
    Serial.println(F("ENTER OVERRIDE")); //Enter key was being picked up as an ascii one
    OnSpecialKeysChanged(key, false);
  } else{
    if (c){
      showASCII(c);
      Serial.println(F(" released"));
      Keyboard.release(c);
  } else{
    OnSpecialKeysChanged(key, false);
  }
  //Serial.println(F("released"));
  //Serial.println(key);
  }
}

void KbdRptParser::OnControlKeysChanged(uint8_t before, uint8_t after) {
    MODIFIERKEYS beforeMod;
    *((uint8_t*)&beforeMod) = before;

    MODIFIERKEYS afterMod;
    *((uint8_t*)&afterMod) = after;

    // Check each modifier key for press and release
    if (beforeMod.bmLeftCtrl == 0 && afterMod.bmLeftCtrl == 1) {
        Serial.println(F("LeftCtrl pressed"));
        Keyboard.press(KEY_LEFT_CTRL);
        crouched = true;

    }
    if (beforeMod.bmLeftCtrl == 1 && afterMod.bmLeftCtrl == 0) {
        Serial.println(F("LeftCtrl released"));
        Keyboard.release(KEY_LEFT_CTRL);
        crouched = false;
    }

    if (beforeMod.bmLeftShift == 0 && afterMod.bmLeftShift == 1) {
        Serial.println(F("LeftShift pressed"));
        Keyboard.press(KEY_LEFT_SHIFT);
    }
    if (beforeMod.bmLeftShift == 1 && afterMod.bmLeftShift == 0) {
        Serial.println(F("LeftShift released"));
        if (!(sprintKey == KEY_LEFT_SHIFT && pedalSprint.held)){ // Don't blip it if the pedals are holding it
          Keyboard.release(KEY_LEFT_SHIFT);
        }
    }

    if (beforeMod.bmLeftAlt == 0 && afterMod.bmLeftAlt == 1) {
        Serial.println(F("LeftAlt pressed"));
        Keyboard.press(KEY_LEFT_ALT);
    }
    if (beforeMod.bmLeftAlt == 1 && afterMod.bmLeftAlt == 0) {
        Serial.println(F("LeftAlt released"));
        Keyboard.release(KEY_LEFT_ALT);
    }

    if (beforeMod.bmLeftGUI == 0 && afterMod.bmLeftGUI == 1) {
        Serial.println(F("LeftGUI pressed"));
        Keyboard.press(KEY_LEFT_GUI);
    }
    if (beforeMod.bmLeftGUI == 1 && afterMod.bmLeftGUI == 0) {
        Serial.println(F("LeftGUI released"));
        Keyboard.release(KEY_LEFT_GUI);
    }

    if (beforeMod.bmRightCtrl == 0 && afterMod.bmRightCtrl == 1) {
        Serial.println(F("RightCtrl pressed"));
        Keyboard.press(KEY_RIGHT_CTRL);
    }
    if (beforeMod.bmRightCtrl == 1 && afterMod.bmRightCtrl == 0) {
        Serial.println(F("RightCtrl released"));
        Keyboard.release(KEY_RIGHT_CTRL);
    }

    if (beforeMod.bmRightShift == 0 && afterMod.bmRightShift == 1) {
        Serial.println(F("RightShift pressed"));
        Keyboard.press(KEY_RIGHT_SHIFT);
    }
    if (beforeMod.bmRightShift == 1 && afterMod.bmRightShift == 0) {
        Serial.println(F("RightShift released"));
        Keyboard.release(KEY_RIGHT_SHIFT);
    }

    if (beforeMod.bmRightAlt == 0 && afterMod.bmRightAlt == 1) {
        Serial.println(F("RightAlt pressed"));
        Keyboard.press(KEY_RIGHT_ALT);
    }
    if (beforeMod.bmRightAlt == 1 && afterMod.bmRightAlt == 0) {
        Serial.println(F("RightAlt released"));
        Keyboard.release(KEY_RIGHT_ALT);
    }

    if (beforeMod.bmRightGUI == 0 && afterMod.bmRightGUI == 1) { //Windows key
        Serial.println(F("RightGUI pressed"));
        Keyboard.press(KEY_RIGHT_GUI);
    }
    if (beforeMod.bmRightGUI == 1 && afterMod.bmRightGUI == 0) {
        Serial.println(F("RightGUI released"));
        Keyboard.release(KEY_RIGHT_GUI);
    }
}

void KbdRptParser::OnSpecialKeysChanged(uint8_t key, bool pressed) { //CAPS LOCK APPEARS TO BE ALREADY HANDLED?
    // Define special keys' HID usage codes

    if (pressed){ // True if press, False if release
      //Serial.print(F("pressed"));
          // Never used a switch case, be nice or I will return us all to IF ELSE purgatory
      switch (key){
        case EscapeCode:
          Serial.println(F("ESCAPE KEY PRESSED"));
          Keyboard.press(KEY_ESC);
          break;
      
        case BackspaceCode:
          Serial.println(F("BACKSPACE KEY PRESSED"));
          Keyboard.press(KEY_BACKSPACE);
          break;

        case TabCode:
          Serial.println(F("Tab KEY PRESSED"));
          Keyboard.press(KEY_TAB);
          break;
        
        case EnterCode:
          Serial.println(F("Enter KEY PRESSED"));
          Keyboard.press(KEY_RETURN);
          break;
        
        case RightArrowCode:
          Serial.println(F("Right Arrow KEY PRESSED"));
          Keyboard.press(KEY_RIGHT_ARROW);
          break;

        case LeftArrowCode:
          Serial.println(F("Left Arrow KEY PRESSED"));
          Keyboard.press(KEY_LEFT_ARROW);
          break;

        case DownArrowCode:
          Serial.println(F("Down Arrow KEY PRESSED"));
          Keyboard.press(KEY_DOWN_ARROW);
          break;
        
        case UpArrowCode:
          Serial.println(F("Up Arrow KEY PRESSED"));
          Keyboard.press(KEY_UP_ARROW);
          break;

        case CapsLockCode:
          Serial.println(F("Caps Lock KEY PRESSED"));
          //Keyboard.press(KEY_CAPS_LOCK);
          break;
        
        case PageUpCode:
          Serial.println(F("PageUp KEY PRESSED"));
          Keyboard.press(KEY_PAGE_UP);
          break;
        
        case PageDownCode:
          Serial.println(F("Page Down KEY PRESSED"));
          Keyboard.press(KEY_PAGE_DOWN);
          break;
        
        case EndCode:
          Serial.println(F("End KEY PRESSED"));
          Keyboard.press(KEY_END);
          break;
        
        case HomeCode:
          Serial.println(F("Home KEY PRESSED"));
          Keyboard.press(KEY_HOME);
          break;

        default:
          Serial.print(F("Unknown keypress, please define above keycode 0x: ")); //may be incorrect
          Serial.print(key, HEX);
          Serial.println(F(""));
          break;
    }
    } else {
      //Serial.print(F("unpressed"));
      switch (key){
        case EscapeCode:
          Serial.println(F("ESCAPE KEY UNPRESSED"));
          Keyboard.release(KEY_ESC);
          break;
      
        case BackspaceCode:
          Serial.println(F("BACKSPACE KEY UNPRESSED"));
          Keyboard.release(KEY_BACKSPACE);
          break;

        case TabCode:
          Serial.println(F("Tab KEY UNPRESSED"));
          Keyboard.release(KEY_TAB);
          break;
        
        case EnterCode:
          Serial.println(F("Enter KEY UNPRESSED"));
          Keyboard.release(KEY_RETURN);
          break;
        
        case RightArrowCode:
          Serial.println(F("Right Arrow KEY UNPRESSED"));
          Keyboard.release(KEY_RIGHT_ARROW);
          break;

        case LeftArrowCode:
          Serial.println(F("Left Arrow KEY UNPRESSED"));
          Keyboard.release(KEY_LEFT_ARROW);
          break;

        case DownArrowCode:
          Serial.println(F("Down Arrow KEY UNPRESSED"));
          Keyboard.release(KEY_DOWN_ARROW);
          break;
        
        case UpArrowCode:
          Serial.println(F("Up Arrow KEY UNPRESSED"));
          Keyboard.release(KEY_UP_ARROW);
          break;

        case CapsLockCode:
          Serial.println(F("Caps Lock KEY UNPRESSED"));
          //Keyboard.release(KEY_CAPS_LOCK);
          break;
        
        case PageUpCode:
          Serial.println(F("PageUp KEY UNPRESSED"));
          Keyboard.release(KEY_PAGE_UP);
          break;
        
        case PageDownCode:
          Serial.println(F("Page Down KEY UNPRESSED"));
          Keyboard.release(KEY_PAGE_DOWN);
          break;
        
        case EndCode:
          Serial.println(F("End KEY UNPRESSED"));
          Keyboard.release(KEY_END);
          break;
        
        case HomeCode:
          Serial.println(F("Home KEY UNPRESSED"));
          Keyboard.release(KEY_HOME);
          break;

        default:
          Serial.print(F("Unknown keypress, please define above keycode 0x: ")); //may be incorrect
          Serial.print(key, HEX);
          Serial.println(F(""));
          break;
      }
    }
}

void KbdRptParser::showASCII(uint8_t key)
{
  Serial.print(F("ASCII: "));
  Serial.print((char)key);
};

USB     Usb;
// USBHub     Hub(&Usb);  //In example code this is commented out, some report removing the comment made the code work?
HIDBoot<USB_HID_PROTOCOL_KEYBOARD>    HidKeyboard(&Usb);

KbdRptParser Prs;

void setup(){
  Serial.begin( 115200 );
  //_____USB_HOST_STUFF_____
  #if !defined(__MIPSEL__)
    //while (!Serial); // Wait for serial port to connect - used on Leonardo, Teensy and other boards with built-in USB CDC serial connection
    delay(1);
  #endif
  Serial.println(F("Start"));
  if (Usb.Init() == -1)
    Serial.println(F("OSC did not start."));
  delay( 200 );
  HidKeyboard.SetReportParser(0, &Prs);
  
  //_____Leonardo_Keyboard_Stuff_____
  Keyboard.begin();

  //_____Rotary_Encoder_Stuff_____
  pinMode(encoder_Pin_1, INPUT);
  pinMode(encoder_Pin_2, INPUT);
  digitalWrite(encoder_Pin_1, HIGH); // turn pullup resistor on
  digitalWrite(encoder_Pin_2, HIGH); // turn pullup resistor on
  attachInterrupt(digitalPinToInterrupt(encoder_Pin_1), updateEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(encoder_Pin_2), updateEncoder, CHANGE);
  lastTime = millis();
  randomSeed(analogRead(A0) ^ micros()); // A0 is unconnected, so it reads noise
  pedalW.opposite = &pedalS;
  pedalS.opposite = &pedalW;
  pedalA.opposite = &pedalD;
  pedalD.opposite = &pedalA;

  loadSettings();

  Keyboard.releaseAll(); // Sometimes unplugging/restarting can get keys stuck pressed
}

//_____Config_App_Stuff_____
void loadSettings(){
  SavedSettings saved;
  EEPROM.get(0, saved);
  if (saved.magic == SETTINGS_MAGIC && memcmp(&saved.defaults, &DEFAULT_SETTINGS, sizeof(Settings)) == 0){
    settings = saved.values;
  }
}

void saveSettings(){
  SavedSettings saved = {SETTINGS_MAGIC, DEFAULT_SETTINGS, settings};
  EEPROM.put(0, saved); // Only writes bytes that changed
  settingsSaved = true;
}

void resetSettings(){
  settings = DEFAULT_SETTINGS;
  EEPROM.put(0, (uint16_t)0); // Forget the saved settings
  settingsSaved = true;
}

void printSettings(){ // e.g. CFG walk=2000 sprint=16000 stop=9000 sprintOnPress=0 moveWhenNoInput=1 saved=1
  Serial.print(F("CFG walk=")); Serial.print(settings.walkSpeed);
  Serial.print(F(" sprint=")); Serial.print(settings.sprintSpeed);
  Serial.print(F(" stop=")); Serial.print(settings.stopSprintSpeed);
  Serial.print(F(" sprintOnPress=")); Serial.print(settings.sprintOnPress);
  Serial.print(F(" moveWhenNoInput=")); Serial.print(settings.moveWhenNoInput);
  Serial.print(F(" reverseFlip=")); Serial.print(settings.reverseFlipsKeys);
  Serial.print(F(" quickStop=")); Serial.print(settings.quickStop);
  Serial.print(F(" quickStopDrop=")); Serial.print(settings.quickStopDrop);
  Serial.print(F(" counterStrafe=")); Serial.print(settings.counterStrafe);
  Serial.print(F(" counterStrafeWindow=")); Serial.print(settings.counterStrafeWindow);
  Serial.print(F(" stopMoving=")); Serial.print(settings.stopMovingPercent);
  Serial.print(F(" minHold=")); Serial.print(settings.minKeyHold);
  Serial.print(F(" minRelease=")); Serial.print(settings.minKeyRelease);
  Serial.print(F(" smoothing=")); Serial.print(settings.smoothingTime);
  Serial.print(F(" saved=")); Serial.println(settingsSaved);
}

bool applySetting(const char* name, long value){
  if (value < 0) value = 0;
  if (strcmp(name, "walk") == 0) settings.walkSpeed = value;
  else if (strcmp(name, "sprint") == 0) settings.sprintSpeed = value;
  else if (strcmp(name, "stop") == 0) settings.stopSprintSpeed = value;
  else if (strcmp(name, "sprintOnPress") == 0){
    if (settings.sprintOnPress != (value != 0)){
      Keyboard.release(sprintKey); // Don't leave it held from the old mode
    }
    settings.sprintOnPress = value != 0;
  }
  else if (strcmp(name, "moveWhenNoInput") == 0) settings.moveWhenNoInput = value != 0;
  else if (strcmp(name, "reverseFlip") == 0) settings.reverseFlipsKeys = value != 0;
  else if (strcmp(name, "quickStop") == 0){
    settings.quickStop = value != 0;
    quickStopped = false;
  }
  else if (strcmp(name, "quickStopDrop") == 0) settings.quickStopDrop = min(value, 100L);
  else if (strcmp(name, "counterStrafe") == 0) settings.counterStrafe = value != 0;
  else if (strcmp(name, "counterStrafeWindow") == 0) settings.counterStrafeWindow = min(value, 2000L);
  else if (strcmp(name, "stopMoving") == 0) settings.stopMovingPercent = min(value, 100L);
  else if (strcmp(name, "minHold") == 0) settings.minKeyHold = max(value, MIN_KEY_HOLD_FLOOR);
  else if (strcmp(name, "minRelease") == 0) settings.minKeyRelease = max(value, MIN_KEY_RELEASE_FLOOR);
  else if (strcmp(name, "smoothing") == 0) settings.smoothingTime = min(value, 1000L);
  else return false;
  settingsSaved = false;
  return true;
}

// Commands (one per line): GET, SET <name>=<value>, SAVE, RESET. Each replies with a CFG line, or ERR
void handleCommand(char* command){
  if (strcmp(command, "GET") == 0){
    // Just print the settings below
  } else if (strncmp(command, "SET ", 4) == 0){
    char* equals = strchr(command, '=');
    if (equals == NULL){
      Serial.println(F("ERR expected SET name=value"));
      return;
    }
    *equals = '\0';
    if (!applySetting(command + 4, atol(equals + 1))){
      Serial.println(F("ERR unknown setting"));
      return;
    }
  } else if (strcmp(command, "SAVE") == 0){
    saveSettings();
  } else if (strcmp(command, "RESET") == 0){
    resetSettings();
  } else{
    Serial.println(F("ERR unknown command"));
    return;
  }
  printSettings();
}

void readSerialCommands(){
  while (Serial.available() > 0){
    char c = Serial.read();
    if (c == '\n' || c == '\r'){
      if (commandLength > 0){
        commandBuffer[commandLength] = '\0';
        handleCommand(commandBuffer);
        commandLength = 0;
      }
    } else if (commandLength < sizeof(commandBuffer) - 1){
      commandBuffer[commandLength++] = c;
    }
  }
}

void updateSpeedDirection(){
  // Calculate speed and direction every 100 ms
  unsigned long currentTime = millis();
  if (currentTime - lastTime >= timerInterval)
  {
    noInterrupts(); // encoderValue is 4 bytes, so the encoder interrupt could change it halfway through reading it
    long currentEncoderValue = encoderValue;
    interrupts();
    long encoderDelta = currentEncoderValue - lastEncoderValue;
    pedalForward = (encoderDelta > 0) ? true : (encoderDelta < 0) ? false : true; //defualt to forward

    unsigned long elapsed = currentTime - lastTime;
    speed = abs(encoderDelta) / (elapsed / 1000.0); // Pulses per second

    // Bridge the slow point of each pedal stroke for the move and sprint thresholds:
    // follow the speed up straight away, but only let it fall gradually
    long smoothingTime = speed > movementSpeed ? min(SMOOTHING_RISE_TIME, settings.smoothingTime) : settings.smoothingTime;
    movementSpeed += (speed - movementSpeed) * (elapsed / (float)(smoothingTime + elapsed));

    // Switch direction once the pedals have clearly turned the other way, even without stopping
    if (encoderDelta != 0){
      bool forward = encoderDelta > 0;
      if (forward == movingForward){
        reversedSamples = 0;
      } else if (moving && ++reversedSamples >= REVERSE_CONFIRM_SAMPLES){
        movingForward = forward;
        reversedSamples = 0;
        movementSpeed = speed; // Start the new direction fresh, not with the old direction's speed
      }
    }

    lastEncoderValue = currentEncoderValue;
    lastTime = currentTime;

    updateQuickStop(currentTime);

    // Output speed and direction
    //Serial.print(F("Speed: "));
    
    Serial.print(0); // To freeze the lower limit
    Serial.print(F(" "));
    Serial.print(35000); // To freeze the upper limit
    
    Serial.print(F(" "));
    Serial.print(movementSpeed);
    Serial.print(F(" "));
    Serial.print(quickStopped); // 1 while quick stop has released the movement keys
    Serial.print(F(" "));
    Serial.println(speed); // Unsmoothed
  
    //Serial.print(F(" pulses/sec, Forward: "));
    //Serial.println(pedalForward);
  }
}

// Pedalling gives the speed a push every stroke, so it rises and falls a bit even at a steady pace.
// Momentum only ever slows down, so once the speed falls well below the slowest point of the last stroke, treat it as stopping
void updateQuickStop(unsigned long currentTime){
  smoothSpeed += (speed - smoothSpeed) * SPEED_SMOOTHING;

  if (speedFalling){
    if (smoothSpeed > speedExtreme * (1 + TURN_MARGIN) + TURN_NOISE){
      // Pushed again: the lowest point is the slowest point of this stroke
      lastStrokeLow = speedExtreme;
      lastStrokeTime = currentTime;
      speedFalling = false;
      waitForPush = false;
      speedExtreme = smoothSpeed;
      quickStopped = false;
      return;
    }
    speedExtreme = min(speedExtreme, smoothSpeed);
  } else{
    if (smoothSpeed < speedExtreme * (1 - TURN_MARGIN) - TURN_NOISE){
      speedFalling = true;
      fallPeak = speedExtreme;
      speedExtreme = smoothSpeed;
    } else{
      speedExtreme = max(speedExtreme, smoothSpeed);
    }
  }

  float stopReference = (currentTime - lastStrokeTime > STROKE_TIMEOUT) ? fallPeak : lastStrokeLow;
  if (settings.quickStop && !quickStopped && speedFalling
      && smoothSpeed < stopReference * (100 - settings.quickStopDrop) / 100.0){
    quickStopped = true; // movementInputs lets go of the movement keys
    movementSpeed = min(movementSpeed, speed); // So walk/run and the direction catch up with the stop straight away
  }
}

void updateEncoder(){
  int MSB = digitalRead(encoder_Pin_1); // MSB = most significant bit
  int LSB = digitalRead(encoder_Pin_2); // LSB = least significant bit

  int encoded = (MSB << 1) | LSB;       // converting the 2 pin value to single number
  int sum = (lastEncoded << 2) | encoded; // adding it to the previous encoded value

  if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011)
    encoderValue++;
  if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000)
    encoderValue--;

  lastEncoded = encoded; // store this value for next time
}

// Presses or releases a key for the pedals, keeping it down for at least settings.minKeyHold and up for at least settings.minKeyRelease
void updatePedalKey(PedalKey &pedalKey, unsigned long currentTime){
  if (pedalKey.wanted){
    pedalKey.lastWanted = currentTime;
  }
  bool shouldHold = currentTime < pedalKey.lastWanted + unpressInterval; // Allows smooth movement between speed updates
  unsigned long minTime = pedalKey.held ? settings.minKeyHold : settings.minKeyRelease;
  PedalKey* opposite = pedalKey.opposite;
  // If the opposite key is on its way up, wait until it's let go plus a gap. If you're holding both on purpose, allow it
  bool oppositeTooRecent = opposite != NULL
      && ((opposite->held && !opposite->wanted) || (!opposite->held && currentTime - opposite->changedAt < OPPOSITE_KEY_GAP));
  if (shouldHold != pedalKey.held
      && currentTime - pedalKey.changedAt >= minTime
      && currentTime - lastPedalKeyChange >= pedalKeyStagger
      && !(shouldHold && oppositeTooRecent)){
    pedalKey.held = shouldHold;
    pedalKey.changedAt = currentTime;
    lastPedalKeyChange = currentTime;
    pedalKeyStagger = random(KEY_STAGGER_MIN, KEY_STAGGER_MAX + 1);
  }
  if (pedalKey.held){
    Keyboard.press(pedalKey.key);
  } else if (pedalKey.manual && currentTime < pedalKey.manualUntil){
    // You're pressing it yourself for a counter strafe, leave it down
  } else{
    pedalKey.manual = false;
    Keyboard.release(pedalKey.key); // Also ends the short tap from pressing the key without pedalling
  }
}

void movementInputs(){
  unsigned long currentTime = millis();

  // Start moving above the walk speed, stop a bit below it so it doesn't flicker
  if (movementSpeed >= settings.walkSpeed){
    if (!moving){
      movingForward = pedalForward; // After that it only changes once the pedals clearly turn the other way (see updateSpeedDirection)
    }
    moving = true;
  } else if (movementSpeed < settings.walkSpeed * settings.stopMovingPercent / 100.0){
    moving = false;
  }

  // Sprint key. Once pressed it stays pressed until the speed passes the other threshold, so it doesn't flicker
  bool sprintWanted;
  if (settings.sprintOnPress){
    // Pressed to go faster
    sprintWanted = movementSpeed >= settings.sprintSpeed || (pedalSprint.held && movementSpeed >= settings.stopSprintSpeed);
  } else{
    // Pressed to walk, released to go faster
    //!crouched fixes bug where you cannot walk left while croud in valorant if you let go and re press "a"
    sprintWanted = moving && !crouched && (movementSpeed <= settings.stopSprintSpeed || (pedalSprint.held && movementSpeed <= settings.sprintSpeed));
  }
  pedalSprint.wanted = sprintWanted;

  // Hold the keys you're pressing while pedalling. With no keys pressed, go forwards or backwards with the pedals
  bool move = moving && !quickStopped;
  bool flip = settings.reverseFlipsKeys && !movingForward; // Pedalling backwards reverses the keys you hold
  bool holdW = flip ? sPressed : wPressed;
  bool holdA = flip ? dPressed : aPressed;
  bool holdS = flip ? wPressed : sPressed;
  bool holdD = flip ? aPressed : dPressed;
  if (moving && ((movingForward || flip) ? sPressed : wPressed)){
    waitForPush = true; // Pressing against the pedals: don't go back to moving with them until you push again
  }
  bool autoMove = move && settings.moveWhenNoInput && !waitForPush && !(wPressed || aPressed || sPressed || dPressed);
  pedalW.wanted = (move && holdW) || (autoMove && movingForward);
  pedalA.wanted = move && holdA;
  pedalS.wanted = (move && holdS) || (autoMove && !movingForward);
  pedalD.wanted = move && holdD;

  // Shuffle the order, so when two keys change together neither always goes first
  uint8_t order[pedalKeyCount];
  for (uint8_t i = 0; i < pedalKeyCount; i++){
    uint8_t j = random(i + 1);
    order[i] = order[j];
    order[j] = i;
  }
  for (uint8_t i = 0; i < pedalKeyCount; i++){
    updatePedalKey(*pedalKeys[order[i]], currentTime);
  }
  if (pedalW.held || pedalA.held || pedalS.held || pedalD.held){
    lastPedalMoveTime = currentTime;
  }
}

void loop()
{
  updateSpeedDirection();
  readSerialCommands();
  Usb.Task();
  movementInputs();
}

