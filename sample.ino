#include <Arduino_GFX_Library.h>
#include <ESP32Time.h>
#include <Wire.h>
#include "Arduino.h"
#include "XPowersLib.h"
#include "bigFont.h"
#include "midleFont.h"
#include "smallFont.h"
#include "valueFont.h"


double rad=0.01745;

float x[360]; //outer point
float y[360];
float px[360]; //ineer point
float py[360];
float lx[360]; //long line 
float ly[360];
float shx[360]; //short line 
float shy[360];
float tx[360]; //text
float ty[360];

int PPgraph[20]={0};

int angle=0;
int value=0;
int chosenFont;
int chosenColor;
int r=118;
int sx=-2;
int sy=120;
int inc=18;
int a=0;
int prev=0;
String secs="00";
int second1=0;
int second2=0;

int deb=0;
int deb2=0;
int fase=0; //stoped
bool playing=0;

#include <TFT_eSPI.h>
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);


ESP32Time rtc(0); 

// ==== LCD PINS ====
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK  38
#define LCD_RESET 2
#define LCD_CS    12

#define LCD_WIDTH  466
#define LCD_HEIGHT 466

// ==== TOUCH (I2C) ====
#define IIC_SDA 15
#define IIC_SCL 14



// ==== DISPLAY BUS ====
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, 
  LCD_SCLK, 
  LCD_SDIO0, 
  LCD_SDIO1,
  LCD_SDIO2, 
  LCD_SDIO3
);

// ==== DISPLAY DRIVER ====
Arduino_CO5300 *gfx = new Arduino_CO5300(
  bus, 
  LCD_RESET, 
  0,              // rotation
  LCD_WIDTH, 
  LCD_HEIGHT, 
  6, 0, 0, 0
);


int n=0;
int xt = 0, yt = 0;


#define BUTTON 0

unsigned short grays[13];
#define red  0xD041
#define blue 0x0105
#define bck TFT_BLACK
char dd[7]={'r','u','n','t','i','m','e'};


void setup() {
 
  pinMode(BUTTON, INPUT_PULLUP); 
  rtc.setTime(0,0,0,10,23,2023,0); 
  sprite.createSprite(400,240);
  
  gfx->begin();
  gfx->setBrightness(140);
  gfx->fillScreen(0);

  Wire.begin(IIC_SDA, IIC_SCL);


     int co=220;
     for(int i=0;i<13;i++)
     {
     grays[i]=tft.color565(co, co, co);
     co=co-20;
     }

       for(int i=0;i<360;i++)
  {
       x[i]=(r*cos(rad*i))+sx;
       y[i]=(r*sin(rad*i))+sy;
       px[i]=((r-5)*cos(rad*i))+sx;
       py[i]=((r-5)*sin(rad*i))+sy;

       lx[i]=((r-24)*cos(rad*i))+sx;
       ly[i]=((r-24)*sin(rad*i))+sy;

       shx[i]=((r-12)*cos(rad*i))+sx;
       shy[i]=((r-12)*sin(rad*i))+sy;

      tx[i]=((r+28)*cos(rad*i))+sx;
      ty[i]=((r+28)*sin(rad*i))+sy;
  }

     for(int i=0;i<20;i++)
      PPgraph[i]=random(1,12);
  draw();
}

void draw()
{

  sprite.fillSprite(0);
  sprite.fillRect(54,120,24,4,TFT_RED);

    for(int j=0;j<20;j++)
    for(int i=0;i<PPgraph[j];i++)
    sprite.fillRect(190+(j*6),90-(i*4),4,3,grays[6]);
  
  sprite.fillRect(180,136,120,3,grays[8]);
  sprite.fillRect(186,130,3,34,grays[8]);
  sprite.fillRect(136,0,40,135,blue);
  sprite.fillRect(136,224,40,16,blue);

  sprite.fillCircle(372,76,28,blue);

 
  sprite.drawArc(372,76,24,10,0,angle,0x130F,blue);

  sprite.setTextDatum(8);
  sprite.loadFont(smallFont);
  sprite.setTextColor(grays[7],bck);
  sprite.drawString("AMOLED",342,124);
  sprite.drawString("*HRS*",178,218);
  sprite.unloadFont();

   sprite.loadFont(midleFont);

   for(int i=0;i<120;i++)
 {
   a=angle+(i*3);
   if(a>359)
   a=(angle+(i*3))-360;
   
   sprite.drawPixel(x[a],y[a],grays[6]);

   if(i%3==0)
   sprite.drawWedgeLine(x[a],y[a],x[a]-6,y[a],1,2,grays[5],bck);

   if(i%6==0)
   sprite.drawWedgeLine(x[a],y[a],x[a]-18,y[a],2,3,grays[4],bck);
   if(i%12==0){
   sprite.drawWedgeLine(x[a],y[a],x[a]-30,y[a],2,4,grays[3],bck);
   }

}
    
  sprite.setTextDatum(4);
  sprite.setTextColor(grays[2],grays[9]);
  
  for(int i=0;i<7;i++)
  {
    sprite.fillSmoothRoundRect(186+(i*30),2,26,26,3,grays[9],bck);
    sprite.drawString(String(dd[i]),186+((i+1)*30)-17,16);
  }
  sprite.unloadFont();


  sprite.drawWedgeLine(199+(prev*30),35,199+(prev*30),40,1,3,grays[3],bck); ////////////
  sprite.setTextDatum(0);
  sprite.setTextColor(grays[1],bck);
  sprite.loadFont(bigFont);
  if(fase==0)
  sprite.drawString("00:00",196,150);
  else
  sprite.drawString(rtc.getTime().substring(3,8),196,150);
  sprite.unloadFont();

  sprite.setTextDatum(0);
  sprite.setTextColor(grays[4],bck);
  sprite.loadFont(midleFont);
  sprite.drawString("VOLOS TIME",190,104);  ////////////////////////date hard coded
  sprite.setTextDatum(4);
  sprite.fillRect(0,145,50,30,grays[10]);
  sprite.setTextColor(grays[3],grays[10]);
  sprite.drawString("mil",25,162); 
   
  sprite.unloadFont();


  sprite.setTextDatum(4);
  sprite.setTextColor(grays[2],bck);
  sprite.loadFont(valueFont);
  if(fase==0)
  sprite.drawString("00",24,124);
  else
  sprite.drawString(String(rtc.getMillis()/10),24,124);
  sprite.setTextColor(grays[4],bck);
     if(fase==0)
     sprite.drawString("00",154,174);
   else
   sprite.drawString(rtc.getTime().substring(0,2),154,174);   /// /////////////////////////////////seconds
  sprite.unloadFont();

  sprite.setTextColor(grays[8],bck);
  sprite.drawString("CAN YOU READ THIS",346,128);

  
  gfx->draw16bitBeRGBBitmap(40,120,(uint16_t*)sprite.getPointer(),400,240);
}


void loop() {

  angle++;
  if(angle>356)
  angle=0;

   for(int i=0;i<20;i++)
   PPgraph[i]=random(1,12);

  if(digitalRead(BUTTON)==0)
  {
  if(deb==0) {deb=1; 
   fase++;
   if(fase==1)
   {
   playing=1;
   rtc.setTime(0,0,0,10,23,2023,0);
   }

    if(fase==2)
    playing=0;

    if(fase==3)
    {
      fase=0;
    }
  
   draw();}
  }else deb=0;
 
 
  
 second1=rtc.getSecond();

 if(second1!=second2) 
 {
  second2=second1;
  prev++;
  if(prev>6)
  prev=0;
 }

  if(playing)
  draw();  
}