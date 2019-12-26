#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <Ticker.h> // タイマー割り込み用
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
/* 基本属性定義  */
#define DEVICE_NAME "ESP32" // デバイス名
#define SPI_SPEED 115200	// SPI通信速度
#define WIDTH_1 1024.0
#define WIDTH_2 2048.0
#define WIDTH_3 3072.0
#define WIDTH_4 4096.0

/* シグナル種別 */
#define SIGNAL_ERROR 'E' // (Error:異常発生)

/* UUID定義 */
#define SERVICE_UUID "28b0883b-7ec3-4b46-8f64-8559ae036e4e"			  // サービスのUUID
#define CHARACTERISTIC_UUID_TX "2049779d-88a9-403a-9c59-c7df79e1dd7c" // 送信用のUUID

/* 通信制御用 */
BLECharacteristic *pCharacteristicTX; // 送信用キャラクタリスティック
bool deviceConnected = false;		  // デバイスの接続状態
bool bAbnormal = false;				  // デバイス異常判定


/* 通信データ */
struct tmpData
{ // 計測データ
	double pmData;
};
struct tmpData data;
double bandWidth = WIDTH_3; //位寬調整(訊號佔據的寬度)
float coef = 700;			//係數誤差調整
int incomeByte[7];
int sensorData;
int z = 0;
int sum;
struct tmpSignal
{ // シグナル
	char hdr1;
	char signalCode;
};
struct tmpSignal signaldata = {0xff, 0x00};

/* LEDピン */
// const int ledPin = 16; // 接続ピン
// int ledState = LOW;	// 状態

/* プッシュボタン */
// const int buttonPin = 32; // 接続ピン

/* タイマー制御用 */
Ticker ticker;
bool bReadyTicker = false;
const int iIntervalTime = 1; // 計測間隔（1秒）資料儲存間隔(整數)時間(秒)

/* SDカードに読み取と記錄用 */
void readFile(fs::FS &fs, const char *path)
{
	Serial.printf("Reading file: %s\n", path);

	File file = fs.open(path);
	if (!file)
	{
		Serial.println("Failed to open file for reading");
		return;
	}

	Serial.print("Read from file: ");
	while (file.available())
	{
		Serial.write(file.read());
	}
	file.close();
}

void writeFile(fs::FS &fs, const char *path, const char *message)
{
	Serial.printf("Writing file: %s\n", path);

	File file = fs.open(path, FILE_WRITE);
	if (!file)
	{
		Serial.println("Failed to open file for writing");
		return;
	}
	if (file.print(message))
	{
		Serial.println("File written");
	}
	else
	{
		Serial.println("Write failed");
	}
	file.close();
}

void appendFile(fs::FS &fs, const char *path, const char *message)
{
	Serial.printf("Appending to file: %s\n", path);

	File file = fs.open(path, FILE_APPEND);
	if (!file)
	{
		Serial.println("Failed to open file for appending");
		return;
	}
	if (file.print(message))
	{
		Serial.println("Message appended");
	}
	else
	{
		Serial.println("Append failed");
	}
	file.close();
}

/*********************< Callback classes and functions >**********************/
// 接続・切断時コールバック
class funcServerCallbacks : public BLEServerCallbacks
{
	void onConnect(BLEServer *pServer)
	{
		deviceConnected = true;
	}
	void onDisconnect(BLEServer *pServer)
	{
		deviceConnected = false;
	}
};

//  タイマー割り込み関数  //
static void kickRoutine()
{
	bReadyTicker = true;
}

/*****************************************************************************
 *                          Predetermined Sequence                           *
 *****************************************************************************/
void setup()
{
	Serial.begin(2400);
	// 初期化処理を行ってBLEデバイスを初期化する
	doInitialize();
	BLEDevice::init(DEVICE_NAME);

	// Serverオブジェクトを作成してコールバックを設定する
	BLEServer *pServer = BLEDevice::createServer();
	pServer->setCallbacks(new funcServerCallbacks());

	// Serviceオブジェクトを作成して準備処理を実行する
	BLEService *pService = pServer->createService(SERVICE_UUID);
	doPrepare(pService);

	// サービスを開始して、SERVICE_UUIDでアドバタイジングを開始する
	pService->start();
	BLEAdvertising *pAdvertising = pServer->getAdvertising();
	pAdvertising->addServiceUUID(SERVICE_UUID);
	pAdvertising->start();
	// タイマー割り込みを開始する
	ticker.attach(iIntervalTime, kickRoutine);
	// Serial.println("Waiting to connect ...");
}

void loop()
{
	// 接続が確立されていて異常でなければ
	if (deviceConnected && !bAbnormal)
	{
		// タイマー割り込みによって主処理を実行する
		if (bReadyTicker)
		{
			doMainProcess();
			bReadyTicker = false;
		}
	}
}

/*  初期化処理  */
void doInitialize()
{
	// Serial.begin(SPI_SPEED);
	// pinMode(buttonPin, INPUT);
	// pinMode(ledPin, OUTPUT);
	// digitalWrite(ledPin, HIGH);
	if (!SD.begin())
	{
		Serial.println("Card Mount Failed");
		return;
	}
	uint8_t cardType = SD.cardType();

	if (cardType == CARD_NONE)
	{
		Serial.println("No SD card attached");
		return;
	}

	Serial.print("SD Card Type: ");
	if (cardType == CARD_MMC)
	{
		Serial.println("MMC");
	}
	else if (cardType == CARD_SD)
	{
		Serial.println("SDSC");
	}
	else if (cardType == CARD_SDHC)
	{
		Serial.println("SDHC");
	}
	else
	{
		Serial.println("UNKNOWN");
	}

	uint64_t cardSize = SD.cardSize() / (1024 * 1024);
	Serial.printf("SD Card Size: %lluMB\n", cardSize);

	writeFile(SD, "/hello.txt", "Record Start:\n");
}

/*  準備処理  */
void doPrepare(BLEService *pService)
{
	// Notify用のキャラクタリスティックを作成する
	pCharacteristicTX = pService->createCharacteristic(
		CHARACTERISTIC_UUID_TX,
		BLECharacteristic::PROPERTY_NOTIFY);
	pCharacteristicTX->addDescriptor(new BLE2902());
}

/*  主処理ロジック  */
void doMainProcess()
{
	// データを読み取る
	while (Serial.available() > 0)
	{

		sensorData = Serial.read();
		if (sensorData == 170)
		{
			z = 0;
			incomeByte[z] = sensorData;
		}
		else
		{
			z++;
			incomeByte[z] = sensorData;
		}
		if (z == 6)
		{
			sum = incomeByte[1] + incomeByte[2] + incomeByte[3] + incomeByte[4];

			if (incomeByte[5] == sum && incomeByte[6] == 255)
			{
				for (int k = 0; k < 7; k++)
				{
					Serial.print(incomeByte[k]);
					Serial.print("|");
				}

				Serial.print(" Vo=");
				float vo = (incomeByte[1] * 256.0 + incomeByte[2]) / bandWidth * 5.00;
				Serial.print(vo, 3);
				Serial.print("v | ");
				float c = vo * coef;

				Serial.print(" PM2.5 = ");
				Serial.print(c, 2);
				Serial.print("ug/m3 ");
				Serial.println();
				data.pmData = (double)c;
				char buff[20];
				sprintf(buff, "%f", c);
				appendFile(SD, "/hello.txt", buff);
				appendFile(SD, "/hello.txt", "\n");
				readFile(SD, "/hello.txt");
			}
			else
			{
				z = 0;
				Serial.flush();
				sensorData = 0;
				for (int m = 0; m < 7; m++)
				{
					incomeByte[m] = 0;
				}
			}
			z = 0;
		}
	}

	// 計測失敗なら再試行させる
	// if (isnan(c)) {
	//     Serial.println("Failed to read sensor!");
	//     return;
	// }
	// 構造体に値を設定して送信する
	pCharacteristicTX->setValue((uint8_t *)&data, sizeof(tmpData));
	pCharacteristicTX->notify();
	// シリアルモニターに表示する

	// Serial.print("Send data: ");
	// Serial.println(data.pmData);
}
