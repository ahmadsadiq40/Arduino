#include <SPI.h>
#include <DMD_STM32.h>          
#include <fonts/Arial14.h>
#include <fonts/SystemFont5x7.h>

// ── P10 Pin Configuration (STM32F103C8) ────────────────────────
// Ye pins aapne assign kiye hain
#define PIN_DMD_OE    PA11  
#define PIN_DMD_A     PA12  
#define PIN_DMD_B     PB3   // Needs Debug Disable
#define PIN_DMD_CLK   PB4   // Needs Debug Disable
#define PIN_DMD_SCLK  PA15  // Needs Debug Disable
#define PIN_DMD_DATA  PB5   

// DMD Object
// Format: DMD(width, height, OE, A, B, SCLK, CLK, DATA);
DMD dmd(1, 1, PIN_DMD_OE, PIN_DMD_A, PIN_DMD_B, PIN_DMD_SCLK, PIN_DMD_CLK, PIN_DMD_DATA);

// ── 7-Segment Pins ─────────────────────────────────────────
const int segA  = PA7;
const int segB  = PA6;
const int segC  = PA5;
const int segD  = PA4;
const int segE  = PA3;
const int segF  = PA2;
const int segG  = PA0;
const int segDP = PA1;

// ── Digit Control Pins ─────────────────────────────────────
const int digit1 = PB11;
const int digit2 = PB10;
const int digit3 = PB1;
const int digit4 = PB0;

// ── Other Pins ─────────────────────────────────────────────
const int sensorPin  = PA8;
const int coinPin    = PC15;
const int tonePin    = PB13;
const int relayPin   = PA9;

// ── Segment arrays ─────────────────────────────────────────
int segments[] = {PA7, PA6, PA5, PA4, PA3, PA2, PA0};
int digitPins[] = {PB11, PB10, PB1, PB0};

bool numbers[10][7] = {
  {1,1,1,1,1,1,0}, // 0
  {0,1,1,0,0,0,0}, // 1
  {1,1,0,1,1,0,1}, // 2
  {1,1,1,1,0,0,1}, // 3
  {0,1,1,0,0,1,1}, // 4
  {1,0,1,1,0,1,1}, // 5
  {1,0,1,1,1,1,1}, // 6
  {1,1,1,0,0,0,0}, // 7
  {1,1,1,1,1,1,1}, // 8
  {1,1,1,1,0,1,1}, // 9
};

// ── Machine state ──────────────────────────────────────────
enum State { IDLE, LAFORZA_BLINK, READY, PLAYING };
State machineState = IDLE;
int credits    = 0;
int totalCoins = 0;

// ── Coin detection ─────────────────────────────────────────
bool          lastCoinState    = HIGH;
bool          coinArmed        = true;
unsigned long lastCoinRiseTime = 0;
const unsigned long COIN_DEBOUNCE  = 1000UL;
const unsigned long COIN_MIN_PULSE = 25UL;
const unsigned long COIN_MAX_PULSE = 200UL;

// ── 7seg hardware ───────────────────────────────────────────
int           displayNum[4] = {0, 0, 0, 0};
int           currentDigit  = 0;
unsigned long lastSwitch    = 0;
unsigned long startTime     = 0;
int           highScore     = 0;

// ── Scroll state ───────────────────────────────────────────
int           scrollX      = 32;
unsigned long lastScroll   = 0;
const int     SCROLL_SPEED = 60;
const char*   scrollMsg    = "LAFORZA";

// ── Score animation state ──────────────────────────────────
bool          animating     = false;
int           animCurrent   = 0;
int           animTarget    = 0;
unsigned long lastAnimStep  = 0;
unsigned long animHoldStart = 0;
bool          holding       = false;
int           blinkCount    = 0;
bool          blinkVisible  = true;
unsigned long lastBlink     = 0;
bool          blinking      = false;
const unsigned long HOLD_DURATION = 6000UL;

// ── Tone state ─────────────────────────────────────────────
unsigned long lastClick = 0;

// ── READY blink + relay state ──────────────────────────────
int           lfBlinkCount    = 0;
bool          lfVisible       = true;
unsigned long lfLastBlink     = 0;
const int     LF_BLINK_MS     = 300;
const int     LF_BLINK_TOTAL  = 4;
bool          waitingRelay    = false;
unsigned long relayWaitStart  = 0;
int           relayPulseCount = 0;
bool          relayActive     = false;
bool          relayGap        = false;
unsigned long relayOnStart    = 0;
unsigned long relayGapStart   = 0;
const int     RELAY_PULSES    = 3;
const unsigned long RELAY_ON_MS  = 1000UL;
const unsigned long RELAY_GAP_MS = 250UL;

// ══════════════════════════════════════════════════════════════
//  7-SEG MULTIPLEXING (Non-Blocking)
// ══════════════════════════════════════════════════════════════
void showDigit(int index, int number) {
  // 1. Pehle sab digits OFF karein (Common Anode = HIGH for OFF)
  for (int i = 0; i < 4; i++) {
    digitalWrite(digitPins[i], HIGH);
  }

  // 2. Segments set karein
  for (int i = 0; i < 7; i++) {
    // Common Anode: Segment ON = LOW
    digitalWrite(segments[i], numbers[number][i] ? LOW : HIGH);
  }
  
  // Decimal Point OFF
  digitalWrite(segDP, HIGH);

  // 3. Sirf required digit ON karein (Common Anode = LOW for ON)
  digitalWrite(digitPins[index], LOW);
}

void refreshDisplay() {
  // Har 2ms par digit switch karein (Non-blocking)
  if (micros() - lastSwitch >= 2000) {
    showDigit(currentDigit, displayNum[currentDigit]);
    currentDigit = (currentDigit + 1) % 4;
    lastSwitch = micros();
  }
}

void updateDisplay(int score) {
  displayNum[0] = score / 1000;
  displayNum[1] = (score / 100) % 10;
  displayNum[2] = (score / 10)  % 10;
  displayNum[3] = score % 10;
}

// ══════════════════════════════════════════════════════════════
//  P10 helpers
// ══════════════════════════════════════════════════════════════
void fillRect(int x, int y, int w, int h) {
  for (int row = y; row < y + h; row++)
    dmd.drawLine(x, row, x + w - 1, row, GRAPHICS_ON);
}

void drawSeg7Digit(int digit, int ox, int oy, int dw, int dh, int sw) {
  bool segs[7];
  for (int i = 0; i < 7; i++) segs[i] = numbers[digit][i];
  int midY = oy + dh / 2;
  if (segs[0]) fillRect(ox, oy,            dw, sw);
  if (segs[6]) fillRect(ox, midY - sw / 2, dw, sw);
  if (segs[3]) fillRect(ox, oy + dh - sw,  dw, sw);
  if (segs[5]) fillRect(ox,           oy,  sw, dh / 2);
  if (segs[1]) fillRect(ox + dw - sw, oy,  sw, dh / 2);
  if (segs[4]) fillRect(ox,           midY, sw, dh / 2);
  if (segs[2]) fillRect(ox + dw - sw, midY, sw, dh / 2);
}

void drawScoreP10(int score, bool visible) {
  dmd.clearScreen();
  if (!visible) return;
  int digits[4];
  digits[0] = score / 1000;
  digits[1] = (score / 100) % 10;
  digits[2] = (score / 10)  % 10;
  digits[3] = score % 10;
  int panelW = 32, panelH = 16, gap = 1;
  int dw = (panelW - 3 * gap) / 4;
  int sw = 2;
  int curX = 0;
  for (int i = 0; i < 4; i++) {
    drawSeg7Digit(digits[i], curX, 0, dw, panelH, sw);
    curX += dw + gap;
  }
}

void drawReady() {
  dmd.selectFont(SystemFont5x7);
  dmd.clearScreen();
  dmd.drawString(0, 4, "READY");
}

void drawLaforza() {
  dmd.selectFont(SystemFont5x7);
  dmd.clearScreen();
  dmd.drawString(-4, 4, scrollMsg);
}

// ══════════════════════════════════════════════════════════════
//  Sounds
// ══════════════════════════════════════════════════════════════
void playCoinSound() {
  for (int f = 400; f <= 1200; f += 80) {
    tone(tonePin, f, 18);
    delay(18);
  }
  noTone(tonePin);
}

void playScoreLandSound() {
  for (int f = 900; f >= 200; f -= 70) {
    tone(tonePin, f, 15);
    delay(15);
  }
  delay(60);
  for (int f = 300; f <= 800; f += 100) {
    tone(tonePin, f, 20);
    delay(20);
  }
  noTone(tonePin);
}

// ══════════════════════════════════════════════════════════════
//  Scroll LAFORZA
// ══════════════════════════════════════════════════════════════
void scrollLaforza() {
  if (millis() - lastScroll >= SCROLL_SPEED) {
    dmd.selectFont(SystemFont5x7);
    dmd.clearScreen();
    dmd.drawString(scrollX, 4, scrollMsg);
    scrollX--;
    if (scrollX < -40) scrollX = 32;
    lastScroll = millis();
  }
}

// ══════════════════════════════════════════════════════════════
//  Click tone
// ══════════════════════════════════════════════════════════════
void tickClick(int current, int target) {
  int freq = map(current, 0, target, 200, 1200);
  freq = constrain(freq, 200, 1200);
  int remaining = target - current;
  unsigned long clickInterval;
  if (remaining > 20)
    clickInterval = map(remaining, 20, target, 120, 30);
  else
    clickInterval = map(remaining, 0, 20, 400, 120);
  clickInterval = constrain(clickInterval, 30, 400);
  if (millis() - lastClick >= clickInterval) {
    tone(tonePin, freq, 20);
    lastClick = millis();
  }
}

// ══════════════════════════════════════════════════════════════
//  Score animation
// ══════════════════════════════════════════════════════════════
void startScoreAnimation(int score) {
  animTarget   = score;
  animCurrent  = 0;
  animating    = true;
  holding      = false;
  blinking     = false;
  blinkCount   = 0;
  blinkVisible = true;
  lastAnimStep = millis();
  lastClick    = millis();
}

bool p10Busy() {
  return animating || blinking || holding;
}

void tickScoreAnimation() {
  if (!animating && !holding && !blinking) return;

  if (blinking) {
    noTone(tonePin);
    if (millis() - lastBlink >= 200) {
      blinkVisible = !blinkVisible;
      drawScoreP10(animTarget, blinkVisible);
      lastBlink = millis();
      if (!blinkVisible) blinkCount++;
      if (blinkCount >= 2) {
        blinking      = false;
        holding       = true;
        blinkVisible  = true;
        animHoldStart = millis();
        drawScoreP10(animTarget, true);
      }
    }
    return;
  }

  if (holding) {
    if (millis() - animHoldStart >= HOLD_DURATION) {
      holding   = false;
      animating = false;
    }
    return;
  }

  if (animating) {
    int remaining = animTarget - animCurrent;
    tickClick(animCurrent, animTarget);

    if (remaining <= 0) {
      noTone(tonePin);
      playScoreLandSound();
      animating    = false;
      blinking     = true;
      blinkVisible = true;
      blinkCount   = 0;
      lastBlink    = millis();
      drawScoreP10(animTarget, true);
      return;
    }

    unsigned long stepDelay;
    if (remaining > 20)
      stepDelay = max(1UL, (unsigned long)(3200 / max(1, animTarget - 20)));
    else
      stepDelay = 40;

    if (millis() - lastAnimStep >= stepDelay) {
      animCurrent++;
      drawScoreP10(animCurrent, true);
      lastAnimStep = millis();
    }
  }
}

// ══════════════════════════════════════════════════════════════
//  READY blink + relay sequence
// ══════════════════════════════════════════════════════════════
void enterLaforzaBlink() {
  machineState    = LAFORZA_BLINK;
  lfBlinkCount    = 0;
  lfVisible       = true;
  lfLastBlink     = millis();
  waitingRelay    = false;
  relayActive     = false;
  relayGap        = false;
  relayPulseCount = 0;
  drawReady();
}

void tickLaforzaBlink() {
  if (relayActive) {
    if (millis() - relayOnStart >= RELAY_ON_MS) {
      digitalWrite(relayPin, HIGH);
      relayActive = false;
      relayPulseCount++;
      if (relayPulseCount >= RELAY_PULSES) {
        machineState = READY;
        drawScoreP10(0, true);
      } else {
        relayGap      = true;
        relayGapStart = millis();
      }
    }
    return;
  }

  if (relayGap) {
    if (millis() - relayGapStart >= RELAY_GAP_MS) {
      relayGap     = false;
      relayActive  = true;
      relayOnStart = millis();
      digitalWrite(relayPin, LOW);
    }
    return;
  }

  if (waitingRelay) {
    if (millis() - relayWaitStart >= 3000) {
      waitingRelay = false;
      relayActive  = true;
      relayOnStart = millis();
      digitalWrite(relayPin, LOW);
    }
    return;
  }

  if (millis() - lfLastBlink >= LF_BLINK_MS) {
    lfVisible = !lfVisible;
    if (lfVisible) drawReady();
    else dmd.clearScreen();
    lfLastBlink = millis();
    lfBlinkCount++;
    if (lfBlinkCount >= LF_BLINK_TOTAL) {
      waitingRelay   = true;
      relayWaitStart = millis();
      drawReady();
    }
  }
}

// ══════════════════════════════════════════════════════════════
//  Credit system
// ══════════════════════════════════════════════════════════════
void startNewGame() {
  credits--;
  scrollX = 32;
  enterLaforzaBlink();
}

// ══════════════════════════════════════════════════════════════
//  Coin check — NC mode
// ══════════════════════════════════════════════════════════════
void checkCoin() {
  bool coinState = digitalRead(coinPin);

  if (coinState == HIGH) {
    coinArmed = true;
  }

  if (coinArmed && coinState == LOW && lastCoinState == HIGH) {
    if (millis() - lastCoinRiseTime >= COIN_DEBOUNCE) {

      unsigned long pulseStart = millis();
      while (digitalRead(coinPin) == LOW &&
             millis() - pulseStart < COIN_MAX_PULSE) {
        // Wait for pulse end
      }
      unsigned long pulseLength = millis() - pulseStart;

      if (pulseLength >= COIN_MIN_PULSE) {
        lastCoinRiseTime = millis();
        coinArmed = false;
        credits++;
        totalCoins++;
        Serial.print("Coin inserted! Total coins: ");
        Serial.print(totalCoins);
        Serial.print("  |  Credits: ");
        Serial.println(credits);
        playCoinSound();
        if (machineState == IDLE) {
          startNewGame();
        }
      } else {
        Serial.print("Noise ignored — pulse: ");
        Serial.print(pulseLength);
        Serial.println("ms");
      }
    }
  }
  lastCoinState = coinState;
}

// ══════════════════════════════════════════════════════════════
//  Score calculate
// ══════════════════════════════════════════════════════════════
int calculateScore(unsigned long timeUS) {
  if (timeUS <= 2000)  return 1000;
  if (timeUS >= 16250) return 1;
  return (int)map(timeUS, 2000, 16250, 1000, 1);
}

// ══════════════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(250000);
  
  // 7-Segment Pins Setup
  for (int i = 0; i < 7; i++) pinMode(segments[i], OUTPUT);
  pinMode(segDP, OUTPUT);
  for (int i = 0; i < 4; i++) {
    pinMode(digitPins[i], OUTPUT);
    digitalWrite(digitPins[i], HIGH); // Common Anide OFF
  }
  
  // Other Pins
  pinMode(sensorPin,  INPUT);
  pinMode(coinPin,    INPUT_PULLUP);
  pinMode(tonePin,    OUTPUT);
  pinMode(relayPin,   OUTPUT);
  digitalWrite(relayPin, HIGH);
  
  // IMPORTANT: Disable Debug ports to use PB3, PB4, PA15 as GPIO
  disableDebugPorts(); 
  
  // DMD Setup
  dmd.selectFont(SystemFont5x7); 
  dmd.begin();                   
  SPI.setClockDivider(SPI_CLOCK_DIV32);

  dmd.clearScreen();
  updateDisplay(0);
  Serial.println("Ready. Waiting for coin...");
}

// ══════════════════════════════════════════════════════════════
//  LOOP
// ══════════════════════════════════════════════════════════════
void loop() {
  refreshDisplay(); // Always refresh 7-segment
  checkCoin();

  switch (machineState) {

    case IDLE:
      scrollLaforza();
      break;

    case LAFORZA_BLINK:
      tickLaforzaBlink();
      break;

    case READY:
      if (digitalRead(sensorPin) == HIGH) {
        startTime = micros();
        while (digitalRead(sensorPin) == HIGH) refreshDisplay(); // Keep refreshing while waiting
        unsigned long elapsed = micros() - startTime;
        int score = calculateScore(elapsed);
        Serial.print("Score: "); Serial.println(score);
        startScoreAnimation(score);
        machineState = PLAYING;
        if (score > highScore) {
          highScore = score;
          updateDisplay(highScore);
        }
      }
      break;

    case PLAYING:
      tickScoreAnimation();
      if (!p10Busy()) {
        if (credits > 0) {
          startNewGame();
        } else {
          machineState = IDLE;
          scrollX = 32;
          dmd.clearScreen();
        }
      }
      break;
  }
}
