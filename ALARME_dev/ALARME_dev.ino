#include <gprs.h>
#include <softwareserial.h>
#define TIMEOUT 5000

//ALARME COMMANDS
int Alarme_state = 0;

//TEMPO
unsigned long start_timer, current_time;       //Variables use for timer
int secondes = 0, minutes = 0;
const int period = 1, tempo_IO = 40;           //timers period in secondes / Tempo IN OUT in secondes
const int auto_activate_time = 20;             //Time before auto activation in minutes
bool led_osc = LOW;

//SIM CARD
GPRS gprs;                                     //Num Alarm : +33644824601
int sms_number = "+33624944955";               //Number to alert
String ping_number = "!";                      //Save last number received and send it if different than sms_number
char ping_numberToChar[12];                    //Convert "ping_number" To Char (GPRS needs Char)
char currentLine[50] = "";                     //Variables to hold last line of serial output from SIM800
int currentLineIndex = 0;
bool nextLineIsMessage = false;
String message = "";                           //String message To Send
char messageToChar[50];                        //Convert "message" To Char (GPRS needs Char)

//EXT COMMUNICATION IO
const int led_R = 12;
const int led_G = 13;
const int led_B = 11;
const int piezo_pin = 2;
const int relay_pin = 5;
const int buzz_pin = 10;
//BUTTON IO
const int B_sms = 4;                           //Activate (LOW) or desactivate (PULLUP) SMS SERVICES
const int B_test = 3;                          //Test Button (use for debug)
const int B_light = 6;                         //Light ON/OFF button (activate relay_pin)
//PIRS
int pir_1_pin = A0;
int pir_2_pin = A1;
//POWER SUPPLY IO
const int BA_input = A2;                       //BATTERY (12v)
const int DC_input = A3;                       //DC INPUT (12v)
float BA_volt;
float DC_volt;
float R1 = 100000.00;                          //Resistance R1 (100K)
float R2 = 10000.00;                           //Resistance R2 (10K)
int volt_transmit[3] = {0, 0, 1};              //Allows voltages sms to be sent only once (DC Alert,BATT Alert,"Power?" answer)


void setup() {
  //Define OUTPUT
  pinMode(led_R, OUTPUT);
  pinMode(led_G, OUTPUT);
  pinMode(led_B, OUTPUT);
  pinMode(piezo_pin, OUTPUT);
  pinMode(relay_pin, OUTPUT);
  pinMode(buzz_pin, OUTPUT);
  //Define INPUT
  pinMode(pir_1_pin, INPUT);
  pinMode(pir_2_pin, INPUT);
  pinMode(B_sms, INPUT_PULLUP);
  pinMode(B_test, INPUT_PULLUP);
  pinMode(B_light, INPUT);
  pinMode(BA_input, INPUT);
  pinMode(DC_input, INPUT);

  //Activate Serial comm                          //TEST
  //Serial.begin(9600);
  //while (!Serial);

  //SIM CARD activation if B_sms LOW
  if (!digitalRead(B_sms))
  {
    //Serial.println("Starting SIM800 SMS Command Processor");
    gprs.preInit();
    delay(1000);
    while (0 != gprs.init())
    {
      delay(1000);
      //Serial.print("init error\r\n");
    }
    //Set SMS mode to ASCII
    if (0 != gprs.sendCmdAndWaitForResp("AT+CMGF=1\r\n", "OK", TIMEOUT)) {
      ERROR("ERROR:CNMI");
      return;
    }
    //Start listening to New SMS Message Indications
    if (0 != gprs.sendCmdAndWaitForResp("AT+CNMI=1,2,0,0,0\r\n", "OK", TIMEOUT)) {
      ERROR("ERROR:CNMI");
      return;
    }
    //Serial.println("SIM800 : Ready!");
  } else {
    //Serial.println("GSM Service OFFline");
  }

  //Define Alarm ON or Standby on start (use Light switch)
  if (digitalRead(B_light))
  {
    tone(piezo_pin, 500, 100);
    Alarme_state = 0;                           //Set to "0" Alarm starts when Arduino is turned ON
  } else {
    tone(piezo_pin, 1000, 100);
    Alarme_state = 1;                           //Set to "1" Alarm Standby (wait for sms)
    digitalWrite(buzz_pin, HIGH);               //BUZZ Alert : Alarm is ON
    delay(80);
    digitalWrite(buzz_pin, LOW);
  }
  start_timer = millis();                      //Start timer millis
}


void loop() {
  function_send_sms(0);                       //Send SMS set to false (avoid sending more than one SMS at the time)
  function_read_sms();                        //Read SMS

  power_check();                              //Read POWER voltage from DC and BATERRY
  tempo_secondes(period);                     //Update timer

  //TEST button
  //Use to stop Alarm at any time.
  if (!digitalRead(B_test) && Alarme_state != 0) {
    delay(200);
    tone(piezo_pin, 1200, 50);
    reset_timer();
    minutes = 0;
    Alarme_state = 0;
    //Else If Alarm is standby : Start function TEST
  } else if (!digitalRead(B_test)) {
    delay(200);
    tone(piezo_pin, 1200, 50);
    reset_timer();
    minutes = 0;
    Alarme_state = 5;
  }
  
  //LIGHT button
  //If Alarm is standby : Switch ON relay pin.
  if (digitalRead(B_light) && Alarme_state == 0) {
    digitalWrite(relay_pin, HIGH);
  } else {
    digitalWrite(relay_pin, LOW);
  }

  //ALARM SWITCH CASE
  //defined what to do for each Alarm states (Standby, ON, IN MOTION, OFF, ALERT, TEST)
  switch (Alarme_state) {
    case 0: //Standby instructions
      auto_activate();
      message = "Standby " + ping_number;
      digitalWrite(led_B, HIGH); digitalWrite(led_G, LOW); digitalWrite(led_R, LOW);
      digitalWrite(buzz_pin, LOW);
      break;

    case 1: //ON instructions
      message = "ON " + ping_number;
      digitalWrite(led_B, LOW); digitalWrite(led_G, led_osc); digitalWrite(led_R, LOW);
      digitalWrite(relay_pin, LOW);
      digitalWrite(buzz_pin, LOW);
      if (led_osc) tone(piezo_pin, 1200, 50);
      if (secondes >= tempo_IO) {
        reset_timer();
        tone(piezo_pin, 1000, 100);
        while (1 != function_send_sms(1));
        Alarme_state = 2;
      }
      break;

    case 2: //IN MOTION instructions
      message = "IN MOTION " + ping_number;
      digitalWrite(led_B, LOW);
      digitalWrite(relay_pin, LOW);
      digitalWrite(buzz_pin, LOW);
      if (digitalRead(pir_1_pin) || digitalRead(pir_2_pin)) {
        digitalWrite(led_G, led_osc);
        digitalWrite(led_R, !led_osc);
        if (led_osc) tone(piezo_pin, 1200, 50);
      } else if (!digitalRead(pir_1_pin) && !digitalRead(pir_2_pin)) {
        reset_timer();
        digitalWrite(led_R, HIGH);
        digitalWrite(led_G, LOW);
      }
      if ((digitalRead(pir_1_pin) || digitalRead(pir_2_pin)) && secondes >= tempo_IO) {
        reset_timer();
        message = "ALERT " + ping_number;
        while (1 != function_send_sms(1));
        Alarme_state = 4;
      }
      break;

    case 3: //OFF instructions
      message = "OFF " + ping_number;
      digitalWrite(led_B, LOW); digitalWrite(led_G, LOW); digitalWrite(led_R, LOW);
      digitalWrite(relay_pin, LOW);
      digitalWrite(buzz_pin, LOW);
      tone(piezo_pin, 500, 100);
      
      while (1 != function_send_sms(1));
      Alarme_state = 0;
      break;

    case 4: //ALERT instructions
      message = "ALERT " + ping_number;
      digitalWrite(led_B, LOW); digitalWrite(led_G, LOW);
      digitalWrite(led_R, led_osc);
      digitalWrite(relay_pin, !led_osc);
      digitalWrite(buzz_pin, HIGH);
      if (!led_osc) tone(piezo_pin, 750, 500);

      if (secondes == tempo_IO) {
        reset_timer();
        while (1 != function_send_sms(1));
        Alarme_state = 2;
      }
      break;

    case 5: //TEST instructions
      message = "TEST " + ping_number;
      function_test();
      break;
  }
}


//SEND SMS FUNCTION
int function_send_sms(bool sms_reset) {
  if (sms_reset == 0) return false;
  if (digitalRead(B_sms) == LOW) {
    message.toCharArray(messageToChar, 50);
    gprs.sendSMS (sms_number, messageToChar);
    if (ping_number != sms_number) {
      ping_number.toCharArray(ping_numberToChar, 12);
      gprs.sendSMS (ping_numberToChar, messageToChar);
    }
    tone(piezo_pin, 1200, 50);
    return true;
  } else {
    //Serial.println(message);
    return true;
  }
}
//RECEIVE SMS FUNCTION
int function_read_sms() {
  //If there is serial output from SIM800
  if (gprs.serialSIM800.available()) {
    char lastCharRead = gprs.serialSIM800.read();
    //Read each character from serial output until \r or \n is reached (which denotes end of line)
    if (lastCharRead == '\r' || lastCharRead == '\n') {
      String lastLine = String(currentLine);
      //If last line read +CMT, New SMS Message Indications was received.
      //Hence, next line is the message content.
      if (lastLine.startsWith("+CMT:")) {
        //Serial.println(lastLine);
        ping_number = lastLine.substring(7, 19);
        if (ping_number == sms_number) {
          ping_number = "!";
        }
        nextLineIsMessage = true;
      } else if (lastLine.length() > 0) {
        if (nextLineIsMessage) {
          //Serial.println(lastLine);
          //Serial.println(message);
          //Read message content and set Alarm according to SMS content
          if (lastLine.indexOf("ON") >= 0) {                                      //TURN ON Alarm
            reset_timer();
            digitalWrite(buzz_pin, HIGH);                                         //BUZZ Alert : Alarm is OFF
            delay(80);
            digitalWrite(buzz_pin, LOW);
            Alarme_state = 1;
          } else if (lastLine.indexOf("OFF") >= 0) {                              //TURN OFF Alarm
            reset_timer();
            minutes = 0;
            digitalWrite(buzz_pin, HIGH);                                         //BUZZ Alert : Alarm is OFF
            delay(80);
            digitalWrite(buzz_pin, LOW);
            Alarme_state = 3;
          } else if (lastLine.indexOf("State?") >= 0) {                           //Returns Alarme_state
            function_send_sms(1);
          } else if (lastLine.indexOf("Power?") >= 0) {                           //Returns DC/BATT voltages
            volt_transmit[0] = 0;
            volt_transmit[1] = 0;
            volt_transmit[2] = 0;
            power_check();
          }
          nextLineIsMessage = false;
        }
      }
      //Clear char array for next line of read
      for ( int i = 0; i < sizeof(currentLine);  ++i ) {
        currentLine[i] = (char)0;
      }
      currentLineIndex = 0;
    } else {
      currentLine[currentLineIndex++] = lastCharRead;
    }
  }
}


//TEST FUNCTION
int function_test() {
  digitalWrite(led_B, LOW); digitalWrite(led_G, LOW); digitalWrite(led_R, LOW);
  digitalWrite(relay_pin, LOW); digitalWrite(buzz_pin, LOW);

  while (secondes == 1) {                                                           //TEST BLUE LED
    tempo_secondes(period);
    tone(piezo_pin, 250, 100);
    digitalWrite(led_B, HIGH); digitalWrite(led_G, LOW); digitalWrite(led_R, LOW);
  }
  while (secondes == 2) {                                                           //TEST GREEN LED
    tempo_secondes(period);
    tone(piezo_pin, 500, 100);
    digitalWrite(led_B, LOW); digitalWrite(led_G, HIGH); digitalWrite(led_R, LOW);
  }
  while (secondes == 3) {                                                           //TEST RED LED
    tempo_secondes(period);
    tone(piezo_pin, 750, 100);
    digitalWrite(led_B, LOW); digitalWrite(led_G, LOW); digitalWrite(led_R, HIGH);
  }
  while (secondes == 4) {                                                           //TEST RELAY
    tempo_secondes(period);
    tone(piezo_pin, 1000, 100);
    digitalWrite(led_B, LOW); digitalWrite(led_G, LOW); digitalWrite(led_R, LOW);
    digitalWrite(relay_pin, HIGH);
  }
  while (secondes == 5) {                                                           //TEST BUZZER
    tempo_secondes(period);
    digitalWrite(relay_pin, LOW);
    digitalWrite(buzz_pin, HIGH);
    delay(300);
    digitalWrite(buzz_pin, LOW);
  }
  if (secondes >= 6) {                                                              //TEST SMS
    tone(piezo_pin, 1200, 400);
    Alarme_state = 0;
    while (1 != function_send_sms(1));
  }
}


//POWER FUNCTION
int power_check() {
  //Ajustment to find the values as seen on voltmeter (-3V for here)
  DC_volt = ((analogRead(DC_input) * 5.00 / 1024.00) / (R2 / (R1 + R2))) - 3;
  BA_volt = ((analogRead(BA_input) * 5.00 / 1024.00) / (R2 / (R1 + R2))) - 3;

  if (DC_volt == 0.00 && volt_transmit[0] == 0) {
    message = "DC POWER DOWN " + ping_number;
    while (1 != function_send_sms(1));
    tone(piezo_pin, 750, 1000);
    volt_transmit[0] = 1;
  }
  if (BA_volt <= 11.00 && volt_transmit[1] == 0) {
    message = "BATTERY LOW " + ping_number;
    while (1 != function_send_sms(1));
    tone(piezo_pin, 750, 1000);
    volt_transmit[1] = 1;
  }

  if (volt_transmit[2] == 0) {
    message = "DC POWER : ";
    message.concat(DC_volt);
    while (1 != function_send_sms(1));
    message = "BATT POWER : ";
    message.concat(BA_volt);
    while (1 != function_send_sms(1));
    volt_transmit[2] = 1;
  }
}


//TIMERS
int tempo_secondes(int timer) {
  current_time = millis();
  if ((current_time - start_timer) >= timer * 1000)
  {
    start_timer = current_time;
    secondes++ ;
    minutes = secondes / 60;
    led_osc = !led_osc;
  }
}
int reset_timer() {
  secondes = 0;
}


//AUTO ACTIVATION
//When Standby for more than "auto_activate_time" (20min) with no movement: automatically switch ON
int auto_activate() {
  if (auto_activate_time >= minutes) {
    //
    if (!digitalRead(pir_1_pin) && !digitalRead(pir_2_pin)) {
      tone(piezo_pin, 1000, 1000);
      reset_timer();
      minutes = 0;
      Alarme_state = 1;
    } else {
      reset_timer();
      minutes = 0;
      Alarme_state = 0;
    }
  }
}
