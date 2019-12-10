/*
 * File:      BLEClient.ino
 * Function:  BLEコネクション通信のセントラルとして動作します。
 *            準備システムなので名称をBLEClientとし、単一方向の受信だけで送信機能はありません。
 *            目的のデバイスが見つかると接続して、定期的にサーバーから送信される計測データを受信
 *            してシリアルモニターに詳細を表示し、OLEDディスプレイに温度と湿度を表示します（OLED
 *            への表示はデバイス識別番号1のもののみ）。
 *            サーバーからの異常情報をキャッチするとLEDを点灯して知らせ、プッシュボタンを押すと
 *            消灯します。
 * Date:      2019/04/25
 * Author:    M.Ono
 * 
 * Hardware   MCU:  ESP32 (DOIT ESP32 DEVKIT V1 Board)
 *            ブレッドボードに上記開発ボードとOLEDディスプレイ、プッシュボタン、LEDを配線
 *            OLEDディスプレイはSSD1306  (0.96inch 128x64dot) 
 */
#include <BLEDevice.h>
#include <Wire.h>                   // For I2C interface
#include <Arduino.h>

/* 基本属性定義  */
#define SPI_SPEED   115200          // SPI通信速度

/* シグナル種別 */
#define SIGNAL_ERROR   'E'          // (Error:異常発生)


/* UUID定義 */
BLEUUID serviceUUID("28b0883b-7ec3-4b46-8f64-8559ae036e4e");   // サービスのUUID
BLEUUID CHARA_UUID_RX("2049779d-88a9-403a-9c59-c7df79e1dd7c"); // RXのUUID

/* 通信制御用 */
BLERemoteCharacteristic* pRemoteCharacteristicRX;  // 受信用キャラクタリスティック
BLEAdvertisedDevice* targetDevice;      // 目的のBLEデバイス
bool  doConnect = false;                // 接続指示
bool  doScan = false;                   // スキャン指示
bool  deviceConnected = false;          // デバイスの接続状態
bool  bInAlarm  = false;                // デバイス異常
bool  enableMeasurement = false;        // 計測情報が有効

/* 通信データ */
struct tmpData {                        // 計測データ
    int   buttonState;
};
struct tmpData  data;

/* LEDピン */
const int ledPin = 16;                  // 接続ピン

/*********************< Callback classes and functions >**********************/
// 接続・切断時コールバック
class funcClientCallbacks: public BLEClientCallbacks {
    void onConnect(BLEClient* pClient) {
    };
    void onDisconnect(BLEClient* pClient) {
        deviceConnected = false;
    }
};

// アドバタイジング受信時コールバック
class advertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        Serial.print("Advertised Device found: ");
        Serial.println(advertisedDevice.toString().c_str());

        // 目的のBLEデバイスならスキャンを停止して接続準備をする
        if (advertisedDevice.haveServiceUUID()) {
            BLEUUID service = advertisedDevice.getServiceUUID();
            Serial.print("Have ServiceUUI: "); Serial.println(service.toString().c_str());
            if (service.equals(serviceUUID)) {
                BLEDevice::getScan()->stop();
                targetDevice = new BLEAdvertisedDevice(advertisedDevice);
                doConnect = doScan = true;
            }
        }
    }
};

// Notify時のコールバック関数
static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic,
                uint8_t* pData, size_t length, bool isNotify) {

    // 受信メッセージからボタンを切り出して表示用に編集する
    memcpy(&data, pData, length);
    int c = data.buttonState;
    if(c == 0)
        digitalWrite(ledPin, HIGH);
    else
        digitalWrite(ledPin, LOW);    
    enableMeasurement = true;
    Serial.print("Received data: ");  Serial.print(c);
}

/*****************************************************************************
 *                          Predetermined Sequence                           *
 *****************************************************************************/
void setup() {
    // 初期化処理を行ってBLEデバイスを初期化する
    doInitialize();
    BLEDevice::init("");
    Serial.println("Client application start...");

    // Scanオブジェクトを取得してコールバックを設定する
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new advertisedDeviceCallbacks());
    // アクティブスキャンで10秒間スキャンする
    pBLEScan->setActiveScan(true);
    pBLEScan->start(10);
}

void loop() {
    // アドバタイジング受信時に一回だけサーバーに接続する
    if (doConnect == true) {
        if (doPrepare()) {
            Serial.println("Connected to the BLE Server.");
        } else {
            Serial.println("Failed to connect to the BLE server.");
        }
        doConnect = false;
    }
    // 接続状態なら
    if (deviceConnected) {
        // 測定値が有効かつ異常でなければOLEDに表示する
        if (enableMeasurement && !bInAlarm) {
            
            enableMeasurement = false;
        }
    } else if (doScan) {
        BLEDevice::getScan()->start(0);
    }
}

/*  初期化処理  */
void doInitialize() {
    Serial.begin(SPI_SPEED);
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, HIGH);

    Serial.println("BLE Client start ...");
}

/*  準備処理  */
bool doPrepare() {
    // クライアントオブジェクトを作成してコールバックを設定する
    BLEClient* pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new funcClientCallbacks());
    Serial.println(" - Created client.");

    // リモートBLEサーバーと接続して
    pClient->connect(targetDevice);
    Serial.println(" - Connected to server.");

    // サービスへの参照を取得する
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
        Serial.print("Failed to find serviceUUID: ");
        Serial.println(serviceUUID.toString().c_str());
        pClient->disconnect();
        return false;
    }
    Serial.println(" - Found target service.");

    // キャラクタリスティックへの参照を取得して
    pRemoteCharacteristicRX = pRemoteService->getCharacteristic(CHARA_UUID_RX);
    if (pRemoteCharacteristicRX == nullptr) {
      Serial.print("Failed to find characteristicUUID: ");
      Serial.println(CHARA_UUID_RX.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found characteristic CHARA_UUID_RX.");

    // Notifyのコールバック関数を割り当てる
    if (pRemoteCharacteristicRX->canNotify()) {
        pRemoteCharacteristicRX->registerForNotify(notifyCallback);
        Serial.println(" - Registered notify callback function.");
    }

    deviceConnected = true;
    return true;
}