// Disconnect Nextion to upload sketch to arduino.
// Hardware serial used, permits to trace serial coms with nextion via usb port of arduino and serial monitor
// can debug with NEXTION Simulator/Editor through Arduino USB
// Nextion charset ISO 8859-1 (latin-1)

#include <SimpleTimer.h>

//NEXTION

#include <Nextion.h>
#include <NextionPage.h>
#include <NextionVariableNumeric.h>
#include <NextionText.h>
#include <NextionNumber.h>
#include <NextionWaveform.h>
#include <NextionDualStateButton.h>
#include <SPI.h>
#include <OneWire.h>

#define NEXTION_PORT Serial

Nextion nex(NEXTION_PORT);

//NEXTION vars:
//NextionVariableNumeric varNexTempSet(nex, 0, 7, "TempSet"); //page 0, id 7
NextionVariableNumeric varVisWave(nex, 0, 26, "visWave"); //visWave id 26 -> check if waveform is visible

//NEXTION TXT:
NextionText txtNexTempFrig(nex, 0, 9, "TempFrigTxt");
NextionText txtNexTempCong(nex, 0, 10, "TempCongTxt");
NextionText txtNexTempSet(nex, 0, 8, "TempSetTxt");
//n0 id 2 : rpm
NextionNumber numNexRPM(nex, 0, 2, "n0");
//waveform ID 13 name s0
NextionWaveform graph(nex, 0, 13, "s0");
//Boost button state
NextionDualStateButton boostBt(nex, 0, 3, "bt0");

//Vars et const
float tempFrigDS;
float tempCongDs;
float tempSet = 4;
// Compressor entre 2000 et 3500
float   compresRPM=0;
float   compresRPMStart = 2500;
#define compresRPM_MIN 2200
#define compresResistorAt_MIN 0
#define compresRPM_MAX 3500
#define compresResistorAt_MAX 1523
#define potRw 180 // RW value depends on T°, Vres and Iw
#define coefRPM_Res (compresResistorAt_MAX-compresResistorAt_MIN)/(compresRPM_MAX-compresRPM_MIN)

#define compRPMRampSpeed 9.4
#define compresRPMafterBoost 2500
bool  boostUsed = 0;

//timer
SimpleTimer timer;

//DS18B20
OneWire  ds(A0);  // on pin A0 (a 4.7K resistor is necessary)
bool dsMesured = 0;
byte dsaddr1[8], dsaddr2[8];

// SPI MCP41HVX1
// MCP41100 connected to  arduino Board
// CS >>> D10
// SCLK >> D13
// DI  >>> D11
// 

byte address = 0x11;
#define CS 10       //D10
#define SHDWN 15    //A1
#define addr  0x00



void setup()
{
  NEXTION_PORT.begin(19200);
  nex.init();
  timer.setInterval(60000, setRPMandGraph); //toutes les minutes vitesse compresseur et graphe
  timer.setInterval(500, sendNex); //toutes les 500ms actualise l'affichage et valeur TempSet
  timer.setInterval(1500, getDsTemperature); //toutes les 1500ms mesure la température ou recupère les valeurs
  
  
  pinMode (CS, OUTPUT);
  pinMode (SHDWN, OUTPUT);
  digitalWrite(SHDWN, LOW); //coupe le compresseur
  
  SPI.begin();

  Serial.begin(19200);
  
  nex.poll();
  
}

void loop() {
  //toutes les minutes vitesse compresseur et graphe
  //toutes les 500ms actualise l'affichage
  //toutes les 1500ms mesure la température ou recupère les valeurs
  timer.run();

}


void setRPMandGraph()
{
  double graphVal;
  float tempRef;
  
   //Gestion vitesse compresseur et graphique toutes les minutes
  if(compresRPM == 0)
    // Compresseur éteint, attendre niveau tempSet+1°C avant start
    {
      tempRef = tempSet+1;
      if(tempFrigDS > tempRef)
      {
        compresRPM = compresRPMStart;
        digitalPotRPMWrite(compresRPM);
        digitalWrite(SHDWN, HIGH); //démarre le compresseur
      }
    }else{
    //Compresseur en route
      if(tempFrigDS > tempSet)
      //si température supérieure à tempsSet
      {
        // Compresseur en fonctionnement, application du ramp dans la limite du max
        if(compresRPM<compresRPM_MAX)
        {
          compresRPM = compresRPM+compRPMRampSpeed;
          digitalPotRPMWrite(compresRPM);
        }
        //activation du boost
        if(boostBt.isActive())
        {
          compresRPM = compresRPM_MAX;
          digitalPotRPMWrite(compresRPM);
          boostUsed =1;
        }else{
          //desactivation du boost
          if(boostUsed)
          {
            compresRPM = compresRPMafterBoost;
            digitalPotRPMWrite(compresRPM);
            boostUsed=0;
          }
        }
      }else{
        //archive rpm de stop - 300 rpm (systeme AOE)
        //sauf si boost, valeur de référence
        if(boostUsed == 1)
        {
          compresRPMStart = compresRPMafterBoost;
          boostUsed =0;
        }else{
          tempRef=compresRPM-300;
          if(tempRef>compresRPM_MIN)
          {
            compresRPMStart = compresRPM-300;
          }else{
            compresRPMStart = compresRPM_MIN;
          }
        }
        //stope le compresseur
        compresRPM=0;
        digitalWrite(SHDWN, LOW); //coupe le compresseur
      }
    }
  
  //actualisation graphique toutes les minutes 220pix
  if(compresRPM>1500)
  {
    graphVal = (compresRPM-1500)/2200;
    graphVal = graphVal*220;
  }else{
    graphVal = 0;
  }
  graph.addValue(0, graphVal);         //RPM max graph 1500 à 3700
  graphVal = tempFrigDS*(220/44)+110;
  if(graphVal > 220)
  {
    graphVal = 220;
  }
  if(graphVal < 0)
  {
    graphVal = 0;
  }
  graph.addValue(1, graphVal);       //Temp Frigo -22 à +22
  graphVal = tempCongDs*(220/44)+110;
  if(graphVal > 220)
  {
    graphVal = 220;
  }
  if(graphVal < 0)
  {
    graphVal = 0;
  }
  graph.addValue(2, graphVal);       //Temp congel
}

float getTempSet()
{
  String tempNex;
  char nexBuffer[10];

  txtNexTempSet.getText(nexBuffer,10);
  nex.poll();
  tempNex=String(nexBuffer);
  if(tempNex.indexOf(char(0xB0)) > -1)
  {
  tempNex.remove(tempNex.indexOf(char(0xB0)));
  }
  if(tempNex.indexOf('C') > -1)
  {
  tempNex.remove(tempNex.indexOf('C'));
  }
  if(tempNex.indexOf(' ') > -1)
  {
  tempNex.remove(tempNex.indexOf(' '));
  }
  return(tempNex.toFloat());
}


void sendNex() 
{
  String tempToNex;
  char nexBuffer[10];
  int visiWave=1;

  nex.poll();
  visiWave=varVisWave.getValue();
  tempSet=getTempSet();
  nex.poll();
  
  if(visiWave == 0)
  {
    numNexRPM.setValue(compresRPM);
        
    tempToNex = String(tempFrigDS,1);
    tempToNex = String(tempToNex + char(0xB0));
    tempToNex = String(tempToNex + "C");
    tempToNex.toCharArray(nexBuffer, 10);
    txtNexTempFrig.setText(nexBuffer);
      
    tempToNex = String(tempCongDs,1);
    tempToNex = String(tempToNex + char(0xB0));
    tempToNex = String(tempToNex + "C");
    tempToNex.toCharArray(nexBuffer, 10);
    txtNexTempCong.setText(nexBuffer);

    nex.refresh("TempCongTxt");
    nex.refresh("TempFrigTxt");
    nex.refresh("n0");
    
    //nex.sendCommand("vis TempCongTxt,1");
    //nex.sendCommand("vis TempFrigTxt,1");
    //nex.sendCommand("vis n0,1"); 
  }else{
    nex.refresh("s0");
  }
  nex.poll();
}

void digitalPotRPMWrite(float comRPM)
{
  //comRPM
  //2000 ->    0 Ohms / 5mA
  //2500 ->  277 Ohms / 4mA
  //3000 ->  692 Ohms / 3mA
  //3500 -> 1523 Ohms / 2mA
  // not linear but not realy a probleam
  // (1523-0)/(3500-2000)=1,015
  // 1,015*(2000-2000) = 0
  // 
  //255 = 0 Ohm
  //0   = 5 kOhms
  //Rw = 200 Ohm
  float resValue=0;

  resValue = coefRPM_Res*(comRPM-compresRPM_MIN);
  if(resValue>=potRw)
  {
    resValue=resValue-potRw;  // Rw offset correction
  }
  else
  {
    resValue=0;
  }
  resValue = resValue/(5000/255);
  resValue = int(resValue);
  resValue = 255-resValue;
  digitalWrite(CS, LOW);
  SPI.transfer(addr);
  SPI.transfer(resValue);
  digitalWrite(CS, HIGH);
}

/**
 * Fonction de lecture de la température via un capteur DS18B20.
 */
void getDsTemperature(void)
{
  byte data[9];
  // data[] : Données lues depuis le scratchpad
  // dsaddr1[] : Adresse du module 1-Wire détecté
  // dsaddr2[] : Adresse du module 1-Wire détecté
  // tempFrigDS
  // tempCongDs

  if (!dsMesured)
    {
    /* Reset le bus 1-Wire ci nécessaire (requis pour la lecture du premier capteur) */
    ds.reset();
    ds.reset_search();
  
    /* Recherche le prochain capteur 1-Wire disponible */
    ds.search(dsaddr1);
    ds.search(dsaddr2);
    
    /* Reset le bus 1-Wire et sélectionne le capteur */
    /* Lance une prise de mesure de température et attend la fin de la mesure */
    ds.reset();
    ds.select(dsaddr1);
    ds.write(0x44, 1);
    
    ds.reset();
    ds.select(dsaddr2);
    ds.write(0x44, 1);

    dsMesured = 1;
    //delay(800);
  }
  else
  {
    /* Reset le bus 1-Wire, sélectionne le capteur et envoie une demande de lecture du scratchpad */
    ds.reset();
    ds.select(dsaddr1);
    ds.write(0xBE);
   
   /* Lecture du scratchpad */
    for (byte i = 0; i < 9; i++) {
      data[i] = ds.read();
    }
     
    /* Calcul de la température en degré Celsius */
    tempFrigDS = (int16_t) ((data[1] << 8) | data[0]) * 0.0625; 
  
      /* Reset le bus 1-Wire, sélectionne le capteur et envoie une demande de lecture du scratchpad */
    ds.reset();
    ds.select(dsaddr2);
    ds.write(0xBE);
   
   /* Lecture du scratchpad */
    for (byte i = 0; i < 9; i++) {
      data[i] = ds.read();
    }
     
    /* Calcul de la température en degré Celsius */
    tempCongDs = (int16_t) ((data[1] << 8) | data[0]) * 0.0625; 
    
    dsMesured = 0;
  }
  
}
