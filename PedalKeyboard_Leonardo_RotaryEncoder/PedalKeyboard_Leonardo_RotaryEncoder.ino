#include <hidboot.h>
#include <usbhub.h>
#include <Keyboard.h>
// Satisfy the IDE, which needs to see the include statment in the ino too. //This is from the usb host example, not sure what it does
#ifdef dobogusinclude
#include <spi4teensy3.h>
#endif
#include <SPI.h>
//------------------------------------------------------
//_____Variables_To_Change_____
const int WALK_SPEED = 2000; // Speed at which walk is triggered
const int WALK_STOP_SPEED = 1400; // Speed must drop below this to stop moving (gap to WALK_SPEED stops keys chattering when speed hovers near the threshold)
const int SPRINT_SPEED = 16000;//22000; // Speed at which sprint is triggered
const int STOP_SPRINT_SPEED = 9000;//6000 12000; // Speed must drop below this value to stop sprinting
const unsigned long MIN_MODE_DWELL = 150; // ms. Minimum time between walk<->sprint switches, so the sprint key can never be toggled rapidly
const unsigned long MOVE_RELEASE_DELAY = 0; // ms. Extra time to keep moving after speed drops below WALK_STOP_SPEED. 0 = stop as fast as possible
const float RISE_ALPHA = 0.3; // Speed filter response when speeding up (0-1, higher = faster). Slow rise ignores single-window spikes
const float FALL_ALPHA = 0.7; // Speed filter response when slowing down. Fast fall keeps stopping responsive
const bool DEBUG_SPEED_PLOT = false; // Print speed to the serial plotter every update. Leave off when playing: serial writes can stall the loop
bool sprint_on_press = false; //True if pressing sprint makes you faster, false if it makes you slower (eg csgo pressing shift)
bool move_when_no_input = true; //walk forwards and backwards if pedalling and no keys pressed
const uint8_t sprintKey = KEY_LEFT_SHIFT;
//---------------------------------------


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

//_____Keyboard_Stuff_____
const unsigned long timerInterval = 5; // Speed update every timerInterval ms

//BEGIN_CONTROL_LOGIC (hardware independent apart from Keyboard, extracted by the off-target test)
// A key the pedal logic controls. `down` is what the PC currently sees, so a report is only
// sent when the state really changes (Keyboard.press/release send a USB report on every call).
struct GatedKey {
  uint8_t code;
  bool down;
};
GatedKey gkW = {'w', false};
GatedKey gkA = {'a', false};
GatedKey gkS = {'s', false};
GatedKey gkD = {'d', false};
GatedKey gkSprint = {sprintKey, false};

float filtSpeed = 0;              // Asymmetrically filtered speed
bool moving = false;              // Pedalling fast enough to move (with hysteresis)
bool sprintMode = false;          // In the sprint speed range (with hysteresis)
unsigned long lastAboveStop = 0;  // Last time speed was above WALK_STOP_SPEED while moving
unsigned long lastModeChange = 0; // Last walk<->sprint switch

void setGated(GatedKey &k, bool want) {
  if (want == k.down) return;
  if (want) Keyboard.press(k.code); else Keyboard.release(k.code);
  k.down = want;
}

// Call once per speed sample. Decides moving/sprintMode from the new speed.
void updateControlState(float rawSpeed, unsigned long now) {
  // Fast attack on slowdown, slow on speedup: stopping is seen quickly, spikes upward are smoothed
  float alpha = (rawSpeed > filtSpeed) ? RISE_ALPHA : FALL_ALPHA;
  filtSpeed += alpha * (rawSpeed - filtSpeed);

  if (filtSpeed >= WALK_SPEED) {
    if (!moving) {
      moving = true;
      sprintMode = filtSpeed > STOP_SPRINT_SPEED; // Start in the right mode without waiting for the dwell
      lastModeChange = now;
    }
    lastAboveStop = now;
  } else if (moving) {
    if (filtSpeed >= WALK_STOP_SPEED) {
      lastAboveStop = now;                        // Inside the hysteresis gap, keep moving
    } else if (now - lastAboveStop >= MOVE_RELEASE_DELAY) {
      moving = false;
    }
  }

  // Walk<->sprint switching has hysteresis (SPRINT_SPEED / STOP_SPRINT_SPEED) and a minimum dwell
  if (moving && (now - lastModeChange >= MIN_MODE_DWELL)) {
    if (!sprintMode && filtSpeed > SPRINT_SPEED) {
      sprintMode = true;
      lastModeChange = now;
    } else if (sprintMode && filtSpeed < STOP_SPRINT_SPEED) {
      sprintMode = false;
      lastModeChange = now;
    }
  }
}

// Call every loop. Only sends USB reports when a key actually needs to change.
void applyKeys() {
  bool noKeys = !(wPressed || aPressed || sPressed || dPressed);
  // Sprint key: pressed to sprint, or pressed to walk (eg csgo shift). Not while crouched
  // (fixes bug where you cannot walk left while crouched in valorant if you let go and re press "a")
  bool wantSprintKey = moving && (sprint_on_press ? sprintMode : (!sprintMode && !crouched));
  setGated(gkSprint, wantSprintKey);

  // Physical keys are only passed through while pedalling. Pedalling with no keys pressed moves
  // forwards or backwards (direction of the pedals).
  setGated(gkW, moving && (wPressed || (noKeys && move_when_no_input && pedalForward)));
  setGated(gkA, moving && aPressed);
  setGated(gkS, moving && (sPressed || (noKeys && move_when_no_input && !pedalForward)));
  setGated(gkD, moving && dPressed);
}
//END_CONTROL_LOGIC


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

  Serial.print(" >");
  PrintHex<uint8_t>(key, 0x80);
  Serial.print("< ");

  Serial.print((mod.bmRightCtrl   == 1) ? "C" : " ");
  Serial.print((mod.bmRightShift  == 1) ? "S" : " ");
  Serial.print((mod.bmRightAlt    == 1) ? "A" : " ");
  Serial.print((mod.bmRightGUI    == 1) ? "G" : " ");
};
*/

void KbdRptParser::OnKeyDown(uint8_t mod, uint8_t key)
{
  //Serial.print("DN ");
  //PrintKey(mod, key);
  switch (key){
        case WKey:
          Serial.println("W pressed override for movement");
          wPressed = true;
          gkW.down = true; // passed through below, applyKeys() releases it if not pedalling
          break;
        case AKey:
          Serial.println("A pressed override for movement");
          aPressed = true;
          gkA.down = true; // passed through below, applyKeys() releases it if not pedalling
          break;
        case SKey:
          Serial.println("S pressed override for movement");
          sPressed = true;
          gkS.down = true; // passed through below, applyKeys() releases it if not pedalling
          break;
        case DKey:
          Serial.println("D pressed override for movement");
          dPressed = true;
          gkD.down = true; // passed through below, applyKeys() releases it if not pedalling
          break;
  }
  uint8_t c = OemToAscii(mod, key);
  if (key == EnterCode){
    Serial.println("ENTER OVERRIDE"); //Enter key was being picked up as an ascii one
    OnSpecialKeysChanged(key, true);
  } else{
    if (c){
      showASCII(c);
      Serial.println(" pressed");
      Keyboard.press(c);
  } else{
      OnSpecialKeysChanged(key, true);
  }
  //Serial.println("pressed");
  //Serial.println(key);
  }
}

void KbdRptParser::OnKeyUp(uint8_t mod, uint8_t key){
  //Serial.print("keypress ");
  //PrintKey(mod, key);
  switch (key){
        case WKey:
          Serial.println("W unpressed ovveride for movement");
          wPressed = false;
          gkW.down = false;
          break;
        case AKey:
          Serial.println("A unpressed ovveride for movement");
          aPressed = false;
          gkA.down = false;
          break;
        case SKey:
          Serial.println("S unpressed ovveride for movement");
          sPressed = false;
          gkS.down = false;
          break;
        case DKey:
          Serial.println("D unpressed ovveride for movement");
          dPressed = false;
          gkD.down = false;
          break;
  }
  uint8_t c = OemToAscii(mod, key);
  if (key == EnterCode){
    Serial.println("ENTER OVERRIDE"); //Enter key was being picked up as an ascii one
    OnSpecialKeysChanged(key, false);
  } else{
    if (c){
      showASCII(c);
      Serial.println(" released");
      Keyboard.release(c);
  } else{
    OnSpecialKeysChanged(key, false);
  }
  //Serial.println("released");
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
        Serial.println("LeftCtrl pressed");
        Keyboard.press(KEY_LEFT_CTRL);
        crouched = true;

    }
    if (beforeMod.bmLeftCtrl == 1 && afterMod.bmLeftCtrl == 0) {
        Serial.println("LeftCtrl released");
        Keyboard.release(KEY_LEFT_CTRL);
        crouched = false;
    }

    if (beforeMod.bmLeftShift == 0 && afterMod.bmLeftShift == 1) {
        Serial.println("LeftShift pressed");
        Keyboard.press(KEY_LEFT_SHIFT);
        if (sprintKey == KEY_LEFT_SHIFT) gkSprint.down = true;
    }
    if (beforeMod.bmLeftShift == 1 && afterMod.bmLeftShift == 0) {
        Serial.println("LeftShift released");
        Keyboard.release(KEY_LEFT_SHIFT);
        if (sprintKey == KEY_LEFT_SHIFT) gkSprint.down = false;
    }

    if (beforeMod.bmLeftAlt == 0 && afterMod.bmLeftAlt == 1) {
        Serial.println("LeftAlt pressed");
        Keyboard.press(KEY_LEFT_ALT);
    }
    if (beforeMod.bmLeftAlt == 1 && afterMod.bmLeftAlt == 0) {
        Serial.println("LeftAlt released");
        Keyboard.release(KEY_LEFT_ALT);
    }

    if (beforeMod.bmLeftGUI == 0 && afterMod.bmLeftGUI == 1) {
        Serial.println("LeftGUI pressed");
        Keyboard.press(KEY_LEFT_GUI);
    }
    if (beforeMod.bmLeftGUI == 1 && afterMod.bmLeftGUI == 0) {
        Serial.println("LeftGUI released");
        Keyboard.release(KEY_LEFT_GUI);
    }

    if (beforeMod.bmRightCtrl == 0 && afterMod.bmRightCtrl == 1) {
        Serial.println("RightCtrl pressed");
        Keyboard.press(KEY_RIGHT_CTRL);
    }
    if (beforeMod.bmRightCtrl == 1 && afterMod.bmRightCtrl == 0) {
        Serial.println("RightCtrl released");
        Keyboard.release(KEY_RIGHT_CTRL);
    }

    if (beforeMod.bmRightShift == 0 && afterMod.bmRightShift == 1) {
        Serial.println("RightShift pressed");
        Keyboard.press(KEY_RIGHT_SHIFT);
    }
    if (beforeMod.bmRightShift == 1 && afterMod.bmRightShift == 0) {
        Serial.println("RightShift released");
        Keyboard.release(KEY_RIGHT_SHIFT);
    }

    if (beforeMod.bmRightAlt == 0 && afterMod.bmRightAlt == 1) {
        Serial.println("RightAlt pressed");
        Keyboard.press(KEY_RIGHT_ALT);
    }
    if (beforeMod.bmRightAlt == 1 && afterMod.bmRightAlt == 0) {
        Serial.println("RightAlt released");
        Keyboard.release(KEY_RIGHT_ALT);
    }

    if (beforeMod.bmRightGUI == 0 && afterMod.bmRightGUI == 1) { //Windows key
        Serial.println("RightGUI pressed");
        Keyboard.press(KEY_RIGHT_GUI);
    }
    if (beforeMod.bmRightGUI == 1 && afterMod.bmRightGUI == 0) {
        Serial.println("RightGUI released");
        Keyboard.release(KEY_RIGHT_GUI);
    }
}

void KbdRptParser::OnSpecialKeysChanged(uint8_t key, bool pressed) { //CAPS LOCK APPEARS TO BE ALREADY HANDLED?
    // Define special keys' HID usage codes

    if (pressed){ // True if press, False if release
      //Serial.print("pressed");
          // Never used a switch case, be nice or I will return us all to IF ELSE purgatory
      switch (key){
        case EscapeCode:
          Serial.println("ESCAPE KEY PRESSED");
          Keyboard.press(KEY_ESC);
          break;
      
        case BackspaceCode:
          Serial.println("BACKSPACE KEY PRESSED");
          Keyboard.press(KEY_BACKSPACE);
          break;

        case TabCode:
          Serial.println("Tab KEY PRESSED");
          Keyboard.press(KEY_TAB);
          break;
        
        case EnterCode:
          Serial.println("Enter KEY PRESSED");
          Keyboard.press(KEY_RETURN);
          break;
        
        case RightArrowCode:
          Serial.println("Right Arrow KEY PRESSED");
          Keyboard.press(KEY_RIGHT_ARROW);
          break;

        case LeftArrowCode:
          Serial.println("Left Arrow KEY PRESSED");
          Keyboard.press(KEY_LEFT_ARROW);
          break;

        case DownArrowCode:
          Serial.println("Down Arrow KEY PRESSED");
          Keyboard.press(KEY_DOWN_ARROW);
          break;
        
        case UpArrowCode:
          Serial.println("Up Arrow KEY PRESSED");
          Keyboard.press(KEY_UP_ARROW);
          break;

        case CapsLockCode:
          Serial.println("Caps Lock KEY PRESSED");
          //Keyboard.press(KEY_CAPS_LOCK);
          break;
        
        case PageUpCode:
          Serial.println("PageUp KEY PRESSED");
          Keyboard.press(KEY_PAGE_UP);
          break;
        
        case PageDownCode:
          Serial.println("Page Down KEY PRESSED");
          Keyboard.press(KEY_PAGE_DOWN);
          break;
        
        case EndCode:
          Serial.println("End KEY PRESSED");
          Keyboard.press(KEY_END);
          break;
        
        case HomeCode:
          Serial.println("Home KEY PRESSED");
          Keyboard.press(KEY_HOME);
          break;

        default:
          Serial.print("Unknown keypress, please define above keycode 0x: "); //may be incorrect
          Serial.print(key, HEX);
          Serial.println("");
          break;
    }
    } else {
      //Serial.print("unpressed");
      switch (key){
        case EscapeCode:
          Serial.println("ESCAPE KEY UNPRESSED");
          Keyboard.release(KEY_ESC);
          break;
      
        case BackspaceCode:
          Serial.println("BACKSPACE KEY UNPRESSED");
          Keyboard.release(KEY_BACKSPACE);
          break;

        case TabCode:
          Serial.println("Tab KEY UNPRESSED");
          Keyboard.release(KEY_TAB);
          break;
        
        case EnterCode:
          Serial.println("Enter KEY UNPRESSED");
          Keyboard.release(KEY_RETURN);
          break;
        
        case RightArrowCode:
          Serial.println("Right Arrow KEY UNPRESSED");
          Keyboard.release(KEY_RIGHT_ARROW);
          break;

        case LeftArrowCode:
          Serial.println("Left Arrow KEY UNPRESSED");
          Keyboard.release(KEY_LEFT_ARROW);
          break;

        case DownArrowCode:
          Serial.println("Down Arrow KEY UNPRESSED");
          Keyboard.release(KEY_DOWN_ARROW);
          break;
        
        case UpArrowCode:
          Serial.println("Up Arrow KEY UNPRESSED");
          Keyboard.release(KEY_UP_ARROW);
          break;

        case CapsLockCode:
          Serial.println("Caps Lock KEY UNPRESSED");
          //Keyboard.release(KEY_CAPS_LOCK);
          break;
        
        case PageUpCode:
          Serial.println("PageUp KEY UNPRESSED");
          Keyboard.release(KEY_PAGE_UP);
          break;
        
        case PageDownCode:
          Serial.println("Page Down KEY UNPRESSED");
          Keyboard.release(KEY_PAGE_DOWN);
          break;
        
        case EndCode:
          Serial.println("End KEY UNPRESSED");
          Keyboard.release(KEY_END);
          break;
        
        case HomeCode:
          Serial.println("Home KEY UNPRESSED");
          Keyboard.release(KEY_HOME);
          break;

        default:
          Serial.print("Unknown keypress, please define above keycode 0x: "); //may be incorrect
          Serial.print(key, HEX);
          Serial.println("");
          break;
      }
    }
}

void KbdRptParser::showASCII(uint8_t key)
{
  Serial.print("ASCII: ");
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
  Serial.println("Start");
  if (Usb.Init() == -1)
    Serial.println("OSC did not start.");
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

  Keyboard.releaseAll(); // Sometimes unplugging/restarting can get keys stuck pressed
}

void updateSpeedDirection(){
  // Calculate speed and direction every timerInterval ms
  unsigned long currentTime = millis();
  if (currentTime - lastTime >= timerInterval)
  {
    noInterrupts(); // encoderValue is 32 bit, the ISR can change it mid read on an 8 bit AVR
    long encoderValue_copy = encoderValue;
    interrupts();
    long encoderDelta = encoderValue_copy - lastEncoderValue;
    if (encoderDelta > 0) pedalForward = true;
    else if (encoderDelta < 0) pedalForward = false; // no change in position keeps the last direction

    speed = abs(encoderDelta) / ((currentTime - lastTime) / 1000.0); // Pulses per second

    lastEncoderValue = encoderValue_copy;
    lastTime = currentTime;

    updateControlState(speed, currentTime);

    if (DEBUG_SPEED_PLOT) {
      Serial.print(0); // To freeze the lower limit
      Serial.print(" ");
      Serial.print(35000); // To freeze the upper limit
      Serial.print(" ");
      Serial.print(speed);
      Serial.print(" ");
      Serial.println(filtSpeed);
    }
  }
}

void updateEncoder(){
#if defined(__AVR_ATmega32U4__)
  // Pins 2 and 3 are PD1 and PD0 on the Leonardo, so one port read gives (pin2 << 1) | pin3. Much faster than digitalRead
  int encoded = PIND & 0x03;
#else
  int MSB = digitalRead(encoder_Pin_1); // MSB = most significant bit
  int LSB = digitalRead(encoder_Pin_2); // LSB = least significant bit
  int encoded = (MSB << 1) | LSB;       // converting the 2 pin value to single number
#endif
  int sum = (lastEncoded << 2) | encoded; // adding it to the previous encoded value

  if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011)
    encoderValue++;
  if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000)
    encoderValue--;

  lastEncoded = encoded; // store this value for next time
}

void loop()
{
  updateSpeedDirection();
  Usb.Task();
  applyKeys();
}

