/*

LocoNet Handregler (Throttle)

by Philipp Gahtow, 
http://pgahtow.de/wiki/index.php?title=Arduino_Loconet_Throttle

u8glib
https://github.com/olikraus/u8glib/wiki/userreference
*/

#include <avr/pgmspace.h>
#include <U8glib.h>
#include "LocoNet.h"

//Adresse des Handregler als ID:
#define ID 0x20

#define EEPROMOn true    //soll der EEPROM genutzt werden?

lnMsg        *LnPacket;
LnBuf        LnTxBuffer;

//Arduino Pin定義
//https://www.flickr.com/photos/arms22/8457793740/
// 0:DI:Rxd
// 1:DO:Txd
// 2:DO:SHIFT LED
// 3:DO:TRACK LED
// 4:--:not use
// 5:--:not use
// 6:DO:pwm A
// 7:DO:LocoNET Txd
// 8:DO:LocoNET Rxd
// 9:DO:OLED D1
//10:DO:OLED D0
//11:DO:OLED DC
//12:DO:OLED CS
//13:DO:OLRD RES
//A0:AI:analog key
//A1:DI:shift key  :Pull UP設定
//A2:DI:EC11 DIR    :Pull UP設定
//A3:DI:EC11 A     :Pull UP設定
//A4:DI:EC11 B     :Pull UP設定
//A5:--:not use

#define Led_Shift 2  //Shift LED
#define Led_Track 3  //Track LED
#define LoconetRxd 8
#define LoconetTxd 7
#define Shift2Pin 13   //Shift2 Button
#define analogkey A0
#define ShiftPin A1    //Shift Button
#define DirPin A2
#define EnAPin A3    //Incremental A
#define EnBPin A4    //Incremental B

//#define PowerPin 2    //Power down Sensor Pin

#define sizebut 10      //Anzahl der Taster（ボタンの数）
int butState[sizebut] = {0, 0, 0, 0, 0, 0, 1, 0, 0, 0};    //last Button State

#define F0 0
#define F1 1
#define F2 2
#define F3 3
#define F4 4
#define F5 5
#define F6 6
#define F7 7
#define F8 8
#define F9 9
#define F10 10
#define F11 11
#define F12 12

#define EnA 6
#define EnB 7

#define Down -1
#define Up 1
//#define None 0

#define THROWN 0
#define CLOSED 32

#define AdrMax 2024      //größe einstellbare Adresse サイズ調節可能なアドレス
#define SpeedMax 127     //höchste Geschwindigkeit. (vielleicht auch hier Stufen nutzen -> FredI!)

// font list
//https://github.com/olikraus/u8glib/wiki/fontsize
//128x64
//u8gの使い方
//http://jumbleat.com/2016/09/03/how_to_use_u8glib_part2/
U8GLIB_SSD1306_128X64 u8g(/* clock=*/ 10, /* data=*/9, /* cs=*/ 12, /* dc=*/11, /* reset=*/ 13); // SPI

LocoNetThrottleClass Throttle;
LocoNetClass Loconet;
        
uint32_t  LastThrottleTimerTick;
uint32_t  AnalogKeyTimerTick;
uint32_t  LastAdrTimerTick;
uint32_t  DispTimerTick;
uint32_t  TimerTick;     // なんでもタイマ
uint32_t  BlinkTick;    //点滅様タイマ

//word Adresse = 1;    //default Adresse 16-bit
byte Slot = 0xFF;           //akt. Slot zur Adr. (kein)
byte AdrH = 0;          //high Anteil Adresse
byte AdrL = 0;          //low Anteil Adresse
byte PointDir = 0;

byte PowerOff = 0;    //Versorgungsspannung liegt an.

//EEPROM Speicherbereiche:
//#define EEPROMAdrH 13    //Adresse High
//#define EEPROMAdrL 14    //Adresse Low
//#define EEPROMSlot 16    //Slot


// 画面ステート定義
enum {
  IniSc = 0,
  IniEvSc,
  AddressSc,
  AddressPosSc,       // アドレス設定の桁設定
  AddressNumSc,
  AddressEvSc,        //　アドレス設定の値設定
  SpeedSc,
  SpeedEvSc,
  EmgSc,
  EmgEvSc,
  FunctionSc,
  FunctionEvSc,
  PointSc,
  PointEvSc,          // Point   
} ScreenState;

// u8g描画ステート定義
enum {
  Idle = 0,           // Idle
  DrawingStart,       // 描画開始
  Drawing,            // 描画中
  DrawingCompleted,   // 描画完了
} u8gState;

enum {
  None = 0,
  DIR_single_click,
  DIR_double_click,
  DIR_long_click,
  SHIFT_single_click,
  SHIFT_double_click,
  SHIFT_long_click,
  Func0,
  Func1,
  Func2,
  Func3,
  Func4,  
} KeyEvent;

// ファンクッションキー押下時のボタン位置→ファンクッション番号変換テーブル
char FuncTable[5][3]={
// 00 01 02
  {0,0,0},
  {1,5,9},
  {2,6,10},
  {3,7,11},
  {4,8,12}
};

// ポイントキー押下時のボタン位置→ポイント番号変換テーブル
char PointTable[5][4]={
// 00 01 02
  {1,6,11,16},
  {2,7,12,17},
  {3,8,13,18},
  {4,9,14,19},
  {5,10,15,20}
};
              

char MenuSel = 0;     // Menuが現在どこを選択しているか
char MenuSelMAX = 4;  // Menuで選択できる最大数 0 1 2 3 (0を含めて4行)

char SpeedTableSel = 0;     // Menuが現在どこを選択しているか
char SpeedTableSelMAX = 3;  // Menuで選択できる最大数 0 1 2 3 (0を含めて4行)

char SetupMenuSel = 0;     // Menuが現在どこを選択しているか
char SetupMenuSelMAX = 4;  // Menuで選択できる最大数 0 1 2 3 (0を含めて4行)

int Address = 0;
int AddressSelMAX = 1024;

int PointAddress = 1;
int PointAddressMAX = 999;
char PointLineSel = 0;


char AddressPosSel = 3;
char NumSel = 0;      //桁設定用変数

//char KeyEvent = 0;
char EncoderEvent = 0;
int spd=1;

//--------------------------------------------------------------------------------------------------
// 初期化
//--------------------------------------------------------------------------------------------------
void setup()
{
  //key board pinMode 初期化
  pinMode(A0,INPUT);
  pinMode(A1,INPUT_PULLUP); 
  pinMode(A2,INPUT_PULLUP); 
  pinMode(A3,INPUT_PULLUP); 
  pinMode(A4,INPUT_PULLUP); 
  pinMode(Led_Shift , OUTPUT);  // Shift LED
  pinMode(Led_Track , OUTPUT);  // Track LED

  digitalWrite(Led_Shift, HIGH);  // HIGHでOFF,LOでON
  digitalWrite(Led_Track, HIGH);  // HIGHでOFF,LOでON 
  
  LocoNet.init(LoconetTxd);   // First initialize the LocoNet interface
                              // RxPinは,PB0固定 
  Throttle.init(0, 0, ID);    // erzeuge Handregler

  Serial.begin(57600);
  Serial.println("");
  Serial.println(F("hello, 2in1 LocoNET/DCC Throttle AYA006-1"));    // F() はFLASHメモリにデータを置く
  ScreenState = 0;
  Address = 1;             // 初期アドレス = 1
}



//--------------------------------------------------------------------------------------------------
// main loop
//--------------------------------------------------------------------------------------------------
void loop() {

#if 0
  static char ledf = 0;
  if(ledf==0){
    digitalWrite(Led_Shift, LOW);  // HIGHでOFF,LOでON
    ledf = 1;
  } else {
    digitalWrite(Led_Shift, HIGH);  // HIGHでOFF,LOでON
    ledf = 0;
  }
#endif
  
  EncoderState();         // エンコーダー処理
  DirKeyState();          // DIRキー処理
  ShiftKeyState2();       // SHIFTキー処理  
  FunctionKeyState();     // ファンクッションキー処理
  DisplayState();         // 画面処理
  adrState(0);            // アドレス取得処理
  pointState(0);          // ポイント処理
//  debugstate();
    
  // Check for any received LocoNet packets
  LnPacket = LocoNet.receive() ;
  if (LnPacket)  {
  //Serial.println("LnPacket");
    if (!LocoNet.processSwitchSensorMessage(LnPacket))
      Throttle.processMessage(LnPacket) ; 
  }
  
  if(isTime(&LastThrottleTimerTick, 100)) {       // Locoには100ms周期でアクセスが必要だそうです。
    Throttle.process100msActions(); 
    if (Throttle.getState() != TH_ST_IN_USE){     //no Slot - Blink LED F0
      digitalWrite(Led_Track, HIGH);
    }else
       digitalWrite(Led_Track, LOW);     
    if (Throttle.getState() == TH_ST_SLOT_MOVE){    //Blink off
      digitalWrite(Led_Track, HIGH);
    }
  }
}


// --------------------------------------------------------------------------------
// 画面表示ステートマシン
// --------------------------------------------------------------------------------
void DisplayState(){
    static char buf[5];

    switch(ScreenState){
      //----------- 初期画面
      case IniSc:
            TimerTick = millis();
            ScreenState = IniEvSc;
            break;
      case IniEvSc:
            if(u8gState==Idle)
              u8gState = DrawingStart;          // 表示ステート:描画開始
            if( millis()-TimerTick > 2000 ){    // 1000=1sec
              ScreenState = AddressEvSc;
            }
            break;
                    
      //----------- アドレス設定
      case AddressSc :
            u8gState = DrawingStart;      // 表示ステート:描画開始
            ScreenState = AddressPosSc;
            break;

      case AddressPosSc :                   // アドレス桁設定
            if(u8gState==Idle)
              u8gState = DrawingStart;      // 表示ステート:描画開始
            if(EncoderEvent == Up){           // Up処理
                      AddressPosSel++;
                      EncoderEvent = None;
                      if ( AddressPosSel >= 4 )
                        AddressPosSel = 0;
                      u8gState = DrawingStart;    // 表示ステート:描画開始     
            } else if(EncoderEvent == Down){  // Down処理
                      AddressPosSel--;
                      EncoderEvent = None;
                      if ( AddressPosSel < 0 )
                        AddressPosSel = 3;
                      u8gState = DrawingStart;    // 表示ステート:描画開始     
            } else if(KeyEvent == DIR_single_click){  // シングルクリック：決定
              KeyEvent = None;
              ScreenState = AddressNumSc;
            } else if(KeyEvent == DIR_double_click){  // ダブルクリック：速度画面へ
              KeyEvent = None;
              ScreenState = SpeedSc;                      
            }
            break;

      case AddressNumSc:
            sprintf(buf,"%04d",Address);
            NumSel = buf[AddressPosSel] - 0x30;     // 文字から 0x30 減算すると数値になります。
            ScreenState = AddressEvSc;
            break;

      case AddressEvSc :                    //　アドレス値設定
            if(u8gState==Idle)
              u8gState = DrawingStart;      // 表示ステート:描画開始
            if(EncoderEvent == Up){           // Up処理
                      NumSel++;
                      EncoderEvent = None;
                      if ( NumSel >= 10 )
                        NumSel = 0;
              buf[AddressPosSel] = NumSel + 0x30; // 数値から0x30を加算して文字に変換
              Address = atoi(buf);

                      u8gState = DrawingStart;    // 表示ステート:描画開始     
            } else if(EncoderEvent == Down){  // Down処理
                      NumSel--;
                      EncoderEvent = None;
                      if ( NumSel < 0 )
                        NumSel = 9;
              buf[AddressPosSel] = NumSel + 0x30; // 数値から0x30を加算して文字に変換
              Address = atoi(buf);

                      u8gState = DrawingStart;    // 表示ステート:描画開始     
            } else if(KeyEvent == DIR_double_click){
              KeyEvent = None;
              ScreenState = AddressPosSc;
            } else if(KeyEvent == DIR_single_click){
              KeyEvent = None;
              ScreenState = SpeedSc;
            }
            break;      
            
      //----------- 速度設定
      case SpeedSc :                       
            Serial.println("LocoNET_Connect");
            adrState(1);
            u8gState = DrawingStart;                    // 表示ステート:描画開始
            ScreenState = SpeedEvSc;
            break;
      case SpeedEvSc :                                  // Menu Event処理
#if 0
            if (Throttle.getState() != TH_ST_IN_USE){   // Adr. 利用可能？
               Serial.println("TH_ST_IN_USE(not)");
               KeyEvent = None;                         
               ScreenState = AddressSc;               // アドレス設定画面へ
            }
#endif
            ThrottleCont();                           // スロットルの増加・現象処理

            if(KeyEvent == DIR_long_click){      // DIR長押し
                      Serial.println("Dis Connect");
                      Throttle.releaseAddress();
                      Throttle.freeAddress(Address);
                      Throttle.releaseAddress();
//                    Slot = 0;
                      ScreenState = AddressNumSc;           // アドレス設定画面へ
                      KeyEvent = None;

            } else if(KeyEvent == DIR_double_click){
                      KeyEvent = None;
                      if (Throttle.getSpeed() == 1) {                     // 速度 1 の時、ダブルクリックでForward/Reverse切り替え
                        Throttle.setDirection(!Throttle.getDirection());  //DIR
                        Serial.println(Throttle.getDirection());
                        u8gState = DrawingStart;                          // 表示ステート:描画開始     
                      }
            
            } else if(KeyEvent == DIR_single_click){
                      if (Throttle.getState() == TH_ST_IN_USE ) {         // Func=0 DIRクリック状態?
                        if (Throttle.getSpeed() > 1) {                //EMERGENCY STOP (停車時速度=1)
                          Serial.println("EMG STOP");
                          Throttle.setSpeed(1);
                          ScreenState = EmgSc;
                          u8gState = DrawingStart;          // 表示ステート:描画開始     
                          KeyEvent = None;
                        } 
                      }
                      
            } else if(KeyEvent == SHIFT_single_click){
                        ScreenState = FunctionSc;            // ファンクッション設定画面へ
                        KeyEvent = None;             

            } else if(KeyEvent == SHIFT_double_click){
                        ScreenState = PointSc;              // ポイント設定画面へ
                        KeyEvent = None;             
            
            } else if(KeyEvent == Func0 || KeyEvent == Func1 || KeyEvent == Func2 || KeyEvent == Func3 || KeyEvent == Func4 ){
                        KeyEvent = None; 
            }
            
            break;

      //----------- 非常停止画面
      case EmgSc:
            u8gState = DrawingStart;      // 表示ステート:描画開始
            TimerTick = millis();
            ScreenState = EmgEvSc;
            break;            

      case EmgEvSc:
            if(u8gState==Idle)
              u8gState = DrawingStart;          // 表示ステート:描画開始
            if( millis()-TimerTick > 2000 ){    // 1000=1sec
              ScreenState = SpeedSc;
            }
            break;
            

      //----------- ファンクッション設定画面
      case FunctionSc:
            u8gState = DrawingStart;      // 表示ステート:描画開始
            ScreenState = FunctionEvSc;
            break;

      case FunctionEvSc:
            if(u8gState==Idle)
              u8gState = DrawingStart;      // 表示ステート:描画開始

            ThrottleCont();                           // スロットルの増加・現象処理

            if(KeyEvent == DIR_single_click){
              KeyEvent = None;
              ScreenState = AddressPosSc;
            } else if(KeyEvent == DIR_double_click){  // DIR ダブルクリックで速度画面へ
              KeyEvent = None;
              ScreenState = SpeedSc;
            } else if(KeyEvent == SHIFT_double_click){// SHIFT ダブルクリックで速度画面へ
              KeyEvent = None;
              ScreenState = SpeedSc;
            } else if(KeyEvent == SHIFT_single_click){// SHIFT シングルクリック ポイントライン変更
              PointLineSel++;
              if(PointLineSel >= 4)
                PointLineSel = 0;
              KeyEvent = None;
              u8gState = DrawingStart;
            }

            if(KeyEvent >= Func0 && KeyEvent <= Func4){
              Throttle.setFunction(FuncTable[KeyEvent-Func0][PointLineSel], !Throttle.getFunction(FuncTable[KeyEvent-Func0][PointLineSel]) ); //FuncKeyは+1されているので、Locoに設定するときは-1する。
                                                                            //画面表示が合わない、Pointと同じような制御形態をとるべきか？
              Serial.print("FuncKey:");
              Serial.print(KeyEvent-Func0, DEC);
              Serial.print(" PointLineSel:");
              Serial.println(PointLineSel, DEC);
              KeyEvent = None;

            break;      


      //----------- ポイント設定画面
      case PointSc :
            u8gState = DrawingStart;      // 表示ステート:描画開始
            ScreenState = PointEvSc;
            break;

      case PointEvSc:
            if(u8gState==Idle)
              u8gState = DrawingStart;      // 表示ステート:描画開始
            if(EncoderEvent == Up){           // Up処理
                      PointAddress++;
                      EncoderEvent = None;
                      if ( PointAddress >= PointAddressMAX )
                        PointAddress = PointAddressMAX;
                      u8gState = DrawingStart;    // 表示ステート:描画開始     
            } else if(EncoderEvent == Down){  // Down処理
                      PointAddress--;
                      EncoderEvent = None;
                      if ( PointAddress < 0 )
                        PointAddress = 1;
                      u8gState = DrawingStart;    // 表示ステート:描画開始     
            } else if(KeyEvent == DIR_single_click){
              KeyEvent = None;
              ScreenState = AddressPosSc;
            } else if(KeyEvent == DIR_double_click){  // DIR ダブルクリックで速度画面へ
              KeyEvent = None;
              ScreenState = SpeedSc;
            } else if(KeyEvent == SHIFT_double_click){// SHIFT ダブルクリックで速度画面へ
              KeyEvent = None;
              ScreenState = SpeedSc;
            } else if(KeyEvent == SHIFT_single_click){// SHIFT シングルクリック ポイントライン変更
              PointLineSel++;
              if(PointLineSel >= 4)
                PointLineSel = 0;
              KeyEvent = None;
              u8gState = DrawingStart;
            }

            if(KeyEvent >= Func0 && KeyEvent <= Func4)
              pointState(PointTable[KeyEvent-Func0][PointLineSel]);

              Serial.print("FuncKey:");
              Serial.print(KeyEvent-Func0, DEC);
              Serial.print(" PointLineSel:");
              Serial.println(PointLineSel, DEC);
              KeyEvent = None;
            }
            
            break;      

//--------------------------------------------------------------------------
      default:
            break;
    }

  if(u8gState!=0){
    if( millis()-DispTimerTick > 100 ){    // 1000=1sec
      switch(u8gState){
        case DrawingStart:
              u8g.firstPage();               // LCDに書き込む宣言
              u8gState = Drawing;            // 描画中
              break;

        case Drawing:
              do {
                ScreenDisplay();
              } while(u8g.nextPage()!=0);
              u8gState = DrawingCompleted;   // 描画完了
              break;

        case DrawingCompleted:
              u8gState = Idle; 
              break;

        default :
              break;
      }
      DispTimerTick = millis();                 // 現在の時刻に更新
    }
  }
}
  
// --------------------------------------------------------------------------------
//　表示画面作成
//  ScreenState によって、表示する画面が決まる
// --------------------------------------------------------------------------------
void ScreenDisplay(){

  char buf[22];
  char i;
  static char bf = 0;

  
  switch(ScreenState){
    case IniSc:
            break;
    case IniEvSc:
            u8g.setFont(u8g_font_profont12);
            u8g.drawStr( 0, 12*1, "2in1 Loco/DCC Throttle");
            u8g.drawStr( 0, 12*2, "Mode:[LocoNET]");
            sprintf(buf,"Initail Address:%04d",Address);
            u8g.drawStr( 0, 12*3, buf);
            u8g.drawStr( 0, 12*4, "Address:[valiable]"); // fixedly:固定、可変
            u8g.drawStr( 0, 12*5, "NobSTEP:[5]");
            break;
            
    case AddressPosSc:  
    case AddressEvSc:
            u8g.setFont(u8g_font_profont12);
            u8g.drawStr( 0, 22, "Addr:");
            if(Throttle.getDirection())
              u8g.drawStr( 0, 46, "[FWD]");
            else
              u8g.drawStr( 0, 46, "[REV]");            
            u8g.drawStr( 0, 62, "Speed:");

            u8g.drawStr( 100, 64, "Km/h");

            u8g.setFont(u8g_font_gdr25r);
            sprintf(buf,"%04d",Address);
//               u8g.drawStr( 45, 24, buf);

            if( millis()-BlinkTick > 250 ){    // 1000=1sec
              if(bf == 0){
//              u8g.drawBox(40, 0, 80, 24);       // ここで書き換えると下半分が表示されない？
//              u8g.setDefaultBackgroundColor();
//              u8g.drawStr( 40, 24, buf);
//              u8g.setDefaultForegroundColor();
                BlinkTick = millis();
                bf = 1;
              }else{
//              u8g.drawStr( 40, 24, buf);
                BlinkTick = millis();
                bf = 0;                
              }
            }

            if(bf==0){
                //u8g.drawBox(40, 25, 20, 2);
                if(ScreenState==AddressPosSc) cursor(40,25,0,AddressPosSel,buf);
                if(ScreenState==AddressEvSc){
                  cursor(40,25,1,AddressPosSel,buf);

                }

         
            } else{
             u8g.drawStr( 40, 24, buf); 
            }
            
            sprintf(buf,"%3d",spd-1);
            u8g.drawStr( 40, 63, buf);    
            break;

    case EmgSc:
    case EmgEvSc:
    case SpeedEvSc:
            u8g.setFont(u8g_font_profont12);
           
            u8g.drawStr( 0, 22, "Addr:");
            if(Throttle.getDirection())
              u8g.drawStr( 0, 46, "[FWD]");
            else
              u8g.drawStr( 0, 46, "[REV]");  
            u8g.drawStr( 0, 62, "Speed:");
            
            if(ScreenState == SpeedEvSc)
              u8g.drawStr( 100, 64, "Km/h");

            u8g.setFont(u8g_font_gdr25r);
            sprintf(buf,"%04d",Address);
            u8g.drawStr( 40, 24, buf);

            if(ScreenState == EmgSc || ScreenState == EmgEvSc)
              strcpy(buf, "EMG");
            else {
              sprintf(buf,"%3d",spd - 1);   // Throttle.getSpeed()で取得すると1〜126なので、-1 減算する
            }
            u8g.drawStr( 40, 63, buf);     
            break;

    case FunctionSc:
    case FunctionEvSc:
            FunctionCursor();
            break;

    case PointSc:
    case PointEvSc:
            PointCursor();
            
            break;

//--------------------------------------------------------------------------

    default:
            break;             
    }

}

void ThrottleCont(){
  spd = Throttle.getSpeed();
            
  if(EncoderEvent == Up){                   // Up処理
    if(KeyEvent == SHIFT_long_click){       // SHIFT長押しで+5
      spd = spd + 5;                        
      KeyEvent == None;
    }
                else
                  spd++;
            if ( spd >= 126 )
              spd = 126;
                  
                Throttle.setSpeed(spd);
                KeyEvent == None;
                EncoderEvent = None;
                u8gState = DrawingStart;          // 表示ステート:描画開始     
              } 
              
                else if(EncoderEvent == Down){            // Down処理
                if(KeyEvent == SHIFT_long_click){ // SHIFT長押しで-5
                  KeyEvent == None;
                  spd = spd - 5;
                }
                  else
                    spd--;
                EncoderEvent = None;

                if ( spd < 1 )
                  spd = 1;
                Throttle.setSpeed(spd);
                u8gState = DrawingStart;          // 表示ステート:描画開始     
                }
}
                

//--------------------------------------------------------------------------------------------------
// FunctionCursor
// ファンクッション設定画面におけるカーソル描画
//--------------------------------------------------------------------------------------------------
void FunctionCursor(){
  char buf[20];
  u8g.setFont(u8g_font_profont12);
  sprintf(buf,"ADR:%04d  SLOT:%03d",Address, Throttle.getSpeed());
  u8g.drawStr( 0, 12, buf);
  u8g.drawStr( 12,24,"F0  F1  F2  F3  F4");
  u8g.drawStr( 12,36,"F0  F5  F6  F7  F8");   
  u8g.drawStr( 12,48,"F0  F9  F10 F11 F12");   
  u8g.drawStr( 12,60,"F0  F13 F14 F15 F16");   
  u8g.drawStr( 0,24+12*PointLineSel,">");
}



//--------------------------------------------------------------------------------------------------
// Pointcursor
// ポイント設定画面におけるカーソル描画
//--------------------------------------------------------------------------------------------------
void PointCursor(){
  char buf[20];
  u8g.setFont(u8g_font_profont12);
  sprintf(buf,"PointAddress:%03d",PointAddress);
  u8g.drawStr( 0, 12, buf);
  u8g.drawStr( 12,24,"P1  P2  P3  P4  P5");
  u8g.drawStr( 12,36,"P6  P7  P8  P9  P10");   
  u8g.drawStr( 12,48,"P11 P12 P13 P14 P15");   
  u8g.drawStr( 12,60,"P16 P17 P18 P19 P20");  
  u8g.drawStr( 0,24+12*PointLineSel,">");
}

//--------------------------------------------------------------------------------------------------
// cursor
// カーソルブリンク処理
// mode 0:アンダーラインタイプ
// mode 1:１文字選択タイプ
//--------------------------------------------------------------------------------------------------
void cursor(char x,char y, char mode,char pos, char *buf){
  char pbuf[5];

  switch(mode){
    case 0:
            u8g.drawStr( 40, 24, buf);
            u8g.drawBox( 40+pos*19, y, 20, 2);  
            break;
    case 1:
            for(int i=0;i<=4;i++){
              pbuf[0]=buf[i];pbuf[1]=0x00;
              if(i==pos){ // drawBox -> setDefaultBackgroundColor -> drawStr -> setDefaultForegroundColor この順番が大事
                u8g.drawBox( 40+pos*19, y-25, 20, 25);
                u8g.setDefaultBackgroundColor();
                u8g.drawStr( 40+i*19, 24, pbuf);
                u8g.setDefaultForegroundColor();    
              } else
                u8g.drawStr( 40+i*19, 24, pbuf);
            }
            break;
    default:
            break;
  }
}



//--------------------------------------------------------------------------------------------------
// Encoderの処理ステート
//--------------------------------------------------------------------------------------------------
void EncoderState(){
    int EnAState = digitalRead(EnAPin);

    butState[EnB] = digitalRead(EnBPin);
    
    if ((butState[EnA] == LOW) && (EnAState == HIGH)) {
      if (butState[EnB] == LOW) {   //Down
        EncoderEvent = Down;
     } 
      else                          //Up
         EncoderEvent = Up;       
    }
    
    butState[EnA] = EnAState;
}


//--------------------------------------------------------------------------------------------------
// SHIFTキーの処理ステート
// シングルクリック、ダブルクリック、長押しを判定
//--------------------------------------------------------------------------------------------------
void ShiftKeyState2(){
  static int state = 0;
  static char bufp = 0;
  static uint32_t Ti;
  static char cKeybuf[20];
  
  char pc = 0;
  char ps = 0;
  char i;
  char di;
  char Max = 10;

  switch(state){
    case 0:
            Ti = millis();
            state = 1;
            break;
    case 1:                                       // 50msec周期の監視
            if( millis()-Ti >50){
              di = digitalRead(ShiftPin);         // ボタン読み込み
              if(di == 0 || bufp > 0) {           // 最初の検出か？ bufpが0以上
                cKeybuf[bufp++] = di;
                if(bufp >= Max){            
                  state = 2;
                  bufp = 0;
                  break;
                }
              }
              Ti = millis();
            }
            break;
    case 2:                                     // シングルクリック・ダブルクリック・長押し判定
#if 0
            Serial.println("");
            Serial.print("Keybuf:");  
            for(i=0;i<=Max-1;i++)
              Serial.print(cKeybuf[i],DEC);
            Serial.print(":");
#endif                  
            for( i = 0 ; i <= Max - 2 ; i++){   // 変化点を変数pcでカウント
              if(cKeybuf[i] != cKeybuf[i+1])
                pc = pc + 1;
              ps = ps + cKeybuf[i];             // 0が多いと長押し判定に使える
            }
#if 0
            Serial.print(pc,DEC);
            Serial.print(":");   
            Serial.print(ps,DEC);
            Serial.print(":");  
#endif            
            switch(pc){
              case 0: Serial.println("Long press");
                      KeyEvent = SHIFT_long_click;
                      break;
              case 1:
              case 2:if(ps<=5){
                        Serial.println("Long press");
                        KeyEvent = SHIFT_long_click;
                      } else {
                        Serial.println("single click");
                        KeyEvent = SHIFT_single_click;
                      }
                     break;
              case 3:
              case 4:Serial.println("Double click");
                        KeyEvent = SHIFT_double_click;
                      break;

            }
//            Serial.println("");
            state = 0;
            break;
    default:
            break;
  }  
}





//--------------------------------------------------------------------------------------------------
// DIRキーの処理ステート
// シングルクリック、ダブルクリック、長押しを判定
//--------------------------------------------------------------------------------------------------
void DirKeyState(){
  static char state = 0;
  static char bufp = 0;
  static uint32_t Ti;
  static char cKeybuf[20];
  
  char pc = 0;
  char ps = 0;
  char i;
  char di;
  char Max = 10;

  switch(state){
    case 0:
            Ti = millis();
            state = 1;
            break;
    case 1:                                       // 50msec周期の監視
            if( millis()-Ti >50){
              di = digitalRead(DirPin);               // ボタン読み込み
              if(di == 0 || bufp > 0) {           // 最初の検出か？ bufpが0以上
                cKeybuf[bufp++] = di;
                if(bufp >= Max){            
                  state = 2;
                  bufp = 0;
                  break;
                }
              }
              Ti = millis();
            }
            break;
    case 2:                                     // シングルクリック・ダブルクリック・長押し判定
#if 0
            Serial.println("");
            Serial.print("Keybuf:");  
            for(i=0;i<=Max-1;i++)
              Serial.print(cKeybuf[i],DEC);
            Serial.print(":");
#endif                  
            for( i = 0 ; i <= Max - 2 ; i++){   // 変化点を変数pcでカウント
              if(cKeybuf[i] != cKeybuf[i+1])
                pc = pc + 1;
              ps = ps + cKeybuf[i];             // 0が多いと長押し判定に使える
            }
#if 0
            Serial.print(pc,DEC);
            Serial.print(":");   
            Serial.print(ps,DEC);
            Serial.print(":");  
#endif            
            switch(pc){
              case 0:// Serial.println("Long press");
                      KeyEvent = DIR_long_click;
                      break;
              case 1:
              case 2:if(ps<=5){
                        //Serial.println("Long press");
                        KeyEvent = DIR_long_click;
                      } else {
                        //Serial.println("single click");
                        KeyEvent = DIR_single_click;
                      }
                     break;
              case 3:
              case 4://Serial.println("Double click");
                        KeyEvent = DIR_double_click;
                      break;

            }
//            Serial.println("");
            state = 0;
            break;
    default:
            break;
  }  
}


//--------------------------------------------------------------------------------------------------
// ファンクッションキーの処理ステート
// アナログキーボードからのキー判定処理
// 敷居値は別途調整、初回検出値と連続に押下している場合で検出電圧が異なる。
// シングルクリックを判定
// 連続に取り込んでしまうので、１秒のフィルタを追加
//--------------------------------------------------------------------------------------------------
void FunctionKeyState(){
  static char state = 0;
  static int kin;
  int row;

  if(KeyEvent >= Func0 && KeyEvent <= Func4 )   // アナログキーの処理がはけていなかったら未処理
    return;

  switch(state){
    case 0:
            kin = analogRead(analogkey);
            if( kin > 700 )                   // Funcボタンが押されなくなった？
              return;
  
            if( kin < 100 ){                  // F0,F0,F0
              KeyEvent = Func0;
            } else if( kin < 300 ){           // F1,F5,F9
              KeyEvent = Func1;
            } else  if( kin < 500 ){          // F2,F6,F10
              KeyEvent = Func2;
            } else  if( kin < 600 ){          // F3,F7,F11
              KeyEvent = Func3;
            } else  if( kin < 700 ){          // F4,F8,F12
              KeyEvent = Func4;
            }
            AnalogKeyTimerTick = millis(); 
            state = 1;
            
  Serial.print("F:");
  Serial.print(kin,DEC);
  Serial.print(" KeyEvent:");
  Serial.println(KeyEvent,DEC);
            
            break;
     case 1:
            if(millis()-AnalogKeyTimerTick > 1000){  // 1000=1sec
              AnalogKeyTimerTick = millis();
              state = 0;
            }  
            break;
  }
}

//--------------------------------------------------------------------------------------------------
// ポイント切り替え用ステートマシン
// 現状の状態を問い合わせてから、切り替える様にした。そうする事で、１ボタンでc/tの切り替えができて便利
//--------------------------------------------------------------------------------------------------
void pointState(int adr){
  static int state = 0;
  static int pointAdr;
//  Serial.print("pointState:");
//  Serial.println(state,DEC);

  switch(state){
    case 0:
            if( adr != 0 ){
              pointAdr = adr;
              state = 1;
            }
            break;
    case 1:                                             // ポインタの状態を取得
            Loconet.reportSwitch(pointAdr);
            LastAdrTimerTick = millis();
            state = 2;
            break;
    case 2:                                             // 100ms経過後、アドレスを取得
            if(millis()-LastAdrTimerTick>=100) {
              if(PointDir == THROWN)
                Loconet.requestSwitch(pointAdr,1,CLOSED);
              else
                Loconet.requestSwitch(pointAdr,1,THROWN);
              state = 0;
            }
            break;
    default:
            break;
  }
}


//--------------------------------------------------------------------------------------------------
// アドレス取得用ステートマシン
// freeAddress()->setAddress()と実行してもbusyを検出して処理ができないので、100msのウエイトを入れた
//--------------------------------------------------------------------------------------------------

void adrState( int inh ){
  static int state = 0;

//  Serial.print("adrState:");
//  Serial.println(state,DEC);

  switch(state){
    case 0:
            if( inh == 1 )
              state = 1;
            break;
    case 1:                                             // 取得しているスロットを解放
            Throttle.freeAddress(Address);
            LastAdrTimerTick = millis();
            state = 2;
            break;
    case 2:                                             // 100ms経過後、アドレスを取得
            if(millis()-LastAdrTimerTick>=100) {
              Throttle.setAddress(Address); 
              state = 0;
            }
            break;
    default:
            break;
  }
}


//--------------------------------------------------------------------------------------------------
// update Speed via tick
// boolean型はtrue/falseの２値を表現する型
//--------------------------------------------------------------------------------------------------
boolean isTime(unsigned long *timeMark, unsigned long timeInterval) {
    unsigned long timeNow = millis();
    if ( timeNow - *timeMark >= timeInterval) {
        *timeMark = timeNow;
        return true;
    }    
    return false;
}


//--------------------------------------------------------------------------------------------------
// Throttle notify Call-back functions
// if(関数名)で飛んでくるけど、どんな仕組み？
// loconetのメッセージが、LocoSlot -> DC50K -> LocoSlot に戻ってくる？その、戻ってきたら、更新している？
// DCS50K STOPで1,FULLで126
//--------------------------------------------------------------------------------------------------
void notifyThrottleSpeed( uint8_t UserData, TH_STATE State, uint8_t Speed )
{
  Serial.print("notifyThrottleSpeed():");
  Serial.println(Speed);
  spd = Throttle.getSpeed();  // OLEDの画面更新の為
  u8gState = DrawingStart;
}

//--------------------------------------------------------------------------------------------------
// Addressは、車両アドレス？ Slotは車両アドレスが格納されているSlot番号
//--------------------------------------------------------------------------------------------------
void notifyThrottleAddress( uint8_t UserData, TH_STATE State, uint16_t Address, uint8_t isSlot )
{

  Serial.print("notifyThrottleAddress():adress");
  Serial.print(Address);
  Serial.print("_slot:");

  Serial.println(isSlot);

//  updateLCDAdr(Address);
  Slot = isSlot;
}

//--------------------------------------------------------------------------------------------------
// forwardかreverseか？
//--------------------------------------------------------------------------------------------------
void notifyThrottleDirection( uint8_t UserData, TH_STATE State, uint8_t Direction )
{

  Serial.print("notifyThrottleDirection():");
  Serial.println(Direction);

}

//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
void notifyThrottleFunction( uint8_t UserData, uint8_t Function, uint8_t Value ) {
//  Serial.print("notifyThrottleFunction():");
//  Serial.print(Function);
//  Serial.print(":");
//  Serial.println(Value);
//  return;
 
  if (Function == 0) //F0受信？
    Serial.println("F0");
  else {
    Serial.print("F");
    Serial.print(Function);
    Serial.print(":");
    Serial.println(Value);

  }  

}


//--------------------------------------------------------------------------------------------------
void notifyThrottleSlotStatus( uint8_t UserData, uint8_t Status ) {};

//--------------------------------------------------------------------------------------------------
// 何が見える？
//--------------------------------------------------------------------------------------------------
void notifyThrottleState( uint8_t UserData, TH_STATE PrevState, TH_STATE State ) {
//  Serial.print("notifyThrottleState():");
//  Serial.println(Throttle.getStateStr(State));
}

//--------------------------------------------------------------------------------------------------
void notifyThrottleError( uint8_t UserData, TH_ERROR Error ) {
//  Serial.print("notifyThrottleError():");
//  Serial.println(Throttle.getErrorStr(Error));
}

void notifySwitchRequest( uint16_t Address, uint8_t outData, uint8_t dirData) {

  Serial.print("notifySwitchRequest():");
  Serial.print(Address,DEC);
  Serial.print(",");
  Serial.print(outData,DEC); // 常に16?
  Serial.print(",");
  Serial.print(dirData,DEC); // 0:t:分岐  32:c:直線
  Serial.println("");

  PointDir = dirData;
}


//--------------------------------------------------------------------------------------------------



void debugstate(){
     if (Serial.available()) {
        // Process serial input for commands from the host.
        int ch = Serial.read();
        Serial.println(ch);
        if(ch=='A'){
          Serial.print("ADDRESS:");
          Serial.println(Address);          
        }
        if(ch=='S'){
          Serial.print("STATE:");
          Serial.print(Throttle.getState());
          Serial.print(":");
          Serial.println(Throttle.getStateStr(Throttle.getState()));
        }
        if(ch=='T'){
          Serial.print("Throttle.getSpeed:");
          Serial.println(Throttle.getSpeed());
        }

        if(ch=='I'){  // addres をフリーにする
          Serial.println("setAddress");      
          Throttle.setAddress(Address);      //via Adresse
        }
        if(ch=='Z'){  // freeAdderss
          Serial.println("freeAddress");      
          Throttle.freeAddress(Address);      //via Adresse
        }
        if(ch=='R'){  // rereceAdderss
          Serial.println("releaseAddress");      
          Throttle.releaseAddress();      //via Adresse
        }
        if(ch=='O'){  // addres を取得する
          Throttle.dispatchAddress(Address);
          Throttle.freeAddress(Address);
          Throttle.releaseAddress();
        }
        if(ch=='D'){
          Throttle.acquireAddress();
        }

     }
}



