#include <hidboot.h>
#include <usbhub.h>
#include <Keyboard.h>

// Satisfy the IDE, which needs to see the include statment in the ino too. //This is from the usb host example, not sure what it does
#ifdef dobogusinclude
#include <spi4teensy3.h>
#endif
#include <SPI.h>
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

//_____Speed/Movement_Stuff_____
const int WALK_SPEED = 2000; // Speed at which walk is triggered
const int SPRINT_SPEED = 16000;//22000; // Speed at which sprint is triggered
const int STOP_SPRINT_SPEED = 9000;//6000 12000; // Speed must drop below this value to stop sprinting

bool sprint_on_press = true; //True if pressing sprint makes you faster, false if it makes you slower (eg csgo pressing shift)
bool move_when_no_input = true; //walk forwards and backwards if pedalling and no keys pressed
const uint8_t sprintKey = KEY_LEFT_SHIFT; 
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
const unsigned long unpressInterval = 2 * timerInterval; // Allows smooth movement if its bigger than timer interval
unsigned long lastWInput = 0;
unsigned long lastAInput = 0;
unsigned long lastSInput = 0;
unsigned long lastDInput = 0;
unsigned long lastSprintInput = 0;


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
          break;
        case AKey:
          Serial.println("A pressed override for movement");
          aPressed = true;
          break;
        case SKey:
          Serial.println("S pressed override for movement");
          sPressed = true;
          break;
        case DKey:
          Serial.println("D pressed override for movement");
          dPressed = true;
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
          break;
        case AKey:
          Serial.println("A unpressed ovveride for movement");
          aPressed = false;
          break;
        case SKey:
          Serial.println("S unpressed ovveride for movement");
          sPressed = false;
          break;
        case DKey:
          Serial.println("D unpressed ovveride for movement");
          dPressed = false;
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
    }
    if (beforeMod.bmLeftShift == 1 && afterMod.bmLeftShift == 0) {
        Serial.println("LeftShift released");
        Keyboard.release(KEY_LEFT_SHIFT);
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
  // Calculate speed and direction every 100 ms
  unsigned long currentTime = millis();
  if (currentTime - lastTime >= timerInterval)
  {
    long encoderDelta = encoderValue - lastEncoderValue;
    pedalForward = (encoderDelta > 0) ? true : (encoderDelta < 0) ? false : true; //defualt to forward

    speed = abs(encoderDelta) / ((currentTime - lastTime) / 1000.0); // Pulses per second

    lastEncoderValue = encoderValue;
    lastTime = currentTime;

    // Output speed and direction
    //Serial.print("Speed: ");
    
    Serial.print(0); // To freeze the lower limit
    Serial.print(" ");
    Serial.print(35000); // To freeze the upper limit
    
    Serial.print(" ");
    Serial.println(speed);
  
    //Serial.print(" pulses/sec, Forward: ");
    //Serial.println(pedalForward);
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

void movementInputs(){
  /*
  Keyboard.release('w');
  Keyboard.release('a');
  Keyboard.release('s');
  Keyboard.release('d');
  */
  unsigned long currentTime = millis();

  //Release keys if time is over
  if (currentTime >= (lastSprintInput + unpressInterval)) {
    Keyboard.release(sprintKey);
  }
  if (currentTime >= (lastWInput + unpressInterval)) {
    Keyboard.release('w');
  }
  if (currentTime >= (lastAInput + unpressInterval)) {
    Keyboard.release('a');
  }
  if (currentTime >= (lastSInput + unpressInterval)) {
    Keyboard.release('s');
  }
  if (currentTime >= (lastDInput + unpressInterval)) {
    Keyboard.release('d');
  }

  //Extend time if still pressed and pedal fast enough
  if (sprint_on_press){
    if (speed >= SPRINT_SPEED) {
      Keyboard.press(sprintKey);
      lastSprintInput = currentTime;
    }
    else if (speed >= STOP_SPRINT_SPEED){
      lastSprintInput = currentTime;
    }

  } else{
    /* Debugging sprint behaviour
    Serial.println("Speed: "); Serial.println(speed);
    Serial.print("STOP_SPRINT_SPEED: "); Serial.println(STOP_SPRINT_SPEED);
    Serial.print("SPRINT_SPEED: "); Serial.println(SPRINT_SPEED);
    Serial.print("WALK_SPEED: "); Serial.println(WALK_SPEED);
    */
    if (speed <= STOP_SPRINT_SPEED) {
      if (speed >= WALK_SPEED){
        if (!(crouched)){//fixes bug where you cannot walk left while croud in valorant if you let go and re press "a"
          Keyboard.press(sprintKey); //pressing this to walk
          lastSprintInput = currentTime;
        }
      }
    }
    else if (speed <= SPRINT_SPEED){
      if (speed >= WALK_SPEED){
        //Keyboard.press(sprintKey); //pressing this to walk
        if (!(crouched)){
          lastSprintInput = currentTime;
        }
      }
    }
    else{
      Serial.println("What");
    } 
  }

  if (speed >= WALK_SPEED){
    if (pedalForward){
      if (wPressed){
        Keyboard.press('w');
        lastWInput = currentTime;
      }
      if (aPressed){
        Keyboard.press('a');
        lastAInput = currentTime;
      }
      if (sPressed){
        Keyboard.press('s');
        lastSInput = currentTime;
      }
      if (dPressed){
        Keyboard.press('d');
        lastDInput = currentTime;
      }

      if (!(wPressed || aPressed || sPressed || dPressed)){
        if (move_when_no_input){
          Keyboard.press('w');
          lastWInput = currentTime;
        }
      }

    } else{
    //press opposite key if pedalling backward
      if (wPressed){
        Keyboard.press('w');
        lastWInput = currentTime;
      }
      if (aPressed){
        Keyboard.press('a');
        lastAInput = currentTime;
      }
      if (sPressed){
        Keyboard.press('s');
        lastSInput = currentTime;
      }
      if (dPressed){
        Keyboard.press('d');
        lastDInput = currentTime;
      }

      if (!(wPressed || aPressed || sPressed || dPressed)){
        if (move_when_no_input){
          Keyboard.press('s');
          lastSInput = currentTime;
        }
      }

    }
  }

}

void loop()
{
  updateSpeedDirection();
  Usb.Task();
  movementInputs();
}

