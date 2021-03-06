#include <Servo.h>
#include <Stepper.h>
#include <Wire.h>
#include <EEPROM.h>

// Remove the bluetooth device before uploading the code

#define ENA 5
#define ENB 6
#define IN1 7
#define IN2 8
#define IN3 9
#define IN4 11
#define LED 13
#define STEPS  32
#define ITERAZIONIOSTACOLI 3
#define DIM_VARIATIONS 20

enum state{goingForward, goingBackward, turningRight, turningLeft, stayingStill, blocked} currentState, possibleState;// New type to describe the robot state

Stepper small_stepper(STEPS, 2, 10, 4, 12);
Servo head;

// Variables for hardware  
int  Steps2Take; 
unsigned char carSpeed = 150;
bool state = LOW;
char getstr;
int Echo = A4;  
int Trig = A5;
boolean lastActionTurning = false; 

//Functions
void timer(unsigned long differenzaTempo, boolean flagObstacle, boolean flagTimer);
void forward();
void back();
void stop();
void GY_control();

//Variables for gyroscope control
const int MPU_ADDR = 0x68; // I2C address of the MPU-6050. If AD0 pin is set to HIGH, the I2C address will be 0x69.
int16_t accelerometer_x, accelerometer_y, accelerometer_z; // variables for accelerometer raw data
int16_t gyro_x, gyro_y, gyro_z; // variables for gyro raw data
int16_t temperature; // variables for temperature data
char tmp_str[7]; // temporary variable used in convert function
int16_t GY_x[10]; //array with the newest gyroscope values
float variations[DIM_VARIATIONS]; //array with the last variation values
float stopLimit;//max variation value when the robot is still 
float hitLimit;//min variation value if the robot hits something
boolean possibleHIT;




//-------------------------------------------------------------- HARDWARE SECTION --------------------------------------------------------------------\\

//Ultrasonic distance measurement Sub function
int Distance_test() {
  digitalWrite(Trig, LOW);   
  delayMicroseconds(2);
  digitalWrite(Trig, HIGH);  
  delayMicroseconds(20);
  digitalWrite(Trig, LOW);   
  float Fdistance = pulseIn(Echo, HIGH);  
  Fdistance= Fdistance / 58.2;       
  return (int)Fdistance;
}

void armup(){
  small_stepper.setSpeed(500);
  Steps2Take  =  500; 
  small_stepper.step(Steps2Take);
  delay(1000); 
  Serial.println("Arm Up")  ;
  }
  

void armdown(){
  small_stepper.setSpeed(500);
  Steps2Take  =  -500; 
  small_stepper.step(Steps2Take);
  delay(1000);
  Serial.println("Arm Down")  ;
  }

void headCenter(){
  head.write(90);
  Serial.println("Head Center");
}

void back(){ 
  digitalWrite(ENA,HIGH);
  digitalWrite(ENB,HIGH);
  digitalWrite(IN1,LOW);
  digitalWrite(IN2,HIGH);
  digitalWrite(IN3,HIGH);
  digitalWrite(IN4,LOW);
  currentState = goingBackward;
  Serial.println("Back");
}

void forward(){
  digitalWrite(ENA,HIGH);
  digitalWrite(ENB,HIGH);
  digitalWrite(IN1,HIGH);
  digitalWrite(IN2,LOW);
  digitalWrite(IN3,LOW);
  digitalWrite(IN4,HIGH);
  currentState = goingForward;
  Serial.println("Forward");
}

void right(){
  digitalWrite(ENA,HIGH);
  digitalWrite(ENB,HIGH);
  digitalWrite(IN1,HIGH);
  digitalWrite(IN2,LOW);
  digitalWrite(IN3,HIGH);
  digitalWrite(IN4,LOW);
  currentState = turningRight;
  Serial.println("Right");
}

void left(){
  digitalWrite(ENA,HIGH);
  digitalWrite(ENB,HIGH);
  digitalWrite(IN1,LOW);
  digitalWrite(IN2,HIGH);
  digitalWrite(IN3,LOW);
  digitalWrite(IN4,HIGH);
  currentState = turningLeft;
  Serial.println("Left");
}

void stop(){
  digitalWrite(ENA,LOW);
  digitalWrite(ENB,LOW);
  currentState = stayingStill;
  Serial.println("Stop!");
}

void stateChange(){
  state = !state;
  digitalWrite(LED, state);
  Serial.println("Light");  
}





//--------------------------------------------------------------------------------------------------------------------------------------------------\\


//--------------------------------------------------------------- SETUP SECTION --------------------------------------------------------------------\\

void GYSetup() //function for the GY setup
{
  byte error, address;
  int nDevices;
 
  Serial.println("Scanning...");
 
  nDevices = 0;
  for(address = 1; address < 127; address++ ) 
  {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
 
    if (error == 0)
    {
      Serial.print("I2C device found at address 0x");
      if (address<16) 
        Serial.print("0");
      Serial.print(address,HEX);
      Serial.println("  !");
 
      nDevices++;
    }
    else if (error==4) 
    {
      Serial.print("Unknow error at address 0x");
      if (address<16) 
        Serial.print("0");
      Serial.println(address,HEX);
    }    
  }
  if (nDevices == 0)
    Serial.println("No I2C devices found\n");
  else
    Serial.println("done\n");
 
  delay(5000);           // wait 5 seconds for next scan
}

//Function to inizialize the arrays
void setupValues() {
  int cont;
  // Inizialize the GY_x array
  for(cont=0; cont<10; cont+= 1)
  {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x3B); // starting with register 0x3B (ACCEL_XOUT_H) [MPU-6000 and MPU-6050 Register Map and Descriptions Revision 4.2, p.40]
    Wire.endTransmission(false); // the parameter indicates that the Arduino will send a restart. As a result, the connection is kept active.
    Wire.requestFrom(MPU_ADDR, 7*2, true); // request a total of 7*2=14 registers
    GY_x[cont]= Wire.read()<<8 | Wire.read();
    delay(5);
  }
}


//Function to calculate the standard deviation for the limits calcule. The parameters are punctators to the average and devStandard variables in findLimits(), so the values are directly saved there
void deviationStandard(float* average, float* devStandard){
  float sum=0;
  float values[200];
  int cont;
  *devStandard = 0;

  for(cont=0;cont<200;cont+=1){
    values[cont]=GY_control(false);
    sum = sum + values[cont];
    delay(5);
  }


  *average = sum/200;

  for(cont=0;cont<200;cont+=1){
    *devStandard = *devStandard + pow(((double)values[cont] - (double)*average),2); 
  }
  *devStandard = sqrt(*devStandard/200);
  Serial.print("Media: ");
  Serial.println(*average);
  Serial.print("Deviazione Standard: ");
  Serial.println(*devStandard);
}


//Function to find the max variation value for when the robot is still and for when is moving 
void findLimits(){
  float average; 
  float devStandard;

  stop();
  deviationStandard(&average, &devStandard);
  stopLimit = average + devStandard;

  forward();
  delay(10);
  deviationStandard(&average, &devStandard);
  delay(10);
  stop();

  hitLimit = average + devStandard;
}


void setup() {
  delay(1000);
  Serial.begin(9600);
 
  Wire.begin(); //Gyroscope setup
  Wire.beginTransmission(MPU_ADDR); // Begins a transmission to the I2C slave (GY-521 board)
  Wire.write(0x6B); // PWR_MGMT_1 register
  Wire.write(0); // set to zero (wakes up the MPU-6050)
  Wire.endTransmission(true);
  
  digitalWrite(8, LOW); //arm
  digitalWrite(9, LOW);
  digitalWrite(10, LOW);
  digitalWrite(11, LOW);
   
  head.attach(3);  //head
  head.write(90);
  pinMode(Echo, INPUT); 
  pinMode(Trig, OUTPUT);
   
  pinMode(LED, OUTPUT);  //motor 
  pinMode(IN1,OUTPUT);
  pinMode(IN2,OUTPUT);
  pinMode(IN3,OUTPUT);
  pinMode(IN4,OUTPUT);
  pinMode(ENA,OUTPUT);
  pinMode(ENB,OUTPUT);
  
  /*int i=0;
  int16_t valore;
  while(i<1200){
    if (i==240) Serial.println("Partenza!");
    Serial.print("Valore x:");
    EEPROM.get(i, valore);
    Serial.print(valore);
    i+=2;
    Serial.print("//Valore y:");
    EEPROM.get(i, valore);
    Serial.print(valore);
    i+=2;
    Serial.print("//Valore z:");
    EEPROM.get(i, valore);
    Serial.println(valore);
    i+=2;
  }
  Serial.print("Valore finale:");
  Serial.println(EEPROM.read(i));
  Serial.println(i);*/
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------\\

//---------------------------------------------------------------- GYROSCOPE SECTION -------------------------------------------------------------------\\

/*char* convert_int16_to_str(int16_t i) { // converts int16 to string. Moreover, resulting strings will have the same length in the debug monitor.
  sprintf(tmp_str, "%6d", i);
  return tmp_str;
}

void GYValues() {
  int minimo, massimo;
  boolean primo;
  int cont=0, addr=0, i;
  for (i=0; i<EEPROM.length(); i++) {
    EEPROM.write(i, 0);
  }
  while (cont<200) {
   if(cont==40) forward();
    
   if (Serial.available() > 0) {
      getstr = Serial.read();
    if (getstr == 'G') break;
    if (getstr == 'S') stop();
    if (getstr == 'A') forward();
    if (getstr == 'C') back();

   }
  
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B); // starting with register 0x3B (ACCEL_XOUT_H) [MPU-6000 and MPU-6050 Register Map and Descriptions Revision 4.2, p.40]
  Wire.endTransmission(false); // the parameter indicates that the Arduino will send a restart. As a result, the connection is kept active.
  Wire.requestFrom(MPU_ADDR, 7*2, true); // request a total of 7*2=14 registers
  
  // "Wire.read()<<8 | Wire.read();" means two registers are read and stored in the same variable
  accelerometer_x = Wire.read()<<8 | Wire.read(); // reading registers: 0x3B (ACCEL_XOUT_H) and 0x3C (ACCEL_XOUT_L)
  accelerometer_y = Wire.read()<<8 | Wire.read(); // reading registers: 0x3D (ACCEL_YOUT_H) and 0x3E (ACCEL_YOUT_L)
  accelerometer_z = Wire.read()<<8 | Wire.read(); // reading registers: 0x3F (ACCEL_ZOUT_H) and 0x40 (ACCEL_ZOUT_L)
  temperature = Wire.read()<<8 | Wire.read(); // reading registers: 0x41 (TEMP_OUT_H) and 0x42 (TEMP_OUT_L)
  gyro_x = Wire.read()<<8 | Wire.read(); // reading registers: 0x43 (GYRO_XOUT_H) and 0x44 (GYRO_XOUT_L)
  gyro_y = Wire.read()<<8 | Wire.read(); // reading registers: 0x45 (GYRO_YOUT_H) and 0x46 (GYRO_YOUT_L)
  gyro_z = Wire.read()<<8 | Wire.read(); // reading registers: 0x47 (GYRO_ZOUT_H) and 0x48 (GYRO_ZOUT_L)
  
  // print out data
  Serial.print("aX = "); Serial.print(convert_int16_to_str(accelerometer_x));
  Serial.print(" | aY = "); Serial.print(convert_int16_to_str(accelerometer_y));
  Serial.print(" | aZ = "); Serial.print(convert_int16_to_str(accelerometer_z));
  // the following equation was taken from the documentation [MPU-6000/MPU-6050 Register Map and Description, p.30]
  Serial.print(" | tmp = "); Serial.print(temperature/340.00+36.53);
  /*Serial.print(" | gX = "); Serial.print(convert_int16_to_str(gyro_x));
  Serial.print(" | gY = "); Serial.print(convert_int16_to_str(gyro_y));
  Serial.print(" | gZ = "); Serial.print(convert_int16_to_str(gyro_z));
  Serial.println();*/
  /*if (primo) {
    massimo = accelerometer_x;
    minimo = accelerometer_x;
  }
 if (accelerometer_x < minimo) minimo = accelerometer_x;
 if (accelerometer_x > massimo) massimo = accelerometer_x;

 EEPROM.put(addr, accelerometer_x);
 addr+=2;
 EEPROM.put(addr, accelerometer_y);
 addr+=2;
 EEPROM.put(addr, accelerometer_z);
 addr+=2;

 cont+=1;

  // delay
  delay(5);
  }
  Serial.println(minimo);
  Serial.println(massimo);
  EEPROM.write(addr, 1);
  stop();
  Serial.println("Finito");
  Serial.println(addr);
}*/

//Calculates the average of the GY_X array if source is true, ad of the variations array if source is false
float findAverage(boolean source) {
  int cont;
  float sum=0;
  if (source== true){
    for(cont=0;cont<10;cont+=1){
      sum = sum + GY_x[cont];
    }
    return(sum/10);
  }
  else {
    for(cont=0;cont<DIM_VARIATIONS;cont+=1){
      sum = sum + variations[cont];
    }
    return(sum/DIM_VARIATIONS);    
  }
}

//Function to analyse the variation value and determine the robot state
void dataAnalysis(){
  float variationAverage;
  int cont;
  float variationCheck;//variable to controll if there has been a hit
  
  variationAverage = findAverage(false);
  if(variationAverage > hitLimit && currentState == goingForward){//There could have been a hit and the robot is moving forward(doesn't work for backward
    if (currentState == stayingStill){ //If the robot stopped by itself
      Serial.println("Il robot è fermo");
      possibleState == stayingStill;
      return;
    }
    possibleHIT = true;//there could have been a hit
    for (cont=0;cont<5;cont+=1){
      variationCheck = GY_control(false);
      if (variationCheck < stopLimit && currentState == goingForward){ //the robot is trying to move, but it is staying still
        possibleState = blocked;
      }
      if(variationCheck > hitLimit){ //The robot continues to hit something
        possibleState = blocked;
      }
     possibleHIT = false;//The hit wass a false allarm
    }
  }
  else {
    if(variationAverage > stopLimit){
    possibleState = goingForward;
    }
    else{
      possibleState = stayingStill;
      if (currentState == goingForward){
        possibleState = blocked;
      }
    }
   }
  }
  

//Function to calculate the gyroscope value variation when moving forward
float GY_control(boolean analysis){
  int16_t acc_x;
  int iteration;
  for(iteration=0; iteration<DIM_VARIATIONS; iteration+=1){
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x3B); // starting with register 0x3B (ACCEL_XOUT_H) [MPU-6000 and MPU-6050 Register Map and Descriptions Revision 4.2, p.40]
    Wire.endTransmission(false); // the parameter indicates that the Arduino will send a restart. As a result, the connection is kept active.
    Wire.requestFrom(MPU_ADDR, 7*2, true); // request a total of 7*2=14 registers 
    acc_x = Wire.read()<<8 | Wire.read();
    
    float average = findAverage(true);
    float variation;
    int cont;
    
    variation = abs(((float)acc_x-average)/((float)acc_x + average));// Calculates the difference between the new value and the average of the last ten, and normalizes it
    //Updates the GY_X values
    for(cont=1;cont<10;cont+=1){
      GY_x[cont-1] = GY_x[cont];
    }
    GY_x[9] = acc_x;
    
       
    if(analysis==false)  return variation; //Variation is requested to inizialize variations array
    variations[iteration] = variation;
    delay(5);
    //Serial.print("Variazione: ");
    //Serial.println(variation);
  }
   dataAnalysis();  
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------\\


//----------------------------------------------------- OBSTACLES CONTROL SECTION -------------------------------------------------------------------------\\

boolean obstacle(int contatore, boolean flagNotRicorsione, int distanzaMassima) { // function to determine if there's an obstacle. It calls itsself three times, to ddo three controls
  int distanza;                                      // the second parameter tells the function if it has been called for the first time(so while the robot was moving), or by the timer function after an obstacle had already been seen
  distanza = Distance_test(); // distance measured 

  if (distanza < distanzaMassima) { // distance less than 20 cm
    if (contatore == 1 or flagNotRicorsione == true) return (true); // returns true if there's an oobstacle and false instead
    timer(50, true, true);
    return obstacle(contatore-1, false, 20);
  }
  return (false);
}

boolean headLeft(){
    Serial.println("head left");
    int headAngle = head.read() + 50;
    head.write(headAngle);
    while (headAngle < 170) { // turns his head on the left and tells the avoidObsatcle function, that will eventually turn the robot. This until the head hasn't turned for 90 degrees
      headAngle=head.read();
      Serial.println(headAngle);
      headAngle=headAngle + 10;
      head.write(headAngle);
      delay(1000);
      if (obstacle(1, true, 20) == true) {
        return (true);
      }
      delay(500);
    }
   return (false);
}

boolean headRight(){
    Serial.println("head right");
    int headAngle = head.read() - 50;    
    head.write(headAngle);    
    while (headAngle > 10) {
      Serial.println(headAngle);
      headAngle=head.read();
      headAngle=headAngle - 10;
      head.write(headAngle);
      delay(500);
      if (obstacle(1, true, 20) == true){
        return (true);
      }
      delay(500);
    }
   return (false);
}

void avoidObstacle() { // If there's an obstacle turns randomly right or left and controls again if there's another one 
  boolean obstaclePresence;
  int sceltaAzione = random(1,3);
  while (true) {
    if (sceltaAzione == 1) obstaclePresence = headRight();
    else obstaclePresence = headLeft();
    headCenter();
    if (obstaclePresence == true) {
      sceltaAzione = 3 - sceltaAzione; // If there's an obstacle on one side, controls the other one    
      if (sceltaAzione == 1) obstaclePresence = headRight();
      else obstaclePresence = headLeft();
      headCenter();
      if (obstaclePresence == true) { // Controls on the other side. If there's an obstacle goes a bit back
        back();
        timer(1000, true, false);
        continue; // goes up for another control
        } 
      } 
    if (sceltaAzione == 1) right(); // turns right
    else left();
    timer(1500, true, false);
    break;
  }
  stop();
}

//------------------------------------------------------------------------------------------------------------------------------------------------------\\

//---------------------------------------------------------------- MAIN CONTROL SECTION-----------------------------------------------------------------\\

// function remains active for a certain time given by the first parameter e controlls if there are obstacles or if the robot is blocked.
// the second parameter is true if the function is called by the obstacle function
// the third parameter is true if the funciton is called by itsself (ricorsion). This parameter is passed the the obstacle function.
void timer(unsigned long differenzaTempo, boolean flagObstacle, boolean flagTimer) {
  unsigned long tempoIniziale;
  unsigned long tempoCorrente;
  tempoIniziale = millis(); //calculates the time in milliseconds
  while (true) {
    
    if (flagObstacle == false and obstacle(ITERAZIONIOSTACOLI, flagTimer, 15)== true) { // Sees an obstacle

      Serial.println("OSTACOLO!");
      if (flagTimer == false){ // Timer function calle dby indipendent function and not by timer itself
       stop();
       delay(500);      
       stateChange();     
      }
      avoidObstacle();
      if (flagTimer == false) stateChange();
      return;
    }

    GY_control(true);  //Reads and analyses the accellerometer values to determine the robot possible state
    if (possibleState == blocked){//Possible obstacle blocks the robot
      Serial.println("Possibile ostacolo rilevato");
      possibleState = stayingStill;
      stop();
      avoidObstacle(); 
    }
    
    tempoCorrente = millis();
    if ((tempoCorrente - tempoIniziale) > differenzaTempo){ 
      break;// Calculates the time passed from the function call, if bigger than the one given as a parameter exits the function
    }
  }
  if (flagObstacle == false){
  Serial.println(tempoCorrente - tempoIniziale);
  Serial.println(differenzaTempo);
  stop(); //Quando scade il timer, esce e si ferma
  }
}

void indipendent(){
  //SETUP 
  setupValues();
  findLimits();
  Serial.print("Limite fermo:");
  Serial.println(stopLimit);
  Serial.print("Limite urto:");
  Serial.println(hitLimit);
  possibleState = stayingStill;
  possibleHIT = false;
  
  int azione; // next move
  while (true) {
    if (Serial.available() > 0) {
      getstr = Serial.read();
      if (getstr == 'r') {
      stop();
      Serial.println("Done");
      break;
      }
    }
    /*if (obstacle(ITERAZIONIOSTACOLI, false, 20)== true) {
      Serial.println("Rilevato ostacolo!");
      stop();
      delay(500);
      avoidObstacle();
      // timer(0, false, true); // se c'è un ostacolo torna indietro per 2 secondi
    } */
    delay(2000);
    azione = random(1,4); // 1 goes forward, 2 turns right, 3 turns left
    if (azione == 1 or lastActionTurning == true) {
      forward();
      lastActionTurning = false;
      timer(random(8000, 12000), false, false); // function used to complete an action for a determined amount of time given as a parameter
    }
    else {
      if (azione == 2) {
        left();
        lastActionTurning = true;
        timer(random(5000, 8000), false, false); 
      }
      else {
        right();
        lastActionTurning = true;
        timer(random(5000, 8000), false, false);
      }
    }
  }
}



void loop() {

    getstr = Serial.read();
    switch(getstr){
    case 'e': armup(); break;
    case 'g': armdown(); break;
    case 'H': headLeft(); break;
    case 'F': headRight(); break;
    case 'E': headCenter(); break;
    case 'A': forward(); break;
    case 'C': back(); break;    
    case 'B': left(); break;
    case 'D': right();break;
    case 'S': stop();   break;
    case 'r': indipendent(); break;
    }
  }

//-------------------------------------------------------------------------------------------------------------------------------------------------------\\
