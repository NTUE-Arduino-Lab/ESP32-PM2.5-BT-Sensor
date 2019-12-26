#include <Adafruit_SH1106.h>
#include <Adafruit_GFX.h>
#include <BLEDevice.h>
#include <Wire.h> // For I2C interface
#include <Arduino.h>
#include <SPI.h>

/* 基本属性定義  */
#define SPI_SPEED 115200 // SPI通信速度

/* シグナル種別 */
#define SIGNAL_ERROR 'E' // (Error:異常発生)

#define OLED_SDA 22  //OLED資料傳輸控制腳位
#define OLED_SCL 23  //OLED資料傳輸控制腳位

Adafruit_SH1106 display(OLED_SDA, OLED_SCL);

/* UUID定義 */
BLEUUID serviceUUID("28b0883b-7ec3-4b46-8f64-8559ae036e4e");   // サービスのUUID
BLEUUID CHARA_UUID_RX("2049779d-88a9-403a-9c59-c7df79e1dd7c"); // RXのUUID

/* 通信制御用 */
BLERemoteCharacteristic *pRemoteCharacteristicRX; // 受信用キャラクタリスティック
BLEAdvertisedDevice *targetDevice;				  // 目的のBLEデバイス
bool doConnect = false;							  // 接続指示
bool doScan = false;							  // スキャン指示
bool deviceConnected = false;					  // デバイスの接続状態
bool bInAlarm = false;							  // デバイス異常
bool enableMeasurement = false;					  // 計測情報が有効

/* 通信データ */
struct tmpData
{ // 計測データ
	double pmData;
};
struct tmpData data;

/* LEDピン */
const int ledPin = 16; // 接続ピン

/*********************< Callback classes and functions >**********************/
// 接続・切断時コールバック
class funcClientCallbacks : public BLEClientCallbacks
{
	void onConnect(BLEClient *pClient){};
	void onDisconnect(BLEClient *pClient)
	{
		deviceConnected = false;
	}
};

// アドバタイジング受信時コールバック
class advertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
	void onResult(BLEAdvertisedDevice advertisedDevice)
	{
		Serial.print("Advertised Device found: ");
		Serial.println(advertisedDevice.toString().c_str());

		// 目的のBLEデバイスならスキャンを停止して接続準備をする
		if (advertisedDevice.haveServiceUUID())
		{
			BLEUUID service = advertisedDevice.getServiceUUID();
			Serial.print("Have ServiceUUI: ");
			Serial.println(service.toString().c_str());
			if (service.equals(serviceUUID))
			{
				BLEDevice::getScan()->stop();
				targetDevice = new BLEAdvertisedDevice(advertisedDevice);
				doConnect = doScan = true;
			}
		}
	}
};

// Notify時のコールバック関数
static void notifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic,
						   uint8_t *pData, size_t length, bool isNotify)
{

	// 受信メッセージからボタンを切り出して表示用に編集する
	memcpy(&data, pData, length);
	int c = (int)data.pmData;
	enableMeasurement = true;
	Serial.print("Received data: ");
	Serial.println(c);
}

/*****************************************************************************
 *                          Predetermined Sequence                           *
 *****************************************************************************/
void setup()
{
	// 初期化処理を行ってBLEデバイスを初期化する
	doInitialize();
	BLEDevice::init("");
	Serial.println("Client application start...");

	// Scanオブジェクトを取得してコールバックを設定する
	BLEScan *pBLEScan = BLEDevice::getScan();
	pBLEScan->setAdvertisedDeviceCallbacks(new advertisedDeviceCallbacks());
	// アクティブスキャンで10秒間スキャンする
	pBLEScan->setActiveScan(true);
	pBLEScan->start(10);
}

void loop()
{
	// アドバタイジング受信時に一回だけサーバーに接続する
	if (doConnect == true)
	{
		if (doPrepare())
		{
			Serial.println("Connected to the BLE Server.");
		}
		else
		{
			Serial.println("Failed to connect to the BLE server.");
		}
		doConnect = false;
	}
	// 接続状態なら
	if (deviceConnected)
	{
		// 測定値が有効かつ異常でなければOLEDに表示する
		if (enableMeasurement && !bInAlarm)
		{

			enableMeasurement = false;
		}
	}
	else if (doScan)
	{
		BLEDevice::getScan()->start(0);
	}
	display.setTextSize(2);
	display.setTextColor(WHITE);
	display.setCursor(0, 0);
	display.println("CURRENT");
	display.println("PM2.5 :");
	display.print(data.pmData);
	display.println(" ug/m^3");
	display.display();
	delay(2000);
	display.clearDisplay();
}

/*  初期化処理  */
void doInitialize()
{
	Serial.begin(SPI_SPEED);
	pinMode(ledPin, OUTPUT);
	digitalWrite(ledPin, HIGH);

	Serial.println("BLE Client start ...");
	display.begin(SH1106_SWITCHCAPVCC, 0x3C); 
  	display.clearDisplay();
}

/*  準備処理  */
bool doPrepare()
{
	// クライアントオブジェクトを作成してコールバックを設定する
	BLEClient *pClient = BLEDevice::createClient();
	pClient->setClientCallbacks(new funcClientCallbacks());
	Serial.println(" - Created client.");

	// リモートBLEサーバーと接続して
	pClient->connect(targetDevice);
	Serial.println(" - Connected to server.");

	// サービスへの参照を取得する
	BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
	if (pRemoteService == nullptr)
	{
		Serial.print("Failed to find serviceUUID: ");
		Serial.println(serviceUUID.toString().c_str());
		pClient->disconnect();
		return false;
	}
	Serial.println(" - Found target service.");

	// キャラクタリスティックへの参照を取得して
	pRemoteCharacteristicRX = pRemoteService->getCharacteristic(CHARA_UUID_RX);
	if (pRemoteCharacteristicRX == nullptr)
	{
		Serial.print("Failed to find characteristicUUID: ");
		Serial.println(CHARA_UUID_RX.toString().c_str());
		pClient->disconnect();
		return false;
	}
	Serial.println(" - Found characteristic CHARA_UUID_RX.");

	// Notifyのコールバック関数を割り当てる
	if (pRemoteCharacteristicRX->canNotify())
	{
		pRemoteCharacteristicRX->registerForNotify(notifyCallback);
		Serial.println(" - Registered notify callback function.");
	}

	deviceConnected = true;
	return true;
}
