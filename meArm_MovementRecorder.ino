/* meArm movement recorder - Bob Stone May 2014
 *
 * Hardware required:
 *
 * Arduino
 * Phenoptix meArm
 * Adafruit 16-channel PWM Servo driver
 * Wii Classic game controller
 * WiiChuck connector
 * Nokia 5110 LCD display
 *
 * Basic movements:
 *
 * Using inverse kinematics with Wii Classic game controller
 * First stick moves gripper forwards, backwards, left and right
 * Second stick moves gripper up and down (and left and right again).
 * Shoulder buttons open and close the gripper.  The home button 
 * returns to starting point.
 * 
 * Recording and playing back:
 *
 * Use the cursor pad to navigate the on-screen menu, and A or B buttons to 
 * select a menu choice.  Record steps as points in space the arm will 
 * travel between.  Opening and closing the gripper also records a data step.
 * Select 'Replay all steps' to go through the programmed motions and return
 * to the starting position.
 *
 * Uses meArm_Adafruit library from https://github.com/RorschachUK/meArm_Adafruit
 * Uses ClassicController library from https://github.com/wayneandlayne/Video-Game-Shield
 * Uses Adafruit Nokia library from https://github.com/adafruit/Adafruit-PCD8544-Nokia-5110-LCD-library
 * Uses Adafruit GFX library from https://github.com/adafruit/Adafruit-GFX-Library
 * Uses Adafruit PWM Servo library from https://github.com/adafruit/Adafruit-PWM-Servo-Driver-Library
 * Uses QueueList library from http://playground.arduino.cc/Code/QueueList
 * Connect the Wii Classic gamepad via a breakout such as
 * http://www.phenoptix.com/collections/breakout-board/products/wiichuck-a-wii-nunchuck-break-out-board-by-seeed-studio
 *
 * Pins:
 * Arduino    PWMServo  WiiChuck  Nokia
 *    GND         GND        -      Gnd
 *     5V    VCC & V+        +
 *   3.3V                        Vcc+BL*
 *     A4         SDA        d
 *     A5         SCL        c
 *     A2                            DC*
 *     A1                            CE*
 *     A0                           RST*
 *      2                           Clk*
 *      3                           Din*
 *                          *=Use resistor - 10k on the data pins, 100ohm on BL
 *
 * The servos attach to the first block of four servo connections on
 * the Adafruit board, brown wire at the bottom, yellow wire at the top.
 * Adafruit    Servo
 *       0      Base
 *       1  Shoulder (right)
 *       2     Elbow (left)
 *       3   Gripper
 *
 * You can attach to a different block of four by changing the 'begin' call
 * to specify a block 0-3, e.g. to use the second block call arm.begin(1);
 * - to mirror movements to all 4 blocks, call arm.begin(-1);
 * 
 * The Nokia connections are expecting 3V3, so from a 5V Arduino we use
 * resistors - 10k on the GPIO pins (DC, CE, RST, Clk, Din) and 100ohm on the
 * backlight BL.
 */
#include "meArm_Adafruit.h"
#include <Adafruit_PWMServoDriver.h>
#include <Wire.h>
#include <ClassicController.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <QueueList.h>

//Class holding one instruction
class instruction {
  public:
    instruction(bool grip, float x, float y, float z) {
      _type = "g";
      _x=x; _y=y; _z=z;
      _gripper = grip;
    }
    instruction(float x, float y, float z) {
      _type="m";
      _x=x; _y=y; _z=z; _gripper = false;
    }
    instruction() {
      _type="";
      _x=0; _y=0; _z=0; _gripper = false;
    }
    String _type;
    float _x,_y,_z;
    bool _gripper;
};

//hardware instances
meArm arm;
ClassicController cc;
Adafruit_PCD8544 nokia = Adafruit_PCD8544(2, 3, A2, A1, A0);

//Instructions list
QueueList<instruction> instructions;

//Menu control
int cursor = 0;
bool lastButtonPushed = true;

void setup() {
  //Setup static elements of UI
  nokia.begin();
  nokia.setContrast(60);
  nokia.clearDisplay(); // show splashscreen
  nokia.setTextSize(1);
  nokia.setTextColor(BLACK);
  nokia.setCursor(8,0);
  nokia.println(F("Save step"));
  nokia.setCursor(8,8);
  nokia.println(F("Clear all"));
  nokia.setCursor(8,16);
  nokia.println(F("Replay steps"));
  nokia.display();

  arm.begin();
  arm.openGripper();

  cc.begin(WII_PLAYER_1);
  cc.joy_left_set_scaled_min_max(0, 99, 0, 99);
  cc.joy_right_set_scaled_min_max(0, 99, 0, 99);
}

void loop() {
  //read controller state
  cc.update();
  float dx = 0;
  float dy = 0;
  float dz = 0;
  float dg = 0;
  //certain buttons should not repeat continually
  bool currentButton = cc.button_up() || cc.button_down() || cc.button_home() 
       || cc.button_a() || cc.button_b() || cc.shoulder_right() > 16 
       || cc.shoulder_left() > 16;
  if (lastButtonPushed && !currentButton) {
    lastButtonPushed = false;
  } else if (currentButton && !lastButtonPushed) {
    lastButtonPushed = true;
    if (cc.button_home()) {
      //reset to start point
      arm.gotoPoint(0, 100, 50);
      arm.openGripper();
    } 
    if (cc.button_a() || cc.button_b()) {
      //Menu choice button pushed
      switch(cursor) {
        case 0: { // save instruction
            instruction newInstr(arm.getX(), arm.getY(), arm.getZ());
            instructions.push(newInstr);
            break;
          }
        case 1: { // clear instructions
          while(!instructions.isEmpty())
            instructions.pop();
          break;
          }
        case 2: { // replay instructions
            //reset to start state
            arm.gotoPoint(0,100,50);
            arm.openGripper();
            //go through steps
            for (int i=0; i< instructions.count(); i++) {
              instruction instr = instructions.pop();
              if (instr._type == "g")
                if (instr._gripper) {
                  arm.gotoPoint(instr._x, instr._y, instr._z);
                  arm.openGripper();
                } else {
                  arm.gotoPoint(instr._x, instr._y, instr._z);
                  arm.closeGripper();
                }
              else if (instr._type == "m")
                arm.gotoPoint(instr._x, instr._y, instr._z);
              instructions.push(instr);
            }
            //reset to start state
            arm.gotoPoint(0,100,50);
            arm.openGripper();
            break;
          }
      }          
    }
    //Menu movements
    if (cc.button_up() && cursor > 0)
      cursor--;
    else if (cc.button_down() && cursor < 2)
      cursor++;
    //gripper control
    if (cc.shoulder_left() > 16) {
      arm.openGripper();
      instruction newInstr(true, arm.getX(), arm.getY(), arm.getZ());
      instructions.push(newInstr);
    } else if (cc.shoulder_right() > 16) {
      arm.closeGripper();
      instruction newInstr(false, arm.getX(), arm.getY(), arm.getZ());
      instructions.push(newInstr);
    }
  }

  //Movements
  if (cc.joy_left_left() || cc.joy_right_left())
    dx = -5.0;
  else if (cc.joy_left_right() || cc.joy_right_right())
    dx = 5.0;
  if (cc.joy_left_up())
    dy = 5.0;
  else if (cc.joy_left_down())
    dy = -5.0;
  if (cc.joy_right_up())
    dz = 5.0;
  else if (cc.joy_right_down())
    dz = -5.0;

  //Move
  if (!(dx == 0 && dy == 0 && dz == 0))
    arm.goDirectlyTo(arm.getX() + dx, arm.getY() + dy, arm.getZ() + dz);

  //Update UI
  nokia.fillRect(0,32, 84, 16, WHITE);
  nokia.fillRect(0,0, 8,24, WHITE);
  nokia.setCursor(0, cursor * 8);
  nokia.print(">");
  nokia.setCursor(0,32);
  nokia.print(instructions.count());
  nokia.print(F(" steps saved"));
  nokia.setCursor(0,40);
  nokia.print("(");
  nokia.print(int(arm.getX()));
  nokia.print(",");
  nokia.print(int(arm.getY()));
  nokia.print(",");
  nokia.print(int(arm.getZ()));
  nokia.print(")");
  nokia.display();
 
  delay(20);
}
