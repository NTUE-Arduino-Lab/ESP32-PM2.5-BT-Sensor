#include <BLEDevice.h>
#include <Wire.h> // For I2C interface
#include <Arduino.h>
#include <SPI.h>
#include <wifiboy_lib.h>
#include "wb-sprite.h"
/* 基本属性定義  */
#define SPI_SPEED 115200 // SPI通信速度

/* シグナル種別 */
#define SIGNAL_ERROR 'E' // (Error:異常発生)

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
void blit_double(double num, uint16_t x, uint16_t y)
{
	uint16_t d[5];
	num = num * 100.0;
	uint16_t convertNum = (uint16_t)num;

	d[0] = convertNum / 10000;
	d[1] = (convertNum - d[0] * 10000) / 1000;
	d[2] = (convertNum - d[0] * 10000 - d[1] * 1000) / 100;
	d[3] = (convertNum - d[0] * 10000 - d[1] * 1000 - d[2] * 100) / 10;
	d[4] = convertNum - d[0] * 10000 - d[1] * 1000 - d[2] * 100 - d[3] * 10;

	for (int i = 0; i < 5; i++)
	{
		if (i < 3)
			wb_blitBuf8(d[i] * 8 + 120, 8, 240, x + i * 8, y, 8, 8, (uint8_t *)sprites); //將d[0]~d[4]逐個顯示並排列
		if (i >= 3)
			wb_blitBuf8(d[i] * 8 + 120, 8, 240, x + (i + 1) * 8, y, 8, 8, (uint8_t *)sprites);
	}
	wb_blitBuf8(104, 8, 240, x + 3 * 8, y, 8, 8, (uint8_t *)sprites);
}
void blit_str256(const char *str, int x, int y)
{
	for (int i = 0; i < strlen(str); i++)
	{
		if (str[i] >= '@' && str[i] <= ']')
			wb_blitBuf8(8 * (str[i] - '@'), 0, 240, x + i * 8, y, 8, 8, (uint8_t *)sprites);
		if (str[i] >= '!' && str[i] <= '>')
			wb_blitBuf8(8 * (str[i] - '!'), 8, 240, x + i * 8, y, 8, 8, (uint8_t *)sprites);
		if (str[i] == '?')
			wb_blitBuf8(8 * 14, 16, 240, x + i * 8, y, 8, 8, (uint8_t *)sprites);
	}
}
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
	wb_init(0);
	wb_initBuf8();
	for (int i = 0; i < 256; i++) // 定義 256色（唯一色庫）
		wb_setPal8(i, wb_color565(standardColour[i][0], standardColour[i][1], standardColour[i][2]));
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
			wb_clearBuf8();
			blit_str256("PM2.5:", 40, 44);
			blit_double(data.pmData, 40, 60);
			blit_str256("UG/M^3", 40, 76);
			enableMeasurement = false;
			wb_blit8();
		}
	}
	else if (doScan)
	{
		BLEDevice::getScan()->start(0);
	}

	
}

/*  初期化処理  */
void doInitialize()
{
	Serial.begin(SPI_SPEED);
	pinMode(ledPin, OUTPUT);
	digitalWrite(ledPin, HIGH);
	Serial.println("BLE Client start ...");
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
