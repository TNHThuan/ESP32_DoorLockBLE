#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include <SPI.h>
#include <MFRC522.h>

#define bnt_lockDoor 32
#define gpio_statusDoor 33

#define SS_PIN 5
#define RST_PIN 0

byte serverRFID[4][7];
bool checkCorrectRFID = false;
MFRC522::MIFARE_Key key;
MFRC522 rfid = MFRC522(SS_PIN, RST_PIN);

BLEServer* pServer = NULL;
BLECharacteristic* pChar_statusDoor = NULL;
BLECharacteristic* pChar_openDoor = NULL;
BLECharacteristic* pChar_checkPass = NULL;
BLECharacteristic* pChar_changePass = NULL;
BLECharacteristic* pChar_notification = NULL;
BLECharacteristic* pChar_optionsRFID = NULL;
BLE2902 *pBLE2902;
//BLE2902 *pBLE2902Note;

bool deviceConnected = false;
bool oldDeviceConnected = false;
bool correctPassword = false;
uint32_t statusDoor = 0;
uint32_t oldStatusDoor = 1;
String passwordReal = "tnht0411";

uint32_t indexOptionsRFID = 0;
bool optionsRFID = false;   //using for turn on RFID options like status, add, delete
bool changeRFID = false;    //flag using to determine action when insert card like unlock door or change RFID

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define UUID_SERVICE      "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define UUID_STATUSDOOR   "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define UUID_OPENDOOR     "3d9316b0-8c42-47ce-88df-3e9446d01941"
#define UUID_CHECKPASS    "ac81ab0b-c709-4f67-8bf4-90681a714b34"
#define UUID_CHANGEPASS   "e4947e19-9ef1-4a08-ab2b-2fbb7ad40282"
#define UUID_NOTIFICATION "de95936b-5cc2-4ca9-b024-51ce1f60bce3"
#define UUID_OPTIONSRFID  "1ee4a5b8-cf31-464f-9728-699ac2dc89a9"

void SendNote(String noteToSend){
  //1 "Incorrect password"
  //2 "Password changed"
  String strNotifier = (String) noteToSend;
  pChar_notification->setValue(strNotifier.c_str());
  pChar_notification->notify();
}

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

class CharCallBack_OpenDoor: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) override { 
    std::string pChar_value_stdstr = pChar->getValue();
    
    if (pChar_value_stdstr.length() > 0){
      String bufferDisplay = "";
      for (int i = 0; i < pChar_value_stdstr.length() - 1; i++){
        bufferDisplay = bufferDisplay + pChar_value_stdstr[i];
      }

      if(bufferDisplay == passwordReal){
        statusDoor = 1;
      } else {
        statusDoor = 0;
        SendNote("Incorrect password");
      }
    }
  }
};

class CharCallBack_CheckPass: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) override { 
    Serial.println("Debug: Entered CheckPass");
    std::string pChar_value_stdstr = pChar->getValue();
    
    if (pChar_value_stdstr.length() > 0){
      String bufferDisplay = "";
      for (int i = 0; i < pChar_value_stdstr.length() - 1; i++){
        bufferDisplay = bufferDisplay + pChar_value_stdstr[i];
      }

      if(bufferDisplay == passwordReal){
        correctPassword = true;
      } else {
        correctPassword = false;
        SendNote("Incorrect password");
      }
    }
    Serial.println("Debug: Exit CheckPass");
  }
};

class CharCallBack_ChangePass: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) override { 
    std::string pChar_value_stdstr = pChar->getValue();
    
    if (pChar_value_stdstr.length() > 0){
      String bufferDisplay = "";
      for (int i = 0; i < pChar_value_stdstr.length() - 1; i++){
        bufferDisplay = bufferDisplay + pChar_value_stdstr[i];
      }

      if(correctPassword){
        passwordReal = bufferDisplay;
        Serial.println("Debug: Change password succesfully");
        SendNote("Password changed");
      } else {
        Serial.println("Debug: Change password failed");
      }
      correctPassword = false;
    }
  }
};

class CharCallBack_OptionsRFID: public BLECharacteristicCallbacks{
  void onWrite (BLECharacteristic *pChar) override {
    std::string pChar_value_stdstr = pChar->getValue();
    String pChar_value_string = String(pChar_value_stdstr.c_str());
    indexOptionsRFID = pChar_value_string.toInt();

    switch (indexOptionsRFID){
      case 0:
      case 1:
      case 2:
      case 3:
        Serial.println("Debug: Select change RFID!");
        ChangeRFIDCard(indexOptionsRFID);
        break;

      case 5:   //entered confirm RFID permission
        Serial.println("Debug: Select options RFID!");
        optionsRFID = true;   //enable confirm RFID permission process
        break;

      default: 
        break;
    }
  }
};

void setup() {
  Serial.begin(115200);
  SPI.begin(); // Init SPI bus
  rfid.PCD_Init(); // Init MFRC522 

  NewInitForRFIDServer();
  CfgInitForRFIDServer();

  Serial.println("Starting...");

  pinMode(bnt_lockDoor, INPUT);
  pinMode(gpio_statusDoor, OUTPUT);

  // Create the BLE Device
  BLEDevice::init("ESP32_Door");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(BLEUUID(UUID_SERVICE), 30, 0);

  // Create a BLE Characteristic
  pChar_statusDoor = pService->createCharacteristic(
                      UUID_STATUSDOOR,
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_INDICATE
                    );

  pChar_notification = pService->createCharacteristic(
    UUID_NOTIFICATION,
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_INDICATE
  );

  pChar_openDoor = pService->createCharacteristic(
    UUID_OPENDOOR,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE
  );

  pChar_checkPass = pService->createCharacteristic(
    UUID_CHECKPASS,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE
  );

  pChar_changePass = pService->createCharacteristic(
    UUID_CHANGEPASS,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE
  );

  pChar_optionsRFID = pService->createCharacteristic(
    UUID_OPTIONSRFID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_INDICATE
  );

  // Create a BLE Descriptor
  
  pBLE2902 = new BLE2902();
  pBLE2902->setNotifications(true);

  //pBLE2902Note = new BLE2902();
  //pBLE2902Note->setNotifications(true);
  
  // Add all Descriptors here
  pChar_notification->addDescriptor(pBLE2902);
  pChar_statusDoor->addDescriptor(pBLE2902);
  pChar_openDoor->addDescriptor(new BLE2902());
  pChar_checkPass->addDescriptor(new BLE2902());
  pChar_changePass->addDescriptor(new BLE2902());
  pChar_optionsRFID->addDescriptor(pBLE2902);


  // After defining the desriptors, set the callback functions
  pChar_openDoor->setCallbacks(new CharCallBack_OpenDoor());
  pChar_checkPass->setCallbacks(new CharCallBack_CheckPass());
  pChar_changePass->setCallbacks(new CharCallBack_ChangePass());
  pChar_optionsRFID->setCallbacks(new CharCallBack_OptionsRFID());
  
  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(UUID_SERVICE);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("Waiting a client connection to notify...");
}

void loop() {
    // notify changed value

    digitalWrite(gpio_statusDoor, statusDoor);

    if(digitalRead(bnt_lockDoor) == 0){
      if(statusDoor != 0){
        statusDoor = 0;
        Serial.println("Debug: Door locked");
      }
    }

    ReadRFID();

    // connecting
    if (deviceConnected && !oldDeviceConnected) {
        // do stuff here on connecting
        Serial.println("connecting");
        delay(1000);  //make sure all establish is completed
        oldDeviceConnected = deviceConnected;
        pChar_statusDoor->setValue(statusDoor);
        pChar_statusDoor->notify();
    }

    // disconnecting
    if (!deviceConnected && oldDeviceConnected) {
        delay(500); // give the bluetooth stack the chance to get things ready
        pServer->startAdvertising(); // restart advertising
        Serial.println("start advertising");
        oldDeviceConnected = deviceConnected;
    }

    if (deviceConnected) {
      if(statusDoor != oldStatusDoor){
        pChar_statusDoor->setValue(statusDoor);
        pChar_statusDoor->notify();
        oldStatusDoor = statusDoor;
      }  
    }

}

void ReadRFID(){
 	//Read RFID card
 	for (byte i = 0; i < 6; i++) {
 			key.keyByte[i] = 0xFF;
 	}
  
 	// Look for new 1 cards
 	if ( ! rfid.PICC_IsNewCardPresent())
 			return;

 	// Verify if the NUID has been readed
 	if ( 	!rfid.PICC_ReadCardSerial())
 			return;

 	Serial.print(F("Debug: RFID In dec: "));
 	printDec(rfid.uid.uidByte, rfid.uid.size);
  Serial.println();
  if(changeRFID == false){  //no change RFID allow, check this card for purpose
    if(optionsRFID == true){    //check this card to turn on RFID options
      TurnOnRFIDOptions(rfid.uid.uidByte, rfid.uid.size);
    } else {    //check this card to open the door
      OpenDoorWithRFID(rfid.uid.uidByte, rfid.uid.size);
    }
  } else {  //change RFID confirmed, add this card
    ReplaceSlotRFID(rfid.uid.uidByte, rfid.uid.size, indexOptionsRFID);
    CheckRFIDServerStatus();  //update RFID status to client
    changeRFID = false;  //make sure to turn off this change RFID cause it is completed
  }
  

 	// Halt PICC
 	rfid.PICC_HaltA();

 	// Stop encryption on PCD
 	rfid.PCD_StopCrypto1();

}

void NewInitForRFIDServer(){
  for(byte i = 0; i < 4; i++){
    for(byte j = 0; j < 7; j++){
      serverRFID[i][j] = 0;
    }
  }
}

void CfgInitForRFIDServer(){
  serverRFID[0][0] = 83;
  serverRFID[0][1] = 16;
  serverRFID[0][2] = 254;
  serverRFID[0][3] = 244;
  serverRFID[0][4] = 0;
  serverRFID[0][5] = 0;
  serverRFID[0][6] = 0;

  serverRFID[1][0] = 83;
  serverRFID[1][1] = 53;
  serverRFID[1][2] = 24;
  serverRFID[1][3] = 14;
  serverRFID[1][4] = 0;
  serverRFID[1][5] = 0;
  serverRFID[1][6] = 0;

  serverRFID[3][0] = 4;
  serverRFID[3][1] = 109;
  serverRFID[3][2] = 203;
  serverRFID[3][3] = 29;
  serverRFID[3][4] = 25;
  serverRFID[3][5] = 97;
  serverRFID[3][6] = 128;
}

void CheckRFIDServerStatus(){
  uint32_t RFIDServerStatus = 0;

  for(byte i = 0; i < 4; i++){
    for(byte j = 0; j < 7; j++){
 			Serial.print(serverRFID[i][j] < 10 ? " 0" : " ");
 			Serial.print(serverRFID[i][j], DEC);
    }
    Serial.println("");
  }

  for(byte i = 0; i < 4; i++){
    if(serverRFID[i][0] != 0){
      Serial.print("Debug: Slot ");
      Serial.print(i);
      Serial.println(" avalible");
      RFIDServerStatus = RFIDServerStatus << 1;
      RFIDServerStatus += 1;
    } else {
      Serial.print("Debug: Slot ");
      Serial.print(i);
      Serial.println(" unavalible");
      RFIDServerStatus = RFIDServerStatus << 1;
    }
  }
  pChar_optionsRFID->setValue(RFIDServerStatus);
  pChar_optionsRFID->notify();
}

void EraseSlotRFID(byte serverRFIDIndex){
  for(byte i = 0; i < 7; i++){
    serverRFID[serverRFIDIndex][i] = 0;
  }
}

void ReplaceSlotRFID(byte *clientRFID, byte clientRFIDSize, byte serverRFIDIndex){
  EraseSlotRFID(serverRFIDIndex);   //make sure this slot is clean

  for(byte i = 0; i < clientRFIDSize; i++){
    serverRFID[serverRFIDIndex][i] = clientRFID[i];
  }
}

void ChangeRFIDCard(byte serverRFIDIndex){
  if(serverRFID[serverRFIDIndex][0] != 0){  //if RFID exist, then delete it
    Serial.println("Debug: delete slot " + String(serverRFIDIndex));
    EraseSlotRFID(serverRFIDIndex);
    CheckRFIDServerStatus();  //update RFID status to client
  } else {  //this slot is available, add it
    changeRFID = true;  //
    SendNote("Insert your card.");
  }
}

int CompareRFIDVsSingle(byte *clientRFID, byte clientRFIDSize, byte serverRFIDIndex){
  for (byte i = 0; i < clientRFIDSize; i++){
    if(clientRFID[i] != serverRFID[serverRFIDIndex][i]){
      return 0;
    }
  }
  return 1;
}

uint8_t CheckRFIDVsAll(byte *clientRFID, byte clientRFIDSize){
  for(int i = 0; i < 4; i++){
    if(CompareRFIDVsSingle(clientRFID, clientRFIDSize, i)){
      Serial.println("Debug: Slot " + String(i) + ": RFID correct !");
      return 1;
    }
  }
  Serial.println("Debug: RFID incorrect !");
  return 0;
}

void OpenDoorWithRFID(byte *clientRFID, byte clientRFIDSize){
  if(CheckRFIDVsAll(clientRFID, clientRFIDSize)){
    statusDoor=1;
  } else {
    statusDoor=0;
  }
}

void TurnOnRFIDOptions(byte *clientRFID, byte clientRFIDSize){
  if(CheckRFIDVsAll(clientRFID, clientRFIDSize)){   
    Serial.println("Debug: confirmed options RFID permission");
    SendNote("Permission confirmed");
    CheckRFIDServerStatus();    //broadcast about status of RFID cards on server to client
    optionsRFID = false;  //make sure disable options RFID permission because it has completed
  } else {    //wrong card
    Serial.println("Debug: card is not allow for options RFID");
    SendNote("Permission denied");
    optionsRFID = false;  //make sure disable options RFID permission because it has completed
  }
}

void printDec(byte *buffer, byte bufferSize) {
 	for (byte i = 0; i < bufferSize; i++) {
 			Serial.print(buffer[i] < 10 ? " 0" : " ");
 			Serial.print(buffer[i], DEC);
 	}
}