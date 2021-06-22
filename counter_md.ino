// Use the MD_MAX72XX library to create an mechanical pushwheel type display
// When numbers change they are scrolled up or down as if on a cylinder
//
// 'Speed' displayed is read from pot on SPEED_IN analog in.

#include <MD_MAX72xx.h>
#include <SPI.h>
#include "Font_Data.h"
#include "tones.h"

#define DEBUG 0

#if DEBUG
#define PRINT(s, v)   { Serial.print(F(s)); Serial.print(v); }
#define PRINTX(s, v)  { Serial.print(F(s)); Serial.print(v, HEX); }
#define PRINTS(s)   Serial.print(F(s));
#else
#define PRINT(s, v)
#define PRINTS(s)
#endif

#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))

// Define the number of devices we have in the chain and the hardware interface
// NOTE: These pin numbers will probably not work with your hardware and may
// need to be adapted
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

#define CLK_PIN   13  // or SCK
#define DATA_PIN  11  // or MOSI
#define CS_PIN    9  // or SS

// SPI hardware interface
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
// Arbitrary pins
//MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

// Analog input pin for the input value
#define BTN1  8
#define BTN2  7
#define BUZZER  10

// Display and animation parameters
#define CHAR_SPACING  1 // pixels between characters
#define CHAR_COLS 5     // should match the fixed width character columns
#define ANIMATION_FRAME_DELAY 30  // in milliseconds

// Structure to hold the data for each character to be displayed and animated
// this could be expanded to include other character specific data (eg, column
// where it starts if display is spaced irregularly).
struct digitData
{
  uint8_t oldValue, newValue;   // ASCII value for the character
  uint8_t index;                // animation progression index
  uint32_t  timeLastFrame;      // time the last frame started animating
  uint8_t charCols;             // number of valid cols in the charMap
  uint8_t charMap[CHAR_COLS];   // character font bitmap
};

void updateDisplay(uint16_t numDigits, struct digitData *d)
// do the necessary to display current bitmap buffer to the LED display
{
  uint8_t   curCol = 0;

  mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
  mx.clear();

  for (int8_t i = numDigits - 1; i >= 0; i--)
  {
    for (int8_t j = d[i].charCols - 1; j >= 0; j--)
    {
      mx.setColumn(curCol++, d[i].charMap[j]);
    }
    curCol += CHAR_SPACING;
  }
  mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}

bool isNumber(char c)
{
	return c >= '0' && c <= '9';
}

#define DIGITS_SIZE 5

boolean displayValue(char str[DIGITS_SIZE])
// Display the required value on the LED matrix and return true if an animation is current
// Finite state machine will ignore new values while animations are underway.
// Needs to be called repeatedly to ensure animations are completed smoothly.
{
  static struct digitData digit[DIGITS_SIZE];

  const uint8_t ST_INIT = 0, ST_WAIT = 1, ST_ANIM = 2;
  static uint8_t  state = ST_INIT;

  // finite state machine to control what we do
  switch(state)
  {
    case ST_INIT: // Initialize the display - done once only on first call
      PRINTS("\nST_INIT");
      for (int8_t i = 0; i < DIGITS_SIZE; i++)
      {
        digit[i].oldValue = str[i];
      }

      // Display the starting number
      for (int8_t i = DIGITS_SIZE - 1; i >= 0; i--)
      {
        digit[i].charCols = mx.getChar(digit[i].oldValue, CHAR_COLS, digit[i].charMap);
      }
      updateDisplay(DIGITS_SIZE, digit);

      // Now we wait for a change
      state = ST_WAIT;
      break;

    case ST_WAIT: // not animating - save new value digits and check if we need to animate
      PRINTS("\nST_WAIT");
      for (int8_t i = 0; i < DIGITS_SIZE; i++)
      {
        // separate digits
        digit[i].newValue = str[i];

        if (digit[i].newValue != digit[i].oldValue)
        {
          // a change has been found - we will be animating something
          state = ST_ANIM;
          // initialize animation parameters for this digit
          digit[i].index = 0;
          digit[i].timeLastFrame = 0;
        }
      }

      if (state == ST_WAIT) // no changes - keep waiting
        break;
      // else fall through as we need to animate from now

    case ST_ANIM: // currently animating a change
      // work out the new intermediate bitmap for each character
      // 1. Get the 'new' character bitmap into temp buffer
      // 2. Shift this buffer down or up by current index amount
      // 3. Shift the current character by one pixel up or down
      // 4. Combine the new partial character and the existing character to produce a frame
      for (int8_t i = DIGITS_SIZE - 1; i >= 0; i--)
      {
        if ((digit[i].newValue != digit[i].oldValue) && // values are different
           (millis() - digit[i].timeLastFrame >= ANIMATION_FRAME_DELAY)) // timer has expired
        {
          uint8_t newChar[CHAR_COLS] = { 0 };

          if (!isNumber(digit[i].oldValue))
            continue;
          PRINT("\nST_ANIM Digit ", i);
          PRINT(" from '", (char)digit[i].oldValue);
          PRINT("' to '", (char)digit[i].newValue);
          PRINT("' index ", digit[i].index);

          mx.getChar(digit[i].newValue, CHAR_COLS, newChar);
            // scroll down
            for (uint8_t j = 0; j < digit[i].charCols; j++)
            {
              newChar[j] = newChar[j] >> (COL_SIZE - 1 - digit[i].index);
              digit[i].charMap[j] = digit[i].charMap[j] << 1;
              digit[i].charMap[j] |= newChar[j];
            }

          // set new parameters for next animation and check if we are done
          digit[i].index++;
          digit[i].timeLastFrame = millis();
          if (digit[i].index >= COL_SIZE )
            digit[i].oldValue = digit[i].newValue;  // done animating
        }
      }

      updateDisplay(DIGITS_SIZE, digit);  // show new display

      // are we done animating?
      {
        boolean allDone = true;

        for (uint8_t i = 0; allDone && (i < DIGITS_SIZE); i++)
        {
          allDone = allDone && (digit[i].oldValue == digit[i].newValue);
        }

        if (allDone) state = ST_WAIT;
      }
      break;

    default:
      state = 0;
  }

  return(state == ST_WAIT);   // animation has ended
}

char disp_str[DIGITS_SIZE + 1] = "00:00";

#define BIP_DELAY 500
char bip_state = 0;

unsigned int last_sampled_millis = 0;
unsigned int last_count_millis = 0;
unsigned int last_bip_millis = 0;

bool start_count = 0;

enum state {
  STATE_IDLE,
  STATE_COUNTING,
  STATE_BEEPING,
};

enum state state;

#define BUTTONS			2
#define DEBOUNCE_DELAY_MS	15

#define STEPS	19

uint8_t cur_step = 0;
uint8_t minutes_steps[STEPS] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20, 30, 40, 50, 60, 70, 80, 90};

uint8_t min, sec;

struct button {
	char button;
	unsigned short sampling;
};

struct button buttons[BUTTONS] = {
	{BTN1, 0}, 
	{BTN2, 0}
};

void setup()
{

  Serial.begin(115200);

  PRINTS("\n[MD_MAX72xx PushWheel]")

  mx.begin();
  mx.setFont(numeric7Seg);

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, 0);
}

void debounce()
{
	int i, val;
	unsigned int cur_millis;

	cur_millis = millis();
	if (cur_millis - last_sampled_millis <= DEBOUNCE_DELAY_MS)
		return;

	last_sampled_millis = cur_millis;
	for (i = 0; i < BUTTONS; i++) {
		val = digitalRead(buttons[i].button);
		buttons[i].sampling <<= 1;
		buttons[i].sampling |= val;
	}
}

int button_pressed(int button)
{
	int ret = (buttons[button].sampling == (unsigned short) (-1));
	if (ret)
		buttons[button].sampling = 0;

	return ret;
}

static void handle_state()
{
	unsigned int cur_millis = millis();
	switch (state) {
	case STATE_IDLE:
		if (button_pressed(0)) {
			min = minutes_steps[cur_step];
			sec = 0;
			sprintf(disp_str, "%02d:00", minutes_steps[cur_step]);

			cur_step++;
			if (cur_step == STEPS)
				cur_step = 0;
		
		}

		if (button_pressed(1)) {
			cur_step = 0;
			state = STATE_COUNTING;
		}
		break;
	case STATE_COUNTING:
		if (button_pressed(0) || button_pressed(1))
			state = STATE_IDLE;

		if ((cur_millis - last_count_millis) < 1000)
			break;

		if (sec == 0) {
			if (min == 0) {
				state = STATE_BEEPING;
			} else {	
				min--;
				sec = 59;
			}
		} else {
			sec--;
		}

		sprintf(disp_str, "%02d:%02d", min, sec);
		last_count_millis = cur_millis;

		break;
	case STATE_BEEPING:
		if (button_pressed(0) || button_pressed(1)) {
			state = STATE_IDLE;
			noTone(BUZZER);
		}

		if ((cur_millis - last_bip_millis) < BIP_DELAY)
			break;

		if (bip_state == 1) {
			noTone(BUZZER);
			bip_state = 0;
		} else {
			tone(BUZZER, NOTE_D5);
			bip_state = 1;
		}

		last_bip_millis = cur_millis;

		break;
	default:
		break;
	}

}

void loop()
{
	debounce();

	handle_state();

	displayValue(disp_str);
}
